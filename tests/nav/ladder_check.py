#!/usr/bin/env python3
"""Ladder and teleporter check for OpenTech2 (M2: monster navigation, phase 4).

On optech2_ladder (tests/maps/make_ladder_map.py): west, a balcony 192 high
reached only by a ladder; east, a sealed vault reached only by a teleporter.
This check writes the map's re-release navigation file
(bots/navigation/optech2_ladder.nav, format NAV3) with the ladder (link
type 10) and the teleporter (link type 2).

On the dedicated server (no window):
  - the mesh has the ladder and the teleporter,
  - a path from the west floor to the balcony climbs the ladder,
  - a path from the east floor into the vault takes the teleporter,
  - without the file there's neither path.

In the game (no window), god mode:
  - a Berserker on the west floor climbs the ladder (g_monster_nav 2 logs
    it) to the player on the balcony and hits them,
  - a Berserker on the east floor steps onto the teleporter (logged), comes
    out in the vault and hits the player there.
Stock (g_monster_nav 0) is shown for comparison: it gets to neither.
Positions are sampled every second (listentities).

Usage:
  ladder_check.py
Needs build/release/OpenTech2 and OpenTech2-server, the game data and the
test map. About two minutes.
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
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_ladder.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "ladder-check"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
MONSTER = "monster_berserk"
WEST_SPOT = "-256 -256 24 0 90 0"        # west floor, facing the balcony
EAST_SPOT = "256 -400 24 0 90 0"         # east floor, south of the teleporter
BALCONY_PLAYER = "-256 400 217"          # the player on the balcony (192 + 25)
BALCONY_HEIGHT = 192
SPAWN_MS, END_MS = 3500, 25500
UNSET = 1e30


def nav_file():
    """NAV3 version 6: the ladder from its foot to the balcony (with its
    traversal) and the two teleporters, into the vault and back out (no
    traversal, as the re-release's files have them)."""
    nodes = [((-256.0, 220.0, 1.0), [(1, 10, 0, 0)]),                    # ladder foot: to node 1, ladder
             ((-256.0, 300.0, BALCONY_HEIGHT + 1.0), []),               # on the balcony
             ((256.0, -256.0, 9.0), [(3, 2, 0, 0xFFFF)]),                # on the teleporter pad: to node 3, teleport
             ((384.0, 384.0, 1.0), []),                                 # in the vault
             ((128.0, 384.0, 9.0), [(5, 2, 0, 0xFFFF)]),                 # on the vault's pad: to node 5, teleport
             ((384.0, -384.0, 1.0), [])]                                # back out on the east floor
    traversals = [((UNSET, UNSET, UNSET), (-256.0, 224.0, 1.0), (-256.0, 280.0, BALCONY_HEIGHT + 1.0), (0.0, 1.0, 0.0))]
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
        (user_dir / "bots" / "navigation" / "optech2_ladder.nav").write_bytes(nav_file())
    (user_dir / config_name).write_text("\n".join(lines) + "\n")


def run_server(commands, with_nav, log_name):
    with tempfile.TemporaryDirectory(prefix="opentech2-ladder-") as home:
        home_with_map(home, "ladder.cfg", commands + ["quit"], with_nav)
        result = subprocess.run([str(SERVER_BINARY), "-datadir", str(DATA_DIR), "+set", "fixedtime", "16667", "+exec", "ladder.cfg"],
                                env=dict(os.environ, HOME=home), stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=120, check=False)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / f"{log_name}.log").write_text(log)
    return log


def run_game(name, nav, spot, player_spot=None):
    lines = ["set g_damagelog 1", f"set g_monster_nav {nav}", "map optech2_ladder", 'delay 3000 "god"']
    if player_spot:
        lines.append(f'delay 3200 "teleport {player_spot}"')
    lines.append(f'delay {SPAWN_MS} "cmd spawnentity {MONSTER} {spot}"')
    lines += [f'delay {ms} "cmd listentities monsters"' for ms in range(SPAWN_MS + 1000, END_MS, 1000)]
    lines += [f"delay {END_MS} quit"]
    with tempfile.TemporaryDirectory(prefix="opentech2-ladder-") as home:
        home_with_map(home, "laddergame.cfg", lines, True)
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set",
                                     "vid_renderer", "soft", "+set", "fixedtime", "16667", "+set", "r_vsync", "0",
                                     "+set", "cl_maxfps", "60", "+set", "s_initsound", "0", "+set", "cheats", "1",
                                     "+set", "skill", "1", "+exec", "laddergame.cfg"],
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


