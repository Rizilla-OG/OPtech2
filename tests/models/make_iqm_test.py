#!/usr/bin/env python3
"""Writes a small IQM test model (models/optech2/iqmtest.iqm) and its skin.

The model is a square bar 64 units tall on a two-joint skeleton:
  - joint "root" at the bottom, joint "tip" halfway up (z 32),
  - vertices below the middle follow root, above it follow tip, the middle
    ring is shared half and half,
  - animation "bend" (10 frames): tip turns 0 -> 90 degrees about the x axis,
    so the top half bends over towards -y,
  - animation "idle" (1 frame): straight.
The skin is red on the top half and blue on the bottom half, with a white
stripe on one side, so texture orientation is easy to check on screen.

Usage:
  make_iqm_test.py [output folder]   default: build/models (holds models/optech2/)
"""

import math
import struct
import sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
DEFAULT_OUTPUT_DIR = REPO_DIR / "build" / "models"
MODEL_PATH = "models/optech2/iqmtest.iqm"
SKIN_PATH = "models/optech2/iqmtest.tga"

HALF_WIDTH = 4
RING_HEIGHTS = [0, 16, 32, 48, 64]
TIP_HEIGHT = 32
BEND_FRAMES = 10

# IQM vertex array types and formats
POSITION, TEXCOORD, NORMAL, BLENDINDEXES, BLENDWEIGHTS = 0, 1, 2, 4, 5
UBYTE, FLOAT = 1, 7

# pose channels are stored as offset + ushort * scale
TRANSLATE_OFFSET, TRANSLATE_SCALE = -128.0, 256.0 / 65535
UNIT_OFFSET, UNIT_SCALE = -2.0, 4.0 / 65535  # rotation and scale channels


def build_mesh():
    """Side faces of the bar: per ring 5 vertices (corner 0 repeated for the texture seam)."""
    corners = [(HALF_WIDTH, HALF_WIDTH), (-HALF_WIDTH, HALF_WIDTH), (-HALF_WIDTH, -HALF_WIDTH),
               (HALF_WIDTH, -HALF_WIDTH), (HALF_WIDTH, HALF_WIDTH)]
    positions, texcoords, normals, blend_indexes, blend_weights = [], [], [], [], []
    for z in RING_HEIGHTS:
        for corner_index, (x, y) in enumerate(corners):
            positions.append((x, y, z))
            # texture origin top left: top of the bar is v 0
            texcoords.append((corner_index / 4, 1 - z / RING_HEIGHTS[-1]))
            length = math.hypot(x, y)
            normals.append((x / length, y / length, 0))
            if z < TIP_HEIGHT:
                blend_indexes.append((0, 0, 0, 0))
                blend_weights.append((255, 0, 0, 0))
            elif z == TIP_HEIGHT:
                blend_indexes.append((0, 1, 0, 0))
                blend_weights.append((128, 127, 0, 0))
            else:
                blend_indexes.append((1, 0, 0, 0))
                blend_weights.append((255, 0, 0, 0))

    triangles = []
    ring_size = len(corners)
    for ring in range(len(RING_HEIGHTS) - 1):
        for side in range(4):
            bottom_left = ring * ring_size + side
            bottom_right = bottom_left + 1
            top_left = bottom_left + ring_size
            top_right = bottom_right + ring_size
            # counter-clockwise seen from outside
            triangles.append((bottom_left, bottom_right, top_right))
            triangles.append((bottom_left, top_right, top_left))
    return positions, texcoords, normals, blend_indexes, blend_weights, triangles


def pose_channels(translate, rotate):
    return list(translate) + list(rotate) + [1.0, 1.0, 1.0]


def encode(value, offset, scale):
    return max(0, min(65535, round((value - offset) / scale)))


def build_frames():
    """Per frame: [root channels, tip channels], each 10 floats."""
    frames = []
    for frame in range(BEND_FRAMES):
        angle = math.radians(90 * frame / (BEND_FRAMES - 1))
        # rotation about +x by angle: the top bends towards -y
        tip_rotation = (math.sin(angle / 2), 0.0, 0.0, math.cos(angle / 2))
        frames.append([pose_channels((0, 0, 0), (0, 0, 0, 1)), pose_channels((0, 0, TIP_HEIGHT), tip_rotation)])
    frames.append([pose_channels((0, 0, 0), (0, 0, 0, 1)), pose_channels((0, 0, TIP_HEIGHT), (0, 0, 0, 1))])  # idle
    return frames


