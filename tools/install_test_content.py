#!/usr/bin/env python3
"""Builds the OPtech2 test maps and models and installs them into your game
folder, so you can play them yourself:

  optech2_iqm      IQM showcase: the two-bone test bar as still frames and
                   looping its "bend" animation; extra IQM models (--extra)
                   loop their first animation next to it
  optech2_limits   12288 unit hall, 320 brush models, 300 sounds, shadow room
  optech2_stress   ~3800 entities: items, rotating pillars, 300 soldiers

Only adds files: maps/optech2_*.bsp and models/optech2/... in the game
folder (your config, saves and screenshots are not touched). Run it again
after changes to rebuild and reinstall.

Usage:
  install_test_content.py
  install_test_content.py --extra path/to/model.iqm [--extra other.iqm]
      (textures next to the .iqm are copied along; models without a
       licence you can share stay in your game folder only)
  install_test_content.py --game-dir ~/.local/share/OPtech2/baseq2

Needs ericw-tools (python3 tools/get_ericw_tools.py) and the stock data.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(REPO_DIR / "tests" / "maps"))
sys.path.insert(0, str(REPO_DIR / "tests" / "models"))
import make_iqm_test  # noqa: E402
import make_limits_map as map_tools  # noqa: E402

BUILD_DIR = REPO_DIR / "build" / "test-content"
CLIENT_BINARY = REPO_DIR / "build" / "release" / "optech2"
SHOWCASE_MAP = "optech2_iqm"


def default_game_dir():
    """Where the game keeps your files: ~/.optech2 if it exists, else the XDG data folder."""
    old_style = Path.home() / ".optech2"
    if old_style.is_dir():
        return old_style / "baseq2"
    data_home = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    return data_home / "OPtech2" / "baseq2"


def iqm_first_animation(path):
    """(first frame, frame count, frames per second) of the first animation, or None."""
    data = path.read_bytes()
    header = struct.unpack_from("<16s27I", data)
    num_anims, ofs_anims = header[18], header[19]
    if header[0][:15] != b"INTERQUAKEMODEL" or not num_anims:
        return None
    _name, first, count, framerate, _flags = struct.unpack_from("<3IfI", data, ofs_anims)
    return first, count, framerate or 10.0


def iqm_height(path):
    """Height of the model's vertices (z range), to scale small models up."""
    data = path.read_bytes()
    header = struct.unpack_from("<16s27I", data)
    num_vertexarrays, num_vertexes, ofs_vertexarrays = header[8], header[9], header[10]
    for index in range(num_vertexarrays):
        kind, _flags, value_format, size, offset = struct.unpack_from("<5I", data, ofs_vertexarrays + index * 20)
        if kind == 0 and value_format == 7 and size == 3:
            heights = [struct.unpack_from("<f", data, offset + vertex * 12 + 8)[0] for vertex in range(num_vertexes)]
            return max(heights) - min(heights)
    return 64.0


