#!/usr/bin/env python3
"""Writes the lift test map (optech2_lift.map) and compiles it with ericw-tools.

A closed 1024 x 1024 room with one 128 x 128 lift (a func_train between two
path_corners) going up and down 384 units at 200 units/s without stopping,
waiting 1 s at each end. The player starts standing on it.
tests/pmove/lift_check.py rides it and counts prediction misses.

Usage:
  make_lift_map.py              write + compile into build/maps/
  make_lift_map.py --map-only   only write the .map
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_limits_map as limits_map  # noqa: E402  (shared .map writing and compile helpers)

MAP_NAME = "optech2_lift"
ROOM_HALF_SIZE = 512
ROOM_HEIGHT = 640
WALL_THICKNESS = 32
LIFT_HALF_SIZE = 64
LIFT_THICKNESS = 16
LIFT_TRAVEL = 384
LIFT_SPEED = 200


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
    ]
    entity = limits_map.entity
    half, thick = LIFT_HALF_SIZE, LIFT_THICKNESS
    entities = [
        entity({"classname": "worldspawn", "message": "OPtech2 lift test"}, world_brushes),
        # no targetname: a func_train starts moving by itself; its brush's mins go to each path_corner
        entity({"classname": "func_train", "target": "lift_bottom", "speed": str(LIFT_SPEED)},
               [box((-half, -half, 0), (half, half, thick), limits_map.PILLAR_TEXTURE)]),
        entity({"classname": "path_corner", "targetname": "lift_bottom", "target": "lift_top", "wait": "1",
                "origin": f"{-half} {-half} 0"}),
        entity({"classname": "path_corner", "targetname": "lift_top", "target": "lift_bottom", "wait": "1",
                "origin": f"{-half} {-half} {LIFT_TRAVEL}"}),
        entity({"classname": "info_player_start", "origin": f"0 0 {thick + 25}", "angle": "90"}),
    ]
    for x in (-320, 320):
        for y in (-320, 320):
            entities.append(entity({"classname": "light", "origin": f"{x} {y} {height - 96}", "light": "600"}))
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
