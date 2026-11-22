#!/usr/bin/env python3
"""Re-release prediction check for OpenTech2: with id's stock re-release
game library (built as in rr_game_check.py) on base1, the player walks,
turns, jumps, crouch-walks and strafes while cl_showmiss 1 prints every
time the client's predicted position differs from the server's. The client
predicts with the library's own movement code on the re-release movement
state (PS_RR_PMOVE), so it should almost never miss:
  - the client loads the re-release library (game_x64) itself,
  - the server sends the re-release movement state,
  - at most a few misses over the route, none bigger than 1 unit,
  - the player really moved (the route ran),
  - no errors.

Usage:
  rr_predict_check.py [<quake2-rerelease-dll>/rerelease]
Hidden software renderer, temporary HOME, real clock (prediction only has
work to do when commands run ahead of server frames).
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rr_game_check  # noqa: E402  (shares the game build and paths)

OUTPUT_DIR = rr_game_check.REPO_DIR / "build" / "rr-predict-check"
MAX_MISSES = 5            # a stray miss or two (level start, a door) is fine
MAX_MISS_UNITS = 8        # 1 unit, in the 1/8 units cl_showmiss prints

ROUTE = """set cheats 1
set cl_showmiss 1
map base1
delay 3000 "notarget"
delay 3100 "viewpos"
delay 3200 "echo ROUTE start"
delay 3300 "+forward"
delay 3800 "+left"
delay 4300 "-left"
delay 4400 "+moveup"
delay 4500 "-moveup"
delay 5200 "+movedown"
delay 6000 "-movedown"
delay 6100 "-forward"
delay 6200 "+moveleft"
delay 6900 "-moveleft"
delay 7000 "+back"
delay 7100 "+moveup"
delay 7200 "-moveup"
delay 7800 "-back"
delay 7900 "+right"
delay 8400 "-right"
delay 8500 "+forward"
delay 9500 "-forward"
delay 9600 "echo ROUTE end"
delay 9700 "viewpos"
delay 9800 "quit"
"""


def main():
    source_dir = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else rr_game_check.DEFAULT_SOURCE
    if not (source_dir / "g_main.cpp").exists():
        sys.exit(f"no re-release game source in {source_dir}")
    game_library = rr_game_check.build_game(source_dir)

    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    log_path = OUTPUT_DIR / "client.log"
    with tempfile.TemporaryDirectory(prefix="optech2-rr-predict-") as home:
        mod_dir = Path(home) / ".opentech2" / rr_game_check.MOD_NAME
        mod_dir.mkdir(parents=True)
        shutil.copy(game_library, mod_dir / "game_x64.so")
        (mod_dir / "c.cfg").write_text(ROUTE)
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        with open(log_path, "w") as log_file:
            subprocess.run(["stdbuf", "-oL", str(rr_game_check.CLIENT_BINARY),
                            "-datadir", str(rr_game_check.RERELEASE_DATA_DIR),
                            "+set", "game", rr_game_check.MOD_NAME, "+set", "vid_renderer", "soft",
                            "+set", "s_initsound", "0", "+set", "cl_maxfps", "125",
                            "+exec", "c.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=90, check=False)

    log = log_path.read_text(errors="replace")
    report("client game: library game_x64" in log, "client loaded the re-release library")
    report("client game: re-release movement for prediction" in log,
           "client predicts with the library's movement (server sent the re-release state)")

    route = log.split("ROUTE start")[-1].split("ROUTE end")[0] if "ROUTE start" in log else ""
    misses = [abs(int(m.group(1))) for m in re.finditer(r"prediction miss on \d+: (-?\d+)", route)]
    report(len(misses) <= MAX_MISSES, f"{len(misses)} prediction misses on the route (at most {MAX_MISSES})")
    report(not misses or max(misses) <= MAX_MISS_UNITS,
           f"largest miss {max(misses) / 8 if misses else 0:.3f} units (at most {MAX_MISS_UNITS / 8})")

    positions = [tuple(float(v) for v in m.groups()) for m in
                 re.finditer(r"position: (-?[\d.]+) (-?[\d.]+) (-?[\d.]+),", log)]
    if len(positions) >= 2:
        moved = ((positions[-1][0] - positions[0][0]) ** 2 + (positions[-1][1] - positions[0][1]) ** 2) ** 0.5
        report(moved > 64, f"player moved {moved:.0f} units over the route")
    else:
        report(False, "positions printed")

    bad = [line for line in log.splitlines() if "(BAD" in line or "ERROR" in line or "Game Error" in line]
    report(not bad, "no errors" + (f" ({bad[0].strip()[:90]})" if bad else ""))

    passed = all(results)
    print(f"\nRe-release prediction check {'passed' if passed else 'FAILED'}. Log in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
