#!/usr/bin/env python3
"""Entity stress check for OpenTech2: plays the stress map (built by
make_stress_map.py, ~3800 entities) in a GL3 window and measures frame times
with host_speeds, in two phases:
  idle   notarget on: 300 soldiers idle, 500 rotators spinning, 3000 items
  fight  notarget off: soldiers that see the player attack (player in god mode)
The same run on stock base1 gives a baseline to compare against.

Passes when, in every phase on the stress map:
  - the client draws at least 60 frames per second, and
  - one 60 Hz server frame (engine + game) takes on average less than its
    16.7 ms budget.

Usage:
  stress_check.py                  stress map + base1 baseline
  stress_check.py --skip-baseline  stress map only

Opens a 1280x720 window for about 1 minute (vsync off, uncapped frame rate).
Uses a temporary HOME. Logs and one screenshot per phase land in
build/stress-check/ (stress map shots also in docs/screenshots/ with --doc-shots).
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
DOC_SHOTS_WANTED = os.environ.get("OPTECH2_DOC_SHOTS") == "1"  # run_all.py --doc-shots: refresh docs/screenshots/
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
MAP_FILE = REPO_DIR / "build" / "maps" / "optech2_stress.bsp"
OUTPUT_DIR = REPO_DIR / "build" / "stress-check"
DOC_SHOT_DIR = REPO_DIR / "docs" / "screenshots"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DEFAULT_DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))
GAME_TIMEOUT_SECONDS = 120

MIN_CLIENT_FPS = 60
SERVER_FRAME_BUDGET_MS = 1000 / 60
PHASE_SECONDS = 8
MIN_FIGHT_BOLTS = 5
PHASES = ["idle", "fight"]

CLIENT_SETTINGS = [
    "+set", "vid_fullscreen", "0", "+set", "r_mode", "-1",
    "+set", "r_customwidth", "1280", "+set", "r_customheight", "720",
    "+set", "vid_renderer", "gl3", "+set", "r_vsync", "0",
    "+set", "vid_maxfps", "1000", "+set", "cl_maxfps", "-1", "+set", "s_initsound", "0",
    "+set", "con_notifytime", "0",  # no host_speeds text in the screenshots
]
# where the player stands and looks on each map: the stress map from the middle across the room
VIEWPOINTS = {"optech2_stress": "-60 0 24 8 25 0", "base1": "128 -320 32 0 135 0"}

HOST_SPEEDS = re.compile(r"all:\s*([\d.]+) sv:\s*([\d.]+) gm:\s*([\d.]+) cl:\s*([\d.]+) rf:\s*([\d.]+)")


def script_lines(map_name):
    def at(milliseconds, command):
        return f'delay {milliseconds} "{command}"'

    lines = [f"map {map_name}", at(7000, "god"), at(7050, "notarget"), at(7200, f"teleport {VIEWPOINTS[map_name]}"),
             at(7400, "cmd listentities all")]
    start = 9000
    for phase in PHASES:
        if phase == "fight":
            lines.append(at(start - 500, "notarget"))  # toggles it off: monsters can see the player again
        if phase == "fight":
            middle = start + PHASE_SECONDS * 500  # list entities mid-fight: soldier bolts in the air
            lines += [at(middle, "echo list_fight"), at(middle, "cmd listentities all"), at(middle + 400, "echo list_end")]
        lines.append(at(start + PHASE_SECONDS * 1000 - 300, "screenshot png"))
        lines += [at(start, f"echo phase_{phase}"), at(start, "host_speeds 1"),
                  at(start + PHASE_SECONDS * 1000, "host_speeds 0"), at(start + PHASE_SECONDS * 1000, "echo phase_end")]
        start += PHASE_SECONDS * 1000 + 1500
    lines.append(at(start, "quit"))
    return lines


def run_session(map_name, data_dir):
    log_path = OUTPUT_DIR / f"{map_name}.log"
    with tempfile.TemporaryDirectory(prefix="optech2-stress-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        (game_dir / "maps").mkdir(parents=True)
        if map_name == "optech2_stress":
            shutil.copy(MAP_FILE, game_dir / "maps" / MAP_FILE.name)
        (game_dir / "stress.cfg").write_text("\n".join(script_lines(map_name)) + "\n")
        environment = dict(os.environ, HOME=home, SDL_AUDIODRIVER="dummy")
        with open(log_path, "w") as log_file:
            subprocess.run([str(CLIENT_BINARY), "-datadir", str(data_dir), *CLIENT_SETTINGS, "+exec", "stress.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT,
                           timeout=GAME_TIMEOUT_SECONDS, check=False)
        shot_dir = game_dir / "scrnshot"
        shots = sorted(shot_dir.glob("*.png")) if shot_dir.exists() else []
        for phase, shot in zip(PHASES, shots):
            shutil.copy(shot, OUTPUT_DIR / f"{map_name}_{phase}.png")
            if map_name == "optech2_stress":
                DOC_SHOT_DIR.mkdir(parents=True, exist_ok=True)
                if DOC_SHOTS_WANTED:
                    shutil.copy(shot, DOC_SHOT_DIR / f"m1_stress_map_{phase}.png")
    return log_path.read_text(errors="replace")


def measure(log):
    """{phase: stats} from the host_speeds lines between the phase markers, plus the entity count."""
    samples = {phase: [] for phase in PHASES}
    phase = None
    listing = "start"  # entity lines before the first phase are the start listing
    entity_count, fight_bolts = 0, 0
    for line in log.splitlines():
        entity_line = re.match(r"^(\w+): -?[\d.]+ -?[\d.]+ -?[\d.]+\s*$", line)
        if entity_line and entity_line.group(1) != "freed":
            if listing == "start":
                entity_count += 1
            elif listing == "fight" and entity_line.group(1) == "bolt":
                fight_bolts += 1
            continue
        if "list_fight" in line:
            listing = "fight"
            continue
        if "list_end" in line:
            listing = None
            continue
        marker = re.search(r"phase_(\w+)", line)
        if marker:
            if listing == "start":
                listing = None
            phase = marker.group(1) if marker.group(1) in PHASES else None
            continue
        speeds = HOST_SPEEDS.search(line)
        if speeds and phase:
            samples[phase].append(tuple(float(value) for value in speeds.groups()))

    results = {}
    for phase, lines in samples.items():
        if not lines:
            continue
        frames_drawn = [line for line in lines if line[4] > 0]  # rf > 0: the client rendered this frame
        server_frames = [line[1] + line[2] for line in lines if line[2] > 0]  # sv + gm, frames where the game ran
        total_ms = sum(line[0] for line in lines)
        results[phase] = {
            "client fps": len(frames_drawn) / PHASE_SECONDS,
            "frame ms": total_ms / max(len(frames_drawn), 1),
            "render ms": sum(line[4] for line in frames_drawn) / max(len(frames_drawn), 1),
            "client ms": sum(line[3] for line in frames_drawn) / max(len(frames_drawn), 1),
            "server ms": sum(server_frames) / max(len(server_frames), 1),
            "server max ms": max(server_frames, default=0),
            "server frames/s": len(server_frames) / PHASE_SECONDS,
        }
    return entity_count, fight_bolts, results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--skip-baseline", action="store_true", help="don't measure base1")
    arguments = parser.parse_args()

    if not CLIENT_BINARY.exists():
        sys.exit(f"Missing {CLIENT_BINARY}. Build first: cmake --workflow --preset default")
    if not MAP_FILE.exists():
        sys.exit(f"Missing {MAP_FILE}. Build it first: python3 tests/maps/make_stress_map.py")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    maps = ["optech2_stress"] + ([] if arguments.skip_baseline else ["base1"])
    all_results = {}
    bolts_seen = {}
    print(f"{'map':<16}{'phase':<7}{'entities':>9}{'client fps':>11}{'frame ms':>9}{'render ms':>10}"
          f"{'client ms':>10}{'server ms':>10}{'server max':>11}{'sv frames/s':>12}")
    for map_name in maps:
        entity_count, fight_bolts, results = measure(run_session(map_name, arguments.datadir))
        all_results[map_name] = results
        bolts_seen[map_name] = fight_bolts
        for phase in PHASES:
            stats = results.get(phase)
            if not stats:
                print(f"{map_name:<16}{phase:<7}no samples (see {OUTPUT_DIR}/{map_name}.log)")
                continue
            print(f"{map_name:<16}{phase:<7}{entity_count:>9}{stats['client fps']:>11.0f}{stats['frame ms']:>9.2f}"
                  f"{stats['render ms']:>10.2f}{stats['client ms']:>10.2f}{stats['server ms']:>10.2f}"
                  f"{stats['server max ms']:>11.2f}{stats['server frames/s']:>12.0f}")

    stress = all_results.get("optech2_stress", {})
    print(f"\nblaster bolts in the air mid-fight on the stress map: {bolts_seen.get('optech2_stress', 0)} "
          f"(need >= {MIN_FIGHT_BOLTS}: soldiers really attack)")
    passed = bolts_seen.get("optech2_stress", 0) >= MIN_FIGHT_BOLTS and len(stress) == len(PHASES) and all(
        stats["client fps"] >= MIN_CLIENT_FPS and stats["server ms"] < SERVER_FRAME_BUDGET_MS for stats in stress.values())
    print(f"\nStress check {'passed' if passed else 'FAILED'} (need >= {MIN_CLIENT_FPS} fps and server frame < "
          f"{SERVER_FRAME_BUDGET_MS:.1f} ms). Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
