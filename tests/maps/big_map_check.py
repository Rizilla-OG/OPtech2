#!/usr/bin/env python3
"""Big re-release map check for OpenTech2: does the software renderer draw
Call of the Machine's big QBSP maps?

Their surfaces are wider than the software renderer's old 256 pixel surface
cache blocks ("D_SCAlloc: bad cache width", a fatal error). Loads mgu3m3 and
mgu3m2 on the re-release data with the hidden software renderer, takes a
screenshot in each and checks: no fatal error, the screenshot was written,
and it isn't speckled with unlit surface garbage (the light buffers ran out
while a big surface was cached).

Usage: big_map_check.py   (exit 1 on failure; skipped without the re-release data)
Logs and screenshots land in build/big-map-check/.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "big-map-check"
RERELEASE_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2/rerelease"
MAPS = ["mgu3m3", "mgu3m2"]


def run_map(map_name):
    """Loads the map, takes a screenshot; returns (log, screenshot path or None)."""
    with tempfile.TemporaryDirectory(prefix="optech2-bigmap-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        game_dir.mkdir(parents=True)
        (game_dir / "b.cfg").write_text(f'map {map_name}\ndelay 4000 "screenshot png"\ndelay 5000 "quit"\n')
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        try:
            result = subprocess.run([str(CLIENT_BINARY), "-datadir", str(RERELEASE_DIR), "+set", "vid_renderer", "soft",
                                     "+set", "r_vsync", "0", "+set", "cl_maxfps", "60", "+set", "s_initsound", "0",
                                     "+exec", "b.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=120, check=False)
            log = result.stdout + result.stderr
        except subprocess.TimeoutExpired:
            log = "ERROR: the game didn't quit\n"
        shots = sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []
        shot = None
        if shots:
            shot = OUTPUT_DIR / f"{map_name}.png"
            shutil.copy(shots[0], shot)
    (OUTPUT_DIR / f"{map_name}.log").write_text(log)
    return log, shot


def speckle_share(shot):
    """Share of pixels that differ strongly from both neighbours in the row
    (unlit cache garbage is per-pixel noise; real textures are smoother)."""
    try:
        from PIL import Image
    except ImportError:
        return None
    image = Image.open(shot).convert("L")
    width, height = image.size
    pixels = image.load()
    noisy = total = 0
    for y in range(0, height, 2):
        for x in range(1, width - 1):
            middle = pixels[x, y]
            if abs(middle - pixels[x - 1, y]) > 60 and abs(middle - pixels[x + 1, y]) > 60:
                noisy += 1
            total += 1
    return noisy / total


def main():
    if not (RERELEASE_DIR / "baseq2" / "pak0.pak").exists():
        print(f"skip: no re-release data in {RERELEASE_DIR}")
        return 0
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    for map_name in MAPS:
        log, shot = run_map(map_name)
        fatal = [line for line in log.splitlines() if "FATAL ERROR" in line or line.startswith("Error:")]
        report(not fatal, f"{map_name}: no fatal error" + (f" ({fatal[0].strip()})" if fatal else ""))
        report(shot is not None, f"{map_name}: screenshot written")
        if shot:
            share = speckle_share(shot)
            if share is None:
                print(f"skip  {map_name}: no PIL, garbage not checked")
            else:
                report(share < 0.02, f"{map_name}: no speckled surfaces ({share:.2%} noisy pixels)")

    print(f"\n{'Big map check passed' if all(results) else 'Big map check FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
