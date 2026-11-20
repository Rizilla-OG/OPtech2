#!/usr/bin/env python3
"""Packet splitting check for OPtech2.

Starts a dedicated server (co-op, base1, 60 Hz) and one hidden client that
talks to it over UDP on 127.0.0.1 (real network path, not loopback). The
client fills the room with armor shards and turns delta compression off
(cl_nodelta 1), so every server frame is a big full update that must be
split into fragments. Runs twice:
  clean  no loss
  lossy  the server throws away net_fragmentloss % of its fragments

Checks for each run:
  1. the client connects (the connect gamestate is split too) and stays
     connected to the end (its last chat line reaches the server),
  2. the server split messages and the largest one is big (several fragments),
  3. the client put split messages back together (cl_netstats),
  4. clean run: no fragment dropped; lossy run: fragments were dropped and
     the client still got whole messages,
  5. no network warnings on either side.

Usage:
  split_check.py               run the check (exit 1 on failure)
  split_check.py --loss 40     loss % for the lossy run (default 25)

Every process gets its own temporary HOME, so the user's own config and
savegames are never touched. Logs land in build/split-check/.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

CHECK_DIR = Path(__file__).resolve().parent
REPO_DIR = CHECK_DIR.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "optech2"
SERVER_BINARY = REPO_DIR / "build" / "release" / "optech2ded"
OUTPUT_DIR = REPO_DIR / "build" / "split-check"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"

SERVER_PORT = 27997
GAME_TIMEOUT_SECONDS = 120
FRAGMENT_SIZE = 1200  # NETCHAN_FRAGMENT_SIZE in common.h
MIN_LARGEST_MESSAGE = 4 * FRAGMENT_SIZE  # big frames must need several fragments

CLIENT_SETTINGS = [
    "+set", "vid_renderer", "soft", "+set", "r_vsync", "0",  # dummy video driver: vsync never returns
    "+set", "cl_maxfps", "-1", "+set", "s_initsound", "0",
    "+set", "rate", "100000",  # full updates every frame need more than the default rate
]
PLAYER_SPOT = "128 -320 32 0 135 0"
# 400 shards on a 20 x 20 grid in the big room in front of the player
SHARD_SPOTS = [f"{x} {y} 48" for x in range(-250, 250, 25) for y in range(-520, -20, 25)]
SPAWN_START_SECONDS = 14
SPAWNS_PER_FILE = 6  # the server takes at most 8 commands per client packet
SPAWN_FILE_SPACING_MS = 100

WARNINGS = re.compile(r"overflow|dumped unreliable|bad delta|ERROR|Server connection timed out", re.IGNORECASE)


def at(seconds, command):
    """Run a console command this many seconds after the client starts (engine "delay" command)."""
    return f'delay {int(seconds * 1000)} "{command}"'


def client_files():
    """{file name: lines}. The delay queue holds 128 commands, so the spawns run as a chain of small
    cfg files, each spawning a few shards and then exec'ing the next one."""
    batches = [SHARD_SPOTS[index:index + SPAWNS_PER_FILE] for index in range(0, len(SHARD_SPOTS), SPAWNS_PER_FILE)]
    files = {"split.cfg": [f"connect 127.0.0.1:{SERVER_PORT}", at(13, "god"), at(13.2, f"teleport {PLAYER_SPOT}"),
                           at(SPAWN_START_SECONDS, "exec spawn_0.cfg")]}
    for number, batch in enumerate(batches):
        lines = [f"cmd spawnentity item_armor_shard {spot}" for spot in batch]
        if number + 1 < len(batches):
            lines.append(f'delay {SPAWN_FILE_SPACING_MS} "exec spawn_{number + 1}.cfg"')
        else:
            # all shards are in: full updates for 5 s, then report and leave
            lines += ['delay 1000 "set cl_nodelta 1"', 'delay 6000 "cl_netstats"',
                      'delay 6200 "say split_done"', 'delay 8000 "quit"']
        files[f"spawn_{number}.cfg"] = lines
    return files


def start(command, home, log_path, extra_environment=None):
    environment = dict(os.environ, HOME=str(home), **(extra_environment or {}))
    log_file = open(log_path, "w")
    process = subprocess.Popen(["stdbuf", "-oL", *command], env=environment, stdout=log_file, stderr=subprocess.STDOUT)
    return process, log_file


