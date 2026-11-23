#!/usr/bin/env python3
"""Barrier jump check for OpenTech2 (M2: monster navigation, phase 4).

On optech2_jump (tests/maps/make_jump_map.py): the north half is a ledge 36
high behind a glass wall, with one 64-wide gap. This check writes the map's
re-release navigation file (bots/navigation/optech2_jump.nav, format NAV3)
with one barrier jump up through the gap, as the re-release's own files
mark them.

On the dedicated server (no window):
  - with the file, the mesh has the jump, and a path from the floor up onto
    the ledge goes through the gap; a medium monster (64 wide, as wide as
    the gap) gets no path through it,
  - without it there's no jump and no path up.

In the game (no window), with the player on the ledge (god mode):
  - a Berserker (melee only, jumps 40) on the floor in front of the glass
    walks to the gap, jumps up (g_monster_nav 2 logs it) and hits the player.
    Stock (g_monster_nav 0) is shown for comparison: it stays at the glass.
Positions are sampled every second (listentities).

Usage:
  jump_check.py
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
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_jump.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "jump-check"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
MONSTER = "monster_berserk"
FLOOR_SPOT = "256 -192 24 0 90 0"   # on the floor, facing the glass and the player behind it
SPAWN_MS, END_MS = 3500, 23500
LEDGE_HEIGHT = 36
UNSET = 1e30


def on_ledge(spot):
    """Standing on the ledge behind the glass (not in the air in front of it)."""
    x, y, z = spot
    return y > 24 and LEDGE_HEIGHT + 10 < z < LEDGE_HEIGHT + 40


def nav_file():
    """NAV3 version 6: two nodes in front of and behind the gap, one barrier
    jump (link type 5) between them with its traversal's start and end."""
    nodes = [((-384.0, -48.0, 1.0), [(1, 5, 0, 0)]),          # floor, its one link: to node 1, barrier jump, traversal 0
             ((-384.0, 48.0, LEDGE_HEIGHT + 1.0), [])]        # on the ledge
    traversals = [((UNSET, UNSET, UNSET), (-384.0, -4.0, 1.0), (-384.0, 20.0, LEDGE_HEIGHT + 1.0), (0.0, 0.0, 0.0))]
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
        (user_dir / "bots" / "navigation" / "optech2_jump.nav").write_bytes(nav_file())
    (user_dir / config_name).write_text("\n".join(lines) + "\n")


def run_server(commands, with_nav, log_name):
    with tempfile.TemporaryDirectory(prefix="opentech2-jump-") as home:
        home_with_map(home, "jump.cfg", commands + ["quit"], with_nav)
        result = subprocess.run([str(SERVER_BINARY), "-datadir", str(DATA_DIR), "+set", "fixedtime", "16667", "+exec", "jump.cfg"],
                                env=dict(os.environ, HOME=home), stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=120, check=False)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / f"{log_name}.log").write_text(log)
    return log


def run_game(name, nav):
    lines = ["set g_damagelog 1", f"set g_monster_nav {nav}", "map optech2_jump", 'delay 3000 "god"',
             f'delay {SPAWN_MS} "cmd spawnentity {MONSTER} {FLOOR_SPOT}"']
    lines += [f'delay {ms} "cmd listentities monsters"' for ms in range(SPAWN_MS + 1000, END_MS, 1000)]
    lines += [f"delay {END_MS} quit"]
    with tempfile.TemporaryDirectory(prefix="opentech2-jump-") as home:
        home_with_map(home, "jumpgame.cfg", lines, True)
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set",
                                     "vid_renderer", "soft", "+set", "fixedtime", "16667", "+set", "r_vsync", "0",
                                     "+set", "cl_maxfps", "60", "+set", "s_initsound", "0", "+set", "cheats", "1",
                                     "+set", "skill", "1", "+exec", "jumpgame.cfg"],
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


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_jump_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    up = ["map optech2_jump", f"nav_path 256 -320 0 256 256 {LEDGE_HEIGHT}"]
    jump_count = r"Navigation: optech2_jump, \d+ polygons, built in \d+ ms \(.*\), \d+ drops, \d+ lifts, (\d+) jumps"
    too_wide = ["map optech2_jump", f"nav_path 256 -320 0 256 256 {LEDGE_HEIGHT} medium"]

    log = run_server(up, True, "paths")
    jumps = re.search(jump_count, log)
    report(bool(jumps) and int(jumps.group(1)) == 1, f"with the navigation file the mesh has its jump: {jumps.group(1) if jumps else '?'}")
    found = paths(log)
    if found:
        status, corners = found[0]
        through_gap = any(a[2] < 10 and b[2] > 30 and -420 <= a[0] <= -348 for a, b in zip(corners, corners[1:]))
        report(status == "found" and through_gap, f"floor up onto the ledge: {status}, jumps through the gap: {through_gap}")
    else:
        report(False, "the path up was answered")

    wide_log = run_server(too_wide, True, "paths-medium")
    wide_found = paths(wide_log)
    report(bool(wide_found) and wide_found[0][0] != "found",
           f"a medium monster (64 wide, the gap's width) gets no path through the gap: {wide_found[0][0] if wide_found else '?'}")

    bare_log = run_server(up, False, "paths-without-file")
    bare_jumps = re.search(jump_count, bare_log)
    bare_found = paths(bare_log)
    report(bool(bare_jumps) and int(bare_jumps.group(1)) == 0 and bare_found and bare_found[0][0] != "found",
           f"without the file: {bare_jumps.group(1) if bare_jumps else '?'} jumps, path up {bare_found[0][0] if bare_found else '?'}")

    def route_text(route):
        return " > ".join(f"({x:.0f},{y:.0f},{z:.0f})" for x, y, z in route[::3][:7])

    hits, route, nav_log = run_game("nav", 2)
    jumped = re.search(rf"{MONSTER} jumps at (-?\d+) (-?\d+) (-?\d+)", nav_log)
    report(bool(jumped) and any(on_ledge(spot) for spot in route),
           f"with paths the {MONSTER} jumped up through the gap" +
           (f" at ({jumped.group(1)},{jumped.group(2)},{jumped.group(3)})" if jumped else "") + f": {route_text(route)}")
    report(bool(hits), f"and hit the player ({len(hits)} hits" +
           (f", the first {hits[0] - SPAWN_MS / 1000:.1f} s after it spawned)" if hits else ")"))
    hits, route, stock_log = run_game("stock", 0)
    up_there = any(on_ledge(spot) for spot in route)
    print(f"info  stock chase (g_monster_nav 0): {'got up' if up_there else 'stayed on the floor'}, {len(hits)} hits; "
          f"route {route_text(route)}")

    errors = [line for line in (log + wide_log + bare_log + nav_log + stock_log).splitlines()
              if "ERROR" in line or "crashed" in line or "nav: " in line or "isn't a navigation file" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))
    passed = all(results)
    print(f"\nBarrier jump check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
