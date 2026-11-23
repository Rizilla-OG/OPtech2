#!/usr/bin/env python3
"""Writes the cover test map (optech2_cover.map) and compiles it with ericw-tools.

A closed 1024 x 1024 room, 256 high. Wall to wall across its middle runs
a waist-high wall (44 high, more than monsters jump: 40), part of the
world, so a monster behind it can't get round or over. A machine gun
soldier stands right
behind it on the north side, facing the player, who starts on the south
side. Their eyes see each other over the wall, but the soldier's gun is
below its top: tests/monsters/clear_shot_check.py checks that it doesn't
shoot into the wall (re-release clear-shot check).

Usage:
  make_cover_map.py              write + compile into build/maps/
  make_cover_map.py --map-only   only write the .map
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_limits_map as limits_map  # noqa: E402  (shared .map writing and compile helpers)

MAP_NAME = "optech2_cover"
ROOM_HALF_SIZE = 512
ROOM_HEIGHT = 256
WALL_THICKNESS = 32
COVER = ((-512, -64, 0), (512, -48, 44))     # the waist-high wall: above a soldier's gun, below its eyes, too high to jump (40)
MONSTER_SPOT = "0 -20 24"                   # just behind the cover, facing south
PLAYER_SPOT = "0 -400 24"                   # south side, facing north


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
        box(COVER[0], COVER[1], limits_map.PILLAR_TEXTURE),
    ]
    entity = limits_map.entity
    entities = [
        entity({"classname": "worldspawn", "message": "OpenTech2 cover test"}, world_brushes),
        entity({"classname": "info_player_start", "origin": PLAYER_SPOT, "angle": "90"}),
        entity({"classname": "monster_soldier_ss", "origin": MONSTER_SPOT, "angle": "270"}),
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
