#!/usr/bin/env python3
"""Re-release game save check for OpenTech2: with id's stock re-release game
library (built as in rr_game_check.py) on the re-release data:
  - level changes keep the level: a tank spawned on base1 is still there
    after going to base2 and back (the game's WriteLevelJson / ReadLevelJson),
  - a save written in one run loads in a fresh run: the player is back at
    the saved spot with the saved health, and the tank is there
    (WriteGameJson / ReadGameJson and the level files),
  - no errors, every game message readable.

Usage:
  rr_save_check.py [<quake2-rerelease-dll>/rerelease]
Hidden software renderer, temporary HOME.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_game_check  # noqa: E402  (shares the game build and paths)

OUTPUT_DIR = rr_game_check.REPO_DIR / "build" / "rr-save-check"

TRANSITION_SCRIPT = """set cheats 1
map base1
delay 3000 "notarget"
delay 3100 "spawn monster_tank"
delay 3500 "echo STEP before"
delay 3600 "rr_edicts monster_tank"
delay 3700 "gamemap base2"
delay 8000 "echo STEP base2"
delay 8100 "rr_edicts monster_tank"
delay 8200 "gamemap base1"
delay 13000 "echo STEP back"
delay 13100 "rr_edicts monster_tank"
delay 13200 "teleport 200 -300 46 0 90 0"
delay 13400 "save rrcheck"
delay 13800 "quit"
"""

LOAD_SCRIPT = """load rrcheck
delay 4000 "echo STEP loaded"
delay 4100 "viewpos"
delay 4200 "rr_edicts monster_tank"
delay 4300 "rr_edicts player"
delay 4400 "quit"
"""


def run(mod_dir, home, script, log_path):
    (mod_dir / "c.cfg").write_text(script)
    environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with open(log_path, "w") as log_file:
        subprocess.run(["stdbuf", "-oL", str(rr_game_check.CLIENT_BINARY),
                        "-datadir", str(rr_game_check.RERELEASE_DATA_DIR),
                        "+set", "game", rr_game_check.MOD_NAME, "+set", "fixedtime", "16667",
                        "+set", "vid_renderer", "soft", "+set", "s_initsound", "0", "+exec", "c.cfg"],
                       env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=120, check=False)
    return log_path.read_text(errors="replace")


def steps(log):
    """{step name: [rr_edict lines after it]}"""
    found, current = {}, None
    for line in log.splitlines():
        step = re.match(r"STEP (\w+)", line)
        if step:
            current = step.group(1)
            found[current] = []
        elif current and line.startswith("rr_edict "):
            found[current].append(line)
    return found


def main():
    source_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else rr_game_check.DEFAULT_SOURCE
    if not (source_dir / "g_main.cpp").exists():
        sys.exit(f"no re-release game source in {source_dir}")
    game_library = rr_game_check.build_game(source_dir)

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    with tempfile.TemporaryDirectory(prefix="optech2-rr-save-") as home:
        mod_dir = Path(home) / ".opentech2" / rr_game_check.MOD_NAME
        mod_dir.mkdir(parents=True)
        shutil.copy(game_library, mod_dir / "game_x64.so")
        first = run(mod_dir, home, TRANSITION_SCRIPT, OUTPUT_DIR / "transition.log")
        second = run(mod_dir, home, LOAD_SCRIPT, OUTPUT_DIR / "load.log")
        saved = sorted(p.name for p in (mod_dir / "save" / "rrcheck").glob("*")) \
            if (mod_dir / "save" / "rrcheck").exists() else []

    found = steps(first)
    report(len(found.get("before", [])) == 1, "tank spawned on base1")
    report(len(found.get("base2", [])) == 0, "no tank on base2")
    report(found.get("back") == found.get("before"),
           f"tank still there after base2 and back ({found.get('back')})")
    report("base1.sav" in saved and "game.ssv" in saved, f"save written: {', '.join(saved)}")

    loaded = steps(second)
    position = re.search(r"position: (-?\d+) (-?\d+) (-?\d+)", second.split("STEP loaded")[-1])
    report(bool(position) and abs(int(position.group(1)) - 200) <= 2 and abs(int(position.group(2)) + 300) <= 2,
           f"loaded in a fresh run at the saved spot ({position.groups() if position else None})")
    tank = [line for line in loaded.get("loaded", []) if "monster_tank" in line]
    player = [line for line in loaded.get("loaded", []) if " player " in line]
    report(len(tank) == 1, "tank there after loading")
    report(bool(player) and "health 100" in player[0], f"player health kept ({player[0] if player else None})")

    for name, log in (("first run", first), ("second run", second)):
        bad = [line for line in log.splitlines() if "(BAD" in line or "ERROR" in line or "Game Error" in line]
        report(not bad, f"{name}: no errors" + (f" ({bad[0].strip()[:90]})" if bad else ""))

    passed = all(results)
    print(f"\nRe-release save check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
