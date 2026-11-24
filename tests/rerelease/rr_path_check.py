#!/usr/bin/env python3
"""Re-release pathfinding check for OpenTech2: id's stock re-release game
library (built as in rr_game_check.py) asks the engine for monster paths
(GetPathToGoal, its M_NavPathToGoal); the engine answers from our
navigation mesh.

On optech2_nav (tests/maps/make_nav_map.py: a room split by a low wall,
seen over but not climbed, with a slot near one end and a gap at the
other) a Berserker, melee only and with its jumping off (spawnflag 8: it
would jump the wall), is spawned in the north half and the player
stands in the south half (god mode), in view across the wall:
  - the game asks for paths and gets them (sv_rr_pathlog: return code
    in progress, more than one point),
  - the Berserker goes through an opening (the slot or the gap) into the
    player's half and comes within reach of the player in under
    REACH_SECONDS (it takes the slot right away; without paths it bumps
    along the wall and finds the gap much later, if at all),
  - no errors.
  - optech2: a Tank (too wide for the slot) asks for paths on the mesh for
    its size: they lead east to the gap, never into the slot.
Without paths the re-release's monsters run straight at the wall and stay
behind it.

Usage:
  rr_path_check.py [<quake2-rerelease-dll>/rerelease]
Needs the test map (python3 tests/maps/make_nav_map.py). Hidden software
renderer, temporary HOME, about 40 s.
"""

import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_game_check  # noqa: E402  (shares the game build and paths)

OUTPUT_DIR = rr_game_check.REPO_DIR / "build" / "rr-path-check"
MAP_FILE = rr_game_check.REPO_DIR / "build" / "maps" / "optech2_nav.bsp"
PLAYER_SPOT = (-256, -320)
GAP_X = 512 - 192  # the wall's +x end (make_nav_map.py)
SLOT_X = (-480, -416)  # the slot near its -x end
REACH = 110  # a Berserker's melee reach plus both half widths, roughly
END_MS = 33000
REACH_SECONDS = 10
SPAWN_MS = 2900

# spawn puts the monster where the player aims: stand at its spot looking
# straight down, spawn, then go to the player's spot facing north
SCRIPT = ["set cheats 1", "set sv_rr_pathlog 1", "map optech2_nav", 'delay 2500 "god"',
          'delay 2600 "teleport -256 320 24 90 270 0"', 'delay 2900 "spawn monster_berserk spawnflags 8"',
          f'delay 3100 "teleport {PLAYER_SPOT[0]} {PLAYER_SPOT[1]} 24 0 90 0"']
SCRIPT += [f'delay {ms} "rr_edicts monster_berserk"' for ms in range(4000, END_MS, 1000)]
SCRIPT += [f'delay {END_MS} "quit"']


# optech2: the same with a Tank, whose paths must use the gap (it doesn't fit the slot)
TANK_SCRIPT = [line.replace("monster_berserk spawnflags 8", "monster_tank") for line in SCRIPT[:7]]
TANK_SCRIPT += ['delay 12000 "quit"']


