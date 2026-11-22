#!/usr/bin/env python3
"""Writes the ladder and teleporter test map (optech2_ladder.map) and compiles it with ericw-tools.

A closed room, 1024 x 1024 and 320 high, split by a wall at x 0:
  - west: a balcony along the north wall (y 256..512) 192 high, reached
    only by a ladder on its front face at x -280..-232,
  - east: a vault in the north part (y 128..512) behind glass, with a
    misc_teleporter_dest inside; a misc_teleporter pad in the south part
    at 256 -256 sends you in, and one in the vault at 128 384 back out.
tests/nav/ladder_check.py also writes the re-release navigation file for
the map with the ladder and the teleporter. The player starts in the vault.

Usage:
  make_ladder_map.py              write + compile into build/maps/
  make_ladder_map.py --map-only   only write the .map
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_limits_map as limits_map  # noqa: E402  (shared .map writing and compile helpers)

MAP_NAME = "optech2_ladder"
ROOM_HALF_SIZE = 512
ROOM_HEIGHT = 320
WALL_THICKNESS = 32
BALCONY = ((-512, 256), (0, 512), 192)
LADDER = ((-280, 240), (-232, 256))     # on the balcony's front face, y 240..256
VAULT_SOUTH_Y = 128
GLASS_TEXTURE = "e1u1/window1"
LADDER_CONTENTS = 1 | 0x20000000        # solid and ladder, as maps give their ladders
LADDER_TEXTURE = "e1u1/metal2_1"  # (no ladder texture in the stock data; the contents make it one)


def ladder_brush(mins, maxs):
    return limits_map.box_brush(mins, maxs, LADDER_TEXTURE).replace(
        f"{LADDER_TEXTURE} 0 0 0 1 1 0 0 0", f"{LADDER_TEXTURE} 0 0 0 1 1 {LADDER_CONTENTS} 0 0")


def glass_brush(mins, maxs):
    """Window contents (2), 33% translucent: blocks movement, not sight."""
    return limits_map.box_brush(mins, maxs, GLASS_TEXTURE).replace(
        f"{GLASS_TEXTURE} 0 0 0 1 1 0 0 0", f"{GLASS_TEXTURE} 0 0 0 1 1 2 16 0")


def build_map_text():
    size, height, wall = ROOM_HALF_SIZE, ROOM_HEIGHT, WALL_THICKNESS
    box = limits_map.box_brush
    (bx0, by0), (bx1, by1), btop = BALCONY
    (lx0, ly0), (lx1, ly1) = LADDER
    world_brushes = [
        box((-size - wall, -size - wall, -wall), (size + wall, size + wall, 0), limits_map.FLOOR_TEXTURE),
        box((-size - wall, -size - wall, height), (size + wall, size + wall, height + wall), limits_map.CEILING_TEXTURE),
        box((-size - wall, -size - wall, 0), (size + wall, -size, height), limits_map.WALL_TEXTURE),
        box((-size - wall, size, 0), (size + wall, size + wall, height), limits_map.WALL_TEXTURE),
        box((-size - wall, -size, 0), (-size, size, height), limits_map.WALL_TEXTURE),
        box((size, -size, 0), (size + wall, size, height), limits_map.WALL_TEXTURE),
        box((0, -size, 0), (16, size, height), limits_map.WALL_TEXTURE),                      # splits west and east
        box((bx0, by0, 0), (bx1, by1, btop), limits_map.PILLAR_TEXTURE),                    # the balcony
        ladder_brush((lx0, ly0, 0), (lx1, ly1, btop)),                                      # its ladder
        glass_brush((16, VAULT_SOUTH_Y, 0), (size, VAULT_SOUTH_Y + 16, height)),          # the vault's wall (monsters see in)
    ]
    entity = limits_map.entity
    entities = [
        entity({"classname": "worldspawn", "message": "OpenTech2 ladder and teleporter test"}, world_brushes),
        entity({"classname": "info_player_start", "origin": "256 320 25", "angle": "270"}),
        entity({"classname": "misc_teleporter", "origin": "256 -256 24", "target": "vault"}),
        entity({"classname": "misc_teleporter_dest", "origin": "384 384 24", "angle": "270", "targetname": "vault"}),
        # and back out: a pad in the vault to a spot on the east floor
        entity({"classname": "misc_teleporter", "origin": "128 384 24", "target": "outside"}),
        entity({"classname": "misc_teleporter_dest", "origin": "384 -384 24", "angle": "90", "targetname": "outside"}),
    ]
    for x in (-320, 320):
        for y in (-320, 320):
            entities.append(entity({"classname": "light", "origin": f"{x} {y} 260", "light": "700"}))
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
