#!/usr/bin/env python3
"""True-color texture check for OpenTech2: a room textured only with PNG and TGA
images (no .wal files at all), at sizes the stock game never uses:
  north wall  optech2/npot_300x200  PNG, 300 x 200 (not a power of two):
              red / green / blue / yellow quarters
  south wall  optech2/big_4096      PNG, 4096 x 4096: red / blue checker
  floor       optech2/tga_1024      TGA, 1024 x 1024: orange / white stripes
For each renderer setting it looks at each surface and checks that the
screenshot shows that texture's colors (a missing texture shows the
checkerboard placeholder or the wrong colors).

Runs: GL3 with r_retexturing 1 (default), GL3 with r_retexturing 0 (true
color textures must still load), software renderer (it turns every texture
into Quake II's 256 color palette, so the test colors are ones it has).

Usage:
  texture_check.py               all runs (opens a GL3 window twice, ~1 min)
  texture_check.py --only soft   one run: gl3, gl3-noretex or soft

Needs ericw-tools and Pillow. Uses a temporary HOME. Shots land in
build/texture-check/ and docs/screenshots/.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CHECK_DIR = Path(__file__).resolve().parent
REPO_DIR = CHECK_DIR.parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(CHECK_DIR))
import make_limits_map as map_tools  # noqa: E402

from PIL import Image, ImageDraw  # noqa: E402

CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "texture-check"
DOC_SHOT_DIR = REPO_DIR / "docs" / "screenshots"
MAP_NAME = "optech2_textures"
GAME_TIMEOUT_SECONDS = 60

NPOT = "optech2/npot_300x200"
BIG = "optech2/big_4096"
STRIPES = "optech2/tga_1024"
COLORS = {
    NPOT: [(220, 30, 30), (30, 200, 30), (30, 60, 220), (230, 220, 30)],
    BIG: [(200, 30, 30), (30, 50, 200)],  # colors the software renderer's 8-bit palette has
    STRIPES: [(240, 140, 20), (240, 240, 240)],
}
# (surface, camera "x y z pitch yaw roll")
VIEWS = [(NPOT, "0 0 160 0 90 0"), (BIG, "0 0 160 0 270 0"), (STRIPES, "0 0 300 80 90 0")]
RUNS = {
    "gl3": ("gl3", "1"),
    "gl3-noretex": ("gl3", "0"),
    "soft": ("soft", "1"),
}
MIN_MATCHING_SHARE = 0.25  # of the screen's middle, close to one of the texture's colors


def write_textures(game_dir):
    textures = game_dir / "textures" / "optech2"
    textures.mkdir(parents=True, exist_ok=True)

    npot = Image.new("RGB", (300, 200))
    draw = ImageDraw.Draw(npot)
    for index, color in enumerate(COLORS[NPOT]):
        x, y = (index % 2) * 150, (index // 2) * 100
        draw.rectangle((x, y, x + 149, y + 99), fill=color)
    draw.rectangle((0, 0, 299, 199), outline=(0, 0, 0), width=4)
    npot.save(textures / "npot_300x200.png")

    big = Image.new("RGB", (4096, 4096))
    draw = ImageDraw.Draw(big)
    for row in range(8):
        for column in range(8):
            color = COLORS[BIG][(row + column) % 2]
            draw.rectangle((column * 512, row * 512, column * 512 + 511, row * 512 + 511), fill=color)
    big.save(textures / "big_4096.png")

    stripes = Image.new("RGB", (1024, 1024))
    draw = ImageDraw.Draw(stripes)
    for index in range(8):
        draw.rectangle((index * 128, 0, index * 128 + 127, 1023), fill=COLORS[STRIPES][index % 2])
    stripes.save(textures / "tga_1024.tga")


def build_map(work_dir, data_dir, tools_dir, texture_root):
    box = map_tools.box_brush
    size, height, wall = 512, 384, 32
    world = [
        box((-size - wall, -size - wall, -wall), (size + wall, size + wall, 0), STRIPES),
        box((-size - wall, -size - wall, height), (size + wall, size + wall, height + wall), STRIPES),
        box((-size - wall, size, 0), (size + wall, size + wall, height), NPOT),
        box((-size - wall, -size - wall, 0), (size + wall, -size, height), BIG),
        box((-size - wall, -size, 0), (-size, size, height), NPOT),
        box((size, -size, 0), (size + wall, size, height), BIG),
    ]
    entities = [map_tools.entity({"classname": "worldspawn", "message": "OpenTech2 texture test"}, world),
                map_tools.entity({"classname": "info_player_start", "origin": "0 0 24", "angle": "90"})]
    for x in (-256, 256):
        for y in (-256, 256):
            entities.append(map_tools.entity({"classname": "light", "origin": f"{x} {y} 300", "light": "900"}))
    map_path = work_dir / f"{MAP_NAME}.map"
    bsp_path = work_dir / f"{MAP_NAME}.bsp"
    map_path.write_text("\n".join(entities) + "\n")
    game_paths = ["-basedir", str(data_dir / "baseq2"), "-gamedir", str(data_dir / "baseq2"),
                  "-path", str(texture_root)]
    for arguments in (["qbsp", "-qbism", "-subdivide", "240", *game_paths, str(map_path), str(bsp_path)],
                      ["vis", *game_paths, str(bsp_path)], ["light", *game_paths, str(bsp_path)]):
        result = subprocess.run([str(tools_dir / arguments[0]), *arguments[1:]], capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"{arguments[0]} failed:\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return bsp_path


def matching_share(image, colors):
    """Share of pixels in the middle of the screen whose hue matches one of the colors.

    Lighting darkens and brightens, so pixels are compared after scaling both to the
    same brightness."""
    width, height = image.size
    middle = image.crop((width // 4, height // 4, width * 3 // 4, height * 3 // 4)).convert("RGB")
    pixels = middle.tobytes()

    def normalized(color):
        total = sum(color) or 1
        return tuple(channel / total for channel in color)

    targets = [normalized(color) for color in colors]
    matches = 0
    for index in range(0, len(pixels), 3):
        pixel = pixels[index:index + 3]
        if sum(pixel) < 30:
            continue
        shade = normalized(pixel)
        if any(sum(abs(shade[channel] - target[channel]) for channel in range(3)) < 0.18 for target in targets):
            matches += 1
    return matches / (len(pixels) // 3)


def run(name, renderer, retexturing, data_dir, bsp_path):
    with tempfile.TemporaryDirectory(prefix="optech2-textures-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        (game_dir / "maps").mkdir(parents=True)
        write_textures(game_dir)
        shutil.copy(bsp_path, game_dir / "maps")
        script = [f"map {MAP_NAME}", 'delay 5000 "notarget"']
        time_ms = 5300
        for _surface, camera in VIEWS:
            script += [f'delay {time_ms} "teleport {camera}"', f'delay {time_ms + 500} "screenshot png"']
            time_ms += 1000
        script.append(f'delay {time_ms} "quit"')
        (game_dir / "tex.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_AUDIODRIVER="dummy")
        if renderer == "soft":
            environment["SDL_VIDEODRIVER"] = "dummy"
        settings = ["+set", "vid_renderer", renderer, "+set", "vid_fullscreen", "0", "+set", "r_mode", "4",
                    "+set", "r_vsync", "0", "+set", "cl_maxfps", "60", "+set", "s_initsound", "0",
                    "+set", "r_retexturing", retexturing, "+set", "cl_gun", "0", "+set", "crosshair", "0",
                    "+set", "con_notifytime", "0"]
        log_path = OUTPUT_DIR / f"{name}.log"
        with open(log_path, "w") as log_file:
            try:
                subprocess.run([str(CLIENT_BINARY), "-datadir", str(data_dir), *settings, "+exec", "tex.cfg"],
                               env=environment, stdout=log_file, stderr=subprocess.STDOUT,
                               timeout=GAME_TIMEOUT_SECONDS, check=False)
            except subprocess.TimeoutExpired:
                pass
        shot_dir = game_dir / "scrnshot"
        shots = sorted(shot_dir.glob("*.png")) if shot_dir.exists() else []
        saved = []
        for (surface, _camera), shot in zip(VIEWS, shots):
            target = OUTPUT_DIR / f"{name}_{surface.split('/')[1]}.png"
            shutil.copy(shot, target)
            saved.append((surface, target))
        return saved


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=map_tools.DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--tools", type=Path, default=map_tools.DEFAULT_TOOLS_DIR, help="ericw-tools folder")
    parser.add_argument("--only", choices=list(RUNS), help="run only this one")
    arguments = parser.parse_args()

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix="optech2-texmap-") as work:
        work_dir = Path(work)
        write_textures(work_dir)  # ericw reads texture sizes for lighting from here
        bsp_path = build_map(OUTPUT_DIR, arguments.datadir, arguments.tools, work_dir)

    results = []
    for name, (renderer, retexturing) in RUNS.items():
        if arguments.only and name != arguments.only:
            continue
        shots = run(name, renderer, retexturing, arguments.datadir, bsp_path)
        if len(shots) != len(VIEWS):
            results.append(False)
            print(f"FAIL  {name}: {len(shots)} of {len(VIEWS)} screenshots (see {OUTPUT_DIR}/{name}.log)")
            continue
        for surface, path in shots:
            share = matching_share(Image.open(path), COLORS[surface])
            passed = share >= MIN_MATCHING_SHARE
            results.append(passed)
            print(f"{'ok' if passed else 'FAIL':<6}{name:<12} {surface:<22} {share:6.1%} of the view in its colors")
            DOC_SHOT_DIR.mkdir(parents=True, exist_ok=True)
            shutil.copy(path, DOC_SHOT_DIR / f"m1_truecolor_{name}_{surface.split('/')[1]}.png")

    passed = all(results) and bool(results)
    print(f"\nTexture check {'passed' if passed else 'FAILED'}. Shots in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