def run(script, game_library, name):
    with tempfile.TemporaryDirectory(prefix="optech2-rr-path-") as home:
        mod_dir = Path(home) / ".opentech2" / rr_game_check.MOD_NAME
        (mod_dir / "maps").mkdir(parents=True)
        shutil.copy(game_library, mod_dir / "game_x64.so")
        shutil.copy(MAP_FILE, mod_dir / "maps" / MAP_FILE.name)
        (mod_dir / "c.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        log_path = OUTPUT_DIR / f"{name}.log"
        with open(log_path, "w") as log_file:
            subprocess.run(["stdbuf", "-oL", str(rr_game_check.CLIENT_BINARY),
                            "-datadir", str(rr_game_check.RERELEASE_DATA_DIR),
                            "+set", "game", rr_game_check.MOD_NAME, "+set", "vid_renderer", "soft",
                            "+set", "s_initsound", "0", "+set", "skill", "1", "+exec", "c.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=90, check=False)
        return log_path.read_text(errors="replace")


def main():
    source_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else rr_game_check.DEFAULT_SOURCE
    if not (source_dir / "g_main.cpp").exists():
        sys.exit(f"no re-release game source in {source_dir}")
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_nav_map.py")
    game_library = rr_game_check.build_game(source_dir)

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    with tempfile.TemporaryDirectory(prefix="optech2-rr-path-") as home:
        mod_dir = Path(home) / ".opentech2" / rr_game_check.MOD_NAME
        (mod_dir / "maps").mkdir(parents=True)
        shutil.copy(game_library, mod_dir / "game_x64.so")
        shutil.copy(MAP_FILE, mod_dir / "maps" / MAP_FILE.name)
        (mod_dir / "c.cfg").write_text("\n".join(SCRIPT) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        log_path = OUTPUT_DIR / "path.log"
        with open(log_path, "w") as log_file:
            subprocess.run(["stdbuf", "-oL", str(rr_game_check.CLIENT_BINARY),
                            "-datadir", str(rr_game_check.RERELEASE_DATA_DIR),
                            "+set", "game", rr_game_check.MOD_NAME, "+set", "vid_renderer", "soft",
                            "+set", "s_initsound", "0", "+set", "skill", "1", "+exec", "c.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=90, check=False)
        log = log_path.read_text(errors="replace")

    route = [(float(x), float(y)) for x, y in
             re.findall(r"rr_edict \d+ monster_berserk health \d+ origin (-?[\d.]+) (-?[\d.]+)", log)]
    shown = " > ".join(f"({x:.0f},{y:.0f})" for x, y in route[::3][:10])

    answers = re.findall(r"^rr_path .*: code (\d+), (\d+) points", log, re.M)
    good = [a for a in answers if int(a[0]) == 4 and int(a[1]) > 1]  # IN_PROGRESS
    report(bool(good), f"the game asked for paths and got them ({len(good)} of {len(answers)} answers on the way)")
    report(bool(route) and route[0][1] > 40, f"the Berserker spawned behind the wall: {shown[:20] or 'none listed'}")
    went_through = any(x > GAP_X - 40 or SLOT_X[0] - 40 < x < SLOT_X[1] + 40 for x, y in route if abs(y) < 60)
    reached_south = any(y < -40 for _, y in route)
    report(went_through and reached_south, f"it went through an opening into the player's half: {shown}")
    # samples are a second apart from 4 s on
    reached = [4.0 + i - SPAWN_MS / 1000 for i, (x, y) in enumerate(route)
               if math.hypot(x - PLAYER_SPOT[0], y - PLAYER_SPOT[1]) < REACH]
    report(bool(reached) and reached[0] < REACH_SECONDS,
           f"it came within reach of the player {reached[0]:.0f} s after it spawned (under {REACH_SECONDS})"
           if reached else f"it came within reach of the player in {(END_MS - SPAWN_MS) / 1000:.0f} s")

    tank_log = run(TANK_SCRIPT, game_library, "tank")
    tank_paths = [(float(x), float(r)) for x, r in
                  re.findall(r"^rr_path .*: code 4, \d+ points, next (-?[\d.]+) -?[\d.]+ -?[\d.]+, mover (\d+) x \d+",
                             tank_log, re.M)]
    wide = [x for x, radius in tank_paths if radius >= 24]
    report(bool(wide) and all(x > SLOT_X[1] + 40 for x in wide),
           f"a Tank's paths are for its size and head for the gap, not the slot "
           f"({len(wide)} paths, first moves x {', '.join(f'{x:.0f}' for x in wide[:5]) or 'none'})")
    log += tank_log

    bad = [line for line in log.splitlines() if "(BAD" in line or "ERROR" in line or "Game Error" in line]
    report(not bad, "no errors" + (f" ({bad[0].strip()[:90]})" if bad else ""))

    passed = all(results)
    print(f"\nRe-release pathfinding check {'passed' if passed else 'FAILED'}. Log in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
