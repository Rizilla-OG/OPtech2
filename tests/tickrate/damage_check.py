#!/usr/bin/env python3
"""Damage check for OpenTech2: do weapons deal the same damage at 10 and 60 Hz?

For each weapon, one hidden single-player session on base1: a supertank
(1500 health) is spawned 200 units in front of the player, who fires for 3 s.
The game logs every hit (g_damagelog 1); this sums the player's damage and
divides by the ammo used. Then 60 Hz is compared with 10 Hz.

Usage:
  damage_check.py               compare 10 Hz with 60 Hz (exit 1 on failure)
  damage_check.py --rate 40     compare 10 Hz with another rate

Takes about 4 minutes (18 short sessions). Logs land in build/damage-check/.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CHECK_DIR = Path(__file__).resolve().parent
REPO_DIR = CHECK_DIR.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "damage-check"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"

CLIENT_SETTINGS = [
    "+set", "fixedtime", "16667", "+set", "vid_renderer", "soft", "+set", "r_vsync", "0",  # dummy video driver: vsync never returns
    "+set", "cl_maxfps", "60", "+set", "s_initsound", "0",
]
PLAYER_SPOT = "128 -320 32 0 135 0"     # open room on base1
TARGET_SPOT = "-13 -179 64 0 315 0"     # 200 units in front of the player, facing them
WEAPONS = [("shotgun", "shells"), ("super shotgun", "shells"), ("machinegun", "bullets"),
           ("chaingun", "bullets"), ("grenade launcher", "grenades"), ("rocket launcher", "rockets"),
           ("hyperblaster", "cells"), ("railgun", "slugs"), ("bfg10k", "cells")]
# splash damage depends on where the target stood when the blast went off
TOLERANCE = 0.10


def run_session(tick_rate, weapon_name, ammo_name, data_dir, log_path):
    """Fire one weapon at a supertank for 3 s; return the log text."""
    script = [
        "set g_damagelog 1", "set g_monster_freeze 1", "map base1",  # target stands still: every shot lands
        'delay 6000 "god"', 'delay 6050 "notarget"', f'delay 6100 "teleport {PLAYER_SPOT}"',
        f'delay 6300 "cmd spawnentity monster_supertank {TARGET_SPOT}"',
        f'delay 6400 "give {weapon_name}"', f'delay 6450 "give {ammo_name}"', f'delay 6500 "use {weapon_name}"',
        'delay 8200 "cmd playerinfo"', 'delay 8300 "+attack"', 'delay 11300 "-attack"',
        'delay 12800 "cmd playerinfo"', 'delay 13000 "quit"',
    ]
    with tempfile.TemporaryDirectory(prefix="optech2-damage-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        game_dir.mkdir(parents=True)
        (game_dir / "damage.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        with open(log_path, "w") as log_file:
            subprocess.run([str(CLIENT_BINARY), "-datadir", str(data_dir), *CLIENT_SETTINGS,
                            "+set", "sv_tickrate", str(tick_rate), "+exec", "damage.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=60, check=False)
    return log_path.read_text(errors="replace")


def damage_per_ammo(log_text):
    """(ammo used, hits, total damage) by the player in one session."""
    ammo = [int(value) for value in re.findall(r"playerinfo time [\d.]+ health -?\d+ ammo (-?\d+)", log_text)]
    hits = [int(value) for value in re.findall(r"damagelog time [\d.]+ attacker player target \S+ damage (-?\d+)", log_text)]
    used = ammo[0] - ammo[-1] if len(ammo) >= 2 else 0
    return used, len(hits), sum(hits)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--rate", type=int, default=60, help="tick rate to compare with 10 Hz")
    arguments = parser.parse_args()

    if not CLIENT_BINARY.exists():
        sys.exit(f"Missing {CLIENT_BINARY}. Build first: cmake --workflow --preset default")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    failures = 0
    print(f"{'weapon':<18}{'rate':>6}{'ammo':>6}{'hits':>6}{'damage':>8}{'dmg/ammo':>10}")
    for weapon_name, ammo_name in WEAPONS:
        per_ammo = {}
        for tick_rate in (10, arguments.rate):
            log_path = OUTPUT_DIR / f"{weapon_name.replace(' ', '_')}_{tick_rate}hz.log"
            used, hits, total = damage_per_ammo(run_session(tick_rate, weapon_name, ammo_name, arguments.datadir, log_path))
            per_ammo[tick_rate] = total / used if used else None
            print(f"{weapon_name:<18}{tick_rate:>6}{used:>6}{hits:>6}{total:>8}"
                  f"{'-' if per_ammo[tick_rate] is None else f'{per_ammo[tick_rate]:.1f}':>10}")
        reference, other = per_ammo[10], per_ammo[arguments.rate]
        passed = reference is not None and other is not None and abs(other - reference) <= TOLERANCE * reference
        if not passed:
            failures += 1
            print(f"{'':<18}FAIL: damage per ammo differs more than {TOLERANCE:.0%}")

    print(f"\n{failures} failed. Logs in {OUTPUT_DIR}" if failures else f"\nDamage check passed. Logs in {OUTPUT_DIR}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