def build_showcase_map(extra_models, data_dir, tools_dir):
    """IQM showcase room. extra_models: [(model path in game, first frame, count, seconds per frame, scale)]."""
    box = map_tools.box_brush
    size, height, wall = 448, 288, 32
    world = [
        box((-size - wall, -size - wall, -wall), (size + wall, size + wall, 0), map_tools.FLOOR_TEXTURE),
        box((-size - wall, -size - wall, height), (size + wall, size + wall, height + wall), map_tools.CEILING_TEXTURE),
        box((-size - wall, -size - wall, 0), (size + wall, -size, height), map_tools.WALL_TEXTURE),
        box((-size - wall, size, 0), (size + wall, size + wall, height), map_tools.WALL_TEXTURE),
        box((-size - wall, -size, 0), (-size, size, height), map_tools.WALL_TEXTURE),
        box((size, -size, 0), (size + wall, size, height), map_tools.WALL_TEXTURE),
    ]
    entity = map_tools.entity
    entities = [entity({"classname": "worldspawn", "message": "OPtech2 IQM showcase"}, world)]
    for classname in ("info_player_start", "info_player_deathmatch", "info_player_coop"):
        entities.append(entity({"classname": classname, "origin": "0 -360 24", "angle": "90"}))

    bar = make_iqm_test.MODEL_PATH
    # back row: still frames 1, 5, 10 of "bend" and "idle"
    for x, frame in ((-210, 0), (-70, 4), (70, 9), (210, 10)):
        entities.append(entity({"classname": "misc_model", "model": bar, "origin": f"{x} 160 0", "frame": str(frame)}))
    # front: "bend" looping at 10 frames per second, and slower at 4 per second
    entities.append(entity({"classname": "misc_model", "model": bar, "origin": "-60 -40 0",
                            "spawnflags": "1", "frame": "0", "count": "10", "wait": "0.1"}))
    entities.append(entity({"classname": "misc_model", "model": bar, "origin": "60 -40 0",
                            "spawnflags": "1", "frame": "0", "count": "10", "wait": "0.25"}))
    # extra models along the side walls, facing the middle
    for index, (model_path, first, count, seconds, scale) in enumerate(extra_models):
        side = -1 if index % 2 == 0 else 1
        y = -120 + (index // 2) * 160
        properties = {"classname": "misc_model", "model": model_path, "origin": f"{side * 320} {y} 0",
                      "angle": "0" if side < 0 else "180", "scale": f"{scale:.2f}", "frame": str(first)}
        if count > 1:
            properties.update({"spawnflags": "1", "count": str(count), "wait": f"{seconds:.4f}"})
        entities.append(entity(properties))
    for x in (-300, 0, 300):
        for y in (-250, 150):
            entities.append(entity({"classname": "light", "origin": f"{x} {y} 230", "light": "450"}))

    map_path = BUILD_DIR / f"{SHOWCASE_MAP}.map"
    bsp_path = BUILD_DIR / f"{SHOWCASE_MAP}.bsp"
    map_path.write_text("\n".join(entities) + "\n")
    game_paths = ["-basedir", str(data_dir / "baseq2"), "-gamedir", str(data_dir / "baseq2")]
    for arguments in (["qbsp", "-qbism", "-subdivide", "240", *game_paths, str(map_path), str(bsp_path)],
                      ["vis", *game_paths, str(bsp_path)], ["light", *game_paths, str(bsp_path)]):
        result = subprocess.run([str(tools_dir / arguments[0]), *arguments[1:]], capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"{arguments[0]} failed:\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return bsp_path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game-dir", type=Path, default=default_game_dir(), help="your game folder (holds config.cfg)")
    parser.add_argument("--datadir", type=Path, default=map_tools.DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--tools", type=Path, default=map_tools.DEFAULT_TOOLS_DIR, help="ericw-tools folder")
    parser.add_argument("--extra", type=Path, action="append", default=[], help="extra .iqm model to show (repeatable)")
    arguments = parser.parse_args()

    if not any((arguments.tools / name).exists() for name in ("qbsp", "qbsp.exe")):
        sys.exit("ericw-tools missing: run python3 tools/get_ericw_tools.py")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    game_dir = arguments.game_dir
    (game_dir / "maps").mkdir(parents=True, exist_ok=True)

    # test bar model + skin
    make_iqm_test.write_iqm(game_dir / make_iqm_test.MODEL_PATH)
    make_iqm_test.write_skin(game_dir / make_iqm_test.SKIN_PATH)
    print(f"installed {make_iqm_test.MODEL_PATH}")

    # extra models: copied with the images next to them into models/optech2/extra/<name>/
    extra_models = []
    for model_file in arguments.extra:
        target_dir = game_dir / "models" / "optech2" / "extra" / model_file.stem
        target_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(model_file, target_dir / model_file.name)
        for image in list(model_file.parent.glob("*.tga")) + list(model_file.parent.glob("*.png")):
            shutil.copy(image, target_dir / image.name)
            if not (game_dir / image.name).exists():  # materials often name the image without a folder
                shutil.copy(image, game_dir / image.name)
        animation = iqm_first_animation(model_file) or (0, 1, 10.0)
        scale = max(1.0, 64.0 / max(iqm_height(model_file), 1.0))
        model_path = f"models/optech2/extra/{model_file.stem}/{model_file.name}"
        extra_models.append((model_path, animation[0], animation[1], 1.0 / animation[2], scale))
        print(f"installed {model_path} (animation frames {animation[0]}-{animation[0] + animation[1] - 1}, "
              f"{animation[2]:.0f} fps, scale {scale:.1f})")

    shutil.copy(build_showcase_map(extra_models, arguments.datadir, arguments.tools), game_dir / "maps")
    print(f"installed maps/{SHOWCASE_MAP}.bsp")

    for script, bsp in (("make_limits_map.py", "optech2_limits.bsp"), ("make_stress_map.py", "optech2_stress.bsp")):
        subprocess.run([sys.executable, str(REPO_DIR / "tests" / "maps" / script), "--datadir", str(arguments.datadir),
                        "--tools", str(arguments.tools)], check=True, capture_output=True)
        shutil.copy(REPO_DIR / "build" / "maps" / bsp, game_dir / "maps")
        print(f"installed maps/{bsp}")

    print(f"\nGame folder: {game_dir}")
    print("Play (cheats are on in single player: god, notarget, noclip):")
    for map_name in (SHOWCASE_MAP, "optech2_limits", "optech2_stress"):
        print(f'  {CLIENT_BINARY} -datadir "{arguments.datadir}" +map {map_name}')
    return 0


if __name__ == "__main__":
    sys.exit(main())
