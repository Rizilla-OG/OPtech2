#!/usr/bin/env python3
"""M1 limits check for OpenTech2: loads the limits test map (built by
make_limits_map.py) in a hidden single-player client and checks:
  1. the map loads as extended QBSP with no errors,
  2. more than 256 models and more than 256 sounds reach the client and load,
  3. the player can be placed and walk beyond +-4096 units (float coordinates,
     movement and prediction),
  4. screenshots at both far ends and the middle are lit (not black).

Usage:
  limits_check.py              run the check (exit 1 on failure)
  limits_check.py --renderer gl3   use a real GL3 window instead of the hidden
                                   software renderer (also writes the doc shots)

Every run gets its own temporary HOME. Logs and screenshots land in
build/limits-check/.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CHECK_DIR = Path(__file__).resolve().parent
REPO_DIR = CHECK_DIR.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_limits.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "limits-check"
DOC_SHOT_DIR = REPO_DIR / "docs" / "screenshots"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DEFAULT_DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
GAME_TIMEOUT_SECONDS = 90

STOCK_LIMIT = 256
FAR_X = 5600          # well past the old +-4096 edge, 544 units short of the end wall
WALK_SECONDS = 0.6    # walking forward from FAR_X towards +x
MIN_WALKED = 100      # units the player must cover in that time
MIN_BRIGHTNESS = 10   # average 0-255 brightness of a lit screenshot

SHOTS = [("east_end", f"{FAR_X} 0 24 0 0 0"), ("west_end", f"-{FAR_X} 0 24 0 180 0"), ("middle", "0 -700 160 10 90 0"),
         ("shadow_room", "-110 980 230 40 32 0")]  # looking down at the post's shadow (lightmap resolution)


def client_settings(renderer):
    settings = ["+set", "vid_fullscreen", "0", "+set", "r_mode", "4",  # 800x600
                "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
                "+set", "cl_gun", "0", "+set", "crosshair", "0", "+set", "con_notifytime", "0"]
    return ["+set", "fixedtime", "16667", "+set", "vid_renderer", renderer, *settings]


def script_lines():
    def at(milliseconds, command):
        return f'delay {milliseconds} "{command}"'

    lines = ["map optech2_limits", at(6000, "god"), at(6050, "notarget"), at(6200, "cl_configcounts"),
             at(6500, f"teleport {FAR_X} 0 24 0 0 0"), at(7000, "cmd playerinfo"),
             at(7100, "+forward"), at(7100 + int(WALK_SECONDS * 1000), "-forward"), at(8200, "cmd playerinfo")]
    start = 9000
    for _name, spot in SHOTS:
        lines += [at(start, f"teleport {spot}"), at(start + 300, "cmd playerinfo"), at(start + 800, "screenshot png")]
        start += 1500
    lines.append(at(start, "quit"))
    return lines


def average_brightness(png_path):
    """Average brightness 0-255 of a PNG, read with Python only (zlib, 8-bit RGB/RGBA)."""
    import struct
    import zlib
    data = png_path.read_bytes()
    position, width, height, channels, compressed = 8, 0, 0, 3, b""
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        if kind == b"IHDR":
            width, height, _depth, color_type = struct.unpack(">IIBB", body[:10])
            channels = {2: 3, 6: 4}.get(color_type, 3)
        elif kind == b"IDAT":
            compressed += body
        position += 12 + length
    raw = zlib.decompress(compressed)
    stride = width * channels
    previous = bytearray(stride)
    total = 0
    for row in range(height):
        filter_type = raw[row * (stride + 1)]
        line = bytearray(raw[row * (stride + 1) + 1:(row + 1) * (stride + 1)])
        for index in range(stride):
            left = line[index - channels] if index >= channels else 0
            up = previous[index]
            up_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                line[index] = (line[index] + left) & 255
            elif filter_type == 2:
                line[index] = (line[index] + up) & 255
            elif filter_type == 3:
                line[index] = (line[index] + (left + up) // 2) & 255
            elif filter_type == 4:
                guess = left + up - up_left
                distances = (abs(guess - left), abs(guess - up), abs(guess - up_left))
                line[index] = (line[index] + (left, up, up_left)[distances.index(min(distances))]) & 255
        total += sum(value for index, value in enumerate(line) if index % channels < 3)
        previous = line
    return total / (width * height * 3)


def run_session(data_dir, renderer):
    log_path = OUTPUT_DIR / "client.log"
    with tempfile.TemporaryDirectory(prefix="optech2-limits-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        (game_dir / "maps").mkdir(parents=True)
        shutil.copy(MAP_FILE, game_dir / "maps" / MAP_FILE.name)
        (game_dir / "limits.cfg").write_text("\n".join(script_lines()) + "\n")
        environment = dict(os.environ, HOME=home, SDL_AUDIODRIVER="dummy")
        if renderer == "soft":
            environment["SDL_VIDEODRIVER"] = "dummy"
        with open(log_path, "w") as log_file:
            subprocess.run([str(CLIENT_BINARY), "-datadir", str(data_dir), *client_settings(renderer),
                            "+exec", "limits.cfg"], env=environment, stdout=log_file, stderr=subprocess.STDOUT,
                           timeout=GAME_TIMEOUT_SECONDS, check=False)
        shots = sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []
        saved = []
        for (name, _spot), shot in zip(SHOTS, shots):
            target = OUTPUT_DIR / f"{name}_{renderer}.png"
            shutil.copy(shot, target)
            saved.append((name, target))
    return log_path.read_text(errors="replace"), saved


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--renderer", default="soft", choices=["soft", "gl3", "gl4", "gl1"], help="renderer to test")
    arguments = parser.parse_args()

    if not CLIENT_BINARY.exists():
        sys.exit(f"Missing {CLIENT_BINARY}. Build first: cmake --workflow --preset default")
    if not MAP_FILE.exists():
        sys.exit(f"Missing {MAP_FILE}. Build it first: python3 tests/maps/make_limits_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    log, shots = run_session(arguments.datadir, arguments.renderer)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    loaded = re.search(r"Map maps/optech2_limits\.bsp (\w+) with version (\d+)", log)
    report(bool(loaded) and loaded.group(1) == "QBSP",
           f"map loads as extended QBSP ({loaded.group(1) + ' ' + loaded.group(2) if loaded else 'no load line'})")
    errors = [line for line in log.splitlines() if re.search(r"ERROR|overflow|Couldn't load|can't load", line, re.IGNORECASE)]
    report(not errors, "no load errors" + (f" ({errors[0].strip()})" if errors else ""))

    counts = re.search(r"configcounts models (\d+) loaded (\d+) highest (\d+) sounds (\d+) loaded (\d+) highest (\d+)", log)
    if counts:
        models, models_loaded, highest_model, sounds, sounds_loaded, highest_sound = map(int, counts.groups())
        report(models > STOCK_LIMIT and models_loaded == models,
               f"models: {models} received, {models_loaded} loaded, highest index {highest_model}")
        report(sounds > STOCK_LIMIT and sounds_loaded == sounds,
               f"sounds: {sounds} received, {sounds_loaded} loaded, highest index {highest_sound}")
    else:
        report(False, "client printed cl_configcounts")

    origins = [tuple(map(float, match)) for match in
               re.findall(r"playerinfo time [\d.]+ health -?\d+ ammo -?\d+ origin (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)", log)]
    if len(origins) >= 2 + len(SHOTS):
        placed, walked = origins[0], origins[1]
        report(abs(placed[0] - FAR_X) < 1, f"player placed at x {placed[0]:.1f} (beyond 4096)")
        report(walked[0] - placed[0] >= MIN_WALKED and abs(walked[1]) < 1,
               f"player walked {walked[0] - placed[0]:.1f} units in {WALK_SECONDS} s at x > 4096, ending at x {walked[0]:.1f}")
        west = origins[3]
        report(abs(west[0] + FAR_X) < 1, f"player placed at x {west[0]:.1f} (beyond -4096)")
    else:
        report(False, f"got {len(origins)} playerinfo lines, expected {2 + len(SHOTS)}")

    report(len(shots) == len(SHOTS), f"{len(shots)} of {len(SHOTS)} screenshots taken")
    for name, path in shots:
        brightness = average_brightness(path)
        report(brightness >= MIN_BRIGHTNESS, f"{name} screenshot lit (average brightness {brightness:.0f})")
        if arguments.renderer != "soft":
            DOC_SHOT_DIR.mkdir(parents=True, exist_ok=True)
            shutil.copy(path, DOC_SHOT_DIR / f"m1_limits_map_{name}.png")

    passed = all(results)
    print(f"\nLimits check {'passed' if passed else 'FAILED'}. Log and screenshots in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
