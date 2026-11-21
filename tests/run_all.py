#!/usr/bin/env python3
"""Runs every OpenTech2 check and prints one summary.

Each check is its own script under tests/ (see its docstring for what it
tests); this runs them one after another, keeps each one's output in
build/run-all/<name>.log and exits 1 if any failed. Test maps that a check
needs are built first when missing (needs ericw-tools, tools/get_ericw_tools.py;
without them those checks are skipped).

Kinds of check:
  hidden   hidden window, dummy sound: runs anywhere, also on CI
  window   opens a real GL3 window for a moment
  sound    plays through the real sound device (quietly)

Usage:
  run_all.py                 every check
  run_all.py --headless      only hidden ones (CI, or while you play)
  run_all.py --quick         skip the slow ones (several minutes each)
  run_all.py --only lift pmove   only these (names as in the summary)
  run_all.py --list          list the checks
Build first: cmake --workflow --preset default
"""

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent
OUTPUT_DIR = REPO_DIR / "build" / "run-all"
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
MAPS_DIR = REPO_DIR / "build" / "maps"
TOOLS_DIR = REPO_DIR / "build" / "tools" / "ericw-tools"


class Check:
    def __init__(self, name, script, kind="hidden", slow=False, needs_map=None, arguments=()):
        self.name = name
        self.script = REPO_DIR / "tests" / script
        self.kind = kind
        self.slow = slow
        self.needs_map = needs_map  # (bsp name, builder script) or None
        self.arguments = list(arguments)


CHECKS = [
    Check("screenshots", "screenshots/screenshot_check.py", kind="window"),
    Check("cgame", "cgame/cgame_check.py"),
    Check("pmove", "pmove/pmove_check.py", slow=True),
    Check("lift", "pmove/lift_check.py", needs_map=("optech2_lift.bsp", "maps/make_lift_map.py")),
    Check("tickrate", "tickrate/tick_check.py"),
    Check("damage", "tickrate/damage_check.py"),
    Check("savegame", "savegame/save_check.py"),
    Check("multiplayer", "multiplayer/mp_check.py"),
    Check("network", "multiplayer/net_check.py", slow=True),
    Check("packet-split", "multiplayer/split_check.py"),
    Check("limits-map", "maps/limits_check.py", needs_map=("optech2_limits.bsp", "maps/make_limits_map.py")),
    Check("stress-map", "maps/stress_check.py", kind="window", slow=True,
          needs_map=("optech2_stress.bsp", "maps/make_stress_map.py")),
    Check("textures", "maps/texture_check.py", kind="window", slow=True),
    Check("iqm-models", "models/iqm_check.py"),
    Check("sound", "sound/sound_check.py", kind="sound"),
    Check("test-mod", "mod/mod_check.py", kind="window", slow=True),
    Check("mod-stack", "mod/stack_check.py"),
    Check("scripts", "mod/script_check.py"),
]


def prepare_map(check):
    """Builds the test map a check needs; returns a reason to skip, or None."""
    if not check.needs_map:
        return None
    bsp_name, builder = check.needs_map
    if (MAPS_DIR / bsp_name).exists():
        return None
    if not any((TOOLS_DIR / name).exists() for name in ("qbsp", "qbsp.exe")):
        return f"needs {bsp_name}: ericw-tools missing (python3 tools/get_ericw_tools.py)"
    result = subprocess.run([sys.executable, str(REPO_DIR / "tests" / builder)], capture_output=True, text=True)
    (OUTPUT_DIR / f"{check.name}-map.log").write_text(result.stdout + result.stderr)
    return None if result.returncode == 0 else f"building {bsp_name} failed, see {check.name}-map.log"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--headless", action="store_true", help="only checks that need no window or sound device")
    parser.add_argument("--quick", action="store_true", help="skip the slow checks")
    parser.add_argument("--only", nargs="+", metavar="NAME", help="run only these checks")
    parser.add_argument("--list", action="store_true", help="list the checks and exit")
    arguments = parser.parse_args()

    if arguments.list:
        for check in CHECKS:
            print(f"{check.name:<14}{check.kind:<8}{'slow' if check.slow else '':<6}tests/{check.script.relative_to(REPO_DIR / 'tests')}")
        return 0
    if arguments.only:
        unknown = set(arguments.only) - {check.name for check in CHECKS}
        if unknown:
            sys.exit(f"unknown check(s): {', '.join(sorted(unknown))}; see --list")
    if not CLIENT_BINARY.exists():
        sys.exit(f"Missing {CLIENT_BINARY}. Build first: cmake --workflow --preset default")

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    rows = []
    started_all = time.monotonic()
    for check in CHECKS:
        if arguments.only and check.name not in arguments.only:
            continue
        skip = None
        if arguments.headless and check.kind != "hidden":
            skip = f"needs a {check.kind} (--headless)"
        elif arguments.quick and check.slow and not arguments.only:
            skip = "slow (--quick)"
        else:
            skip = prepare_map(check)
        if skip:
            rows.append((check.name, "skip", 0.0, skip))
            print(f"{check.name:<14}skip   {skip}", flush=True)
            continue

        print(f"{check.name:<14}...", end="", flush=True)
        started = time.monotonic()
        log_path = OUTPUT_DIR / f"{check.name}.log"
        with open(log_path, "w") as log_file:
            try:
                result = subprocess.run([sys.executable, str(check.script), *check.arguments], cwd=REPO_DIR,
                                        stdout=log_file, stderr=subprocess.STDOUT, timeout=1800)
                status = "ok" if result.returncode == 0 else "FAIL"
            except subprocess.TimeoutExpired:
                status = "FAIL"
                log_file.write("\nrun_all: timed out after 30 minutes\n")
        seconds = time.monotonic() - started
        # the check's own last line is its verdict
        last_line = next((line for line in reversed(log_path.read_text(errors="replace").splitlines()) if line.strip()), "")
        rows.append((check.name, status, seconds, last_line))
        print(f"\r{check.name:<14}{status:<7}{seconds:5.0f} s  {last_line[:90]}", flush=True)

    failed = [row for row in rows if row[1] == "FAIL"]
    ran = [row for row in rows if row[1] != "skip"]
    print(f"\n{len(ran) - len(failed)} of {len(ran)} checks passed, {len(rows) - len(ran)} skipped, "
          f"{(time.monotonic() - started_all) / 60:.1f} min. Logs in {OUTPUT_DIR}")
    if failed:
        print("Failed: " + ", ".join(row[0] for row in failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
