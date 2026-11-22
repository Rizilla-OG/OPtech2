#!/usr/bin/env python3
"""Long jump check for OpenTech2 (M2: monster navigation, phase 4).

On optech2_leap (tests/maps/make_leap_map.py): a south and a north floor
with a pit between them, 112 wide and 320 deep. This check writes the map's
re-release navigation file (bots/navigation/optech2_leap.nav, format NAV3)
with one long jump across the pit.

On the dedicated server (no window):
  - with the file, the mesh has the leap, and a path from the south floor
    to the north one crosses the pit,
  - without it there's no path across.

In the game (no window), with the player on the north floor (god mode):
  - a Berserker (melee only, leaps up to 192) on the south floor walks to
    the leap, leaps the pit (g_monster_nav 2 logs it) and hits the player.
    Stock (g_monster_nav 0) is shown for comparison: it stays on its side.
Positions are sampled every second (listentities).

Usage:
  leap_check.py
Needs build/release/OpenTech2 and OpenTech2-server, the game data and the
test map. About a minute.
"""

import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
SERVER_BINARY = REPO_DIR / "build" / "release" / "OpenTech2-server"
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_leap.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "leap-check"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
MONSTER = "monster_berserk"
SOUTH_SPOT = "256 -320 24 0 90 0"   # on the south floor, facing the pit and the player beyond it
SPAWN_MS, END_MS = 3500, 23500
UNSET = 1e30


def nav_file():
    """NAV3 version 6: a node on each side of the pit, one long jump (link
    type 1) from south to north with its traversal's start and end at the
    pit's edges."""
    nodes = [((0.0, -100.0, 1.0), [(1, 1, 0, 0)]),   # south floor: to node 1, long jump, traversal 0
             ((0.0, 100.0, 1.0), [])]                # north floor
    traversals = [((UNSET, UNSET, UNSET), (0.0, -60.0, 1.0), (0.0, 60.0, 1.0), (0.0, 0.0, 0.0))]
    links = [link for _, node_links in nodes for link in node_links]
    data = struct.pack("<4sIIIIf", b"NAV3", 6, len(nodes), len(links), len(traversals), 0.8)
    first = 0
    for _, node_links in nodes:
        data += struct.pack("<4H", 0, len(node_links), first, 16)
        first += len(node_links)
    for origin, _ in nodes:
        data += struct.pack("<3f", *origin)
    for target, kind, flags, traversal in links:
        data += struct.pack("<HBBH", target, kind, flags, traversal)
    for funnel, start, end, plane in traversals:
        data += struct.pack("<12f", *funnel, *start, *end, *plane)
    data += struct.pack("<I", 0)  # no entities
    return data


def home_with_map(home, config_name, lines, with_nav):
    user_dir = Path(home) / ".opentech2" / "baseq2"
    (user_dir / "maps").mkdir(parents=True, exist_ok=True)
    shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
    if with_nav:
        (user_dir / "bots" / "navigation").mkdir(parents=True, exist_ok=True)
        (user_dir / "bots" / "navigation" / "optech2_leap.nav").write_bytes(nav_file())
    (user_dir / config_name).write_text("\n".join(lines) + "\n")


def run_server(commands, with_nav, log_name):
    with tempfile.TemporaryDirectory(prefix="opentech2-leap-") as home:
        home_with_map(home, "leap.cfg", commands + ["quit"], with_nav)
        result = subprocess.run([str(SERVER_BINARY), "-datadir", str(DATA_DIR), "+set", "fixedtime", "16667", "+exec", "leap.cfg"],
                                env=dict(os.environ, HOME=home), stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=120, check=False)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / f"{log_name}.log").write_text(log)
    return log


def run_game(name, nav):
    lines = ["set g_damagelog 1", f"set g_monster_nav {nav}", "map optech2_leap", 'delay 3000 "god"',
             f'delay {SPAWN_MS} "cmd spawnentity {MONSTER} {SOUTH_SPOT}"']
    lines += [f'delay {ms} "cmd listentities monsters"' for ms in range(SPAWN_MS + 1000, END_MS, 1000)]
    lines += [f"delay {END_MS} quit"]
    with tempfile.TemporaryDirectory(prefix="opentech2-leap-") as home:
        home_with_map(home, "leapgame.cfg", lines, True)
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set",
                                     "vid_renderer", "soft", "+set", "fixedtime", "16667", "+set", "r_vsync", "0",
                                     "+set", "cl_maxfps", "60", "+set", "s_initsound", "0", "+set", "cheats", "1",
                                     "+set", "skill", "1", "+exec", "leapgame.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
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


def on_north_floor(spot):
    x, y, z = spot
    return y > 60 and 10 < z < 40


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_leap_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    across = ["map optech2_leap", "nav_path 256 -320 0 0 320 0"]
    leap_count = r"Navigation: optech2_leap, \d+ polygons, built in \d+ ms \(.*\), \d+ drops, \d+ lifts, \d+ jumps, (\d+) leaps"

    log = run_server(across, True, "paths")
    leaps = re.search(leap_count, log)
    report(bool(leaps) and int(leaps.group(1)) == 1, f"with the navigation file the mesh has its leap: {leaps.group(1) if leaps else '?'}")
    found = paths(log)
    if found:
        status, corners = found[0]
        crosses = any(a[1] < -56 and b[1] > 56 for a, b in zip(corners, corners[1:]))
        report(status == "found" and crosses, f"south floor to north floor: {status}, leaps the pit: {crosses}")
    else:
        report(False, "the path across was answered")

    bare_log = run_server(across, False, "paths-without-file")
    bare_leaps = re.search(leap_count, bare_log)
    bare_found = paths(bare_log)
    report(bool(bare_leaps) and int(bare_leaps.group(1)) == 0 and bare_found and bare_found[0][0] != "found",
           f"without the file: {bare_leaps.group(1) if bare_leaps else '?'} leaps, path across {bare_found[0][0] if bare_found else '?'}")

    def route_text(route):
        return " > ".join(f"({x:.0f},{y:.0f},{z:.0f})" for x, y, z in route[::3][:7])

    hits, route, nav_log = run_game("nav", 2)
    leapt = re.search(rf"{MONSTER} leaps at (-?\d+) (-?\d+) (-?\d+)", nav_log)
    report(bool(leapt) and any(on_north_floor(spot) for spot in route),
           f"with paths the {MONSTER} leapt the pit" +
           (f" at ({leapt.group(1)},{leapt.group(2)},{leapt.group(3)})" if leapt else "") + f": {route_text(route)}")
    report(bool(hits), f"and hit the player ({len(hits)} hits" +
           (f", the first {hits[0] - SPAWN_MS / 1000:.1f} s after it spawned)" if hits else ")"))
    hits, route, stock_log = run_game("stock", 0)
    across_pit = any(on_north_floor(spot) for spot in route)
    print(f"info  stock chase (g_monster_nav 0): {'got across' if across_pit else 'stayed on its side'}, {len(hits)} hits; "
          f"route {route_text(route)}")

    errors = [line for line in (log + bare_log + nav_log + stock_log).splitlines()
              if "ERROR" in line or "crashed" in line or "nav: " in line or "isn't a navigation file" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))
    passed = all(results)
    print(f"\nLong jump check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
