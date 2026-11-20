#!/usr/bin/env python3
"""Tick rate check for OPtech2: does the game behave the same at 10 and 60 Hz?

Runs one scripted single-player session on base1 at each tick rate, with a
hidden client, and measures:
  - door speed     (door *19 opening, units per second of game time)
  - machine gun    (bullets fired while the trigger is held for 2 s)
  - rocket speed   (units per second of game time)
  - berserker run  (how fast a berserker runs at the player, units per second)
  - weapons        (ammo each weapon uses while fire is held for 3 s: fire rate)

Then compares 60 Hz against 10 Hz within a tolerance per measurement.

Usage:
  tick_check.py                    compare 10 Hz with 60 Hz (exit 1 on failure)
  tick_check.py --rates 10 30 60   compare 10 Hz with each other rate

Each sample sends "playerinfo" and "listentities all" in one packet, so both
read the same server frame and speeds use game time, not wall time. Every
run gets its own temporary HOME. Logs land in build/tick-check/.
"""

import argparse
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CHECK_DIR = Path(__file__).resolve().parent
REPO_DIR = CHECK_DIR.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "optech2"
OUTPUT_DIR = REPO_DIR / "build" / "tick-check"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
GAME_TIMEOUT_SECONDS = 180

CLIENT_SETTINGS = [
    "+set", "vid_renderer", "soft", "+set", "r_mode", "4", "+set", "vid_fullscreen", "0",
    "+set", "r_vsync", "0",  # with the dummy video driver vsync never returns
    "+set", "cl_maxfps", "60", "+set", "s_initsound", "0",
]

BIG_ROOM_SPOT = "128 -320 32 0 135 0"     # open room on base1, looking across it
DOOR_TRIGGER_SPOT = "-1176 1632 40 0 0 0"  # standing here opens door *19 (rises 122 units)
BERSERKER_SPOT = "-84 -108 56 0 315 0"     # 300 units in front of BIG_ROOM_SPOT, facing it
ROCKET_SPOT = "128 -320 32 -80 135 0"      # BIG_ROOM_SPOT looking almost straight up: ~0.4 s of free flight

# allowed difference of the other rate from 10 Hz, as a share of the 10 Hz value
TOLERANCES = {
    "door speed": 0.05,
    "machine gun bullets in 2 s": 0.15,
    "rocket speed": 0.05,
    "berserker run speed": 0.30,  # AI picks run frames with some randomness
}
# ammo used while each weapon fires for 3 s (fire rate check); exact match expected within one shot
WEAPONS = [("shotgun", "shells"), ("super shotgun", "shells"), ("chaingun", "bullets"),
           ("grenade launcher", "grenades"), ("rocket launcher", "rockets"), ("hyperblaster", "cells"),
           ("railgun", "slugs"), ("bfg10k", "cells")]
for weapon_name, _ammo in WEAPONS:
    TOLERANCES[f"{weapon_name} ammo used in 3 s"] = 0.10


def script_lines():
    """Console script (all times in ms after start; the map is loaded by 6 s)."""
    def at(milliseconds, command):
        return f'delay {milliseconds} "{command}"'

    lines = ['alias snap "cmd playerinfo; cmd listentities all"', "map base1",
             at(6000, "god"), at(6050, "notarget")]
    # door: stand in its trigger, sample while it rises
    lines.append(at(6200, f"teleport {DOOR_TRIGGER_SPOT}"))
    lines += [at(milliseconds, "snap") for milliseconds in (6600, 6900, 7200, 7500)]
    # machine gun: hold fire for 2 s
    lines += [at(8000, f"teleport {BIG_ROOM_SPOT}"), at(8050, "give machinegun"), at(8100, "give bullets"),
              at(8300, "use machinegun"), at(9000, "snap"), at(9050, "+attack"), at(11050, "-attack"),
              at(11300, "snap")]
    # rocket: one shot nearly straight up (the longest free flight in this room), sampled while it flies
    lines += [at(11100, "give rocket launcher"), at(11150, "give rockets"), at(11200, "use rocket launcher"),
              at(11700, f"teleport {ROCKET_SPOT}"), at(12500, "+attack"), at(12700, "-attack")]
    lines += [at(milliseconds, "snap") for milliseconds in (12650, 12750, 12850, 12950, 13050)]
    # berserker: let it see the player and run at them
    lines += [at(13500, "notarget"), at(13550, f"teleport {BIG_ROOM_SPOT}"), at(13600, f"cmd spawnentity monster_berserk {BERSERKER_SPOT}"),
              at(14300, "snap"), at(14800, "snap")]
    # weapons: hold fire for 3 s each, sampling ammo before and after
    start = 15500
    lines += [at(start - 400, "notarget"), at(start - 300, f"teleport {BIG_ROOM_SPOT}")]  # monsters ignore the player again
    for weapon_name, ammo_name in WEAPONS:
        lines += [at(start, f"give {weapon_name}"), at(start + 50, f"give {ammo_name}"),
                  at(start + 100, f"use {weapon_name}"), at(start + 1800, "snap"),
                  at(start + 1900, "+attack"), at(start + 4900, "-attack"), at(start + 6500, "snap")]
        start += 7000
    lines.append(at(start, "quit"))
    return lines


