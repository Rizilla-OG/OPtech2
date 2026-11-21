#!/usr/bin/env python3
"""Monster navigation check for OpenTech2 (the M2 goal: "a monster paths
around an obstacle to reach the player").

On optech2_nav (tests/maps/make_nav_map.py), a room split by a low wall
(seen over, not climbed) with a slot near one end and a gap at the other, the player stands in the
south half (god mode) and a Berserker (melee only) is spawned in the north
half, where it sees the player across the wall and charges. Run twice:
  - g_monster_nav 1: the Berserker follows the navigation mesh around the
    wall's end and reaches the player (its first melee hit is logged),
  - g_monster_nav 0 (stock chase): for comparison; it's expected to take
    much longer or not get there in the time given.
Its position is sampled every second (listentities) to show the route.

Hearing at 60 Hz: a Berserker 520 units away in the player's half, facing
away (only a sound can alert it), must hear one blaster shot and attack.
Monsters used to count a noise for one server frame only: at 60 Hz they
think every 6th frame and missed it.

Usage:
  monster_nav_check.py
Needs build/release/OpenTech2, the game data and the test map. Runs with no
window, about a minute.
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
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_nav.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "monster-nav-check"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
MONSTER = "monster_berserk"
MONSTER_SPOT = "-256 320 24 0 270 0"   # behind the wall, facing it
SPAWN_MS, END_MS = 3500, 33500
NOISE_MS = SPAWN_MS  # times are counted from the spawn
GAP_X = 512 - 192                      # the wall ends here (make_nav_map.py)


def run(name, nav):
    lines = ["set g_damagelog 1", f"set g_monster_nav {nav}", "map optech2_nav", 'delay 3000 "god"',
             f'delay {SPAWN_MS} "cmd spawnentity {MONSTER} {MONSTER_SPOT}"']
    lines += [f'delay {ms} "cmd listentities monsters"' for ms in range(NOISE_MS + 1000, END_MS, 1000)]
    lines += [f'delay {END_MS} quit']
    with tempfile.TemporaryDirectory(prefix="opentech2-monster-nav-") as home:
        user_dir = Path(home) / ".opentech2" / "baseq2"
        (user_dir / "maps").mkdir(parents=True)
        shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
        (user_dir / "monsternav.cfg").write_text("\n".join(lines) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set",
                                     "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
                                     "+set", "s_initsound", "0", "+set", "cheats", "1", "+set", "skill", "1",
                                     "+exec", "monsternav.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    hits = [float(t) for t in re.findall(rf"damagelog time ([\d.]+) attacker {MONSTER} target player", log)]
    route = [(float(x), float(y)) for x, y in re.findall(rf"^{MONSTER}: (-?[\d.]+) (-?[\d.]+) ", log, re.M)]
    return hits, route, log


def hearing_run():
    """One blaster shot behind a Berserker that can't see the player: does it attack?"""
    lines = ["set g_damagelog 1", "map optech2_nav", 'delay 3000 "god"',
             'delay 3200 "cmd spawnentity monster_berserk 250 -470 24 0 0 0"', 'delay 4000 "+attack"',
             'delay 4150 "-attack"', "delay 12000 quit"]
    with tempfile.TemporaryDirectory(prefix="opentech2-monster-nav-") as home:
        user_dir = Path(home) / ".opentech2" / "baseq2"
        (user_dir / "maps").mkdir(parents=True)
        shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
        (user_dir / "hearing.cfg").write_text("\n".join(lines) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set",
                                 "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "s_initsound", "0",
                                 "+set", "cheats", "1", "+exec", "hearing.cfg"],
                                env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                timeout=60, check=False)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / "hearing.log").write_text(log)
    return len(re.findall(r"attacker monster_berserk target player", log)), log


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

    nav_hits, nav_route, nav_log = run("nav", 1)
    stock_hits, stock_route, stock_log = run("stock", 0)
    window = (END_MS - NOISE_MS) / 1000

    def route_text(route):
        return " > ".join(f"({x:.0f},{y:.0f})" for x, y in route[::3][:8])

    report(bool(nav_route), f"the {MONSTER} spawned behind the wall: {route_text(nav_route[:1])}")
    went_around = any(x > GAP_X - 40 or -500 < x < -400 for x, _ in nav_route)  # the gap, or the slot
    reached_south = any(y < -40 for _, y in nav_route)
    report(went_around and reached_south,
           f"with paths it went through an opening (the slot or the gap) into the player's half: "
           f"{route_text(nav_route)}")
    if nav_hits:
        first = nav_hits[0] - (NOISE_MS / 1000)
        report(True, f"with paths it reached the player and hit them {len(nav_hits)} times, "
                     f"the first about {first:.1f} s after it spawned")
    else:
        report(False, f"with paths it reached the player within {window:.0f} s")
    stock_first = (stock_hits[0] - NOISE_MS / 1000) if stock_hits else None
    print(f"info  stock chase (g_monster_nav 0): "
          f"{'first hit after about %.1f s' % stock_first if stock_first is not None else 'no hit in %.0f s' % window}; "
          f"route {route_text(stock_route)}")
    if nav_hits:
        nav_first = nav_hits[0] - NOISE_MS / 1000
        report(stock_first is None or nav_first < stock_first,
               "paths got there sooner than the stock chase" if stock_first is not None else
               "the stock chase didn't get there at all")

    heard = [hearing_run() for _ in range(3)]
    report(all(hits > 0 for hits, _ in heard),
           f"at 60 Hz a monster hears a shot it can't see and attacks: {sum(h > 0 for h, _ in heard)} of 3 runs")

    errors = [line for line in (nav_log + stock_log + "".join(log for _, log in heard)).splitlines() if "ERROR" in line or "crashed" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))
    passed = all(results)
    print(f"\nMonster navigation check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
