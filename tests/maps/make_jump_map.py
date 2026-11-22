#!/usr/bin/env python3
"""Writes the barrier jump test map (optech2_jump.map) and compiles it with ericw-tools.

A closed 1024 x 1024 room, 384 high. The north half is a ledge 36 high (too
high to step up, low enough for a Berserker's jump of 40). Along the ledge's
front edge stands a glass wall 112 high (window contents: monsters see
through it, nothing walks through it), with one 64-wide gap at x -416..-352.
The only way up is to jump through that gap; tests/nav/jump_check.py also
writes the re-release navigation file for the map with that one jump.

Usage:
  make_jump_map.py              write + compile into build/maps/
  make_jump_map.py --map-only   only write the .map
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_limits_map as limits_map  # noqa: E402  (shared .map writing and compile helpers)

MAP_NAME = "optech2_jump"
ROOM_HALF_SIZE = 512
ROOM_HEIGHT = 384
WALL_THICKNESS = 32
LEDGE_FRONT_Y = 0          # the ledge covers y 0..512
LEDGE_HEIGHT = 36
GLASS_TOP = LEDGE_HEIGHT + 112
GLASS_DEPTH = 16           # the glass stands on the ledge, y 0..16
GAP = (-416, -352)         # x range left open in the glass
GLASS_TEXTURE = "e1u1/window1_3"


def window_brush(mins, maxs):
    """A box brush with window contents (2) and 33% translucency (surface flag 16)."""
    return limits_map.box_brush(mins, maxs, GLASS_TEXTURE).replace(f"{GLASS_TEXTURE} 0 0 0 1 1 0 0 0",
                                                                     f"{GLASS_TEXTURE} 0 0 0 1 1 2 16 0")


def build_map_text():
    size, height, wall = ROOM_HALF_SIZE, ROOM_HEIGHT, WALL_THICKNESS
    box = limits_map.box_brush
    world_brushes = [
        box((-size - wall, -size - wall, -wall), (size + wall, size + wall, 0), limits_map.FLOOR_TEXTURE),
        box((-size - wall, -size - wall, height), (size + wall, size + wall, height + wall), limits_map.CEILING_TEXTURE),
        box((-size - wall, -size - wall, 0), (size + wall, -size, height), limits_map.WALL_TEXTURE),
        box((-size - wall, size, 0), (size + wall, size + wall, height), limits_map.WALL_TEXTURE),
        box((-size - wall, -size, 0), (-size, size, height), limits_map.WALL_TEXTURE),
        box((size, -size, 0), (size + wall, size, height), limits_map.WALL_TEXTURE),
        box((-size, LEDGE_FRONT_Y, 0), (size, size, LEDGE_HEIGHT), limits_map.PILLAR_TEXTURE),
        window_brush((-size, LEDGE_FRONT_Y, LEDGE_HEIGHT), (GAP[0], LEDGE_FRONT_Y + GLASS_DEPTH, GLASS_TOP)),
        window_brush((GAP[1], LEDGE_FRONT_Y, LEDGE_HEIGHT), (size, LEDGE_FRONT_Y + GLASS_DEPTH, GLASS_TOP)),
    ]
    entity = limits_map.entity
    entities = [
        entity({"classname": "worldspawn", "message": "OpenTech2 barrier jump test"}, world_brushes),
        entity({"classname": "info_player_start", "origin": f"256 256 {LEDGE_HEIGHT + 25}", "angle": "270"}),
    ]
    for x in (-320, 320):
        for y in (-320, 320):
            entities.append(entity({"classname": "light", "origin": f"{x} {y} 200", "light": "600"}))
    return "\n".join(entities) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=limits_map.DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--tools", type=Path, default=limits_map.DEFAULT_TOOLS_DIR, help="ericw-tools folder")
    parser.add_argument("--map-only", action="store_true", help="write the .map, don't compile")
    arguments = parser.parse_args()

    limits_map.OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    limits_map.MAP_NAME = MAP_NAME  # tool logs are named after the map
    map_path = limits_map.OUTPUT_DIR / f"{MAP_NAME}.map"
    map_path.write_text(build_map_text())
    print(f"wrote {map_path}")
    if arguments.map_only:
        return 0
    if not any((arguments.tools / name).exists() for name in ("qbsp", "qbsp.exe")):
        sys.exit(f"ericw-tools not found in {arguments.tools}: run python3 tools/get_ericw_tools.py")

    bsp_path = limits_map.OUTPUT_DIR / f"{MAP_NAME}.bsp"
    game_paths = ["-basedir", str(arguments.datadir / "baseq2"), "-gamedir", str(arguments.datadir / "baseq2")]
    limits_map.run_tool(arguments.tools, ["qbsp", "-qbism", "-subdivide", "240", *game_paths, str(map_path), str(bsp_path)])
    limits_map.run_tool(arguments.tools, ["vis", *game_paths, str(bsp_path)])
    limits_map.run_tool(arguments.tools, ["light", *game_paths, str(bsp_path)])
    print(f"built {bsp_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
