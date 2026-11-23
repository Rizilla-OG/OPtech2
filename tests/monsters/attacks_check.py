#!/usr/bin/env python3
"""Re-release monster attacks check for OpenTech2, on both data sets.

OpenTech2 runs on the classic Quake II data or the re-release's. Some of the
re-release's monster attacks need frames only its models have (the infantry's
run-and-shoot, run201-208; the gunner's second grenade animation,
attak305-324), so the game counts the model's frames and keeps the stock
attack on the classic data.

On optech2_cover (tests/maps/make_cover_map.py), in the room's open north
half, the player (god mode) stands in a corner and a monster spawns about 720
units away, once per data set. With g_damagelog 1 the game logs every
monster shot with its muzzle flash, which tells the attacks apart:
  - infantry: run-and-shoot flashes (MZ2_INFANTRY_MACHINEGUN_14-21) only
    with the re-release data,
  - gunner: second-animation grenade flashes (MZ2_GUNNER_GRENADE2_1-4) only
    with the re-release data, the stock grenade flashes on the classic data,
  - shotgun soldier: fires on both (it cocks after every shot).

Usage:
  attacks_check.py
Needs build/release/OpenTech2, build/maps/optech2_cover.bsp and the game
data (the re-release half is skipped without it). Runs with no window, about
two and a half minutes.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_cover.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "attacks-check"
STEAM_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
DATA_SETS = {"classic": STEAM_DIR, "rerelease": STEAM_DIR / "rerelease"}
PLAYER_SPOT = "480 480 24 0 225 0"      # north-east corner, facing the room
MONSTER_SPOT = "-300 -10 40 0 60 0"     # north half, near the wall, facing the player

INFANTRY_RUN_FLASHES = set(range(232, 240))   # MZ2_INFANTRY_MACHINEGUN_14-21
GUNNER_GRENADE_FLASHES = set(range(53, 57))   # MZ2_GUNNER_GRENADE_1-4
GUNNER_GRENADE2_FLASHES = set(range(257, 261))  # MZ2_GUNNER_GRENADE2_1-4


def run_monster(data_name, monster):
    """Spawns one monster against the player; returns ({flash: shots}, log)."""
    script = ["set g_damagelog 1", "map optech2_cover", 'delay 2000 "god"', f'delay 2100 "teleport {PLAYER_SPOT}"',
              f'delay 2300 "cmd spawnentity {monster} {MONSTER_SPOT}"', 'delay 22000 "quit"']
    with tempfile.TemporaryDirectory(prefix="opentech2-attacks-") as home:
        user_dir = Path(home) / ".opentech2" / "baseq2"
        (user_dir / "maps").mkdir(parents=True)
        shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
        (user_dir / "attacks.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run([str(CLIENT_BINARY), "-datadir", str(DATA_SETS[data_name]),
                                     "+set", "fixedtime", "16667", "+set", "vid_renderer", "soft", "+set", "r_vsync", "0",
                                     "+set", "cl_maxfps", "60", "+set", "s_initsound", "0", "+set", "skill", "1",
                                     "+exec", "attacks.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    (OUTPUT_DIR / f"{data_name}-{monster}.log").write_text(log)
    flashes = Counter(int(flash) for flash in re.findall(rf"shotlog time [\d.]+ monster {monster} flash (\d+)", log))
    return flashes, log


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_cover_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    logs = []
    for data_name, data_dir in DATA_SETS.items():
        if not (data_dir / "baseq2").exists():
            print(f"skip  {data_name} data: {data_dir} has no baseq2/")
            continue
        rerelease = data_name == "rerelease"

        flashes, log = run_monster(data_name, "monster_infantry")
        logs.append(log)
        run_shots = sum(count for flash, count in flashes.items() if flash in INFANTRY_RUN_FLASHES)
        report(sum(flashes.values()) > 0 and (run_shots > 0) == rerelease,
               f"{data_name}: infantry fires ({sum(flashes.values())} shots), run-and-shoot shots {run_shots}"
               f" ({'expected' if rerelease else 'none expected'})")

        flashes, log = run_monster(data_name, "monster_gunner")
        logs.append(log)
        stock = sum(count for flash, count in flashes.items() if flash in GUNNER_GRENADE_FLASHES)
        second = sum(count for flash, count in flashes.items() if flash in GUNNER_GRENADE2_FLASHES)
        report((stock + second) > 0 and (second > 0) == rerelease,
               f"{data_name}: gunner grenades {stock} stock, {second} second animation"
               f" ({'second expected' if rerelease else 'stock only expected'})")

        flashes, log = run_monster(data_name, "monster_soldier")
        logs.append(log)
        report(sum(flashes.values()) >= 3, f"{data_name}: shotgun soldier keeps firing ({sum(flashes.values())} shots)")

    bad = [line for log in logs for line in log.splitlines() if re.search(r"ERROR|Segmentation|assert", line)]
    report(not bad, "no errors in the logs" + (f": {bad[0]}" if bad else ""))
    print(f"logs: {OUTPUT_DIR}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
