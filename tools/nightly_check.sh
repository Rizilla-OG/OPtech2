#!/bin/sh
# Nightly check for OpenTech2: build, every check (tests/run_all.py), cppcheck,
# and the GitHub CI / code scanning state of main. Writes a report to
# build/nightly/<date>.txt and shows a desktop notification with the summary.
# Skips the night when a game is running (it would steal the CPU, and the
# windowed checks open windows). Run by the opentech2-nightly systemd user
# timer; can be run by hand too.
cd "$(dirname "$0")/.." || exit 1
engine="$(pwd)"
mkdir -p build/nightly
report="build/nightly/$(date +%F).txt"

notify()
{
	if command -v notify-send >/dev/null 2>&1; then
		notify-send --app-name=OpenTech2 "OpenTech2 nightly" "$1" 2>/dev/null
	fi
}

# eqgame: EverQuest under Proton; OpenTech2: someone is playing
if pgrep -f "eqgame.exe" >/dev/null 2>&1 || pgrep -x OpenTech2 >/dev/null 2>&1; then
	echo "$(date '+%F %T') skipped: a game is running" >> build/nightly/skipped.log
	exit 0
fi

{
	echo "OpenTech2 nightly, $(date '+%F %T')"
	echo "branch $(git rev-parse --abbrev-ref HEAD) at $(git log --oneline -1)"
	git fetch -q origin 2>/dev/null && echo "vs origin/main: $(git rev-list --left-right --count origin/main...HEAD | awk '{print $2 " ahead, " $1 " behind"}')"
	echo
} > "$report"

summary=""

# build
if nice -n 10 cmake --build --preset default > build/nightly/build.log 2>&1; then
	echo "build: ok" >> "$report"
else
	echo "build: FAILED (build/nightly/build.log)" >> "$report"
	notify "build FAILED, see $engine/$report"
	exit 1
fi

# checks: the windowed ones need a display; without one only the hidden ones run
mode=""
if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
	mode="--headless"
fi
nice -n 10 python3 tests/run_all.py $mode > build/nightly/checks.log 2>&1
checks=$(grep -E "checks passed" build/nightly/checks.log | tail -1 | cut -d. -f1)
failed=$(grep -E "^Failed:" build/nightly/checks.log | tail -1)
echo "checks${mode:+ (headless)}: ${checks:-no result} ${failed}" >> "$report"
summary="${checks:-checks: no result}${failed:+, $failed}"

# cppcheck, like CI
nice -n 10 tools/ci/run_cppcheck.sh -o build/nightly/cppcheck.txt
findings=$(grep -c "|" build/nightly/cppcheck.txt)
echo "cppcheck: $findings findings" >> "$report"
cat build/nightly/cppcheck.txt >> "$report"
summary="$summary; cppcheck $findings"

# GitHub: last CI result per workflow on main, and open code scanning alerts
if command -v gh >/dev/null 2>&1; then
	echo >> "$report"
	echo "CI on main:" >> "$report"
	gh run list -R Rizilla-OG/OPtech2 -b main -L 20 --json name,conclusion,status \
		--jq 'unique_by(.name)[] | "  \(.name): \(.conclusion // .status)"' >> "$report" 2>/dev/null
	ci_failed=$(gh run list -R Rizilla-OG/OPtech2 -b main -L 20 --json name,conclusion \
		--jq '[unique_by(.name)[] | select(.conclusion == "failure")] | length' 2>/dev/null)
	alerts=$(gh api --paginate "repos/Rizilla-OG/OPtech2/code-scanning/alerts?state=open&per_page=100" \
		--jq '.[].number' 2>/dev/null | wc -l)
	echo "open code scanning alerts: $alerts" >> "$report"
	summary="$summary; CI failures ${ci_failed:-?}; alerts $alerts"
fi

echo >> "$report"
echo "summary: $summary" >> "$report"
notify "$summary"