def run_session(label, loss_percent, data_dir):
    """One server + one client; return (server log, client log)."""
    server_log_path = OUTPUT_DIR / f"server_{label}.log"
    client_log_path = OUTPUT_DIR / f"client_{label}.log"
    with tempfile.TemporaryDirectory(prefix="optech2-split-") as temp_folder:
        temp_root = Path(temp_folder)
        server_home, client_home = temp_root / "server", temp_root / "client"
        for home in (server_home, client_home):
            (home / ".optech2" / "baseq2").mkdir(parents=True)

        server, server_log = start(
            [str(SERVER_BINARY), "-datadir", str(data_dir), "+set", "port", str(SERVER_PORT), "+set", "public", "0",
             "+set", "coop", "1", "+set", "deathmatch", "0", "+set", "cheats", "1", "+set", "maxclients", "4",
             "+set", "sv_tickrate", "60", "+set", "sv_netstats", "1",
             "+set", "net_fragmentloss", str(loss_percent), "+map", "base1"],
            server_home, server_log_path)
        time.sleep(3)

        for file_name, lines in client_files().items():
            (client_home / ".optech2" / "baseq2" / file_name).write_text("\n".join(lines) + "\n")
        client, client_log = start(
            [str(CLIENT_BINARY), "-datadir", str(data_dir), *CLIENT_SETTINGS, "+set", "showdrop", "1",
             "+set", "name", "alice", "+exec", "split.cfg"],
            client_home, client_log_path, {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"})
        try:
            client.wait(timeout=GAME_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            client.kill()
        client_log.close()
        server.terminate()
        server.wait(timeout=10)
        server_log.close()
    return server_log_path.read_text(errors="replace"), client_log_path.read_text(errors="replace")


def check_run(label, lossy, server_log, client_log):
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<8}{label}: {text}")

    report("alice entered the game" in server_log or "alice connected" in server_log or "split_done" in server_log,
           "client connected")
    report("alice: split_done" in server_log, "client still connected at the end (chat reached the server)")

    split_counts = [int(value) for value in re.findall(r"netstats alice .* split (\d+) largest \d+", server_log)]
    largest_values = [int(value) for value in re.findall(r"netstats alice .* largest (\d+)", server_log)]
    largest = max(largest_values, default=0)
    report(sum(split_counts) > 0, f"server split {sum(split_counts)} messages")
    report(largest >= MIN_LARGEST_MESSAGE,
           f"largest split message {largest} B (~{-(-largest // FRAGMENT_SIZE)} fragments, need >= {MIN_LARGEST_MESSAGE})")

    stats = re.search(r"netstats received (\d+) B (\d+) pkt reassembled (\d+) fragdrops (\d+)", client_log)
    if not stats:
        report(False, "client printed cl_netstats")
        return all(results)
    reassembled, fragment_drops = int(stats.group(3)), int(stats.group(4))
    report(reassembled > 0, f"client reassembled {reassembled} messages")
    if lossy:
        report(fragment_drops > 0, f"client threw away {fragment_drops} incomplete fragments (loss seen)")
    else:
        report(fragment_drops == 0, f"client threw away {fragment_drops} fragments (none expected)")

    server_warnings = [line for line in server_log.splitlines() if WARNINGS.search(line)]
    client_warnings = [line for line in client_log.splitlines() if WARNINGS.search(line)]
    report(not server_warnings, "server: no network warnings" + (f" ({server_warnings[0]})" if server_warnings else ""))
    report(not client_warnings, "client: no network warnings" + (f" ({client_warnings[0]})" if client_warnings else ""))
    return all(results)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--loss", type=int, default=25, help="percent of server fragments thrown away in the lossy run")
    arguments = parser.parse_args()

    for binary in (CLIENT_BINARY, SERVER_BINARY):
        if not binary.exists():
            sys.exit(f"Missing {binary}. Build first: cmake --workflow --preset default")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    all_passed = True
    for label, loss_percent in (("clean", 0), ("lossy", arguments.loss)):
        server_log, client_log = run_session(label, loss_percent, arguments.datadir)
        all_passed &= check_run(label, loss_percent > 0, server_log, client_log)

    print(f"\nSplit check {'passed' if all_passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
