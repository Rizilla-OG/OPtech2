#!/usr/bin/env python3
"""Timedemo benchmark for OpenTech2.

Plays the stock demos as fast as possible and reports frame times.

Usage:
  benchmark.py                 run and compare with baseline.json
  benchmark.py --save-baseline run and store the results as the new baseline.json

Each demo runs several times; the run with the median average is kept.
The game runs with HOME pointed at a temporary folder, so the user's own
config and savegames are never touched. A game window opens while it runs.
"""

import argparse
import json
import os
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

BENCHMARK_DIR = Path(__file__).resolve().parent
REPO_DIR = BENCHMARK_DIR.parent.parent
GAME_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
BASELINE_PATH = BENCHMARK_DIR / "baseline.json"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DEFAULT_DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))

DEMOS = ["demo1.dm2", "demo2.dm2"]
RUNS_PER_DEMO = 3
RUN_TIMEOUT_SECONDS = 300
SLOWER_WARNING_FRACTION = 0.10  # flag averages more than 10% slower than the baseline

SCREEN_WIDTH, SCREEN_HEIGHT = 1280, 720
GAME_SETTINGS = [
    "+set", "vid_fullscreen", "0", "+set", "r_mode", "-1",
    "+set", "r_customwidth", str(SCREEN_WIDTH), "+set", "r_customheight", str(SCREEN_HEIGHT),
    "+set", "vid_renderer", "gl3", "+set", "r_vsync", "0",
]

# Printed by the engine when a timedemo ends (see CL_TimedemoPrintFrameStats).
STATS_PATTERN = re.compile(
    r"frametime_ms avg ([\d.]+) p50 ([\d.]+) p95 ([\d.]+) p99 ([\d.]+) max ([\d.]+) frames (\d+)")
STAT_NAMES = ["avg", "p50", "p95", "p99", "max"]


def run_timedemo(demo_name, data_dir, temp_home):
    """Play one demo with timedemo on; return its frame-time stats in ms."""
    # The engine only creates the last folder level, so make ~/.opentech2 up front.
    (temp_home / ".opentech2").mkdir(exist_ok=True)
    environment = dict(os.environ, HOME=str(temp_home))
    # stdbuf: the game buffers stdout when it is a pipe, which would hide the stats line until exit.
    command = ["stdbuf", "-oL", str(GAME_BINARY), "-datadir", str(data_dir), *GAME_SETTINGS,
               "+set", "timedemo", "1", "+demomap", demo_name]
    process = subprocess.Popen(command, env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, errors="replace")
    deadline = time.monotonic() + RUN_TIMEOUT_SECONDS
    try:
        for line in process.stdout:
            match = STATS_PATTERN.search(line)
            if match:
                stats = dict(zip(STAT_NAMES, map(float, match.groups()[:5])))
                stats["frames"] = int(match.group(6))
                return stats
            if time.monotonic() > deadline:
                break
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
    sys.exit(f"{demo_name}: no frame-time stats printed (timed out or crashed)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--save-baseline", action="store_true", help="store results as baseline.json")
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    arguments = parser.parse_args()

    if not GAME_BINARY.exists():
        sys.exit(f"Missing {GAME_BINARY}. Build first: cmake --workflow --preset default")

    results = {}
    with tempfile.TemporaryDirectory(prefix="optech2-bench-") as temp_folder:
        for demo_name in DEMOS:
            runs = [run_timedemo(demo_name, arguments.datadir, Path(temp_folder)) for _ in range(RUNS_PER_DEMO)]
            median_average = statistics.median(run["avg"] for run in runs)
            results[demo_name] = next(run for run in runs if run["avg"] == median_average)

    baseline = json.loads(BASELINE_PATH.read_text()) if BASELINE_PATH.exists() else None
    print(f"{'demo':<10} {'avg':>7} {'p50':>7} {'p95':>7} {'p99':>7} {'max':>7}  (ms per frame)")
    slower = []
    for demo_name, stats in results.items():
        print(f"{demo_name:<10} " + " ".join(f"{stats[name]:7.3f}" for name in STAT_NAMES))
        if baseline and not arguments.save_baseline and demo_name in baseline["results"]:
            base_stats = baseline["results"][demo_name]
            print(f"{'baseline':<10} " + " ".join(f"{base_stats[name]:7.3f}" for name in STAT_NAMES))
            if stats["avg"] > base_stats["avg"] * (1 + SLOWER_WARNING_FRACTION):
                slower.append(demo_name)

    if arguments.save_baseline:
        BASELINE_PATH.write_text(json.dumps({
            "machine": platform.node(), "date": time.strftime("%Y-%m-%d"),
            "resolution": f"{SCREEN_WIDTH}x{SCREEN_HEIGHT}", "renderer": "gl3",
            "results": results}, indent=2) + "\n")
        print(f"\nBaseline saved to {BASELINE_PATH}")
    elif slower:
        print(f"\nSlower than baseline by more than {SLOWER_WARNING_FRACTION:.0%}: {', '.join(slower)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
