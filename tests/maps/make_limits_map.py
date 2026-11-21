#!/usr/bin/env python3
"""Writes the M1 limits test map (optech2_limits.map) and compiles it with ericw-tools.

The map goes past the stock Quake II limits that M1 removes:
  - a hall 12288 units long (x -6144..6144), so the far ends lie beyond +-4096,
  - 320 func_wall pillars = 320 inline brush models (stock cap: 256 models),
  - 300 target_speakers with different stock sounds (stock cap: 256 sounds),
compiled to the extended QBSP format (-qbism) with decoupled lightmaps in a
BSPX lump (light -world_units_per_luxel), the two formats the engine claims
to load.

Usage:
  make_limits_map.py                  write + compile into build/maps/
  make_limits_map.py --map-only       only write the .map

Needs ericw-tools (python3 tools/get_ericw_tools.py installs the tested
version into build/tools/ericw-tools; or --tools / $ERICW_TOOLS) and the
stock data (for textures and the sound list).
"""

import argparse
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
OUTPUT_DIR = REPO_DIR / "build" / "maps"
MAP_NAME = "optech2_limits"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
DEFAULT_TOOLS_DIR = Path(os.environ.get("ERICW_TOOLS", REPO_DIR / "build" / "tools" / "ericw-tools"))  # tools/get_ericw_tools.py

# hall interior
HALL_HALF_LENGTH = 6144
HALL_HALF_WIDTH = 768
HALL_HEIGHT = 512
WALL_THICKNESS = 32

PILLAR_COUNT = 320  # > 256 models
PILLAR_SIZE = 16
PILLAR_HEIGHT = 96
SPEAKER_COUNT = 300  # > 256 sounds

FLOOR_TEXTURE = "e1u1/floor3_1"
WALL_TEXTURE = "e1u1/metal2_1"
CEILING_TEXTURE = "e1u1/ceil1_8"
PILLAR_TEXTURE = "e1u1/box3_3"
MARKER_TEXTURE = "e1u1/color1_6"  # end walls, easy to spot in screenshots


def box_brush(mins, maxs, texture):
    """One axis-aligned box brush in Quake II .map format (planes as TrenchBroom writes them)."""
    x1, y1, z1 = mins
    x2, y2, z2 = maxs
    face = f"{texture} 0 0 0 1 1 0 0 0"
    return "\n".join([
        "{",
        f"( {x1} {y1} {z1} ) ( {x1} {y1 + 1} {z1} ) ( {x1} {y1} {z1 + 1} ) {face}",
        f"( {x1} {y1} {z1} ) ( {x1} {y1} {z1 + 1} ) ( {x1 + 1} {y1} {z1} ) {face}",
        f"( {x1} {y1} {z1} ) ( {x1 + 1} {y1} {z1} ) ( {x1} {y1 + 1} {z1} ) {face}",
        f"( {x2} {y2} {z2} ) ( {x2} {y2 + 1} {z2} ) ( {x2 + 1} {y2} {z2} ) {face}",
        f"( {x2} {y2} {z2} ) ( {x2 + 1} {y2} {z2} ) ( {x2} {y2} {z2 + 1} ) {face}",
        f"( {x2} {y2} {z2} ) ( {x2} {y2} {z2 + 1} ) ( {x2} {y2 + 1} {z2} ) {face}",
        "}",
    ])


def entity(properties, brushes=()):
    lines = ["{"] + [f'"{key}" "{value}"' for key, value in properties.items()] + list(brushes) + ["}"]
    return "\n".join(lines)


