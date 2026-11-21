#!/usr/bin/env python3
"""Writes the monster lift test map (optech2_plat.map) and compiles it with ericw-tools.

A closed 1024 x 1024 room, 512 high, with a balcony along its north wall
(y 128..512) 320 high: too high to drop from (more than 256), no stairs.
A 128 x 128 func_plat stands against the balcony's south edge (x -64..64,
y 0..128), placed at the top as maps do; it rests 312 lower (its height
minus the default lip of 8), so its top is 8 above the floor.
The player starts on the floor, in the south half.
tests/nav/plat_check.py asks for paths over it and has monsters ride it.

Usage:
  make_plat_map.py              write + compile into build/maps/
  make_plat_map.py --map-only   only write the .map
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_limits_map as limits_map  # noqa: E402  (shared .map writing and compile helpers)

MAP_NAME = "optech2_plat"
ROOM_HALF_SIZE = 512
ROOM_HEIGHT = 512
WALL_THICKNESS = 32
BALCONY = ((-512, 128), (512, 512), 320)   # (x, y) corners, height
PLAT = ((-64, 0), (64, 128))                # beside the balcony, its top level with it


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
    (x0, y0), (x1, y1), top = BALCONY
    world_brushes.append(box((x0, y0, 0), (x1, y1, top), limits_map.PILLAR_TEXTURE))
    (px0, py0), (px1, py1) = PLAT
    entity = limits_map.entity
    entities = [
        entity({"classname": "worldspawn", "message": "OpenTech2 monster lift test"}, world_brushes),
        entity({"classname": "func_plat"}, [box((px0, py0, 0), (px1, py1, top), limits_map.PILLAR_TEXTURE)]),
        entity({"classname": "info_player_start", "origin": "0 -320 25", "angle": "90"}),
    ]
    for x in (-320, 320):
        for y in (-320, 320):
            entities.append(entity({"classname": "light", "origin": f"{x} {y} {440 if y > 0 else 200}", "light": "600"}))
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
