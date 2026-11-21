#!/usr/bin/env python3
"""Navigation check for OpenTech2 (M2: monster navigation, phases 1 and 2).

On the dedicated server (no window):
  - optech2_nav (tests/maps/make_nav_map.py): a room split by a wall with a
    gap at one end. A path from the south half to the north half must go
    around the wall's end: complete, longer than the straight line, and
    every corner on the floor. A path along the open south half is close
    to the straight line. A point outside the map gives no path (one just
    off the mesh, like a player against a wall, snaps to the nearest floor
    within 32 units).
  - base1: the mesh is built, then loaded from its cache on the next start
    (same polygon count, much faster), and a path across the map is found.
  - our game library asks the engine for paths (GetPathToGoal through the
    optech2_server extension, the "navtest" cheat command): from the player
    start to the north half it's on its way (in progress) toward the gap,
    to a point beside the player it has reached the goal.

Usage:
  nav_check.py
Needs build/release/OpenTech2-server, the game data, and the test map
(built by run_all.py, or python3 tests/maps/make_nav_map.py).
"""

import math
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
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_nav.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "nav-check"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"

SOUTH = (-256.0, -320.0, 24.0)   # the player start's half
NORTH = (-256.0, 320.0, 24.0)    # behind the wall
SOUTH_EAST = (300.0, -320.0, 24.0)
OUTSIDE = (2000.0, 2000.0, 24.0)  # out of the map


def run_server(home, commands, log_name):
    user_dir = Path(home) / ".opentech2" / "baseq2"
    (user_dir / "maps").mkdir(parents=True, exist_ok=True)
    shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
    (user_dir / "nav.cfg").write_text("\n".join(commands + ["quit"]) + "\n")
    result = subprocess.run([str(SERVER_BINARY), "-datadir", str(DATA_DIR), "+exec", "nav.cfg"],
                            env=dict(os.environ, HOME=str(home)), stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, timeout=120, check=False)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / f"{log_name}.log").write_text(log)
    return log


def run_client(home, commands, log_name):
    """The game (no window) on the test map, running console commands."""
    user_dir = Path(home) / ".opentech2" / "baseq2"
    (user_dir / "maps").mkdir(parents=True, exist_ok=True)
    shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
    (user_dir / "navclient.cfg").write_text("\n".join(commands) + "\n")
    environment = dict(os.environ, HOME=str(home), SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    try:
        result = subprocess.run([str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set", "vid_renderer", "soft",
                                 "+set", "r_vsync", "0", "+set", "s_initsound", "0", "+set", "cheats", "1",
                                 "+set", "developer", "1", "+exec", "navclient.cfg"],
                                env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                timeout=60, check=False)
        log = result.stdout + result.stderr
    except subprocess.TimeoutExpired as timeout:
        log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    (OUTPUT_DIR / f"{log_name}.log").write_text(log)
    return log


def navtests(log):
    """[(code, points, length, first move point)] for every navtest answer."""
    return [(int(code), int(points), float(length), (float(x), float(y), float(z)))
            for code, points, length, x, y, z in
            re.findall(r"navtest code (\d+) points (\d+) length (\d+) first (-?\d+) (-?\d+) (-?\d+)", log)]


def paths(log):
    """[(status, corner count, length, [corners])] for every nav_path in the log."""
    found = []
    for match in re.finditer(r"nav_path (\w+) (\d+) points (\d+) units\n((?:  -?\d+ -?\d+ -?\d+\n)*)", log):
        corners = [tuple(float(v) for v in line.split()) for line in match.group(4).splitlines()]
        found.append((match.group(1), int(match.group(2)), float(match.group(3)), corners))
    return found


def path_command(start, goal):
    return "nav_path " + " ".join(f"{v:.0f}" for v in (*start, *goal))


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_nav_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    with tempfile.TemporaryDirectory(prefix="opentech2-nav-") as home:
        log = run_server(home, ["map optech2_nav", path_command(SOUTH, NORTH), path_command(SOUTH, SOUTH_EAST),
                                path_command(SOUTH, OUTSIDE)], "test-map")
        built = re.search(r"Navigation: optech2_nav, (\d+) polygons, built in (\d+) ms", log)
        report(bool(built), "test map: mesh built" + (f", {built.group(1)} polygons in {built.group(2)} ms"
                                                       if built else " (see the log)"))
        found = paths(log)
        if len(found) == 3:
            status, count, length, corners = found[0]
            straight = math.dist(SOUTH[:2], NORTH[:2])
            around = any(corner[0] > 512 - 192 - 32 for corner in corners)  # a corner at the wall's open end
            report(status == "found" and length > straight * 1.5 and around,
                   f"around the wall: {status}, {count} corners, {length:.0f} units "
                   f"(straight line {straight:.0f}; via the gap: {around})")
            report(all(abs(corner[2]) < 4 for corner in corners), "every corner on the floor (z near 0)")
            status, count, length, corners = found[1]
            straight = math.dist(SOUTH[:2], SOUTH_EAST[:2])
            report(status == "found" and length < straight * 1.05,
                   f"open floor: {status}, {length:.0f} units (straight line {straight:.0f})")
            report(found[2][0] == "none", f"a point outside the map: {found[2][0]}")
        else:
            report(False, f"three paths answered ({len(found)})")

        first = run_server(home, ["map base1", "nav_path 128 -320 24 -1248 1585 -24"], "base1-build")
        second = run_server(home, ["map base1"], "base1-cache")
        built = re.search(r"Navigation: base1, (\d+) polygons, built in (\d+) ms", first)
        loaded = re.search(r"Navigation: base1, (\d+) polygons, loaded in (\d+) ms", second)
        report(bool(built and loaded) and built.group(1) == loaded.group(1),
               "base1: built, then loaded from the cache" +
               (f" ({built.group(1)} polygons; {built.group(2)} ms, then {loaded.group(2)} ms)"
                if built and loaded else ""))
        found = paths(first)
        report(bool(found) and found[0][0] == "found",
               "base1: a path from the start to the far soldier" +
               (f", {found[0][1]} corners, {found[0][2]:.0f} units" if found else ""))
        report((Path(home) / ".opentech2" / "baseq2" / "navigation" / "base1.otnav").exists(),
               "cache file in the game folder: navigation/base1.otnav")

        client = run_client(home, ["map optech2_nav", 'delay 3000 "cmd navtest -256 320 0"',
                                   'delay 3200 "cmd navtest -252 -317 0"', "delay 3500 quit"], "game-library")
        report("optech2_server extension connected" in client,
               "the game library takes the engine's optech2_server extension")
        answers = navtests(client)
        if len(answers) == 2:
            code, points, length, first = answers[0]
            report(code == 4 and points >= 3 and first[0] > 0,
                   f"GetPathToGoal across the wall: code {code} (4 = in progress), {points} points, {length:.0f} units, "
                   f"first move toward x {first[0]:.0f} (the gap is at x > 128)")
            code = answers[1][0]
            report(code == 0, f"GetPathToGoal to a point beside the player: code {code} (0 = reached the goal)")
        else:
            report(False, f"two navtest answers ({len(answers)})")

    errors = [line for line in (log + client).splitlines() if "ERROR" in line or "nav: " in line]
    report(not errors, "no errors" + (f" ({errors[0]})" if errors else ""))
    passed = all(results)
    print(f"\nNavigation check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
