#!/usr/bin/env python3
"""q2arpg check for OpenTech2: builds q2arpg (a re-release game mod) for
Linux from a snapshot of its source (a local clone, never its own repo) with
the stock game's build wrapper, and plays it on the re-release data:
  - the server loads it as a re-release library and q2arpg says its version,
    the client game comes from the same library,
  - its HUD draws (gun name, level, XP, cash in the top-left corner),
  - a spawned soldier dies to the starting pistol and the kill gives XP
    (the HUD's XP number goes up),
  - a save written in one run loads in a fresh one, at the saved spot,
  - its own files (q2rpg/loot_stats.csv, q2rpg/q2rpg_events.log) are written
    only under the folder it runs from (here a temporary one),
  - no errors.
Screenshots land in build/q2arpg-check/ and, with run_all.py --doc-shots,
docs/screenshots/.

Usage:
  q2arpg_check.py [<q2arpg snapshot>/rerelease]
Default: ../research/q2arpg-snapshot/rerelease next to the repo (make it with
  git clone --branch main ~/Projects/q2rpg/q2arpg research/q2arpg-snapshot).
Skips when there is no snapshot. Hidden software renderer, temporary HOME.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_game_check  # noqa: E402  (shares paths)

REPO_DIR = rr_game_check.REPO_DIR
DOC_SHOTS_WANTED = os.environ.get("OPTECH2_DOC_SHOTS") == "1"  # run_all.py --doc-shots: refresh docs/screenshots/
GAME_BUILD_DIR = REPO_DIR / "build" / "q2arpg-game"
OUTPUT_DIR = REPO_DIR / "build" / "q2arpg-check"
DEFAULT_SOURCE = REPO_DIR.parent / "research" / "q2arpg-snapshot" / "rerelease"
MOD_NAME = "q2rpg"
HUD_BOX = (0, 0, 520, 60)  # q2arpg's gun / level / XP lines at 1280x720

PLAY = """set cheats 1
map base1
delay 3000 "notarget"
delay 3100 "god"
delay 3200 "screenshot png"
delay 3250 "echo SOLDIERS before"
delay 3260 "rr_edicts monster_soldier"
delay 3300 "spawn monster_soldier"
delay 3400 "echo SOLDIERS spawned"
delay 3410 "rr_edicts monster_soldier"
delay 3500 "+attack"
delay 7500 "-attack"
delay 7600 "echo SOLDIERS after"
delay 7610 "rr_edicts monster_soldier"
delay 8000 "screenshot png"
delay 8100 "teleport 200 -300 46 0 90 0"
delay 8400 "save q2arpgcheck"
delay 8800 "quit"
"""

LOAD = """load q2arpgcheck
delay 4000 "echo STEP loaded"
delay 4100 "viewpos"
delay 4200 "screenshot png"
delay 4400 "quit"
"""


def build_game(source_dir):
    configure = subprocess.run(["cmake", "-S", str(REPO_DIR / "tools" / "rerelease" / "stock_game"),
                                "-B", str(GAME_BUILD_DIR), f"-DRR_SOURCE_DIR={source_dir}",
                                "-DCMAKE_BUILD_TYPE=Release"], capture_output=True, text=True)
    if configure.returncode != 0:
        sys.exit(f"game configure failed:\n{configure.stderr[-3000:]}")
    build = subprocess.run(["cmake", "--build", str(GAME_BUILD_DIR), "-j", str(os.cpu_count() or 4)],
                           capture_output=True, text=True)
    if build.returncode != 0:
        sys.exit(f"game build failed:\n{build.stdout[-3000:]}")
    return GAME_BUILD_DIR / "game_x64.so"


def run(home, run_dir, mod_dir, script, log_path):
    (mod_dir / "c.cfg").write_text(script)
    environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with open(log_path, "w") as log_file:
        subprocess.run(["stdbuf", "-oL", str(rr_game_check.CLIENT_BINARY),
                        "-datadir", str(rr_game_check.RERELEASE_DATA_DIR),
                        "+set", "game", MOD_NAME, "+set", "vid_renderer", "soft",
                        # fixed 60 Hz clock: the delays land on the same tick however busy the
                        # machine is (a busy full suite once saved the player mid-move)
                        "+set", "fixedtime", "16667", "+set", "r_vsync", "0", "+set", "cl_maxfps", "60",
                        "+set", "s_initsound", "0", "+set", "r_mode", "-1",
                        "+set", "r_customwidth", "1280", "+set", "r_customheight", "720",
                        "+exec", "c.cfg"],
                       cwd=run_dir, env=environment, stdin=subprocess.DEVNULL, stdout=log_file, stderr=subprocess.STDOUT,
                       timeout=120, check=False)
    return log_path.read_text(errors="replace")


def hud_pixels(shot):
    """near-white pixels where q2arpg draws its gun / level / XP lines"""
    corner = Image.open(shot).convert("RGB").crop(HUD_BOX)
    pixels = corner.get_flattened_data() if hasattr(corner, "get_flattened_data") else corner.getdata()
    return sum(1 for pixel in pixels if min(pixel) >= 195)


def main():
    source_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_SOURCE
    if not (source_dir / "q2rpg").is_dir():
        print(f"skip: no q2arpg snapshot in {source_dir}")
        return 0
    game_library = build_game(source_dir)

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    with tempfile.TemporaryDirectory(prefix="optech2-q2arpg-") as home:
        mod_dir = Path(home) / ".opentech2" / MOD_NAME
        run_dir = Path(home) / "run"
        mod_dir.mkdir(parents=True)
        run_dir.mkdir()
        (run_dir / MOD_NAME).mkdir()  # where q2arpg writes its own files
        shutil.copy(game_library, mod_dir / "game_x64.so")

        play = run(home, run_dir, mod_dir, PLAY, OUTPUT_DIR / "play.log")
        shots = sorted((mod_dir / "scrnshot").glob("*.png")) if (mod_dir / "scrnshot").exists() else []
        for shot, name in zip(shots, ["start", "kill"]):
            shutil.copy(shot, OUTPUT_DIR / f"{name}.png")
            if DOC_SHOTS_WANTED:
                shutil.copy(shot, rr_game_check.DOC_SHOTS / f"m2_q2arpg_{name}.png")
        if (mod_dir / "scrnshot").exists():
            shutil.rmtree(mod_dir / "scrnshot")

        load = run(home, run_dir, mod_dir, LOAD, OUTPUT_DIR / "load.log")
        written = sorted(str(p.relative_to(run_dir)) for p in run_dir.rglob("*") if p.is_file())

    report("re-release game library (game API 2023)" in play, "server loaded q2arpg as a re-release library")
    version = re.search(r"q2rpg game DLL loaded, v(\S+)", play)
    report(bool(version), f"q2arpg says its version ({version.group(1) if version else None})")
    report("client game: loaded (cgame API 2022)" in play, "client game from the same library")

    if (OUTPUT_DIR / "start.png").exists() and (OUTPUT_DIR / "kill.png").exists():
        report(hud_pixels(OUTPUT_DIR / "start.png") > 1000, "q2arpg's HUD lines drawn top left")
        start_xp = OUTPUT_DIR / "start.png"
        kill_xp = OUTPUT_DIR / "kill.png"
        report(Image.open(start_xp).crop(HUD_BOX).tobytes() != Image.open(kill_xp).crop(HUD_BOX).tobytes(),
               "the HUD changed after the kill (XP)")
    else:
        report(False, "screenshots taken")

    listings = {}
    for part in play.split("SOLDIERS ")[1:]:
        listings[part.split()[0]] = dict(re.findall(r"rr_edict (\d+) monster_soldier health (-?\d+)", part))
    spawned = [n for n in listings.get("spawned", {}) if n not in listings.get("before", {})]
    if spawned:
        health = listings.get("after", {}).get(spawned[0])
        report(health is None or int(health) <= 0,
               f"spawned soldier #{spawned[0]} killed with the starting pistol (health after: {health or 'gone'})")
    else:
        report(False, "spawned soldier listed")

    position = re.search(r"position: (-?\d+) (-?\d+) (-?\d+)", load.split("STEP loaded")[-1])
    report(bool(position) and abs(int(position.group(1)) - 200) <= 2 and abs(int(position.group(2)) + 300) <= 2,
           f"save loaded in a fresh run at the saved spot ({position.groups() if position else None})")

    outside = [path for path in written if not path.startswith(f"{MOD_NAME}/")]
    report(not outside, f"its own files only under the run folder's {MOD_NAME}/ ({', '.join(written) or 'none'})")

    bad = [line for log in (play, load) for line in log.splitlines()
           if "(BAD" in line or "ERROR" in line or "Game Error" in line]
    report(not bad, "no errors" + (f" ({bad[0].strip()[:90]})" if bad else ""))

    passed = all(results)
    print(f"\nq2arpg check {'passed' if passed else 'FAILED'}. Logs and screenshots in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
