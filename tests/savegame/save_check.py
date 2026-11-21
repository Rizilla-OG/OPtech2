#!/usr/bin/env python3
"""Savegame check for OpenTech2.

Plays a scripted single-player session on base1 (weapons and ammo given,
health changed, soldiers spawned, player moved), takes a snapshot (player
health, ammo, position and every entity's class and position), saves, and
then checks that loading the save gives the same snapshot:
  1. loaded again in the same session,
  2. loaded in a fresh game (new process),
The game is paused before the first snapshot and stays paused through the
load, so nothing moves and the snapshots can be compared exactly.

To check that saves survive code changes (the point of the tagged format),
save with one build and load with another:
  save_check.py --save-to build/save-a      (build 1: play, save, snapshot)
  ... change a saved struct, regenerate tables, rebuild ...
  save_check.py --load-from build/save-a    (build 2: load, compare)

Usage:
  save_check.py                    save + same-session load + fresh load
  save_check.py --save-to DIR      only save; keeps the save and snapshot in DIR
  save_check.py --load-from DIR    only load the save in DIR and compare

Hidden software renderer, no sound, temporary HOME (your saves are never
touched). Logs land in build/save-check/.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "save-check"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
GAME_TIMEOUT_SECONDS = 90
SAVE_NAME = "optech2check"

SETTINGS = ["+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
            "+set", "s_initsound", "0", "+set", "skill", "1"]
PLAYER_SPOT = "128 -320 32 0 135 0"
SOLDIER_SPOTS = ["-84 -108 56 0 315 0", "-200 -200 56 0 45 0", "0 -500 56 0 90 0"]
POSITION_TOLERANCE = 2.0


def at(milliseconds, command):
    return f'delay {milliseconds} "{command}"'


def snapshot_lines(milliseconds, label):
    """Console lines that print a snapshot between two markers."""
    return [at(milliseconds, f"echo snapshot_{label}"), at(milliseconds, "cmd listentities all"),
            at(milliseconds + 50, "cmd playerinfo"), at(milliseconds + 100, "cmd iteminfo"),
            at(milliseconds + 300, f"echo end_{label}")]


def parse_snapshot(log, label):
    match = re.search(rf"snapshot_{label}(.*?)end_{label}", log, re.DOTALL)
    if not match:
        return None
    text = match.group(1)
    info = re.search(r"playerinfo time ([\d.]+) health (-?\d+) ammo (-?\d+) origin (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)", text)
    entities = [(name, float(x), float(y), float(z)) for name, x, y, z in
                re.findall(r"^(\w+): (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\s*$", text, re.MULTILINE) if name != "freed"]
    if not info:
        return None
    weapon = re.search(r"^iteminfo weapon (\S+)", text, re.MULTILINE)
    inventory = sorted(re.findall(r"^iteminfo inventory (\S+ -?\d+)", text, re.MULTILINE))  # strings: JSON-safe
    item_entities = sorted(re.findall(r"^iteminfo entity (\S+ \S+ -?\d+ -?\d+ -?\d+)", text, re.MULTILINE))
    return {"time": float(info.group(1)), "health": int(info.group(2)), "ammo": int(info.group(3)),
            "origin": [float(info.group(index)) for index in (4, 5, 6)], "entities": sorted(entities),
            "weapon": weapon.group(1) if weapon else None, "inventory": inventory, "item_entities": item_entities}


def run_game(home, lines, log_name, data_dir):
    game_dir = Path(home) / ".opentech2" / "baseq2"
    game_dir.mkdir(parents=True, exist_ok=True)
    (game_dir / "check.cfg").write_text("\n".join(lines) + "\n")
    environment = dict(os.environ, HOME=str(home), SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    log_path = OUTPUT_DIR / f"{log_name}.log"
    with open(log_path, "w") as log_file:
        try:
            subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(data_dir), *SETTINGS, "+exec", "check.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT,
                           timeout=GAME_TIMEOUT_SECONDS, check=False)
        except subprocess.TimeoutExpired:
            pass
    return log_path.read_text(errors="replace")


def compare(expected, actual, label, report):
    if actual is None:
        report(False, f"{label}: no snapshot after loading (see the log)")
        return
    report(actual["health"] == expected["health"] and actual["ammo"] == expected["ammo"],
           f"{label}: health {actual['health']} ammo {actual['ammo']} (saved: {expected['health']}, {expected['ammo']})")
    distance = max(abs(a - b) for a, b in zip(actual["origin"], expected["origin"]))
    report(distance <= POSITION_TOLERANCE, f"{label}: player position off by {distance:.1f} units")
    report(abs(actual["time"] - expected["time"]) < 0.05,
           f"{label}: game time {actual['time']:.1f} s (saved at {expected['time']:.1f} s)")

    expected_classes = sorted(name for name, *_ in expected["entities"])
    actual_classes = sorted(name for name, *_ in actual["entities"])
    report(expected_classes == actual_classes,
           f"{label}: {len(actual_classes)} entities, same classes as saved ({len(expected_classes)})"
           if expected_classes == actual_classes else
           f"{label}: entity classes differ: {sorted(set(expected_classes) ^ set(actual_classes))[:6]}")

    # items: weapon in hand, inventory, and the item every item entity holds
    # (a save once lost the item of health packs: same class and position,
    # but touching one crashed)
    if "inventory" in expected:
        report(actual.get("weapon") == expected["weapon"] and actual.get("inventory") == expected["inventory"],
               f"{label}: weapon {actual.get('weapon')} and {len(actual.get('inventory', []))} inventory items as saved"
               if actual.get("inventory") == expected["inventory"] else
               f"{label}: items differ: {sorted(set(expected['inventory']) ^ set(actual.get('inventory', [])))[:6]}")
        missing = [line for line in actual.get("item_entities", []) if " NONE " in line]
        report(not missing and actual.get("item_entities") == expected["item_entities"],
               f"{label}: {len(actual.get('item_entities', []))} item entities, each with its item as saved"
               if not missing and actual.get("item_entities") == expected["item_entities"] else
               f"{label}: item entities differ ({len(missing)} without an item): "
               f"{sorted(set(expected['item_entities']) ^ set(actual.get('item_entities', [])))[:4]}")

    # each saved entity must have an entity of its class within the tolerance
    unmatched = []
    remaining = list(actual["entities"])
    for name, x, y, z in expected["entities"]:
        found = next((index for index, (other, ox, oy, oz) in enumerate(remaining)
                      if other == name and max(abs(x - ox), abs(y - oy), abs(z - oz)) <= POSITION_TOLERANCE), None)
        if found is None:
            unmatched.append(name)
        else:
            remaining.pop(found)
    report(not unmatched, f"{label}: every entity back in place" if not unmatched else
           f"{label}: {len(unmatched)} entities moved or missing ({unmatched[:5]})")


def save_session(home, data_dir):
    lines = ["map base1", at(6000, "god"), at(6050, "notarget"), at(6100, f"teleport {PLAYER_SPOT}"),
             at(6300, "give shotgun"), at(6350, "give shells"), at(6400, "give rocket launcher"),
             at(6450, "give rockets"), at(6500, "use rocket launcher")]
    lines += [at(6600 + index * 100, f"cmd spawnentity monster_soldier {spot}") for index, spot in enumerate(SOLDIER_SPOTS)]
    lines += [at(7200, "give health 37")]  # odd health number, to see it come back
    # paused from here on: nothing moves between the snapshots (pause also survives the load)
    lines += [at(8800, "set paused 1")]
    lines += snapshot_lines(9000, "saved")
    lines += [at(9500, f"save {SAVE_NAME}"), at(11000, "give health 50"), at(11500, f"load {SAVE_NAME}")]
    lines += snapshot_lines(16000, "same_session")
    lines.append(at(17000, "quit"))
    return run_game(home, lines, "save_session", data_dir)


def load_session(home, data_dir, log_name):
    # cl_loadpaused 2: the game pauses while loading and stays paused afterwards
    lines = ["set cl_loadpaused 2", at(1000, f"load {SAVE_NAME}")]
    lines += snapshot_lines(7000, "fresh")
    lines.append(at(8000, "quit"))
    return run_game(home, lines, log_name, data_dir)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--save-to", type=Path, help="only save, keep save + snapshot in this folder")
    parser.add_argument("--load-from", type=Path, help="only load the save kept in this folder and compare")
    arguments = parser.parse_args()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    table_check = subprocess.run([sys.executable, "-B", str(REPO_DIR / "tools" / "gen_save_fields.py"), "--check"],
                                 capture_output=True, text=True)
    report(table_check.returncode == 0, table_check.stdout.strip() or table_check.stderr.strip())

    with tempfile.TemporaryDirectory(prefix="optech2-save-") as home:
        save_dir = Path(home) / ".opentech2" / "baseq2" / "save"
        if arguments.load_from:
            snapshot = json.loads((arguments.load_from / "snapshot.json").read_text())
            shutil.copytree(arguments.load_from / "save", save_dir)
            log = load_session(home, arguments.datadir, "load_other_build")
            compare(snapshot, parse_snapshot(log, "fresh"), "load in this build", report)
        else:
            log = save_session(home, arguments.datadir)
            snapshot = parse_snapshot(log, "saved")
            if snapshot is None:
                print(f"FAIL  no snapshot before saving (see {OUTPUT_DIR}/save_session.log)")
                return 1
            print(f"saved: health {snapshot['health']}, ammo {snapshot['ammo']}, {len(snapshot['entities'])} entities, "
                  f"game time {snapshot['time']:.1f} s")
            report((save_dir / SAVE_NAME / "game.ssv").exists(), "save written")
            if arguments.save_to:
                arguments.save_to.mkdir(parents=True, exist_ok=True)
                shutil.rmtree(arguments.save_to / "save", ignore_errors=True)
                shutil.copytree(save_dir, arguments.save_to / "save")
                (arguments.save_to / "snapshot.json").write_text(json.dumps(snapshot))
                print(f"kept the save and snapshot in {arguments.save_to}")
            else:
                compare(snapshot, parse_snapshot(log, "same_session"), "load in the same session", report)
                compare(snapshot, parse_snapshot(load_session(home, arguments.datadir, "load_session"), "fresh"),
                        "load in a fresh game", report)

    errors = [line for line in (OUTPUT_DIR / ("load_other_build.log" if arguments.load_from else "save_session.log"))
              .read_text(errors="replace").splitlines() if "ERROR" in line or "Couldn't" in line]
    report(not errors, "no errors in the log" + (f" ({errors[0].strip()})" if errors else ""))
    passed = all(results)
    print(f"\nSave check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
