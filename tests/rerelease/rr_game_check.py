#!/usr/bin/env python3
"""Re-release game check for OpenTech2: builds id's stock re-release game
library (quake2-rerelease-dll, game API 2023) for Linux out of tree, puts
it in a mod folder and plays base1 with it on the re-release data:
  - the server loads it as a re-release library, the client game module
    comes from the same library,
  - the player walks and jumps (positions from viewpos),
  - the map's monsters spawn and move (the game's own entity info,
    listed with rr_edicts),
  - a spawned soldier is shot with the railgun and dies,
  - no errors.
Screenshots land in build/rr-game-check/ and docs/screenshots/.

Usage:
  rr_game_check.py [<quake2-rerelease-dll>/rerelease]
Default source: ../research/rerelease-stock/rerelease next to the repo
(a clone at commit 8dc1fc9). Needs cmake, g++ and network access the first
time (the game uses the fmt and jsoncpp libraries, fetched by cmake).
Hidden software renderer, temporary HOME.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
GAME_BUILD_DIR = REPO_DIR / "build" / "rerelease-stock-game"
OUTPUT_DIR = REPO_DIR / "build" / "rr-game-check"
DOC_SHOTS = REPO_DIR / "docs" / "screenshots"
DEFAULT_SOURCE = REPO_DIR.parent / "research" / "rerelease-stock" / "rerelease"
RERELEASE_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2/rerelease"
MOD_NAME = "rrstock"

# what the player does; delays in ms after the map starts loading
SCRIPT = """set cheats 1
map base1
delay 3000 "rr_edicts"
delay 3100 "spawn monster_soldier"
delay 3200 "give all"
delay 3300 "use railgun"
delay 4300 "rr_edicts monster_soldier"
delay 4400 "screenshot png"
delay 4600 "+attack"
delay 4800 "screenshot png"
delay 5100 "-attack"
delay 6500 "rr_edicts monster_soldier"
delay 6600 "screenshot png"
delay 6700 "viewpos"
delay 6800 "+forward"
delay 8300 "-forward"
delay 8400 "viewpos"
delay 8500 "+moveup"
delay 8700 "viewpos"
delay 8800 "-moveup"
delay 9600 "rr_edicts"
delay 9700 "quit"
"""


def build_game(source_dir):
    configure = subprocess.run(["cmake", "-S", str(REPO_DIR / "tools" / "rerelease" / "stock_game"),
                                "-B", str(GAME_BUILD_DIR), f"-DRR_SOURCE_DIR={source_dir}",
                                "-DCMAKE_BUILD_TYPE=Release"], capture_output=True, text=True)
    if configure.returncode != 0:
        sys.exit(f"game configure failed:\n{configure.stderr[-3000:]}")
    build = subprocess.run(["cmake", "--build", str(GAME_BUILD_DIR), "-j", str(os.cpu_count() or 4)],
                           capture_output=True, text=True)
    if build.returncode != 0:
        sys.exit(f"game build failed:\n{build.stdout[-3000:]}")
    return GAME_BUILD_DIR / "game_x64.so"


def positions(log):
    return [tuple(float(v) for v in m.groups()) for m in
            re.finditer(r"position: (-?[\d.]+) (-?[\d.]+) (-?[\d.]+),", log)]


def edict_dumps(log):
    """each rr_edicts listing as {number: (classname, health, origin)}"""
    dumps, current = [], {}
    for line in log.splitlines():
        entry = re.match(r"rr_edict (\d+) (\S*) health (-?\d+) origin (\S+) (\S+) (\S+)", line)
        if entry:
            current[int(entry.group(1))] = (entry.group(2), int(entry.group(3)),
                                            tuple(float(v) for v in entry.groups()[3:]))
        elif line.startswith("rr_edicts:"):
            dumps.append(current)
            current = {}
    return dumps


def main():
    source_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_SOURCE
    if not (source_dir / "g_main.cpp").exists():
        sys.exit(f"no re-release game source in {source_dir}")
    if not (RERELEASE_DATA_DIR / "baseq2").exists():
        sys.exit(f"no re-release data in {RERELEASE_DATA_DIR}")
    game_library = build_game(source_dir)

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    log_path = OUTPUT_DIR / "client.log"
    with tempfile.TemporaryDirectory(prefix="optech2-rr-game-") as home:
        mod_dir = Path(home) / ".opentech2" / MOD_NAME
        mod_dir.mkdir(parents=True)
        shutil.copy(game_library, mod_dir / "game_x64.so")
        (mod_dir / "c.cfg").write_text(SCRIPT)
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        with open(log_path, "w") as log_file:
            subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(RERELEASE_DATA_DIR),
                            "+set", "game", MOD_NAME, "+set", "fixedtime", "16667",
                            "+set", "vid_renderer", "soft", "+set", "r_vsync", "0",
                            "+set", "s_initsound", "0", "+exec", "c.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=90, check=False)
        shots = sorted((mod_dir / "scrnshot").glob("*.png")) if (mod_dir / "scrnshot").exists() else []
        names = ["soldier", "railgun", "after"]
        DOC_SHOTS.mkdir(parents=True, exist_ok=True)
        for shot, name in zip(shots, names):
            shutil.copy(shot, OUTPUT_DIR / f"{name}.png")
            shutil.copy(shot, DOC_SHOTS / f"m2_rr_game_{name}.png")

    log = log_path.read_text(errors="replace")
    report("re-release game library (game API 2023)" in log, "server loaded the re-release game library")
    report("client game: loaded (cgame API 2022)" in log, "client game module from the same library")
    report("Outer Base" in log, "base1 (Outer Base) loaded")

    seen = positions(log)
    if len(seen) >= 3:
        walked = ((seen[1][0] - seen[0][0]) ** 2 + (seen[1][1] - seen[0][1]) ** 2) ** 0.5
        report(walked > 200, f"walked {walked:.0f} units in 1.5 s")
        report(seen[2][2] > seen[1][2] + 8, f"jumped: height {seen[1][2]:.0f} -> {seen[2][2]:.0f}")
    else:
        report(False, f"positions printed: {len(seen)} of 3")

    dumps = edict_dumps(log)
    if len(dumps) == 4:
        first, soldiers_before, soldiers_after, last = dumps
        monsters = {n: e for n, e in first.items() if e[0].startswith("monster_")}
        report(len(monsters) >= 10, f"{len(monsters)} monsters spawned on the map")
        moved = [n for n, e in monsters.items() if n in last and last[n][2] != e[2]]
        report(len(moved) >= 3, f"{len(moved)} monsters moved")
        # a new soldier (it may reuse a freed entity number)
        spawned = [n for n in soldiers_before if first.get(n, ("",))[0] != "monster_soldier"]
        if spawned:
            number = spawned[0]
            health_before = soldiers_before[number][1]
            health_after = soldiers_after[number][1] if number in soldiers_after else None
            report(health_before > 0, f"spawned soldier #{number}, health {health_before}")
            report(health_after is None or health_after <= 0,
                   f"railgun killed it (health after: {'gone' if health_after is None else health_after})")
        else:
            report(False, "spawned soldier not listed")
    else:
        report(False, f"rr_edicts listings: {len(dumps)} of 4")

    bad = [line for line in log.splitlines() if "(BAD" in line]
    report(not bad, "every game message readable" + (f" ({bad[0].strip()[:90]})" if bad else ""))
    errors = [line for line in log.splitlines() if "ERROR" in line or "Game Error" in line]
    report(not errors, "no errors" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nRe-release game check {'passed' if passed else 'FAILED'}. Log and screenshots in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
