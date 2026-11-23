#!/usr/bin/env python3
"""Clear-shot check for OpenTech2 (re-release AI port: M_CheckClearShot).

On optech2_cover (tests/maps/make_cover_map.py) a machine gun soldier stands
behind a waist-high wall that runs wall to wall, the player on the far side.
Their eyes see each other over the wall, so stock Quake II's attack check (an
eye-to-eye trace) lets the soldier fire, but its gun is below the wall's top
and every bullet strikes the wall. With the re-release's clear-shot check it
holds fire. Then the player steps over to the soldier's side, and it must
shoot, so the check doesn't pass by a soldier that never fires.

(A machine gun soldier: the other soldiers' run-and-shoot attack is left
unchecked, as in the re-release, and infantry hardly fire at this range.)

With g_damagelog 1 the game logs every monster shot (its muzzle flash, also
shots the wall stops at the muzzle) and every hit.

Usage:
  clear_shot_check.py
Needs build/release/OpenTech2, build/maps/optech2_cover.bsp and the game
data. Runs with no window, about half a minute.
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
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_cover.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "clear-shot-check"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
MONSTER = "monster_soldier_ss"
CROSS_MS = 12000                        # the player steps over to the soldier's side
CLEAR_SPOT = "0 300 24 0 270 0"         # north side, facing the soldier
QUIT_MS = 20000


def run_game():
    """Plays the map once with the player in god mode; returns the game log."""
    script = ["set g_damagelog 1", "map optech2_cover", 'delay 2000 "god"', 'delay 2100 "cmd playerinfo"',
              f'delay {CROSS_MS} "teleport {CLEAR_SPOT}"', f'delay {CROSS_MS + 50} "cmd playerinfo"',
              f'delay {QUIT_MS} "quit"']
    with tempfile.TemporaryDirectory(prefix="opentech2-clearshot-") as home:
        user_dir = Path(home) / ".opentech2" / "baseq2"
        (user_dir / "maps").mkdir(parents=True)
        shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
        (user_dir / "clearshot.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run([str(CLIENT_BINARY), "-datadir", str(DATA_DIR),
                                     "+set", "fixedtime", "16667", "+set", "vid_renderer", "soft", "+set", "r_vsync", "0",
                                     "+set", "cl_maxfps", "60", "+set", "s_initsound", "0", "+set", "skill", "1",
                                     "+exec", "clearshot.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
            return result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            return (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_cover_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    log = run_game()
    (OUTPUT_DIR / "clearshot.log").write_text(log)

    hits = re.findall(r"damagelog time ([\d.]+) attacker (\S+) target (\S+) damage", log)
    shots = [float(time) for time, monster in re.findall(r"shotlog time ([\d.]+) monster (\S+)", log) if monster == MONSTER]
    on_player = [float(time) for time, attacker, target in hits if attacker == MONSTER and target == "player"]
    info_times = [float(time) for time in re.findall(r"playerinfo time ([\d.]+)", log)]
    crossed = info_times[1] if len(info_times) > 1 else None
    before = [time for time in on_player if crossed is None or time < crossed]
    shots_behind = [time for time in shots if crossed is None or time < crossed]
    after = [time for time in on_player if crossed is not None and time >= crossed]

    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    report(crossed is not None, "the map ran, the player crossed over")
    report(not shots_behind and not before, f"soldier behind cover holds fire: {len(shots_behind)} shots")
    report(bool(after),
           f"soldier fires once its shot is clear: {len(after)} hits on the player"
           + (f", first {after[0] - crossed:.1f} s after crossing" if after else ""))
    bad = [line for line in log.splitlines() if re.search(r"ERROR|Segmentation|assert", line)]
    report(not bad, "no errors in the log" + (f": {bad[0]}" if bad else ""))
    print(f"log: {OUTPUT_DIR / 'clearshot.log'}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
