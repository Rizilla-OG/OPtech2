#!/usr/bin/env python3
"""Writes the M1 entity stress map (optech2_stress.map) and compiles it with ericw-tools.

A 6144 x 6144 unit room holding about 3800 entities:
  - 3000 items on a grid (armor shards, small health, bullets): static, but
    many of them visible at once,
  - 500 func_rotating pillars: brush models pushed every server frame,
  - 300 light soldiers: monsters running their AI every frame,
so the server, the network and the client all deal with thousands of
entities. stress_check.py loads it and measures frame times.

Usage:
  make_stress_map.py              write + compile into build/maps/
  make_stress_map.py --map-only   only write the .map
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_limits_map as limits_map  # noqa: E402  (shared .map writing and compile helpers)

MAP_NAME = "optech2_stress"
ROOM_HALF_SIZE = 3072
ROOM_HEIGHT = 512
WALL_THICKNESS = 32

ITEM_GRID = 55  # 55 x 55 = 3025 slots, 3000 used
ITEM_CLASSES = ["item_armor_shard", "item_health_small", "ammo_bullets"]
ITEM_COUNT = 3000
ROTATOR_COUNT = 500
SOLDIER_COUNT = 300


def grid_spots(count, half_size, margin, height):
    """`count` spots on an even square grid inside +-half_size, `margin` from the walls."""
    side = 1
    while side * side < count:
        side += 1
    step = (2 * (half_size - margin)) / max(side - 1, 1)
    spots = []
    for row in range(side):
        for column in range(side):
            if len(spots) == count:
                return spots
            spots.append((round(-half_size + margin + column * step), round(-half_size + margin + row * step), height))
    return spots


CONTENTS_ORIGIN = 16777216  # Quake II brush contents flag: this brush only marks the rotation centre


def origin_brush(x, y, z):
    brush = limits_map.box_brush((x - 4, y - 4, z - 4), (x + 4, y + 4, z + 4), "e1u1/origin")
    return brush.replace(" 0 0 0 1 1 0 0 0", f" 0 0 0 1 1 {CONTENTS_ORIGIN} 0 0")


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
    entities = [entity({"classname": "worldspawn", "message": "OpenTech2 entity stress test", "sky": "unit1_"}, world_brushes)]
    # spawn closet: a sealed room behind the north wall, so 300 soldiers can't shoot the player
    # while the map loads; stress_check.py teleports into the hall after god + notarget
    closet_y = size + wall + 64
    entities[0] = entities[0][:-1] + "\n".join([
        box((-96, closet_y - 32, -32), (96, closet_y + 192, 0), limits_map.FLOOR_TEXTURE),
        box((-96, closet_y - 32, 160), (96, closet_y + 192, 192), limits_map.CEILING_TEXTURE),
        box((-96, closet_y - 32, 0), (96, closet_y, 160), limits_map.WALL_TEXTURE),
        box((-96, closet_y + 160, 0), (96, closet_y + 192, 160), limits_map.WALL_TEXTURE),
        box((-96, closet_y, 0), (-64, closet_y + 160, 160), limits_map.WALL_TEXTURE),
        box((64, closet_y, 0), (96, closet_y + 160, 160), limits_map.WALL_TEXTURE),
    ]) + "\n}"
    entities.append(entity({"classname": "light", "origin": f"0 {closet_y + 80} 120", "light": "300"}))
    for classname in ("info_player_start", "info_player_deathmatch", "info_player_coop"):
        entities.append(entity({"classname": classname, "origin": f"0 {closet_y + 80} 24", "angle": "90"}))

    for index, (x, y, z) in enumerate(grid_spots(ITEM_COUNT, size, 96, 16)):
        entities.append(entity({"classname": ITEM_CLASSES[index % len(ITEM_CLASSES)], "origin": f"{x} {y} {z}"}))

    # rotators on a coarser grid, offset half a cell so they sit between items
    for index, (x, y, _z) in enumerate(grid_spots(ROTATOR_COUNT, size, 160, 0)):
        x, y = x + 28, y + 28
        # the origin brush sets the rotation centre (without one they orbit the map's 0 0 0)
        entities.append(entity({"classname": "func_rotating", "speed": str(60 + (index % 5) * 30), "spawnflags": "1"},
                               [box((x - 8, y - 8, 0), (x + 8, y + 8, 128), limits_map.PILLAR_TEXTURE),
                                origin_brush(x, y, 64)]))

    for index, (x, y, z) in enumerate(grid_spots(SOLDIER_COUNT, size, 300, 24)):
        entities.append(entity({"classname": "monster_soldier_light", "origin": f"{x + 52} {y + 52} {z}",
                                "angle": str((index * 37) % 360)}))

    for x in range(-size + 256, size, 768):
        for y in range(-size + 256, size, 768):
            entities.append(entity({"classname": "light", "origin": f"{x} {y} {height - 64}", "light": "900"}))

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
