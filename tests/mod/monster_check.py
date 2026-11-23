#!/usr/bin/env python3
"""Monster definition and combat event check for OpenTech2 (M2).

A mod folder defines a new monster in defs/moda.monsters, "monster_soldier_elite":
a soldier with 300 health (stock 20), skin 2, scale 1.5, and attacks at 300%
damage. Its script, scripts/moda.lua, halves damage to the elite
(monster_damage), drops a health pack where a monster dies (monster_death,
game.spawn), and logs player_damage and player_death.

On base1, with the player in god mode and unseen:
  - the elite spawns by its new classname and survives the first railgun
    hits: 300 then 150 (the surprise bonus doubles the first), halved by the
    script, it needs three hits,
  - monster_death fires once, after the third hit, and the health pack spawns,
then the player is seen: the elite's attacks are logged at 3x a stock
soldier's (a second session without the mod), and with god mode off the
player dies and player_death names the elite.
Mistakes (a base that is itself a new monster, a word for a number) are
reported.

Usage:
  monster_check.py
Needs build/release/OpenTech2 and the game data. Runs with no window, about
a minute.
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
OUTPUT_DIR = REPO_DIR / "build" / "monster-check"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
PLAYER_SPOT = "128 -320 32 0 135 0"     # open room on base1 (as in tests/tickrate/damage_check.py)
TARGET_SPOT = "-13 -179 40 0 315 0"     # 200 units in front of the player, facing them

MONSTERS = """
{
"classname" "monster_soldier_elite"
"base" "monster_soldier"
"health" "300"
"skin" "2"
"scale" "1.5"
"damage_percent" "300"
}
// mistakes
{
"classname" "monster_soldier_worse"
"base" "monster_soldier_elite"
"health" "lots"
}
"""

SCRIPT = """
game.on("monster_damage", function(monster, damage, player)
    if monster == "monster_soldier_elite" then
        print("elite hit", damage, "halved to", damage // 2)
        return damage // 2
    end
end)
game.on("monster_death", function(monster, player, x, y, z)
    print("monster_death", monster, player)
    print("dropped", game.spawn("item_health_large", x, y, z + 16))
end)
game.on("player_damage", function(player, damage, attacker)
    print("player_damage", damage, attacker)
end)
game.on("player_death", function(player, attacker)
    print("player_death", player, attacker)
end)
"""


def session(name, with_mod, soldier):
    script = [
        "set g_damagelog 1", "map base1",
        'delay 3000 "god"', 'delay 3050 "notarget"', f'delay 3100 "teleport {PLAYER_SPOT}"',
        'delay 3200 "give railgun"', 'delay 3250 "give slugs"', 'delay 3300 "use railgun"',
    ]
    if with_mod:
        script += [
            f'delay 3500 "cmd spawnentity {soldier} {TARGET_SPOT}"',
            'delay 5000 "+attack"', 'delay 9800 "-attack"',          # railgun: about 3 shots
            'delay 10500 "cmd listentities items"',
            f'delay 11000 "cmd spawnentity {soldier} {TARGET_SPOT}"', 'delay 11100 "notarget"',  # seen: it shoots
            'delay 16000 "god"',                                     # god off: the player dies
            'delay 24000 quit',
        ]
    else:
        script += [f'delay 11000 "cmd spawnentity {soldier} {TARGET_SPOT}"', 'delay 11100 "notarget"',
                   'delay 16000 quit']
    with tempfile.TemporaryDirectory(prefix="opentech2-monsters-") as home:
        user_dir = Path(home) / ".opentech2"
        (user_dir / "baseq2").mkdir(parents=True)
        arguments = []
        if with_mod:
            (user_dir / "moda" / "defs").mkdir(parents=True)
            (user_dir / "moda" / "scripts").mkdir(parents=True)
            (user_dir / "moda" / "defs" / "moda.monsters").write_text(MONSTERS)
            (user_dir / "moda" / "scripts" / "moda.lua").write_text(SCRIPT)
            arguments = ["+set", "fs_mods", "moda"]
        (user_dir / "baseq2" / "monsters.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR)] + arguments +
                                    ["+set", "fixedtime", "16667", "+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
                                     "+set", "s_initsound", "0", "+set", "g_classic_balance", "1", "+set", "skill", "1", "+exec", "monsters.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=90, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
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

    log = session("mod", True, "monster_soldier_elite")
    stock = session("stock", False, "monster_soldier")

    report("Monster definitions: defs/moda.monsters" in log, "the mod's monster file is read")
    hits = [int(value) for value in re.findall(r"damagelog time [\d.]+ attacker player target monster_soldier_elite "
                                                r"damage (\d+)", log)]
    death_at = log.find("monster_death monster_soldier_elite")
    hits_before_death = len(re.findall(r"attacker player target monster_soldier_elite", log[:death_at])) \
        if death_at >= 0 else 0
    report(len(hits) >= 3 and hits[0] == 150 and hits[1] == 75,
           f"monster_damage halves the hits: {hits[:4]} (railgun 150, the first doubled by the surprise bonus)")
    report(death_at >= 0 and hits_before_death == 3 and log.count("monster_death monster_soldier_elite") == 1,
           f"the elite has 300 health: died on hit {hits_before_death} (300 = 150 + 75 + 75), death heard once")
    report(re.search(r"^monster_death monster_soldier_elite\s+1$", log, re.M) is not None,
           "monster_death names the monster and the player who killed it")
    report(re.search(r"^dropped\s+true$", log, re.M) is not None and "item_health_large" in log.split("dropped")[-1],
           "game.spawn dropped a health pack where it died")

    elite = [int(value) for value in re.findall(r"^player_damage\s+(\d+)\s+monster_soldier_elite$", log, re.M)]
    soldier = [int(value) for value in re.findall(r"damagelog time [\d.]+ attacker monster_soldier target player "
                                                  r"damage (\d+)", stock)]
    report(bool(elite) and bool(soldier) and min(elite) == 3 * min(soldier) and all(d % 3 == 0 for d in elite),
           f"\"damage_percent\" 300: the elite's hits {sorted(set(elite))[:5]}, a stock soldier's {sorted(set(soldier))[:5]}")
    report(re.search(r"^player_death\s+1\s+monster_soldier_elite$", log, re.M) is not None,
           "player_death: the player died, killed by the elite")
    report("monster_soldier_worse: base monster_soldier_elite is itself a new monster" in log and
           "\"health\" needs a whole number, not \"lots\"" in log, "mistakes are reported")

    errors = [line for line in (log + stock).splitlines() if "ERROR" in line or "crashed" in line or
              "script error" in line]
    report(not errors, "no errors or crashes" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nMonster check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
