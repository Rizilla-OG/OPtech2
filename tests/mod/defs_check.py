#!/usr/bin/env python3
"""Item definition check for OpenTech2 (M2: data-driven definitions).

Makes two mod folders in a temporary home, each with defs/<folder>.items,
and plays base1 with "+set fs_mods moda,modb":
  - changing a stock item: moda gives shells boxes 25 shells, modb 40;
    modb is higher in the stack, so a box gives 40 (stock: 10),
  - a new item: moda's "Boomstick" is a copy of the shotgun that fires
    bullets. It spawns in the map by its classname, picking it up gives
    the Boomstick and bullets, and firing it uses a bullet,
  - mistakes (unknown field, a new item without "base", a word where a
    number goes) are printed and the rest still loads.
The stock game (no definition files) keeps 10 shells per box.

Usage:
  defs_check.py
Needs build/release/OpenTech2 and the game data. Runs with no window.
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
OUTPUT_DIR = REPO_DIR / "build" / "defs-check"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
START = "128 -320 30"  # the base1 start spot, where spawned items land on the player

DEFINITIONS = {
    "moda": """
// shells boxes give more
{
"classname" "ammo_shells"
"quantity" "25"
}

// a new weapon: the shotgun's behaviour, firing bullets
{
"classname" "weapon_boomstick"
"base" "weapon_shotgun"
"pickup_name" "Boomstick"
"ammo" "Bullets"
"quantity" "1"
}

// mistakes, reported and skipped
{
"classname" "ammo_bullets"
"colour" "red"
"quantity" "lots"
}
{
"classname" "item_nothing_like_it"
"pickup_name" "Nothing"
}
""",
    "modb": """
{
"classname" "ammo_shells"
"quantity" "40"
}
""",
}

PICKUPS = [
    "map base1",
    'delay 3000 "lua print(\'before\', game.count(1, \'Shells\'), game.count(1, \'Bullets\'))"',
    f'delay 3200 "spawnentity ammo_shells {START}"',
    f'delay 3700 "spawnentity weapon_boomstick {START}"',
    'delay 4300 "lua print(\'after\', game.count(1, \'Shells\'), game.count(1, \'Boomstick\'), game.count(1, \'Bullets\'))"',
    'delay 4400 "use Boomstick"',
    'delay 5400 "+attack"',
    'delay 5500 "-attack"',
    'delay 6500 "lua print(\'fired\', game.count(1, \'Bullets\'))"',
    'delay 6700 quit',
]


def play(name, with_mods):
    with tempfile.TemporaryDirectory(prefix="opentech2-defs-") as home:
        user_dir = Path(home) / ".opentech2"
        (user_dir / "baseq2").mkdir(parents=True)
        arguments = []
        console = PICKUPS
        if with_mods:
            for folder, text in DEFINITIONS.items():
                (user_dir / folder / "defs").mkdir(parents=True)
                (user_dir / folder / "defs" / f"{folder}.items").write_text(text)
            arguments = ["+set", "fs_mods", "moda,modb"]
        else:
            console = PICKUPS[:4] + ['delay 4300 "lua print(\'after\', game.count(1, \'Shells\'))"', 'delay 4500 quit']
        (user_dir / "baseq2" / "defstest.cfg").write_text("\n".join(console) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR)] + arguments +
                                    ["+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "s_initsound", "0",
                                     "+set", "cheats", "1", "+exec", "defstest.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    log = log.replace(home, "~")
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    return log


def main():
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    log = play("mods", True)
    report("Item definitions: defs/moda.items (1 new)" in log and "Item definitions: defs/modb.items (0 new)" in log,
           "both folders' item files read, moda adds one item")
    before = re.search(r"^before\s+(\d+)\s+(\d+)$", log, re.M)
    after = re.search(r"^after\s+(\d+)\s+(\d+)\s+(\d+)$", log, re.M)
    if before and after:
        shells = int(after.group(1)) - int(before.group(1))
        report(shells == 40, f"a shells box gives modb's 40 (higher in the stack than moda's 25): {shells}")
        report(after.group(2) == "1", f"the new Boomstick spawned by its classname and was picked up: {after.group(2)}")
        bullets = int(after.group(3)) - int(before.group(2))
        report(bullets > 0, f"its pickup gave bullets, its \"ammo\": {bullets}")
    else:
        report(False, "pickup counts printed")
    fired = re.search(r"^fired\s+(\d+)$", log, re.M)
    report(bool(fired and after and int(fired.group(1)) == int(after.group(3)) - 1),
           "firing the Boomstick (the shotgun's code) used one bullet" +
           (f": {after.group(3)} -> {fired.group(1)}" if fired and after else ""))
    report("unknown field \"colour\"" in log and "needs a whole number, not \"lots\"" in log and
           "item_nothing_like_it: a new item needs \"base\"" in log, "mistakes are reported")

    stock = play("stock", False)
    after_stock = re.search(r"^after\s+(\d+)$", stock, re.M)
    report(bool(after_stock and after_stock.group(1) == "10") and "Item definitions" not in stock,
           "stock game, no definition files: a shells box gives 10" +
           (f" ({after_stock.group(1)})" if after_stock else ""))

    errors = [line for line in (log + stock).splitlines() if "ERROR" in line or "crashed" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nDefinitions check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
