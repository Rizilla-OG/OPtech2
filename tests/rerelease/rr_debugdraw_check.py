#!/usr/bin/env python3
"""Re-release debug drawing check for OpenTech2: with id's stock re-release
game library (built as in rr_game_check.py), g_debug_monster_kills 1 makes
the game draw a red box (Draw_Bounds) around every monster that still
counts toward the kills; the engine keeps those lines and the client draws
them. base1's start looks at monsters through the window ahead:
  - with g_debug_monster_kills 1 the view has red debug lines,
  - with 0 it has (almost) none,
  - no errors.
Screenshots land in build/rr-debugdraw-check/ and docs/screenshots/.

Usage:
  rr_debugdraw_check.py [<quake2-rerelease-dll>/rerelease]
Hidden software renderer, temporary HOME.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_game_check  # noqa: E402  (shares the game build and paths)

OUTPUT_DIR = rr_game_check.REPO_DIR / "build" / "rr-debugdraw-check"

SCRIPT = """set cheats 1
set g_debug_monster_kills {kills}
map base1
delay 3000 "notarget"
delay 3500 "screenshot png"
delay 3800 "quit"
"""


def run(mod_dir, home, script, log_path):
    (mod_dir / "c.cfg").write_text(script)
    shots = mod_dir / "scrnshot"
    if shots.exists():
        shutil.rmtree(shots)
    environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with open(log_path, "w") as log_file:
        subprocess.run(["stdbuf", "-oL", str(rr_game_check.CLIENT_BINARY),
                        "-datadir", str(rr_game_check.RERELEASE_DATA_DIR),
                        "+set", "game", rr_game_check.MOD_NAME, "+set", "vid_renderer", "soft",
                        "+set", "s_initsound", "0", "+set", "r_mode", "-1",
                        "+set", "r_customwidth", "1280", "+set", "r_customheight", "720",
                        "+exec", "c.cfg"],
                       env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=90, check=False)
    found = sorted(shots.glob("*.png")) if shots.exists() else []
    return log_path.read_text(errors="replace"), found[0] if found else None


def red_pixels(shot):
    """strongly red pixels: the debug lines (the base's browns stay below)"""
    image = Image.open(shot).convert("RGB")
    pixels = image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata()
    return sum(1 for r, g, b in pixels if r >= 180 and g <= 60 and b <= 60)


def main():
    source_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else rr_game_check.DEFAULT_SOURCE
    if not (source_dir / "g_main.cpp").exists():
        sys.exit(f"no re-release game source in {source_dir}")
    game_library = rr_game_check.build_game(source_dir)

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    rr_game_check.DOC_SHOTS.mkdir(parents=True, exist_ok=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    logs, counts = {}, {}
    with tempfile.TemporaryDirectory(prefix="optech2-rr-debugdraw-") as home:
        mod_dir = Path(home) / ".opentech2" / rr_game_check.MOD_NAME
        mod_dir.mkdir(parents=True)
        shutil.copy(game_library, mod_dir / "game_x64.so")
        for name, kills in (("on", 1), ("off", 0)):
            logs[name], shot = run(mod_dir, home, SCRIPT.format(kills=kills), OUTPUT_DIR / f"{name}.log")
            if shot:
                shutil.copy(shot, OUTPUT_DIR / f"{name}.png")
                counts[name] = red_pixels(OUTPUT_DIR / f"{name}.png")
                if name == "on":
                    shutil.copy(shot, rr_game_check.DOC_SHOTS / "m2_rr_debugdraw_monster_kills.png")

    if "on" in counts and "off" in counts:
        report(counts["on"] > 200, f"red debug boxes drawn with g_debug_monster_kills 1 ({counts['on']} pixels)")
        report(counts["off"] < counts["on"] / 10, f"none with 0 ({counts['off']} pixels)")
    else:
        report(False, f"screenshots taken: {sorted(counts)}")

    bad = [line for log in logs.values() for line in log.splitlines()
           if "(BAD" in line or "ERROR" in line or "Game Error" in line]
    report(not bad, "no errors" + (f" ({bad[0].strip()[:90]})" if bad else ""))

    passed = all(results)
    print(f"\nRe-release debug drawing check {'passed' if passed else 'FAILED'}. Logs and screenshots in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
