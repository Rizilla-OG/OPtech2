#!/usr/bin/env python3
"""Lua script check for OpenTech2 (M2: scripting; two mods load together).

Makes three mod folders in a temporary home, each with its own script
(scripts/<folder>.lua), and plays base1 with "+set fs_mods moda,modb
+set game modc":
  - every folder's script runs, in stack order (moda, modb, then modc),
  - level_start and player_spawn reach every script; moda's spawn handler
    gives 7 shells,
  - item_pickup: modb refuses armor shards, a stimpack still goes through,
  - an error in modc's script is printed and the game carries on,
  - the sandbox: no io, os, debug, dofile, loadfile or require; load() takes
    no binary chunks; an endless loop and a huge allocation are stopped.
The stock game (no scripts) is unchanged: base1 loads with no script lines.
The example mod in mods/survival runs too: players spawn with 75 health, a
Shotgun and 10 shells.

Usage:
  script_check.py
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
OUTPUT_DIR = REPO_DIR / "build" / "script-check"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
START = "128 -320 30"  # the base1 start spot, where spawned items land on the player

SCRIPTS = {
    "moda": """
print("moda loaded")
game.on("level_start", function(map) print("moda level_start " .. map) end)
game.on("player_spawn", function(player)
    game.give(player, "Shells", 7)
    print("moda player_spawn " .. game.name(player))
end)
game.on("item_pickup", function(player, item) print("moda sees pickup " .. item) end)
game.on("item_taken", function(player, item) print("moda took " .. item) end)
""",
    "modb": """
print("modb loaded")
game.on("level_start", function(map) print("modb level_start " .. map) end)
game.on("item_pickup", function(player, item)
    if item == "Armor Shard" then
        print("modb refused " .. item)
        return false
    end
end)
""",
    "modc": """
print("modc loaded")
error("modc's deliberate error")
""",
}

CONSOLE = [
    "map base1",
    'delay 3000 "lua print(\'shells\', game.count(1, \'Shells\'))"',
    f'delay 3300 "spawnentity item_armor_shard {START}"',
    f'delay 3800 "spawnentity item_health_small {START}"',
    'delay 4500 "lua print(\'after pickups\', game.health(1), game.count(1, \'Armor Shard\'))"',
    'delay 4700 "lua print(\'sandbox\', io, os, debug, dofile, loadfile, require)"',
    'delay 4800 "lua print(\'binary\', load(string.dump(function() end)))"',
    'delay 4900 "lua while true do end"',
    'delay 5100 "lua local s = string.rep(\'x\', 200 * 1024 * 1024)"',
    'delay 5300 "lua print(\'still running\', game.map())"',
    'delay 5600 quit',
]


def play(name, with_mods):
    with tempfile.TemporaryDirectory(prefix="opentech2-script-") as home:
        user_dir = Path(home) / ".opentech2"
        (user_dir / "baseq2").mkdir(parents=True)
        arguments = []
        if with_mods:
            for folder, script in SCRIPTS.items():
                (user_dir / folder / "scripts").mkdir(parents=True)
                (user_dir / folder / "scripts" / f"{folder}.lua").write_text(script)
            arguments = ["+set", "fs_mods", "moda,modb", "+set", "game", "modc"]
            console = CONSOLE
        else:
            console = ["map base1", 'delay 3000 quit']
        cfg_dir = user_dir / ("modc" if with_mods else "baseq2")
        (cfg_dir / "scripttest.cfg").write_text("\n".join(console) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR)] + arguments +
                                    ["+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "s_initsound", "0",
                                     "+set", "cheats", "1", "+exec", "scripttest.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    log = log.replace(home, "~")
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    return log


def play_example():
    """mods/survival, copied into a temporary home, on base1."""
    with tempfile.TemporaryDirectory(prefix="opentech2-script-") as home:
        user_dir = Path(home) / ".opentech2"
        (user_dir / "baseq2").mkdir(parents=True)
        shutil.copytree(REPO_DIR / "mods" / "survival", user_dir / "survival")
        (user_dir / "baseq2" / "example.cfg").write_text("\n".join([
            "map base1",
            'delay 3000 "lua print(\'example\', game.health(1), game.count(1, \'Shotgun\'), game.count(1, \'Shells\'))"',
            "delay 3300 quit"]) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set", "fs_mods",
                                     "survival", "+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set",
                                     "s_initsound", "0", "+set", "cheats", "1", "+exec", "example.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    log = log.replace(home, "~")
    (OUTPUT_DIR / "example.log").write_text(log)
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
    loads = re.findall(r"^(mod\w) loaded$", log, re.M)
    report(loads[:3] == ["moda", "modb", "modc"], f"every folder's script runs, in stack order: {', '.join(loads)}")
    report("Script loaded: scripts/moda.lua" in log and "Script loaded: scripts/modb.lua" in log,
           "two mods' scripts loaded together (moda, modb)")
    report("script error in scripts/modc.lua" in log and "modc's deliberate error" in log,
           "an error in a script is printed")
    report("moda level_start base1" in log and "modb level_start base1" in log, "level_start reaches every script")
    report(re.search(r"moda player_spawn \S", log) is not None and re.search(r"^shells\s+7$", log, re.M) is not None,
           "player_spawn: moda gave 7 shells")
    report("modb refused Armor Shard" in log, "item_pickup: modb refused the armor shard")
    after = re.search(r"^after pickups\s+(\d+)\s+(\d+)$", log, re.M)
    report(after is not None and int(after.group(1)) > 100 and after.group(2) == "0" and
           "moda sees pickup Health" in log,
           "the shard stayed on the floor, the stimpack was taken" +
           (f" (health {after.group(1)}, shards {after.group(2)})" if after else ""))
    report(len(re.findall(r"^moda took Health$", log, re.M)) == 1 and "moda took Armor Shard" not in log and
           log.count("moda sees pickup Armor Shard") > 1,
           "item_taken: once for the stimpack, never for the refused shard (asked on every touch)")
    report(re.search(r"^sandbox(\s+nil){6}$", log, re.M) is not None, "no io, os, debug, dofile, loadfile, require")
    report(re.search(r"^binary\s+nil\s+.*binary chunk", log, re.M) is not None, "load() refuses binary chunks")
    report("script ran too long" in log, "an endless loop is stopped")
    report("not enough memory" in log, "a 200 MB allocation is refused")
    report(re.search(r"^still running\s+base1$", log, re.M) is not None, "the game carries on after all that")

    example = play_example()
    found = re.search(r"^example\s+(\d+)\s+(\S+)\s+(\d+)$", example, re.M)
    report("Script loaded: scripts/survival.lua" in example and "script error" not in example and
           bool(found and found.groups() == ("75", "1", "10")),
           "the example mod (mods/survival) runs: " +
           (f"health {found.group(1)}, shotguns {found.group(2)}, {found.group(3)} shells" if found else "no result"))

    stock = play("stock", False)
    report("Script loaded" not in stock and "script error" not in stock and "Couldn't" not in stock.split("map base1")[-1]
           if "map base1" in stock else "Script loaded" not in stock, "the stock game runs no scripts")

    errors = [line for line in (log + example + stock).splitlines() if "ERROR" in line or "crashed" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nScript check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
