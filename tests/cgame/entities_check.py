#!/usr/bin/env python3
"""Entity drawing check for OpenTech2: does the game library draw entities
exactly as the engine did?

Since effects extension 7 the game library draws the server's entities
(AddEntityModels: animation frames, powerup shells, translucency, rotating
and bobbing items, linked models, flares). cl_cgame_entities 0 hands them
back to the engine's old code. Each scene is shot twice on the fixed
60 Hz clock with monsters frozen, once each way, and the two shots must be
the same pixel for pixel. A third shot with entities switched off
(r_drawentities 0) makes sure the scene has entities in view at all.

Scenes:
  base1      classic data: rotating and bobbing items, a brain, a soldier
  base1-rr   the same on the re-release data (its models and effects)
  mgu2m2     re-release data, Call of the Machine: flares and their fade

optech2: since extension 8 the view weapon is the game library's too: the
scenes above match with it in view, cl_gun 0 hides it both ways, and with
quad damage the game library adds the re-release's blue shell to the gun
(the engine never drew one), so there the two must differ.

Usage: entities_check.py   (exit 1 on failure)
Logs and shots land in build/entities-check/.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "entities-check"
STEAM_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
RERELEASE_DIR = STEAM_DIR / "rerelease"

BASE1_SCENE = [
    "set g_monster_freeze 1", "map base1", 'delay 2000 "god"', 'delay 2100 "notarget"',
    'delay 2200 "teleport 128 -320 32 0 135 0"',
    'delay 2400 "cmd spawnentity item_quad 40 -230 40"',
    'delay 2450 "cmd spawnentity item_invulnerability 80 -200 40"',
    'delay 2500 "cmd spawnentity weapon_railgun 10 -260 40"',
    'delay 2550 "cmd spawnentity monster_brain -40 -120 40 0 315 0"',
    'delay 2600 "cmd spawnentity monster_soldier 90 -140 40 0 270 0"',
]
MGU2M2_SCENE = [
    "set g_monster_freeze 1", "map mgu2m2", 'delay 3000 "god"', 'delay 3100 "notarget"',
    'delay 3200 "teleport 24 1723 -2 23 270 0"',
]
# optech2: quad damage on: the view weapon gets its shell
QUAD_SCENE = BASE1_SCENE + ['delay 2700 "give quad damage"', 'delay 2750 "use quad damage"']
SCENES = [
    ("base1", STEAM_DIR, BASE1_SCENE, 4000),
    ("base1-rr", RERELEASE_DIR, BASE1_SCENE, 4000),
    ("mgu2m2", RERELEASE_DIR, MGU2M2_SCENE, 5000),
]

results = []


def report(passed, text):
    results.append(passed)
    print(f"{'ok' if passed else 'FAIL':<6}{text}")


def shoot(name, data_dir, scene, shot_time, settings):
    """Runs the scene with extra console settings; returns the shot as an array, or None."""
    tag = "-".join(f"{key}{value}" for key, value in settings)
    with tempfile.TemporaryDirectory(prefix="optech2-entities-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        game_dir.mkdir(parents=True)
        lines = [f"set {key} {value}" for key, value in settings] + scene
        lines += [f'delay {shot_time} "screenshot png"', f'delay {shot_time + 400} "quit"']
        (game_dir / "e.cfg").write_text("\n".join(lines) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        log_path = OUTPUT_DIR / f"{name}-{tag}.log"
        with open(log_path, "w") as log_file:
            subprocess.run([str(CLIENT_BINARY), "-datadir", str(data_dir), "+set", "fixedtime", "16667",
                            "+set", "vid_renderer", "soft", "+set", "r_mode", "4", "+set", "r_vsync", "0",
                            "+set", "cl_maxfps", "60", "+set", "s_initsound", "0", "+exec", "e.cfg"],
                           env=environment, stdin=subprocess.DEVNULL, stdout=log_file,
                           stderr=subprocess.STDOUT, timeout=120, check=False)
        shots = sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []
        if not shots:
            return None
        shutil.copy(shots[0], OUTPUT_DIR / f"{name}-{tag}.png")
        return np.asarray(Image.open(shots[0]).convert("RGB"), dtype=int)


def differing(first, second):
    return int((np.abs(first - second).sum(axis=2) > 0).sum())


def main():
    if not CLIENT_BINARY.exists():
        sys.exit(f"{CLIENT_BINARY} missing; build first")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    for name, data_dir, scene, shot_time in SCENES:
        if not (data_dir / "baseq2").exists():
            print(f"skip  {name}: no data in {data_dir}")
            continue
        library = shoot(name, data_dir, scene, shot_time, [("cl_cgame_entities", 1)])
        engine = shoot(name, data_dir, scene, shot_time, [("cl_cgame_entities", 0)])
        bare = shoot(name, data_dir, scene, shot_time, [("r_drawentities", 0)])
        if library is None or engine is None or bare is None:
            report(False, f"{name}: a screenshot is missing (see the logs)")
            continue
        report(differing(library, bare) > 500,
               f"{name}: entities in view ({differing(library, bare)} pixels are theirs)")
        report(differing(library, engine) == 0,
               f"{name}: game library draws them as the engine did ({differing(library, engine)} pixels differ)")

    # optech2: the view weapon (effects extension 8)
    if (STEAM_DIR / "baseq2").exists():
        library = shoot("nogun", STEAM_DIR, BASE1_SCENE, 4000, [("cl_cgame_entities", 1), ("cl_gun", 0)])
        engine = shoot("nogun", STEAM_DIR, BASE1_SCENE, 4000, [("cl_cgame_entities", 0), ("cl_gun", 0)])
        with_gun = shoot("nogun", STEAM_DIR, BASE1_SCENE, 4000, [("cl_cgame_entities", 1)])
        quad_library = shoot("quad", STEAM_DIR, QUAD_SCENE, 4000, [("cl_cgame_entities", 1)])
        quad_engine = shoot("quad", STEAM_DIR, QUAD_SCENE, 4000, [("cl_cgame_entities", 0)])
        if any(shot is None for shot in (library, engine, with_gun, quad_library, quad_engine)):
            report(False, "view weapon: a screenshot is missing (see the logs)")
        else:
            report(differing(library, with_gun) > 500,
                   f"view weapon: cl_gun 0 hides it ({differing(library, with_gun)} pixels are the gun's)")
            report(differing(library, engine) == 0,
                   f"view weapon: cl_gun 0 the same both ways ({differing(library, engine)} pixels differ)")
            report(differing(quad_library, quad_engine) > 200,
                   f"view weapon: quad shell on the gun from the game library "
                   f"({differing(quad_library, quad_engine)} pixels are the shell's)")

    print(f"\n{'PASS' if all(results) else 'FAIL'}: {sum(results)}/{len(results)} entity drawing checks")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
