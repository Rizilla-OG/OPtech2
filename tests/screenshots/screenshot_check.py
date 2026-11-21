#!/usr/bin/env python3
"""Screenshot regression check for OpenTech2.

Loads each map in viewpoints.txt, teleports to fixed spots, takes screenshots
and compares them with the saved references in reference/.

Usage:
  screenshot_check.py            compare against references (exit 1 on mismatch)
  screenshot_check.py --update   replace references with fresh screenshots

The game runs with HOME pointed at a temporary folder, so the user's own
config and savegames are never touched. A game window opens while it runs.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops

CHECK_DIR = Path(__file__).resolve().parent
REPO_DIR = CHECK_DIR.parent.parent
GAME_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
REFERENCE_DIR = CHECK_DIR / "reference"
OUTPUT_DIR = REPO_DIR / "build" / "screenshot-check"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"

SCREEN_WIDTH, SCREEN_HEIGHT = 800, 600
FRAMES_AFTER_MAP_LOAD = 900   # wait for the map to load and the client to connect
FRAMES_AFTER_TELEPORT = 60    # let the view settle after each teleport
PIXEL_TOLERANCE = 24          # per-channel difference ignored (flickering lights, noise)
MAX_CHANGED_FRACTION = 0.005  # fail if more than 0.5% of pixels differ beyond tolerance
GAME_TIMEOUT_SECONDS = 180
# Video settings go on the command line so they apply before the window opens.
VIDEO_SETTINGS = [
    "+set", "vid_fullscreen", "0", "+set", "r_mode", "-1",
    "+set", "r_customwidth", str(SCREEN_WIDTH), "+set", "r_customheight", str(SCREEN_HEIGHT),
    "+set", "fixedtime", "16667", "+set", "vid_renderer", "gl3", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
]


def read_viewpoints():
    """Return {map_name: [(name, "x y z pitch yaw roll"), ...]} in file order."""
    viewpoints_by_map = {}
    for line in (CHECK_DIR / "viewpoints.txt").read_text().splitlines():
        fields = line.split("#")[0].split()
        if not fields:
            continue
        map_name, coordinates = fields[0], fields[1:]
        spots = viewpoints_by_map.setdefault(map_name, [])
        shot_name = f"{map_name}_{len(spots) + 1}"
        spots.append((shot_name, " ".join(coordinates)))
    return viewpoints_by_map


def build_script(map_name, spots):
    """Console commands that load the map and take one screenshot per spot."""
    wait_frames = lambda count: "wait; " * count
    lines = [
        "set cl_gun 0", "set crosshair 0", "set con_notifytime 0", "set scr_centertime 0", "set r_drawentities 0",  # world only: no monsters/items moving
        "set deathmatch 0", "set coop 0", "set skill 1",
        f"map {map_name}", wait_frames(FRAMES_AFTER_MAP_LOAD),
        "god", "notarget",  # nothing hurts the player, so no damage flashes
    ]
    for _shot_name, coordinates in spots:
        lines += [f"teleport {coordinates}", wait_frames(FRAMES_AFTER_TELEPORT), "screenshot png", wait_frames(5)]
    lines.append("quit")
    return "\n".join(lines) + "\n"


def capture_map(map_name, spots, data_dir, temp_home):
    """Run the game for one map; return the screenshots it wrote, in order."""
    game_dir = temp_home / ".opentech2" / "baseq2"
    game_dir.mkdir(parents=True, exist_ok=True)
    (game_dir / "screenshot_check.cfg").write_text(build_script(map_name, spots))
    shot_dir = game_dir / "scrnshot"
    if shot_dir.exists():
        shutil.rmtree(shot_dir)

    environment = dict(os.environ, HOME=str(temp_home))
    log_path = OUTPUT_DIR / f"{map_name}.log"
    with open(log_path, "w") as log_file:
        subprocess.run(
            [str(GAME_BINARY), "-datadir", str(data_dir), *VIDEO_SETTINGS, "+exec", "screenshot_check.cfg"],
            env=environment, stdout=log_file, stderr=subprocess.STDOUT,
            timeout=GAME_TIMEOUT_SECONDS, check=False)

    shots = sorted(shot_dir.glob("*.png")) if shot_dir.exists() else []
    if len(shots) != len(spots):
        sys.exit(f"{map_name}: expected {len(spots)} screenshots, got {len(shots)}. See {log_path}")
    return shots


def compare(fresh_path, reference_path, diff_path):
    """Return the fraction of pixels that differ beyond the tolerance."""
    fresh = Image.open(fresh_path).convert("RGB")
    reference = Image.open(reference_path).convert("RGB")
    if fresh.size != reference.size:
        return 1.0
    difference = ImageChops.difference(fresh, reference)
    # A pixel counts as changed when any channel differs by more than the tolerance.
    changed_mask = difference.point(lambda value: 255 if value > PIXEL_TOLERANCE else 0).convert("L")
    changed_mask = changed_mask.point(lambda value: 255 if value > 0 else 0)
    changed_pixels = changed_mask.histogram()[255]
    if changed_pixels:
        changed_mask.save(diff_path)
    return changed_pixels / (fresh.width * fresh.height)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", action="store_true", help="save fresh screenshots as the new references")
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    arguments = parser.parse_args()

    if not GAME_BINARY.exists():
        sys.exit(f"Missing {GAME_BINARY}. Build first: cmake --workflow --preset default")
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    REFERENCE_DIR.mkdir(exist_ok=True)

    failures = []
    with tempfile.TemporaryDirectory(prefix="optech2-shots-") as temp_folder:
        temp_home = Path(temp_folder)
        for map_name, spots in read_viewpoints().items():
            shots = capture_map(map_name, spots, arguments.datadir, temp_home)
            for (shot_name, _coordinates), shot_path in zip(spots, shots):
                fresh_path = OUTPUT_DIR / f"{shot_name}.png"
                shutil.copy(shot_path, fresh_path)
                reference_path = REFERENCE_DIR / f"{shot_name}.png"
                if arguments.update:
                    shutil.copy(shot_path, reference_path)
                    print(f"saved   {shot_name}")
                    continue
                if not reference_path.exists():
                    failures.append(shot_name)
                    print(f"MISSING {shot_name}: no reference (run with --update)")
                    continue
                changed_fraction = compare(fresh_path, reference_path, OUTPUT_DIR / f"{shot_name}_diff.png")
                passed = changed_fraction <= MAX_CHANGED_FRACTION
                print(f"{'ok     ' if passed else 'FAIL   '} {shot_name}: {changed_fraction:.3%} pixels changed")
                if not passed:
                    failures.append(shot_name)

    if failures:
        print(f"\n{len(failures)} failed. Fresh shots and diffs in {OUTPUT_DIR}")
        return 1
    print("\nAll screenshots match." if not arguments.update else "\nReferences updated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