def write_iqm(path):
    positions, texcoords, normals, blend_indexes, blend_weights, triangles = build_mesh()
    frames = build_frames()

    text = b"\0"
    def add_text(value):
        nonlocal text
        offset = len(text)
        text += value.encode() + b"\0"
        return offset

    mesh_name, material = add_text("bar"), add_text(SKIN_PATH)
    root_name, tip_name = add_text("root"), add_text("tip")
    bend_name, idle_name = add_text("bend"), add_text("idle")
    while len(text) % 4:
        text += b"\0"

    header_size = 16 + 27 * 4
    vertex_count, triangle_count = len(positions), len(triangles)
    sections = {}
    offset = header_size
    def place(name, size):
        nonlocal offset
        sections[name] = offset
        offset += size
        offset = (offset + 3) & ~3

    place("text", len(text))
    place("meshes", 6 * 4)
    place("vertexarrays", 5 * 5 * 4)
    place("positions", vertex_count * 12)
    place("texcoords", vertex_count * 8)
    place("normals", vertex_count * 12)
    place("blendindexes", vertex_count * 4)
    place("blendweights", vertex_count * 4)
    place("triangles", triangle_count * 12)
    place("joints", 2 * (4 + 4 + 10 * 4))
    place("poses", 2 * (4 + 4 + 20 * 4))
    place("anims", 2 * 5 * 4)
    channel_count = 20  # 2 poses x 10 channels, all animated
    place("frames", len(frames) * channel_count * 2)
    file_size = offset

    out = bytearray(file_size)
    struct.pack_into("<16s27I", out, 0, b"INTERQUAKEMODEL\0", 2, file_size, 0,
                     len(text), sections["text"],
                     1, sections["meshes"],
                     5, vertex_count, sections["vertexarrays"],
                     triangle_count, sections["triangles"], 0,
                     2, sections["joints"],
                     2, sections["poses"],
                     2, sections["anims"],
                     len(frames), channel_count, sections["frames"], 0,
                     0, 0, 0, 0)
    out[sections["text"]:sections["text"] + len(text)] = text
    struct.pack_into("<6I", out, sections["meshes"], mesh_name, material, 0, vertex_count, 0, triangle_count)

    arrays = [(POSITION, FLOAT, 3, "positions"), (TEXCOORD, FLOAT, 2, "texcoords"), (NORMAL, FLOAT, 3, "normals"),
              (BLENDINDEXES, UBYTE, 4, "blendindexes"), (BLENDWEIGHTS, UBYTE, 4, "blendweights")]
    for index, (kind, value_format, size, name) in enumerate(arrays):
        struct.pack_into("<5I", out, sections["vertexarrays"] + index * 20, kind, 0, value_format, size, sections[name])
    for index in range(vertex_count):
        struct.pack_into("<3f", out, sections["positions"] + index * 12, *positions[index])
        struct.pack_into("<2f", out, sections["texcoords"] + index * 8, *texcoords[index])
        struct.pack_into("<3f", out, sections["normals"] + index * 12, *normals[index])
        struct.pack_into("<4B", out, sections["blendindexes"] + index * 4, *blend_indexes[index])
        struct.pack_into("<4B", out, sections["blendweights"] + index * 4, *blend_weights[index])
    for index, triangle in enumerate(triangles):
        struct.pack_into("<3I", out, sections["triangles"] + index * 12, *triangle)

    # joints: name, parent, translate, rotate (xyzw), scale
    struct.pack_into("<Ii10f", out, sections["joints"], root_name, -1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1)
    struct.pack_into("<Ii10f", out, sections["joints"] + 48, tip_name, 0, 0, 0, TIP_HEIGHT, 0, 0, 0, 1, 1, 1, 1)

    offsets = [TRANSLATE_OFFSET] * 3 + [UNIT_OFFSET] * 7
    scales = [TRANSLATE_SCALE] * 3 + [UNIT_SCALE] * 7
    for index, parent in enumerate((-1, 0)):
        struct.pack_into("<iI20f", out, sections["poses"] + index * 88, parent, 0x3FF, *offsets, *scales)

    struct.pack_into("<3IfI", out, sections["anims"], bend_name, 0, BEND_FRAMES, 10.0, 0)
    struct.pack_into("<3IfI", out, sections["anims"] + 20, idle_name, BEND_FRAMES, 1, 10.0, 0)

    position = sections["frames"]
    for frame in frames:
        for channels in frame:
            for channel_index, value in enumerate(channels):
                struct.pack_into("<H", out, position, encode(value, offsets[channel_index], scales[channel_index]))
                position += 2

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(out))


def write_skin(path, size=64):
    """Uncompressed 24-bit TGA, origin top left: red top half, blue bottom half, white stripe on the left side."""
    pixels = bytearray()
    for row in range(size):
        for column in range(size):
            if column < size // 8:
                color = (255, 255, 255)
            elif row < size // 2:
                color = (40, 40, 220)  # BGR: red
            else:
                color = (220, 60, 40)  # BGR: blue
            pixels += bytes(color)
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, size, size, 24, 0x20)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bytes(pixels))


def main():
    output_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT_DIR
    write_iqm(output_dir / MODEL_PATH)
    write_skin(output_dir / SKIN_PATH)
    print(f"wrote {output_dir / MODEL_PATH} and {output_dir / SKIN_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
