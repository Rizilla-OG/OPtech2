#!/usr/bin/env python3
"""Test mod check for OpenTech2 (M2: a mod adds a weapon with a new effect,
with no engine changes).

Builds mods/testmod (copy of the source tree + testmod.patch + the mod's own
files) into build/mod-check/, checks the engine sources were not touched by
the mod, then plays with "+set game testmod": gives the Test Gun, fires it at
a soldier and checks that
  - the mod's weapon fired (the soldier took damage),
  - its effect, temp entity 201, was drawn by the mod's client code,
  - a screenshot shows the beam (saved to docs/screenshots/).

Usage:
  mod_check.py
Needs cmake and a C/C++ compiler. A real GL3 window for a few seconds.
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
    touched = [f for f in patched + mod_files if f.startswith(ENGINE_PARTS)]
    report(not touched, f"no engine sources changed (mod touches: {', '.join(sorted(set(patched + mod_files)))})")

    with tempfile.TemporaryDirectory(prefix="opentech2-mod-") as home:
        game_dir = Path(home) / ".opentech2" / "testmod"
        game_dir.mkdir(parents=True)
        shutil.copy(library, game_dir / "game.so")
        # base1 start: the first soldier stands ahead; aim at him and fire twice
        (game_dir / "m.cfg").write_text("\n".join([
            "map base1",
            'delay 3000 "god"',
            'delay 3100 "give test gun"',
            'delay 3200 "use test gun"',
            'delay 4500 "+attack"',
            'delay 4600 "-attack"',
            'delay 4680 "screenshot png"',
            'delay 5500 "+attack"',
            'delay 5600 "-attack"',
            'delay 6500 "quit"',
        ]) + "\n")
        environment = dict(os.environ, HOME=home, SDL_AUDIODRIVER="dummy")
        log_path = OUTPUT_DIR / "client.log"
        with open(log_path, "w") as log_file:
            subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR),
                            "+set", "game", "testmod", "+set", "vid_renderer", "gl3", "+set", "vid_fullscreen", "0",
                            "+set", "r_mode", "4", "+set", "s_initsound", "0", "+set", "cheats", "1",
                            "+set", "developer", "1", "+exec", "m.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=60, check=False)
        shots = sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []
        if shots:
            shutil.copy(shots[0], OUTPUT_DIR / "testgun.png")
            shutil.copy(shots[0], DOC_SHOT)

    log = log_path.read_text(errors="replace")
    report(re.search(r"Using '[^']*testmod' for writing", log) is not None, "game folder testmod in use")
    report("effects from the game library" in log, "the mod's game library draws the effects")
    beams = re.findall(r"testmod: beam drawn from (\S+ \S+ \S+) to (\S+ \S+ \S+)", log)
    report(len(beams) >= 2, f"Test Gun fired, its beam (temp entity 201) drawn by the mod {len(beams)} times")
    report(bool(shots), "screenshot of the beam" + (f": {DOC_SHOT.relative_to(REPO_DIR)}" if shots else ""))
    errors = [line for line in log.splitlines() if "ERROR" in line or "bad effect type" in line]
    report(not errors, "no errors" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nMod check {'passed' if passed else 'FAILED'}. Log and screenshot in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
