#!/usr/bin/env python3
"""Monster lift check for OpenTech2 (M2: monster navigation, phase 4).

On optech2_plat (tests/maps/make_plat_map.py): a balcony 320 high (too
high to drop from) and a func_plat beside it, the only way up.

On the dedicated server (no window):
  - the mesh has the lift (in the mesh where it rests, linked to the
    balcony),
  - paths go over it, up and down, for all three monster sizes.

In the game (no window), the player (god mode) on one level, a Berserker
(melee only) on the other:
  - up: from the floor it walks onto the lift, rides it up (g_monster_nav 2
    logs it), steps off onto the balcony and hits the player,
  - down: on the balcony it calls the lift up, rides it down and hits the
    player on the floor.
Stock (g_monster_nav 0) is shown for comparison: func_plat only starts
for players, so it stays below. Positions are sampled every second.

Usage:
  plat_check.py
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
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_plat.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "plat-check"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
MONSTER = "monster_berserk"
FLOOR_SPOT = "256 -400 24 0 90 0"       # on the floor, far enough back to see up onto the balcony
BALCONY_SPOT = "-256 168 344 0 270 0"   # up on the balcony near its edge (it sees the player below)
PLAYER_ON_BALCONY = "256 168 344"       # near the edge, seen from the floor
SPAWN_MS, END_MS = 4000, 34000
BALCONY_HEIGHT = 320


def home_with_map(home, config_name, lines):
    user_dir = Path(home) / ".opentech2" / "baseq2"
    (user_dir / "maps").mkdir(parents=True, exist_ok=True)
    shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
    (user_dir / config_name).write_text("\n".join(lines) + "\n")


def run_server(commands):
    with tempfile.TemporaryDirectory(prefix="opentech2-plat-") as home:
        home_with_map(home, "plat.cfg", commands + ["quit"])
        result = subprocess.run([str(SERVER_BINARY), "-datadir", str(DATA_DIR), "+exec", "plat.cfg"],
                                env=dict(os.environ, HOME=home), stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=120, check=False)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / "paths.log").write_text(log)
    return log


def run_game(name, nav, monster_spot, player_spot=None):
    lines = ["set g_damagelog 1", f"set g_monster_nav {nav}", "map optech2_plat", 'delay 3000 "god"']
    if player_spot:
        lines.append(f'delay 3200 "cmd teleport {player_spot}"')
    lines.append(f'delay {SPAWN_MS} "cmd spawnentity {MONSTER} {monster_spot}"')
    lines += [f'delay {ms} "cmd listentities monsters"' for ms in range(SPAWN_MS + 1000, END_MS, 1000)]
    lines += [f"delay {END_MS} quit"]
    with tempfile.TemporaryDirectory(prefix="opentech2-plat-") as home:
        home_with_map(home, "platgame.cfg", lines)
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set",
                                     "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
                                     "+set", "s_initsound", "0", "+set", "cheats", "1", "+set", "skill", "1",
                                     "+exec", "platgame.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=120, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    hits = [float(t) for t in re.findall(rf"damagelog time ([\d.]+) attacker {MONSTER} target player", log)]
    route = [tuple(float(v) for v in match) for match in
             re.findall(rf"^{MONSTER}: (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)", log, re.M)]
    return hits, route, log


def paths(log):
    """[(status, [corners])] for every nav_path in the log."""
    found = []
    for match in re.finditer(r"nav_path (\w+) \d+ points \d+ units\n((?:  -?\d+ -?\d+ -?\d+\n)*)", log):
        found.append((match.group(1), [tuple(float(v) for v in line.split()) for line in match.group(2).splitlines()]))
    return found


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_plat_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    up, down = "0 -320 0 0 320 320", "0 320 320 0 -320 0"
    log = run_server(["map optech2_plat", f"nav_path {up}", f"nav_path {down}", f"nav_path {up} medium",
                      f"nav_path {up} large"])
    lifts = re.search(r"Navigation: optech2_plat, .*, (\d+) lifts", log)
    report(bool(lifts) and int(lifts.group(1)) == 1, f"the mesh has the lift: {lifts.group(1) if lifts else 0}")
    found = paths(log)
    if len(found) == 4:
        for (status, corners), text in zip(found, ("floor up to the balcony", "balcony down to the floor",
                                                   "up, medium monster", "up, large monster")):
            report(status == "found" and any(abs(a[2] - b[2]) > 250 for a, b in zip(corners, corners[1:])),
                   f"{text}: {status}, over the lift")
    else:
        report(False, f"four paths answered ({len(found)})")

    def route_text(route):
        return " > ".join(f"({x:.0f},{y:.0f},{z:.0f})" for x, y, z in route[::3][:9])

    def first_hit(hits):
        return f", the first {hits[0] - SPAWN_MS / 1000:.1f} s after it spawned" if hits else ""

    hits, route, up_log = run_game("up-nav", 2, FLOOR_SPOT, PLAYER_ON_BALCONY)
    report("rides a lift up" in up_log and any(z > BALCONY_HEIGHT for _, _, z in route),
           f"up: the {MONSTER} rode the lift to the balcony: {route_text(route)}")
    report(bool(hits), f"and hit the player ({len(hits)} hits{first_hit(hits)})")
    hits, route, down_log = run_game("down-nav", 2, BALCONY_SPOT)
    report("rides a lift down" in down_log and any(z < 100 for _, _, z in route),
           f"down: it called the lift and rode it to the floor: {route_text(route)}")
    report(bool(hits), f"and hit the player ({len(hits)} hits{first_hit(hits)})")
    hits, route, stock_log = run_game("up-stock", 0, FLOOR_SPOT, PLAYER_ON_BALCONY)
    print(f"info  stock chase (g_monster_nav 0), up: {len(hits)} hits; route {route_text(route)}")

    errors = [line for line in (log + up_log + down_log + stock_log).splitlines()
              if "ERROR" in line or "crashed" in line or "nav: " in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))
    passed = all(results)
    print(f"\nLift check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
