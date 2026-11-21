#!/usr/bin/env python3
"""Test mod check for OpenTech2 (M2: a mod, with no engine changes, adds a
weapon with a new visual effect, changes a HUD element and alters player
movement).

Builds mods/testmod (copy of the source tree + testmod.patch + the mod's own
files) into build/mod-check/, checks the mod only touches game code (the
shared movement code in src/common/shared is compiled into the game library
as well; the engine binary used below is the stock one, not rebuilt), then
plays base1 twice with the same stock engine, once with the stock game and
once with "+set game testmod", and compares:
  - weapon and effect: the Test Gun fires and its beam (temp entity 201) is
    drawn by the mod's client code (mod run only),
  - movement: the mod's jump goes about twice as high, and the client's
    prediction stays with it (the client predicts with the mod's movement),
  - HUD: the mod's "TEST MOD" label is on screen, the stock game has none.
A screenshot of the Test Gun goes to docs/screenshots/.

Usage:
  mod_check.py
Needs cmake and a C/C++ compiler. A real GL3 window for a few seconds per run.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
MOD_DIR = REPO_DIR / "mods" / "testmod"
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "mod-check"
DOC_SHOT = REPO_DIR / "docs" / "screenshots" / "m2_testmod_testgun.png"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
ENGINE_PARTS = ("src/client/", "src/server/", "src/common/", "src/backends/")
SHARED_GAME_CODE = ("src/common/shared/",)  # compiled into the game library too
JUMP_AT_MS = 5000
SAMPLES = range(JUMP_AT_MS - 200, JUMP_AT_MS + 1200, 50)  # playerinfo every 50 ms around the jump


def build_mod():
    tree = OUTPUT_DIR / "tree"
    shutil.copytree(REPO_DIR, tree, ignore=shutil.ignore_patterns("build", ".git", "docs", "tests", "mods"))
    patch = subprocess.run(["patch", "-p1", "-i", str(MOD_DIR / "testmod.patch")], cwd=tree,
                           capture_output=True, text=True)
    if patch.returncode != 0:
        sys.exit(f"testmod.patch doesn't apply:\n{patch.stdout}{patch.stderr}")
    patched = re.findall(r"patching file (\S+)", patch.stdout)
    shutil.copytree(MOD_DIR / "src", tree / "src", dirs_exist_ok=True)
    build_dir = OUTPUT_DIR / "mod-build"
    for command in (["cmake", "-S", str(tree), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release"],
                    ["cmake", "--build", str(build_dir), "--target", "game", "-j8"]):
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"test mod build failed:\n{result.stdout[-3000:]}\n{result.stderr[-3000:]}")
    return next(build_dir.rglob("game.so")), patched


def play(name, mod_library=None):
    """base1 with the stock engine; returns (log, screenshot path or None)."""
    game = "testmod" if mod_library else "baseq2"
    with tempfile.TemporaryDirectory(prefix="opentech2-mod-") as home:
        game_dir = Path(home) / ".opentech2" / game
        game_dir.mkdir(parents=True)
        if mod_library:
            shutil.copy(mod_library, game_dir / "game.so")
        lines = ["map base1", 'delay 3000 "god"', 'delay 3050 "notarget"', 'delay 3100 "clear"']
        # the jump, with the player's height sampled around it
        lines += [f'delay {JUMP_AT_MS} "+moveup"', f'delay {JUMP_AT_MS + 100} "-moveup"']
        lines += [f'delay {when} "cmd playerinfo"' for when in SAMPLES]
        if mod_library:
            lines += ['delay 7000 "give test gun"', 'delay 7100 "use test gun"', 'delay 8300 "+attack"',
                      'delay 8400 "-attack"', 'delay 8480 "screenshot png"', 'delay 9300 "+attack"',
                      'delay 9400 "-attack"', 'delay 10300 "quit"']
        else:
            lines += ['delay 8480 "screenshot png"', 'delay 9000 "quit"']
        (game_dir / "m.cfg").write_text("\n".join(lines) + "\n")
        environment = dict(os.environ, HOME=home, SDL_AUDIODRIVER="dummy")
        log_path = OUTPUT_DIR / f"{name}.log"
        arguments = ["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set", "game", game,
                     "+set", "fixedtime", "16667", "+set", "vid_renderer", "gl3", "+set", "vid_fullscreen", "0", "+set", "r_mode", "4",
                     "+set", "s_initsound", "0", "+set", "cheats", "1", "+set", "cl_showmiss", "1",
                     "+set", "developer", "1", "+set", "con_notifytime", "0", "+exec", "m.cfg"]
        with open(log_path, "w") as log_file:
            subprocess.run(arguments, env=environment, stdout=log_file, stderr=subprocess.STDOUT,
                           timeout=90, check=False)
        shots = sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []
        shot = None
        if shots:
            shot = OUTPUT_DIR / f"{name}.png"
            shutil.copy(shots[0], shot)
    return log_path.read_text(errors="replace"), shot


def jump_height(log):
    heights = [float(z) for z in re.findall(r"playerinfo time \S+ health \S+ ammo \S+ origin \S+ \S+ (\S+)", log)]
    return (max(heights) - heights[0]) if len(heights) > 2 else 0.0


def misses_after_spawn(log):
    """Prediction misses after the first two seconds (a spawn always has one)."""
    return [abs(int(value)) for frame, value in re.findall(r"prediction miss on (\d+): (-?\d+)", log)
            if int(frame) > 120]


def label_pixels(shot):
    """Bright text pixels where the mod's "TEST MOD" label goes (top centre)."""
    if not shot:
        return 0
    from PIL import Image  # only needed here
    image = Image.open(shot).convert("RGB")
    width, height = image.size
    left, top = width // 2 - 60, 4
    count = 0
    for x in range(left, left + 120):
        for y in range(top, top + 18):
            if sum(image.getpixel((x, y))) > 450:
                count += 1
    return count


