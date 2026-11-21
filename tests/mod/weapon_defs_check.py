#!/usr/bin/env python3
"""Weapon definition check for OpenTech2 (M2: data-driven definitions).

A mod folder changes three stock weapons in defs/<folder>.items:
  - shotgun: 30 pellets of 2 damage (stock: 12 of 4),
  - railgun: 7 damage (stock: 150 in single player),
  - rocket launcher: 50 damage (+0..19, stock 100) and speed 100 (stock 650).
Each weapon fires one shot at a supertank 200 units away (the damage check's
setup), once with the stock game and once with the mod, and the game's hit
log (g_damagelog 1) is compared. A weapon field on an item that isn't a
weapon is reported.

The supertank hasn't noticed the player (notarget), so stock Quake II's
surprise bonus doubles the first hit (g_combat.c, T_Damage): a direct hit
counts twice in both runs, and one shotgun pellet logs double.

Usage:
  weapon_defs_check.py
Needs build/release/OpenTech2 and the game data. Runs with no window, about
a minute and a half.
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
OUTPUT_DIR = REPO_DIR / "build" / "weapon-defs-check"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
PLAYER_SPOT = "128 -320 32 0 135 0"     # open room on base1 (as in tests/tickrate/damage_check.py)
TARGET_SPOT = "-13 -179 64 0 315 0"     # 200 units in front of the player, facing them

DEFINITIONS = """
{
"classname" "weapon_shotgun"
"pellets" "30"
"damage" "2"
}
{
"classname" "weapon_railgun"
"damage" "7"
}
{
"classname" "weapon_rocketlauncher"
"damage" "50"
"speed" "100"
}
// a weapon field on an item that isn't a weapon
{
"classname" "ammo_shells"
"damage" "99"
}
"""

WEAPONS = [("shotgun", "shells"), ("railgun", "slugs"), ("rocket launcher", "rockets")]


def one_shot(weapon_name, ammo_name, with_mod):
    """Fires one shot at a supertank; returns [(time, damage)] of the player's hits and the fire time."""
    script = [
        "set g_damagelog 1", "map base1",
        'delay 6000 "god"', 'delay 6050 "notarget"', f'delay 6100 "teleport {PLAYER_SPOT}"',
        f'delay 6300 "cmd spawnentity monster_supertank {TARGET_SPOT}"',
        f'delay 6400 "give {weapon_name}"', f'delay 6450 "give {ammo_name}"', f'delay 6500 "use {weapon_name}"',
        'delay 8250 "cmd playerinfo"', 'delay 8300 "+attack"', 'delay 8350 "-attack"', 'delay 11500 "quit"',
    ]
    with tempfile.TemporaryDirectory(prefix="opentech2-weapons-") as home:
        user_dir = Path(home) / ".opentech2"
        (user_dir / "baseq2").mkdir(parents=True)
        arguments = []
        if with_mod:
            (user_dir / "moda" / "defs").mkdir(parents=True)
            (user_dir / "moda" / "defs" / "moda.items").write_text(DEFINITIONS)
            arguments = ["+set", "fs_mods", "moda"]
        (user_dir / "baseq2" / "weapons.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run([str(CLIENT_BINARY), "-datadir", str(DATA_DIR)] + arguments +
                                    ["+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
                                     "+set", "s_initsound", "0", "+exec", "weapons.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=60, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    name = weapon_name.replace(" ", "_") + ("-mod" if with_mod else "-stock")
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    hits = [(float(time), int(damage)) for time, damage in
            re.findall(r"damagelog time ([\d.]+) attacker player target monster_supertank damage (-?\d+)", log)]
    fired = re.search(r"playerinfo time ([\d.]+)", log)
    return hits, float(fired.group(1)) if fired else 0.0, log


def main():
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    shots = {}
    logs = ""
    for weapon_name, ammo_name in WEAPONS:
        for with_mod in (False, True):
            hits, fired, log = one_shot(weapon_name, ammo_name, with_mod)
            shots[(weapon_name, with_mod)] = (hits, fired)
            logs += log

    stock, mod = shots[("shotgun", False)][0], shots[("shotgun", True)][0]
    # one log line per pellet hit; the smallest is a pellet's damage (one may be doubled)
    report(bool(stock) and min(d for _, d in stock) == 4 and len(stock) <= 12,
           f"stock shotgun: {len(stock)} pellets hit, {min((d for _, d in stock), default=0)} damage each")
    report(len(mod) > 12 and min((d for _, d in mod), default=0) == 2 and max(d for _, d in mod) <= 4,
           f"shotgun with \"pellets\" 30, \"damage\" 2: {len(mod)} pellets hit, "
           f"{min((d for _, d in mod), default=0)} damage each")

    stock, mod = shots[("railgun", False)][0], shots[("railgun", True)][0]
    report(len(stock) == 1 and len(mod) == 1 and stock[0][1] in (150, 300) and mod[0][1] * 150 == stock[0][1] * 7,
           f"railgun with \"damage\" 7: hit {[d for _, d in mod]} (stock 150: {[d for _, d in stock]}; "
           f"doubled by the surprise bonus)")

    (stock, stock_fired), (mod, mod_fired) = shots[("rocket launcher", False)], shots[("rocket launcher", True)]
    stock_direct = max((damage for _, damage in stock), default=0)
    mod_direct = max((damage for _, damage in mod), default=0)
    surprise = 2 if stock_direct >= 200 else 1
    report(100 * surprise <= stock_direct < 120 * surprise and 50 * surprise <= mod_direct < 70 * surprise,
           f"rocket with \"damage\" 50: direct hit {mod_direct} (stock {stock_direct})")
    stock_flight = (stock[0][0] - stock_fired) if stock else 0
    mod_flight = (mod[0][0] - mod_fired) if mod else 0
    report(stock_flight > 0 and mod_flight > stock_flight * 3,
           f"rocket with \"speed\" 100: hit after {mod_flight:.2f} s (stock 650: {stock_flight:.2f} s)")

    report("ammo_shells: \"damage\" is for weapons; this item isn't one" in logs,
           "a weapon field on ammo is reported")
    errors = [line for line in logs.splitlines() if "ERROR" in line or "crashed" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nWeapon definitions check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