def on_balcony(spot):
    x, y, z = spot
    return x < 0 and y > 256 and BALCONY_HEIGHT + 10 < z < BALCONY_HEIGHT + 40


def in_vault(spot):
    x, y, z = spot
    return x > 16 and y > 144 and 10 < z < 40


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_ladder_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    queries = ["map optech2_ladder", f"nav_path -256 -256 0 -256 400 {BALCONY_HEIGHT}", "nav_path 256 -400 0 256 320 0"]
    counts = r"Navigation: optech2_ladder, .*, (\d+) ladders, (\d+) teleporters"

    log = run_server(queries, True, "paths")
    found_counts = re.search(counts, log)
    report(bool(found_counts) and found_counts.groups() == ("1", "2"),
           f"with the navigation file the mesh has the ladder and the teleporter: {found_counts.groups() if found_counts else '?'}")
    found = paths(log)
    if len(found) == 2:
        status, corners = found[0]
        climbs = any(a[2] < 10 and b[2] > BALCONY_HEIGHT - 10 for a, b in zip(corners, corners[1:]))
        report(status == "found" and climbs, f"west floor to the balcony: {status}, climbs: {climbs}")
        status, corners = found[1]
        teleports = any(a[1] < 0 and b[1] > 144 for a, b in zip(corners, corners[1:]))
        report(status == "found" and teleports, f"east floor into the vault: {status}, teleports: {teleports}")
    else:
        report(False, f"two paths answered ({len(found)})")

    bare_log = run_server(queries, False, "paths-without-file")
    bare_found = paths(bare_log)
    report(len(bare_found) == 2 and all(status != "found" for status, _ in bare_found),
           f"without the file: {', '.join(status for status, _ in bare_found)}")

    def route_text(route):
        return " > ".join(f"({x:.0f},{y:.0f},{z:.0f})" for x, y, z in route[::3][:7])

    hits, route, climb_log = run_game("ladder", 2, WEST_SPOT, BALCONY_PLAYER)
    climbed = re.search(rf"{MONSTER} climbs a ladder at (-?\d+) (-?\d+) (-?\d+)", climb_log)
    report(bool(climbed) and any(on_balcony(spot) for spot in route) and bool(hits),
           f"with paths the {MONSTER} climbed the ladder" +
           (f" at ({climbed.group(1)},{climbed.group(2)},{climbed.group(3)})" if climbed else "") +
           f" and hit the player on the balcony ({len(hits)} hits): {route_text(route)}")

    hits, route, teleport_log = run_game("teleport", 2, EAST_SPOT)
    teleported = re.search(rf"{MONSTER} teleports to (-?\d+) (-?\d+) (-?\d+)", teleport_log)
    report(bool(teleported) and any(in_vault(spot) for spot in route) and bool(hits),
           f"with paths the {MONSTER} took the teleporter" +
           (f" to ({teleported.group(1)},{teleported.group(2)},{teleported.group(3)})" if teleported else "") +
           f" and hit the player in the vault ({len(hits)} hits): {route_text(route)}")

    hits, route, stock_climb = run_game("ladder-stock", 0, WEST_SPOT, BALCONY_PLAYER)
    print(f"info  stock chase, ladder: {'got up' if any(on_balcony(s) for s in route) else 'stayed down'}, {len(hits)} hits")
    hits, route, stock_teleport = run_game("teleport-stock", 0, EAST_SPOT)
    print(f"info  stock chase, teleporter: {'got in' if any(in_vault(s) for s in route) else 'stayed out'}, {len(hits)} hits")

    errors = [line for line in (log + bare_log + climb_log + teleport_log + stock_climb + stock_teleport).splitlines()
              if "ERROR" in line or "crashed" in line or "nav: " in line or "isn't a navigation file" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))
    passed = all(results)
    print(f"\nLadder and teleporter check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
