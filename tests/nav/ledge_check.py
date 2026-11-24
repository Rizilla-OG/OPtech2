#!/usr/bin/env python3
"""Ledge drop check for OpenTech2 (M2: monster navigation, phase 4).

On optech2_ledge (tests/maps/make_ledge_map.py): a low ledge (128 high)
and a high one (320 high) along the north wall, no way up to either.

On the dedicated server (no window):
  - the mesh has ledge drops,
  - a path from the low ledge to the floor is found and drops (a corner up
    on the ledge, the next on the floor),
  - from the floor up onto the ledge there's none (drops are one way),
  - from the high ledge there's none (more than 256 down).

In the game (no window), with the player on the floor (god mode):
  - a Berserker (melee only) on the low ledge walks off it (g_monster_nav 2
    logs it) and hits the player. Stock (g_monster_nav 0) is shown for
    comparison: a Berserker sometimes jumps down on its own.
  - one on the high ledge stays up.
  - optech2: drop heights per monster: a Supertank (drops 64 at most) on the
    low ledge stays up; with a monster definition giving it "drop_height"
    "128" it walks off.
Positions are sampled every second (listentities).

Usage:
  ledge_check.py
Needs build/release/OpenTech2 and OpenTech2-server, the game data and the
test map. About a minute.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
SERVER_BINARY = REPO_DIR / "build" / "release" / "OpenTech2-server"
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_ledge.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "ledge-check"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
MONSTER = "monster_berserk"
LOW_SPOT = "-256 128 152 0 270 0"    # on the low ledge near its lip (it sees the player over it)
HIGH_SPOT = "352 128 344 0 270 0"   # on the high ledge near its lip
SPAWN_MS, END_MS = 3500, 23500


def home_with_map(home, config_name, lines, defs=None):
    user_dir = Path(home) / ".opentech2" / "baseq2"
    (user_dir / "maps").mkdir(parents=True, exist_ok=True)
    if defs:
        (user_dir / "defs").mkdir(exist_ok=True)
        (user_dir / "defs" / "baseq2.monsters").write_text(defs)
    shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
    (user_dir / config_name).write_text("\n".join(lines) + "\n")


def run_server(commands):
    with tempfile.TemporaryDirectory(prefix="opentech2-ledge-") as home:
        home_with_map(home, "ledge.cfg", commands + ["quit"])
        result = subprocess.run([str(SERVER_BINARY), "-datadir", str(DATA_DIR), "+set", "fixedtime", "16667", "+exec", "ledge.cfg"],
                                env=dict(os.environ, HOME=home), stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=120, check=False)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / "paths.log").write_text(log)
    return log


def run_game(name, nav, spot, monster=MONSTER, defs=None):
    lines = ["set g_damagelog 1", f"set g_monster_nav {nav}", "map optech2_ledge", 'delay 3000 "god"',
             f'delay {SPAWN_MS} "cmd spawnentity {monster} {spot}"']
    lines += [f'delay {ms} "cmd listentities monsters"' for ms in range(SPAWN_MS + 1000, END_MS, 1000)]
    lines += [f"delay {END_MS} quit"]
    with tempfile.TemporaryDirectory(prefix="opentech2-ledge-") as home:
        home_with_map(home, "ledgegame.cfg", lines, defs)
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set",
                                     "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
                                     "+set", "s_initsound", "0", "+set", "cheats", "1", "+set", "skill", "1",
                                     "+exec", "ledgegame.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    hits = [float(t) for t in re.findall(rf"damagelog time ([\d.]+) attacker {monster} target player", log)]
    route = [tuple(float(v) for v in match) for match in
             re.findall(rf"^{monster}: (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)", log, re.M)]
    return hits, route, log


def paths(log):
    """[(status, [corners])] for every nav_path in the log."""
    found = []
    for match in re.finditer(r"nav_path (\w+) \d+ points \d+ units\n((?:  -?\d+ -?\d+ -?\d+\n)*)", log):
        found.append((match.group(1), [tuple(float(v) for v in line.split()) for line in match.group(2).splitlines()]))
    return found


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_ledge_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    log = run_server(["map optech2_ledge", "nav_path -256 320 128 -256 -320 0", "nav_path -256 -320 0 -256 320 128",
                      "nav_path 352 320 320 352 -320 0", "nav_path -256 320 128 -256 -320 0 large"])
    drops = re.search(r"Navigation: optech2_ledge, \d+ polygons, built in \d+ ms \(.*\), (\d+) drops", log)
    report(bool(drops) and int(drops.group(1)) > 0, f"the mesh has ledge drops: {drops.group(1) if drops else 0}")
    found = paths(log)
    if len(found) == 4:
        status, corners = found[0]
        dropped = any(a[2] > 100 and b[2] < 10 for a, b in zip(corners, corners[1:]))
        report(status == "found" and dropped, f"low ledge to the floor: {status}, drops off it: {dropped}")
        report(found[1][0] != "found", f"floor up onto the ledge: {found[1][0]} (drops are one way)")
        report(found[2][0] != "found", f"high ledge (320) to the floor: {found[2][0]} (too far to drop)")
        report(found[3][0] == "found", f"low ledge to the floor for a large monster: {found[3][0]}")
    else:
        report(False, f"four paths answered ({len(found)})")

    def route_text(route):
        return " > ".join(f"({x:.0f},{y:.0f},{z:.0f})" for x, y, z in route[::3][:7])

    hits, route, nav_log = run_game("low-nav", 2, LOW_SPOT)
    walked_off = re.search(rf"{MONSTER} walks off a ledge at (-?\d+) (-?\d+) (-?\d+)", nav_log)
    report(bool(walked_off) and any(z < 60 for _, _, z in route),
           f"with paths the {MONSTER} walked off the low ledge" +
           (f" at ({walked_off.group(1)},{walked_off.group(2)},{walked_off.group(3)})" if walked_off else "") +
           f": {route_text(route)}")
    report(bool(hits), f"and hit the player ({len(hits)} hits" +
           (f", the first {hits[0] - SPAWN_MS / 1000:.1f} s after it spawned)" if hits else ")"))
    hits, route, stock_log = run_game("low-stock", 0, LOW_SPOT)
    stock_first = f"first after {hits[0] - SPAWN_MS / 1000:.1f} s" if hits else "none"
    print(f"info  stock chase (g_monster_nav 0): {len(hits)} hits ({stock_first}); route {route_text(route)}")
    hits, route, high_log = run_game("high-nav", 2, HIGH_SPOT)
    report(bool(route) and all(z > 300 for _, _, z in route) and not hits and "walks off" not in high_log,
           f"on the high ledge it stayed up: {route_text(route)}")

    # optech2: drop heights per monster; the Supertank needs room, so a bit back from the lip
    tank_spot = "-256 136 152 0 270 0"
    _, route, tank_log = run_game("supertank", 2, tank_spot, "monster_supertank")
    report(bool(route) and all(z > 100 for _, _, z in route) and "walks off" not in tank_log,
           f"a Supertank (drops 64) stays on the 128 ledge: {route_text(route)}")
    _, route, tank_def_log = run_game("supertank-def", 2, tank_spot, "monster_supertank",
                                      '{\n"classname" "monster_supertank"\n"drop_height" "128"\n}\n')
    report("monster_supertank walks off a ledge" in tank_def_log and any(z < 60 for _, _, z in route),
           f"with \"drop_height\" \"128\" it walks off: {route_text(route)}")
    high_log += tank_log + tank_def_log

    errors = [line for line in (log + nav_log + stock_log + high_log).splitlines()
              if "ERROR" in line or "crashed" in line or "nav: " in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))
    passed = all(results)
    print(f"\nLedge drop check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
