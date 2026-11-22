#!/usr/bin/env python3
"""Re-release HUD check for OpenTech2: with id's stock re-release game
library (built as in rr_game_check.py):
  - server prints and chat go to the client game's notify lines (the kill
    feed), drawn by the library in the top-left corner; with its
    cl_notifytime 0 they are gone, so the engine's own notify lines are not
    drawn under them,
  - say reaches everyone as chat (the library leaves chat to the engine and
    would answer "invalid game command"),
  - a deathmatch obituary shows in the feed, and the library's "##P0"
    player names read as the player's name (the game refuses kill for 5 s
    after spawning),
  - no errors.
Screenshots land in build/rr-hud-check/ and docs/screenshots/.

Usage:
  rr_hud_check.py [<quake2-rerelease-dll>/rerelease]
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

OUTPUT_DIR = rr_game_check.REPO_DIR / "build" / "rr-hud-check"
FEED_BOX = (0, 0, 480, 70)  # the notify lines' corner at 1280x720

SINGLE = """set cheats 1
set name Riz
set cl_notifytime {notifytime}
map base1
delay 3000 "god"
delay 3100 "notarget"
delay 3200 "say hello from the kill feed"
delay 3600 "screenshot png"
delay 3900 "quit"
"""

DEATHMATCH = """set name Riz
set cl_notifytime 5
set deathmatch 1
map q2dm1
delay 6500 "kill"
delay 7000 "screenshot png"
delay 7300 "quit"
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


def bright_pixels(shot):
    """near-white pixels in the feed's corner: the text"""
    corner = Image.open(shot).convert("RGB").crop(FEED_BOX)
    pixels = corner.get_flattened_data() if hasattr(corner, "get_flattened_data") else corner.getdata()
    return sum(1 for pixel in pixels if min(pixel) >= 195)  # the font is (255, 236, 200)


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

    with tempfile.TemporaryDirectory(prefix="optech2-rr-hud-") as home:
        mod_dir = Path(home) / ".opentech2" / rr_game_check.MOD_NAME
        mod_dir.mkdir(parents=True)
        shutil.copy(game_library, mod_dir / "game_x64.so")
        logs, shots = {}, {}
        for name, script in (("feed", SINGLE.format(notifytime=5)),
                             ("hidden", SINGLE.format(notifytime=0)),
                             ("deathmatch", DEATHMATCH)):
            logs[name], shot = run(mod_dir, home, script, OUTPUT_DIR / f"{name}.log")
            if shot:
                shots[name] = OUTPUT_DIR / f"{name}.png"
                shutil.copy(shot, shots[name])
                shutil.copy(shot, rr_game_check.DOC_SHOTS / f"m2_rr_hud_{name}.png")

    feed = logs["feed"]
    report("Riz: hello from the kill feed" in feed, "say reached everyone as chat")
    report("invalid game command" not in feed, "the library didn't refuse say")

    if "feed" in shots and "hidden" in shots:
        shown, hidden = bright_pixels(shots["feed"]), bright_pixels(shots["hidden"])
        report(shown > 500, f"feed lines drawn in the top-left corner ({shown} text pixels)")
        report(hidden < shown / 10, f"gone with the library's cl_notifytime 0 ({hidden} text pixels): "
                                    "the engine's own lines aren't drawn")
    else:
        report(False, f"screenshots taken: {sorted(shots)}")

    deathmatch = logs["deathmatch"]
    report("Riz entered the game" in deathmatch and "##P" not in deathmatch,
           "##P0 read as the player's name")
    report("Riz killed themselves." in deathmatch, "deathmatch obituary printed")
    if "deathmatch" in shots:
        report(bright_pixels(shots["deathmatch"]) > 100, "obituary drawn in the feed")
    else:
        report(False, "deathmatch screenshot taken")

    bad = [line for log in logs.values() for line in log.splitlines()
           if "(BAD" in line or "ERROR" in line or "Game Error" in line]
    report(not bad, "no errors" + (f" ({bad[0].strip()[:90]})" if bad else ""))

    passed = all(results)
    print(f"\nRe-release HUD check {'passed' if passed else 'FAILED'}. Logs and screenshots in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