def main():
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    library, patched = build_mod()
    mod_files = [str(p.relative_to(MOD_DIR)) for p in (MOD_DIR / "src").rglob("*") if p.is_file()]
    touched = sorted(set(patched + mod_files))
    engine_touched = [f for f in touched if f.startswith(ENGINE_PARTS) and not f.startswith(SHARED_GAME_CODE)]
    report(not engine_touched, f"no engine-only sources changed (mod touches: {', '.join(touched)})")

    stock_log, stock_shot = play("stock")
    mod_log, mod_shot = play("testmod", library)

    report(re.search(r"Using '[^']*testmod' for writing", mod_log) is not None, "game folder testmod in use")
    report("effects from the game library" in mod_log and "player movement from the game library" in mod_log,
           "the mod's game library draws the effects and runs the client's movement")

    # weapon and effect
    beams = re.findall(r"testmod: beam drawn from (\S+ \S+ \S+) to (\S+ \S+ \S+)", mod_log)
    report(len(beams) >= 2, f"Test Gun fired, its beam (temp entity 201) drawn by the mod {len(beams)} times")
    if mod_shot:
        DOC_SHOT.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(mod_shot, DOC_SHOT)

    # movement
    stock_jump, mod_jump = jump_height(stock_log), jump_height(mod_log)
    report(stock_jump > 30 and mod_jump > stock_jump * 1.7,
           f"movement: jump height {stock_jump:.0f} units stock, {mod_jump:.0f} with the mod")
    mod_misses = misses_after_spawn(mod_log)
    report(sum(mod_misses) / 8.0 < 8.0,
           f"prediction follows the mod's jump: {len(mod_misses)} misses, {sum(mod_misses) / 8.0:.1f} units in total")

    # HUD
    stock_label, mod_label = label_pixels(stock_shot), label_pixels(mod_shot)
    report(mod_label > 40 and stock_label < 5,
           f"HUD: \"TEST MOD\" label on screen ({mod_label} label pixels with the mod, {stock_label} stock)")

    errors = [line for line in (stock_log + mod_log).splitlines() if "ERROR" in line or "bad effect type" in line]
    report(not errors, "no errors" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nMod check {'passed' if passed else 'FAILED'}. Logs and screenshots in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