def stock_sounds(data_dir, count):
    """The first `count` .wav files in pak0.pak, as paths relative to sound/."""
    with open(data_dir / "baseq2" / "pak0.pak", "rb") as pak:
        _, directory_offset, directory_length = struct.unpack("<4sii", pak.read(12))
        pak.seek(directory_offset)
        names = [pak.read(64)[:56].split(b"\0")[0].decode() for _ in range(directory_length // 64)]
    sounds = sorted(name[len("sound/"):] for name in names if name.startswith("sound/") and name.endswith(".wav"))
    if len(sounds) < count:
        sys.exit(f"pak0.pak has only {len(sounds)} sounds, need {count}")
    return sounds[:count]


def build_map_text(data_dir):
    length, width, height, wall = HALL_HALF_LENGTH, HALL_HALF_WIDTH, HALL_HEIGHT, WALL_THICKNESS
    world_brushes = [
        box_brush((-length - wall, -width - wall, -wall), (length + wall, width + wall, 0), FLOOR_TEXTURE),
        box_brush((-length - wall, -width - wall, height), (length + wall, width + wall, height + wall), CEILING_TEXTURE),
        box_brush((-length - wall, -width - wall, 0), (length + wall, -width, height), WALL_TEXTURE),
        box_brush((-length - wall, width, 0), (length + wall, width + wall, height), WALL_TEXTURE),
        box_brush((-length - wall, -width, 0), (-length, width, height), MARKER_TEXTURE),
        box_brush((length, -width, 0), (length + wall, width, height), MARKER_TEXTURE),
    ]
    # shadow room: a sealed side room lit by one low light, with a thin post casting a hard
    # shadow on the floor. Shows the lightmap resolution (decoupled BSPX lightmap vs stock).
    room_y = width + wall + 64
    world_brushes += [
        box_brush((-288, room_y - 32, -32), (288, room_y + 544, 0), FLOOR_TEXTURE),
        box_brush((-288, room_y - 32, 256), (288, room_y + 544, 288), CEILING_TEXTURE),
        box_brush((-288, room_y - 32, 0), (288, room_y, 256), WALL_TEXTURE),
        box_brush((-288, room_y + 512, 0), (288, room_y + 544, 256), WALL_TEXTURE),
        box_brush((-288, room_y, 0), (-256, room_y + 512, 256), WALL_TEXTURE),
        box_brush((256, room_y, 0), (288, room_y + 512, 256), WALL_TEXTURE),
        box_brush((36, room_y + 252, 0), (44, room_y + 260, 160), PILLAR_TEXTURE),  # the post
    ]
    entities = [entity({"classname": "worldspawn", "message": "OpenTech2 limits test", "sky": "unit1_"}, world_brushes)]
    entities.append(entity({"classname": "light", "origin": f"0 {room_y + 256} 64", "light": "400"}))

    for classname in ("info_player_start", "info_player_deathmatch", "info_player_coop"):
        entities.append(entity({"classname": classname, "origin": "0 0 24", "angle": "0"}))

    # pillars: two rows along the hall, one func_wall (= one inline model) each
    per_row = PILLAR_COUNT // 2
    spacing = (2 * length - 512) // per_row
    for index in range(PILLAR_COUNT):
        row_y = -400 if index % 2 == 0 else 400
        x = -length + 256 + (index // 2) * spacing
        half = PILLAR_SIZE // 2
        entities.append(entity({"classname": "func_wall"},
                               [box_brush((x - half, row_y - half, 0), (x + half, row_y + half, PILLAR_HEIGHT), PILLAR_TEXTURE)]))

    # speakers: one stock sound each, not looping, so they only register their sound index
    speaker_spacing = (2 * length - 512) // SPEAKER_COUNT
    for index, sound in enumerate(stock_sounds(data_dir, SPEAKER_COUNT)):
        x = -length + 256 + index * speaker_spacing
        entities.append(entity({"classname": "target_speaker", "origin": f"{x} 0 64", "noise": sound,
                                "targetname": f"speaker{index}"}))

    # lights every 512 units along the hall
    for x in range(-length + 256, length, 512):
        for y in (-384, 384):
            entities.append(entity({"classname": "light", "origin": f"{x} {y} {height - 64}", "light": "700"}))

    return "\n".join(entities) + "\n"


def run_tool(tools_dir, arguments):
    started = time.monotonic()
    program = arguments[0] + (".exe" if os.name == "nt" else "")
    result = subprocess.run([str(tools_dir / program), *arguments[1:]], capture_output=True, text=True)
    log_path = OUTPUT_DIR / f"{MAP_NAME}_{arguments[0]}.log"
    log_path.write_text(result.stdout + result.stderr)
    if result.returncode != 0:
        sys.exit(f"{arguments[0]} failed (exit {result.returncode}), see {log_path}")
    print(f"{arguments[0]:<6} ok ({time.monotonic() - started:.1f} s)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--tools", type=Path, default=DEFAULT_TOOLS_DIR, help="ericw-tools folder (qbsp, vis, light)")
    parser.add_argument("--map-only", action="store_true", help="write the .map, don't compile")
    parser.add_argument("--luxel", type=int, default=16,
                        help="world units per luxel of the decoupled (BSPX) lightmap; 0 = none")
    arguments = parser.parse_args()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    map_path = OUTPUT_DIR / f"{MAP_NAME}.map"
    map_path.write_text(build_map_text(arguments.datadir))
    print(f"wrote {map_path}")
    if arguments.map_only:
        return 0

    if not any((arguments.tools / name).exists() for name in ("qbsp", "qbsp.exe")):
        sys.exit(f"ericw-tools not found in {arguments.tools}: run python3 tools/get_ericw_tools.py "
                 "(or pass --tools / set $ERICW_TOOLS)")
    bsp_path = OUTPUT_DIR / f"{MAP_NAME}.bsp"
    game_paths = ["-basedir", str(arguments.datadir / "baseq2"), "-gamedir", str(arguments.datadir / "baseq2")]
    run_tool(arguments.tools, ["qbsp", "-qbism", "-subdivide", "240", *game_paths, str(map_path), str(bsp_path)])
    run_tool(arguments.tools, ["vis", *game_paths, str(bsp_path)])
    decoupled = ["-world_units_per_luxel", str(arguments.luxel)] if arguments.luxel > 0 else []
    run_tool(arguments.tools, ["light", *decoupled, *game_paths, str(bsp_path)])
    print(f"built {bsp_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
