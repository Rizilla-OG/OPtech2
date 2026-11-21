#!/usr/bin/env python3
"""Item definition check for OpenTech2 (M2: data-driven definitions).

Makes two mod folders in a temporary home, each with defs/<folder>.items,
and plays base1 with "+set fs_mods moda,modb":
  - changing a stock item: moda gives shells boxes 25 shells, modb 40;
    modb is higher in the stack, so a box gives 40 (stock: 10),
  - a new item: moda's "Boomstick" is a copy of the shotgun that fires
    bullets. It spawns in the map by its classname, picking it up gives
    the Boomstick and bullets, and firing it uses a bullet,
  - "gives": moda's "Shell Box" (a copy of ammo_shells) adds 30 Shells,
  - mistakes (unknown field, a new item without "base", a word where a
    number goes) are printed and the rest still loads,
  - saves match items by name: a save made with moda loads with another
    folder's item added before moda's (item numbers shifted), keeping the
    Boomstick in hand; without moda it loads too, the Boomstick dropped.
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

// a bigger box that adds to the stock Shells
{
"classname" "ammo_shells_big"
"base" "ammo_shells"
"pickup_name" "Shell Box"
"gives" "Shells"
"quantity" "30"
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
    "modz": """
// an extra item, so moda's items get other numbers when modz is below it
{
"classname" "item_extra_quad"
"base" "item_quad"
"pickup_name" "Extra Quad"
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
    'delay 6600 "lua print(\'shells before box\', game.count(1, \'Shells\'))"',
    f'delay 6700 "spawnentity ammo_shells_big {START}"',
    'delay 7300 "lua print(\'box\', game.count(1, \'Shells\'), game.count(1, \'Shell Box\'))"',
    # a health pack left lying (full health): "Health" has no classname,
    # which the save's item table has to handle
    'delay 7350 "lua game.set_health(1, 100)"',
    f'delay 7360 "spawnentity item_health {START}"',
    'delay 7400 "save itemtest"',
    'delay 8400 quit',
]

LOAD = [
    "load itemtest",
    'delay 2500 "lua game.set_health(1, 50)"',
    'delay 2900 "lua print(\'healed\', game.health(1))"',
    'delay 3000 "lua print(\'loaded\', game.weapon(1), game.count(1, \'Boomstick\'), game.count(1, \'Bullets\'), '
    'game.count(1, \'Shells\'))"',
    'delay 3300 quit',
]
# without moda there's no Boomstick to count
LOAD_WITHOUT = [LOAD[0], 'delay 3000 "lua print(\'loaded\', \'[\' .. game.weapon(1) .. \']\', game.count(1, \'Bullets\'))"',
                LOAD[-1]]


def play(name, with_mods, home=None, fs_mods="moda,modb", console=None):
    with tempfile.TemporaryDirectory(prefix="opentech2-defs-") as temporary_home:
        home = home or temporary_home
        user_dir = Path(home) / ".opentech2"
        (user_dir / "baseq2").mkdir(parents=True, exist_ok=True)
        arguments = []
        console = console or PICKUPS
        if with_mods:
            for folder, text in DEFINITIONS.items():
                (user_dir / folder / "defs").mkdir(parents=True, exist_ok=True)
                (user_dir / folder / "defs" / f"{folder}.items").write_text(text)
            arguments = ["+set", "fs_mods", fs_mods]
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

    save_home = tempfile.mkdtemp(prefix="opentech2-defs-save-")
    log = play("mods", True, home=save_home)
    report("Item definitions: defs/moda.items (2 new)" in log and "Item definitions: defs/modb.items (0 new)" in log,
           "both folders' item files read, moda adds two items")
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

    box = re.search(r"^shells before box\s+(\d+)$", log, re.M)
    box_after = re.search(r"^box\s+(\d+)\s+(\d+)$", log, re.M)
    report(bool(box and box_after and int(box_after.group(1)) - int(box.group(1)) == 30 and box_after.group(2) == "0"),
           "\"gives\": the Shell Box added 30 Shells and has no count of its own" +
           (f" ({box.group(1)} -> {box_after.group(1)} shells)" if box and box_after else ""))
    report("mismatch" not in log and re.search(r"Saving game|save.*itemtest|itemtest", log) is not None,
           "saved the game")

    shifted = play("load-shifted", True, home=save_home, fs_mods="modz,moda,modb", console=LOAD)
    loaded = re.search(r"^loaded\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)$", shifted, re.M)
    report(bool(loaded and loaded.group(1) == "Boomstick" and loaded.group(2) == "1" and
                after and loaded.group(3) == fired.group(1)),
           "loaded with modz's item added below moda (numbers shifted): Boomstick in hand, same bullets" +
           (f" ({loaded.group(1)}, bullets {loaded.group(3)})" if loaded else ""))

    healed = re.search(r"^healed\s+(\d+)$", shifted, re.M)
    report(bool(healed and int(healed.group(1)) > 50) and "crashed" not in shifted,
           "a health pack saved lying on the floor still works after loading" +
           (f" (50 -> {healed.group(1)} health)" if healed else ""))

    without = play("load-without", True, home=save_home, fs_mods="modz,modb", console=LOAD_WITHOUT)
    loaded = re.search(r"^loaded\s+\[([^]]*)\]\s+(\d+)$", without, re.M)
    report("the save has an item this game doesn't, weapon_boomstick; dropped" in without and
           bool(loaded and fired and loaded.group(2) == fired.group(1) and loaded.group(1) == "Blaster"),
           "loaded without moda: the Boomstick is dropped with a message, Blaster in hand, bullets kept" +
           (f" (in hand: {loaded.group(1) or 'nothing'}, bullets {loaded.group(2)})" if loaded else ""))
    shutil.rmtree(save_home, ignore_errors=True)

    stock = play("stock", False)
    after_stock = re.search(r"^after\s+(\d+)$", stock, re.M)
    report(bool(after_stock and after_stock.group(1) == "10") and "Item definitions" not in stock,
           "stock game, no definition files: a shells box gives 10" +
           (f" ({after_stock.group(1)})" if after_stock else ""))

    errors = [line for line in (log + shifted + without + stock).splitlines()
              if "ERROR" in line or "crashed" in line or "mismatch" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nDefinitions check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
