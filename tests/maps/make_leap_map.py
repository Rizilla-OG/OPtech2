#!/usr/bin/env python3
"""Writes the long jump test map (optech2_leap.map) and compiles it with ericw-tools.

A closed room, 1024 x 1024: a south floor and a north floor (both at z 0)
with a pit between them, 112 wide (y -56..56) and 320 deep, too deep to
drop into and climb out of. The only way across is to leap the gap;
tests/nav/leap_check.py also writes the re-release navigation file for the
map with that one long jump. The player starts on the north floor.

Usage:
  make_leap_map.py              write + compile into build/maps/
  make_leap_map.py --map-only   only write the .map
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_limits_map as limits_map  # noqa: E402  (shared .map writing and compile helpers)

MAP_NAME = "optech2_leap"
ROOM_HALF_SIZE = 512
ROOM_HEIGHT = 256
PIT_DEPTH = 320
PIT_HALF_WIDTH = 56          # the pit covers y -56..56
WALL_THICKNESS = 32


def build_map_text():
    size, height, wall, depth, half = ROOM_HALF_SIZE, ROOM_HEIGHT, WALL_THICKNESS, PIT_DEPTH, PIT_HALF_WIDTH
    box = limits_map.box_brush
    world_brushes = [
        # the pit's bottom, and the two floors as solid blocks down to it
        box((-size - wall, -size - wall, -depth - wall), (size + wall, size + wall, -depth), limits_map.FLOOR_TEXTURE),
        box((-size, -size, -depth), (size, -half, 0), limits_map.FLOOR_TEXTURE),
        box((-size, half, -depth), (size, size, 0), limits_map.FLOOR_TEXTURE),
        box((-size - wall, -size - wall, height), (size + wall, size + wall, height + wall), limits_map.CEILING_TEXTURE),
        box((-size - wall, -size - wall, -depth), (size + wall, -size, height), limits_map.WALL_TEXTURE),
        box((-size - wall, size, -depth), (size + wall, size + wall, height), limits_map.WALL_TEXTURE),
        box((-size - wall, -size, -depth), (-size, size, height), limits_map.WALL_TEXTURE),
        box((size, -size, -depth), (size + wall, size, height), limits_map.WALL_TEXTURE),
    ]
    entity = limits_map.entity
    entities = [
        entity({"classname": "worldspawn", "message": "OpenTech2 long jump test"}, world_brushes),
        entity({"classname": "info_player_start", "origin": "0 320 25", "angle": "270"}),
    ]
    for x in (-320, 320):
        for y in (-320, 320):
            entities.append(entity({"classname": "light", "origin": f"{x} {y} 200", "light": "600"}))
    entities.append(entity({"classname": "light", "origin": f"0 0 {-depth + 64}", "light": "300"}))
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