def run_session(tick_rate, data_dir):
    """Run the scripted session at one tick rate; return the log text."""
    log_path = OUTPUT_DIR / f"{tick_rate}hz.log"
    with tempfile.TemporaryDirectory(prefix="optech2-tick-") as home:
        game_dir = Path(home) / ".optech2" / "baseq2"
        game_dir.mkdir(parents=True)
        (game_dir / "tick.cfg").write_text("\n".join(script_lines()) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        with open(log_path, "w") as log_file:
            subprocess.run([str(CLIENT_BINARY), "-datadir", str(data_dir), *CLIENT_SETTINGS,
                            "+set", "sv_tickrate", str(tick_rate), "+exec", "tick.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT,
                           timeout=GAME_TIMEOUT_SECONDS, check=False)
    return log_path.read_text(errors="replace")


def parse_snaps(log_text):
    """Split the log into samples: [{"time", "ammo", "entities": [(classname, (x, y, z))]}].

    Within one sample the entity list arrives before the playerinfo line, so
    entity lines are collected until the next playerinfo line closes the sample.
    """
    snaps = []
    pending_entities = []
    for line in log_text.splitlines():
        info = re.search(r"playerinfo time ([\d.]+) health (-?\d+) ammo (-?\d+)", line)
        if info:
            snaps.append({"time": float(info.group(1)), "ammo": int(info.group(3)), "entities": pending_entities})
            pending_entities = []
            continue
        entity = re.match(r"(\w+): (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\s*$", line.strip())
        if entity:
            pending_entities.append((entity.group(1), tuple(float(entity.group(i)) for i in (2, 3, 4))))
    return snaps


def first_origin(snap, classname):
    for name, origin in snap["entities"]:
        if name == classname:
            return origin
    return None


def speed_between(snap_a, snap_b, classname):
    """Units per second of game time an entity moved between two samples."""
    origin_a, origin_b = first_origin(snap_a, classname), first_origin(snap_b, classname)
    elapsed = snap_b["time"] - snap_a["time"]
    if origin_a is None or origin_b is None or elapsed <= 0:
        return None
    return math.dist(origin_a, origin_b) / elapsed


def measure(log_text):
    """Turn one session's samples into the four measurements (None when missing)."""
    snaps = parse_snaps(log_text)
    expected_samples = 13 + 2 * len(WEAPONS)
    if len(snaps) != expected_samples:
        return {name: None for name in TOLERANCES}, f"expected {expected_samples} samples, got {len(snaps)}"
    door, gun_before, gun_after, rocket_snaps, berserk_a, berserk_b = (
        snaps[0:4], snaps[4], snaps[5], snaps[6:11], snaps[11], snaps[12])
    # the rocket fires on the first weapon frame after +attack and explodes on the ceiling:
    # use the first and last samples that still show it
    flying = [snap for snap in rocket_snaps if first_origin(snap, "rocket") is not None]
    # the door is the first func_door listed; use the samples taken while it was still rising
    door_heights = [(snap["time"], first_origin(snap, "func_door")[2]) for snap in door]
    rising = [(time, height) for time, height in door_heights if 0 < height < 122]
    door_speed = None
    if len(rising) >= 2:
        door_speed = (rising[-1][1] - rising[0][1]) / (rising[-1][0] - rising[0][0])
    weapon_results = {}
    for index, (weapon_name, _ammo) in enumerate(WEAPONS):
        before, after = snaps[13 + 2 * index], snaps[14 + 2 * index]
        weapon_results[f"{weapon_name} ammo used in 3 s"] = before["ammo"] - after["ammo"]
    return {
        **weapon_results,
        "door speed": door_speed,
        "machine gun bullets in 2 s": gun_before["ammo"] - gun_after["ammo"],
        "rocket speed": speed_between(flying[0], flying[-1], "rocket") if len(flying) >= 2 else None,
        "berserker run speed": speed_between(berserk_a, berserk_b, "monster_berserk"),
    }, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--rates", type=int, nargs="+", default=[60], help="tick rates to compare with 10 Hz")
    arguments = parser.parse_args()

    if not CLIENT_BINARY.exists():
        sys.exit(f"Missing {CLIENT_BINARY}. Build first: cmake --workflow --preset default")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    results = {}
    for tick_rate in [10, *arguments.rates]:
        results[tick_rate], problem = measure(run_session(tick_rate, arguments.datadir))
        if problem:
            print(f"{tick_rate} Hz: {problem}. See {OUTPUT_DIR}/{tick_rate}hz.log")

    failures = 0
    header = f"{'measurement':<34}" + "".join(f"{str(rate) + ' Hz':>12}" for rate in results)
    print(header)
    for name, tolerance in TOLERANCES.items():
        reference = results[10][name]
        row = f"{name:<34}"
        verdicts = []
        for tick_rate, values in results.items():
            value = values[name]
            row += f"{'-' if value is None else f'{value:.1f}':>12}"
            if tick_rate == 10:
                continue
            passed = (value is not None and reference is not None and
                      abs(value - reference) <= tolerance * abs(reference))
            verdicts.append(passed)
        failures += verdicts.count(False)
        print(row + ("   ok" if all(verdicts) else f"   FAIL (allowed {tolerance:.0%})"))

    print(f"\n{failures} failed. Logs in {OUTPUT_DIR}" if failures else f"\nTick rate check passed. Logs in {OUTPUT_DIR}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
