#!/usr/bin/env python3
"""Player movement check for OpenTech2 (M2: movement is game code).

Player movement lives in the game library (src/game/g_pmove.c compiles the
shared src/common/shared/pmove.c) and the client predicts with the game
library's copy. This check plays a scripted walk on base1 (run, strafe, turn,
jump, crouch) with cl_showmiss 1 and counts prediction misses:

  A  stock game, prediction from the game library     -> no misses
  B  stock game, engine's own copy (no client module)  -> no misses
  C  test mod with changed movement (max speed 450),
     prediction from the mod's game library            -> no misses
  D  the same mod, engine's copy                       -> misses (what A/C fix)

The test mod is built from a copy of the tree in build/pmove-check/ (the
repo is not changed).

Usage:
  pmove_check.py
Needs cmake and a C/C++ compiler. Hidden software renderer, temporary HOME.
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
OUTPUT_DIR = REPO_DIR / "build" / "pmove-check"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
MOD_NAME = "pmovetest"

# the walk: (milliseconds from map start, command)
WALK = [
    (4000, "+forward"), (5500, "+moveright"), (6500, "-moveright"),
    (6600, "+moveup"), (6700, "-moveup"), (7500, "+left"), (8200, "-left"),
    (8300, "+movedown"), (9300, "-movedown"), (9400, "+moveup"), (9500, "-moveup"),
    (10500, "-forward"), (11000, "cmd playerinfo"), (11500, "quit"),
]


def build_test_mod():
    """Game library with max speed 450 instead of 300, from a copy of the tree."""
    tree = OUTPUT_DIR / "tree"
    shutil.copytree(REPO_DIR, tree, ignore=shutil.ignore_patterns("build", ".git", "docs", "tests"))
    pmove_source = tree / "src" / "common" / "shared" / "pmove.c"
    text = pmove_source.read_text()
    if "static float pm_maxspeed = 300;" not in text:
        sys.exit("pm_maxspeed line not found in pmove.c")
    pmove_source.write_text(text.replace("static float pm_maxspeed = 300;", "static float pm_maxspeed = 450;"))
    build_dir = OUTPUT_DIR / "mod-build"
    for command in (["cmake", "-S", str(tree), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release"],
                    ["cmake", "--build", str(build_dir), "--target", "game", "-j8"]):
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"test mod build failed:\n{result.stdout[-2000:]}\n{result.stderr[-2000:]}")
    library = next(build_dir.rglob("game.so"))
    return library


def play(name, mod_library=None, engine_movement=False):
    with tempfile.TemporaryDirectory(prefix="optech2-pmove-") as home:
        game_dir = Path(home) / ".opentech2" / (MOD_NAME if mod_library else "baseq2")
        game_dir.mkdir(parents=True)
        if mod_library:
            shutil.copy(mod_library, game_dir / "game.so")
        script = ["map base1"] + [f'delay {when} "{command}"' for when, command in WALK]
        (game_dir / "p.cfg").write_text("\n".join(script) + "\n")
        arguments = [str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set", "fixedtime", "16667", "+set", "vid_renderer", "soft", "+set", "r_vsync", "0",
                     "+set", "s_initsound", "0", "+set", "cheats", "1", "+set", "cl_showmiss", "1",
                     "+set", "cl_run", "1"]  # run: 400 u/s, over the stock 300 cap
        if mod_library:
            arguments += ["+set", "game", MOD_NAME]
        if engine_movement:
            arguments += ["+set", "cl_cgame", "/nonexistent.so"]  # no client module: engine's copy
        arguments += ["+exec", "p.cfg"]
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        result = subprocess.run(["stdbuf", "-oL"] + arguments, env=environment, capture_output=True,
                                text=True, errors="replace", timeout=60)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    misses = re.findall(r"prediction miss on \d+: (\d+)", log)
    from_game = "player movement from the game library" in log
    position = re.search(r"playerinfo .* origin (\S+) (\S+) (\S+)", log)
    origin = tuple(float(v) for v in position.groups()) if position else None
    return len(misses), sum(int(m) for m in misses), from_game, origin


def main():
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    stock_game = play("A_stock_game_movement")
    stock_engine = play("B_stock_engine_movement", engine_movement=True)
    mod_library = build_test_mod()
    mod_game = play("C_mod_game_movement", mod_library=mod_library)
    mod_engine = play("D_mod_engine_movement", mod_library=mod_library, engine_movement=True)

    def describe(result):
        return f"{result[0]} misses (total {result[1]}), end {result[3]}"

    report(stock_game[2] and stock_game[0] == 0, f"A stock, game library movement: {describe(stock_game)}")
    report(not stock_engine[2] and stock_engine[0] == 0, f"B stock, engine movement: {describe(stock_engine)}")
    report(mod_game[2] and mod_game[0] == 0, f"C mod, game library movement: {describe(mod_game)}")
    report(mod_engine[0] > 0, f"D mod, engine movement (expected misses): {describe(mod_engine)}")

    passed = all(results)
    print(f"\nPlayer movement check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
