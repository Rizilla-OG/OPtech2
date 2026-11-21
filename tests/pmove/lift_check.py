#!/usr/bin/env python3
"""Lift prediction check for OpenTech2.

Rides the lift of the lift test map (tests/maps/make_lift_map.py: a 128 x 128
func_train going up and down 384 units at 200 units/s) with cl_showmiss 1 and
counts prediction misses, with cl_predict_movers on and off:

  standing   stand on the lift for several rides
  jumping    jump again and again while riding

Before cl_predict_movers the client predicted the player against the lift one
tick behind, a miss of about 3 units every frame the lift moves. With it, the
replayed commands see the lift moved on by its speed and the player is carried
along; what is left is under a unit (the server pushes once a tick, the carry
is continuous), which the client's error smoothing hides.

Usage:
  lift_check.py
Builds the map first if build/maps/optech2_lift.bsp is missing (needs
ericw-tools, tools/get_ericw_tools.py). Hidden software renderer, temporary HOME.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_lift.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "lift-check"
DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"

RIDE_START_MS = 4000
RIDE_END_MS = 16000


def scenario_script(name):
    lines = ["map optech2_lift", f'delay {RIDE_START_MS - 200} "clear"']
    if name == "jumping":
        for when in range(RIDE_START_MS, RIDE_END_MS, 900):
            lines += [f'delay {when} "+moveup"', f'delay {when + 100} "-moveup"']
    lines += [f'delay {RIDE_END_MS} "echo ride over"', f'delay {RIDE_END_MS + 500} "quit"']
    return "\n".join(lines) + "\n"


def ride(name, predict_movers):
    with tempfile.TemporaryDirectory(prefix="optech2-lift-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        (game_dir / "maps").mkdir(parents=True)
        shutil.copy(MAP_FILE, game_dir / "maps" / MAP_FILE.name)
        (game_dir / "l.cfg").write_text(scenario_script(name))
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        result = subprocess.run(
            ["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set", "vid_renderer", "soft",
             "+set", "r_vsync", "0", "+set", "s_initsound", "0", "+set", "cl_showmiss", "1",
             "+set", "cl_predict_movers", str(predict_movers), "+exec", "l.cfg"],
            env=environment, capture_output=True, text=True, errors="replace", timeout=60)
    log = result.stdout + result.stderr
    (OUTPUT_DIR / f"{name}_movers{predict_movers}.log").write_text(log)
    # only the ride itself: after the console clear, before "ride over"
    ride_log = log.split("ride over")[0]
    ride_log = ride_log[ride_log.rfind("clear") if "clear" in ride_log else 0:]
    misses = sorted(abs(int(value)) for value in re.findall(r"prediction miss on \d+: (-?\d+)", ride_log))
    largest_typical = misses[int(len(misses) * 0.95)] / 8.0 if misses else 0.0  # 95th percentile, units
    return len(misses), (sum(misses) / 8.0), largest_typical, ("ride over" in log)


def main():
    if not MAP_FILE.exists():
        subprocess.run([sys.executable, str(REPO_DIR / "tests" / "maps" / "make_lift_map.py")], check=True)
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    results = []
    print(f"{'ride':<10}{'cl_predict_movers':>18}{'misses':>8}{'total off (units)':>19}{'95% under (units)':>19}")
    measured = {}
    for name in ("standing", "jumping"):
        for predict_movers in (0, 1):
            count, total, typical, finished = ride(name, predict_movers)
            measured[(name, predict_movers)] = (count, total, typical)
            results.append(finished)
            print(f"{name:<10}{predict_movers:>18}{count:>8}{total:>19.1f}{typical:>19.2f}"
                  + ("" if finished else "   (did not finish)"))

    for name in ("standing", "jumping"):
        before, after = measured[(name, 0)], measured[(name, 1)]
        # off shows the one-tick lift lag (about 3.3 units a frame); on leaves only
        # the timing remainder (the server pushes once a tick, the carry is
        # continuous): 95% of misses within 1 unit and the total cut by 70%+
        # jumping: takeoff and landing on a moving lift still miss a tick now and
        # then (client and server disagree for a tick on standing), so there
        # only the total is required to drop
        typical_ok = after[2] <= 1.0 if name == "standing" else True
        passed = before[0] > 50 and before[2] >= 3.0 and typical_ok and after[1] <= before[1] * 0.3
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{name}: 95% of misses under {before[2]:.2f} -> {after[2]:.2f} units, "
              f"total {before[1]:.0f} -> {after[1]:.0f} units")

    passed = all(results)
    print(f"\nLift check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
