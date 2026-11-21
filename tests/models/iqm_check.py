#!/usr/bin/env python3
"""IQM model check for OpenTech2: builds the test model (make_iqm_test.py) and a
small room with four misc_model copies of it, then loads the room in the game
and takes a screenshot:
  left to right: "bend" frame 1 (straight), frame 5 (45 degrees),
  frame 10 (90 degrees, bent towards the camera), "idle" (straight).
Checks that the model loads with no errors, that the client has it, and that
the four copies differ where they should (bent vs straight) in the screenshot.

Usage:
  iqm_check.py                    hidden software renderer
  iqm_check.py --renderer gl3     real GL3 window; also saves the shot to docs/screenshots/

Needs ericw-tools (python3 tools/get_ericw_tools.py). Uses a temporary HOME.
Logs and the screenshot land in build/iqm-check/.
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
sys.path.insert(0, str(CHECK_DIR))
sys.path.insert(0, str(REPO_DIR / "tests" / "maps"))
sys.dont_write_bytecode = True
import make_iqm_test  # noqa: E402
import make_limits_map as map_tools  # noqa: E402

CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "iqm-check"
DOC_SHOT_DIR = REPO_DIR / "docs" / "screenshots"
MAP_NAME = "optech2_iqmtest"
GAME_TIMEOUT_SECONDS = 40

# (x position, frame): bend frames 0..9, then idle (frame 10)
MODEL_COPIES = [(-150, 0), (-50, 4), (50, 9), (150, 10)]
CAMERA = "0 -260 48 0 90 0"  # looking at the four copies along +y


def build_map(work_dir, data_dir, tools_dir):
    box = map_tools.box_brush
    size, height, wall = 320, 256, 32
    world = [
        box((-size - wall, -size - wall, -wall), (size + wall, size + wall, 0), map_tools.FLOOR_TEXTURE),
        box((-size - wall, -size - wall, height), (size + wall, size + wall, height + wall), map_tools.CEILING_TEXTURE),
        box((-size - wall, -size - wall, 0), (size + wall, -size, height), map_tools.WALL_TEXTURE),
        box((-size - wall, size, 0), (size + wall, size + wall, height), map_tools.WALL_TEXTURE),
        box((-size - wall, -size, 0), (-size, size, height), map_tools.WALL_TEXTURE),
        box((size, -size, 0), (size + wall, size, height), map_tools.WALL_TEXTURE),
    ]
    entities = [map_tools.entity({"classname": "worldspawn", "message": "OpenTech2 IQM test"}, world)]
    entities.append(map_tools.entity({"classname": "info_player_start", "origin": "0 -260 24", "angle": "90"}))
    for x, frame in MODEL_COPIES:
        entities.append(map_tools.entity({"classname": "misc_model", "model": make_iqm_test.MODEL_PATH,
                                          "origin": f"{x} 0 0", "frame": str(frame)}))
    for x in (-200, 0, 200):
        entities.append(map_tools.entity({"classname": "light", "origin": f"{x} -150 200", "light": "500"}))

    map_path = work_dir / f"{MAP_NAME}.map"
    bsp_path = work_dir / f"{MAP_NAME}.bsp"
    map_path.write_text("\n".join(entities) + "\n")
    game_paths = ["-basedir", str(data_dir / "baseq2"), "-gamedir", str(data_dir / "baseq2")]
    for arguments in (["qbsp", "-qbism", "-subdivide", "240", *game_paths, str(map_path), str(bsp_path)],
                      ["vis", *game_paths, str(bsp_path)], ["light", *game_paths, str(bsp_path)]):
        result = subprocess.run([str(tools_dir / arguments[0]), *arguments[1:]], capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"{arguments[0]} failed:\n{result.stdout}{result.stderr}")
    return bsp_path


def column_difference(image_a, image_b):
    """Share of pixels that differ clearly between two same-sized crops."""
    pixels_a, pixels_b = image_a.tobytes(), image_b.tobytes()
    changed = sum(1 for index in range(0, len(pixels_a), 3)
                  if abs(pixels_a[index] - pixels_b[index]) + abs(pixels_a[index + 1] - pixels_b[index + 1])
                  + abs(pixels_a[index + 2] - pixels_b[index + 2]) > 60)
    return changed / (len(pixels_a) // 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=map_tools.DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--tools", type=Path, default=map_tools.DEFAULT_TOOLS_DIR, help="ericw-tools folder")
    parser.add_argument("--renderer", default="soft", choices=["soft", "gl3", "gl4", "gl1"])
    arguments = parser.parse_args()

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    with tempfile.TemporaryDirectory(prefix="optech2-iqm-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        (game_dir / "maps").mkdir(parents=True)
        make_iqm_test.write_iqm(game_dir / make_iqm_test.MODEL_PATH)
        make_iqm_test.write_skin(game_dir / make_iqm_test.SKIN_PATH)
        shutil.copy(build_map(OUTPUT_DIR, arguments.datadir, arguments.tools), game_dir / "maps")

        script = [f"map {MAP_NAME}", 'delay 5000 "notarget"', f'delay 5200 "teleport {CAMERA}"',
                  'delay 5600 "modellist"', 'delay 6000 "screenshot png"', 'delay 7000 "quit"']
        (game_dir / "iqm.cfg").write_text("\n".join(script) + "\n")
        environment = dict(os.environ, HOME=home, SDL_AUDIODRIVER="dummy")
        if arguments.renderer == "soft":
            environment["SDL_VIDEODRIVER"] = "dummy"
        settings = ["+set", "fixedtime", "16667", "+set", "vid_renderer", arguments.renderer, "+set", "vid_fullscreen", "0", "+set", "r_mode", "4",
                    "+set", "r_vsync", "0", "+set", "cl_maxfps", "60", "+set", "s_initsound", "0",
                    "+set", "cl_gun", "0", "+set", "crosshair", "0", "+set", "developer", "1"]
        log_path = OUTPUT_DIR / "client.log"
        with open(log_path, "w") as log_file:
            subprocess.run([str(CLIENT_BINARY), "-datadir", str(arguments.datadir), *settings, "+exec", "iqm.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT,
                           timeout=GAME_TIMEOUT_SECONDS, check=False)
        shots = sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []
        shot_path = OUTPUT_DIR / f"iqm_{arguments.renderer}.png"
        if shots:
            shutil.copy(shots[0], shot_path)

    log = log_path.read_text(errors="replace")
    errors = [line for line in log.splitlines() if "Mod_LoadModel_IQM" in line or "iqmtest" in line and "rror" in line]
    report(not errors, "model loads with no IQM errors" + (f" ({errors[0].strip()})" if errors else ""))
    report("iqmtest.iqm" in log, "client has models/optech2/iqmtest.iqm (modellist)")
    report(shot_path.exists(), "screenshot taken")
    if shot_path.exists():
        from PIL import Image
        image = Image.open(shot_path).convert("RGB")
        width, height = image.size
        quarter = width // 4
        # the upper part of each copy: straight copies look alike, bent ones don't
        crops = [image.crop((quarter * index, height // 6, quarter * (index + 1), height // 2)) for index in range(4)]
        straight_vs_idle = column_difference(crops[0], crops[3].transpose(Image.Transpose.FLIP_LEFT_RIGHT))
        straight_vs_bent = column_difference(crops[0], crops[2])
        report(straight_vs_bent > straight_vs_idle + 0.02,
               f"bent copy differs from straight ({straight_vs_bent:.1%} vs {straight_vs_idle:.1%} between the two straight ones)")
        if arguments.renderer != "soft":
            DOC_SHOT_DIR.mkdir(parents=True, exist_ok=True)
            shutil.copy(shot_path, DOC_SHOT_DIR / "m1_iqm_test_model.png")

    passed = all(results)
    print(f"\nIQM check {'passed' if passed else 'FAILED'}. Log and screenshot in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
