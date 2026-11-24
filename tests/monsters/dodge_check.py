#!/usr/bin/env python3
"""Active dodging check for OpenTech2 (re-release AI port: M_CheckDodge).

On optech2_ledge (tests/maps/make_ledge_map.py) a monster stands on the high
ledge, 320 up, which it can't leave; the player, in god mode, shoots up at
it from the floor, about 520 units away:
  1. rockets at a shotgun soldier: it sees them coming and sidesteps or
     ducks (re-release: the monster watches projectiles in flight; stock
     Quake II checked once, when a player fired),
  2. grenades at an infantry: lobbed projectiles are never ducked, only
     stepped away from (stock Quake II ignored grenades),
  3. rockets at the soldier from behind it, with the player unheard
     (notarget): it can't see them coming, no dodge until one hits.
The monsters get 100000 health from a mod folder's definitions, so every
shot finds them alive. With g_damagelog 1 the game logs each dodge ("dodgelog ... sidestep" or
"duck") and each hit.

Usage:
  dodge_check.py
Needs build/release/OpenTech2, build/maps/optech2_ledge.bsp and the game
data. Runs with no window, about
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
OUTPUT_DIR = REPO_DIR / "build" / "dodge-check"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_ledge.bsp"
# the monster stands on the high ledge (320 up, too high to drop) and can't
# come down to the player, who shoots up at it from the floor, 430 units out
PLAYER_SPOT = "352 -300 24 -39 90 0"     # floor, looking north and up at the ledge's lip
MONSTER_SPOT = "352 90 344 0 270 0"      # on the high ledge near its lip, facing the player
MONSTER_BACK_SPOT = "352 90 344 0 90 0"   # same spot, facing away
SHOTS = 8
# a mod folder makes the targets hard to kill, so every shot finds one alive
TOUGH_MONSTERS = """
{
"classname" "monster_soldier"
"health" "100000"
}
{
"classname" "monster_infantry"
"health" "100000"
}
"""
FIRST_SHOT_MS = 7000
SHOT_SPACING_MS = 1500


def run_scene(name, monster, monster_spot, weapon, ammo, unheard=False):
    """Fires SHOTS shots at a monster; returns the game log. unheard: notarget, so it can't hear the shots."""
    script = ["set g_damagelog 1", "map optech2_ledge", 'delay 6000 "god"', f'delay 6100 "teleport {PLAYER_SPOT}"',
              *(['delay 6050 "notarget"'] if unheard else []),
              f'delay 6300 "cmd spawnentity {monster} {monster_spot}"',
              f'delay 6400 "give {weapon}"', f'delay 6450 "give {ammo}"', f'delay 6500 "use {weapon}"']
    for shot in range(SHOTS):
        at = FIRST_SHOT_MS + shot * SHOT_SPACING_MS
        # aim again before each shot (a teleport holds the pitch level for a moment)
        script += [f'delay {at - 400} "teleport {PLAYER_SPOT}"', f'delay {at} "+attack"', f'delay {at + 50} "-attack"']
    script += [f'delay {FIRST_SHOT_MS + SHOTS * SHOT_SPACING_MS + 1500} "quit"']
    with tempfile.TemporaryDirectory(prefix="opentech2-dodge-") as home:
        user_dir = Path(home) / ".opentech2" / "baseq2"
        (user_dir / "maps").mkdir(parents=True)
        shutil.copy(MAP_FILE, user_dir / "maps" / MAP_FILE.name)
        (user_dir / "dodge.cfg").write_text("\n".join(script) + "\n")
        mod_dir = Path(home) / ".opentech2" / "dodgetest" / "defs"
        mod_dir.mkdir(parents=True)
        (mod_dir / "dodgetest.monsters").write_text(TOUGH_MONSTERS)
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run([str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set", "fs_mods", "dodgetest", "+set", "freelook", "1", "+set", "lookspring", "0",
                                     "+set", "fixedtime", "16667", "+set", "vid_renderer", "soft", "+set", "r_vsync", "0",
                                     "+set", "cl_maxfps", "60", "+set", "s_initsound", "0", "+set", "skill", "2",
                                     "+exec", "dodge.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=120, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the game didn't quit\n"
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    return log


def dodges(log, monster):
    return re.findall(rf"dodgelog time [\d.]+ monster {monster} (sidestep|duck) eta [\d.]+( lobbed)?", log)


def failed_rolls(log, monster):
    # optech2: the skill roll said no: logged once per projectile, then no new roll until it arrives
    return re.findall(rf"dodgelog time [\d.]+ monster {monster} nododge eta", log)


def main():
    if not MAP_FILE.exists():
        sys.exit(f"{MAP_FILE} missing: run python3 tests/maps/make_ledge_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    rockets = run_scene("rockets", "monster_soldier", MONSTER_SPOT, "rocket launcher", "rockets")
    rocket_dodges = dodges(rockets, "monster_soldier")
    hits = len(re.findall(r"damagelog time [\d.]+ attacker player target monster_soldier ", rockets))
    rolls = len(rocket_dodges) + len(failed_rolls(rockets, "monster_soldier"))
    report(len(rocket_dodges) >= 1,
           f"soldier dodges rockets in flight: {len(rocket_dodges)} dodges of {SHOTS} rockets "
           f"({sum(kind == 'sidestep' for kind, _ in rocket_dodges)} sidesteps, "
           f"{sum(kind == 'duck' for kind, _ in rocket_dodges)} ducks), {hits} hits")
    # optech2: one roll per rocket: it used to roll again every think and nearly always dodge
    report(rolls <= SHOTS,
           f"soldier rolls once per rocket: {rolls} rolls for {SHOTS} rockets")

    grenades = run_scene("grenades", "monster_infantry", MONSTER_SPOT, "grenade launcher", "grenades")
    grenade_dodges = dodges(grenades, "monster_infantry")
    lobbed = [kind for kind, gravity in grenade_dodges if gravity]
    report(bool(lobbed) and all(kind == "sidestep" for kind in lobbed),
           f"infantry steps away from grenades, never ducks them: {len(lobbed)} dodges "
           f"({', '.join(sorted(set(lobbed))) or 'none'})")

    behind = run_scene("behind", "monster_soldier", MONSTER_BACK_SPOT, "rocket launcher", "rockets", unheard=True)
    first_hit = re.search(r"damagelog time ([\d.]+) attacker player target monster_soldier ", behind)
    # once hit it turns round and may dodge later rockets: count only before the first hit
    before_hit = [time for time in re.findall(r"dodgelog time ([\d.]+) monster monster_soldier", behind)
                  if not first_hit or float(time) < float(first_hit.group(1))]
    report(first_hit is not None and not before_hit,
           f"soldier facing away doesn't dodge: {len(before_hit)} dodges before it was hit")

    bad = [line for log in (rockets, grenades, behind) for line in log.splitlines()
           if re.search(r"ERROR|Segmentation|assert", line)]
    report(not bad, "no errors in the logs" + (f": {bad[0]}" if bad else ""))
    print(f"logs: {OUTPUT_DIR}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
