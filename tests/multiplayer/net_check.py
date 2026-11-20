#!/usr/bin/env python3
"""Network traffic check for OPtech2: how much data goes to and from players?

Runs a dedicated co-op server on base1 with two hidden clients (alice, bob)
at each tick rate, with sv_netstats 1, through three phases:
  idle   both players stand still
  fight  both strafe and fire hyperblasters
  busy   30 soldiers spawned around them, fighting
The server prints each client's traffic once a second; this averages it per
phase and prints KB/s per player in each direction (netchan payload plus 28
bytes UDP/IPv4 header per packet) and frames held back by the rate limiter.

Usage:
  net_check.py                     tick rates 10, 40 and 60
  net_check.py --rates 60          only 60 Hz
  net_check.py --rate-limit 5000   set the clients' rate (bytes/s) to test the limiter

Takes about 45 s per tick rate, no windows. Logs land in build/net-check/.
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
OUTPUT_DIR = REPO_DIR / "build" / "net-check"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"

SERVER_PORT = 27998
UDP_IP_HEADER_BYTES = 28
GAME_TIMEOUT_SECONDS = 120
PHASES = ["idle", "fight", "busy"]

CLIENT_SETTINGS = [
    "+set", "vid_renderer", "soft", "+set", "r_vsync", "0",  # dummy video driver: vsync never returns
    "+set", "cl_maxfps", "-1", "+set", "s_initsound", "0",  # automatic packet rate, as players have it
]
ALICE_SPOT = "128 -320 32 0 135 0"
BOB_SPOT = "-84 -108 32 0 315 0"
# 30 soldiers on a grid in the big room around both players
SOLDIER_SPOTS = [f"{x} {y} 64 0 {(x * 7 + y) % 360} 0" for x in range(-250, 251, 100) for y in range(-500, -99, 100)]


def at(seconds, command):
    """Run a console command this many seconds after the client starts (engine "delay" command)."""
    return f'delay {int(seconds * 1000)} "{command}"'


def client_script(name, spot, spawns_monsters):
    lines = [f"connect 127.0.0.1:{SERVER_PORT}", at(13, "god"), at(13.2, f"teleport {spot}"),
             at(13.5, "give hyperblaster"), at(13.6, "give cells"), at(13.7, "use hyperblaster")]
    if name == "alice":
        lines.append(at(15, "say phase_idle"))
        lines.append(at(21, "say phase_fight"))
    lines += [at(21, "+attack"), at(21, "+moveleft"), at(23.5, "-moveleft"), at(23.5, "+moveright"),
              at(26, "-moveright"), at(26, "-attack")]
    if spawns_monsters:
        lines.append(at(26.5, "say phase_busy"))
        # at most a few commands per packet reach the server, so spread the spawns out
        lines += [at(27 + index * 0.05, f"cmd spawnentity monster_soldier {spot_text}")
                  for index, spot_text in enumerate(SOLDIER_SPOTS)]
        lines.append(at(38, "say phase_end"))
    lines.append(at(39, "quit"))
    return lines


def start(command, home, log_path, extra_environment=None):
    environment = dict(os.environ, HOME=str(home), **(extra_environment or {}))
    log_file = open(log_path, "w")
    process = subprocess.Popen(["stdbuf", "-oL", *command], env=environment, stdout=log_file, stderr=subprocess.STDOUT)
    return process, log_file


def run_session(tick_rate, data_dir, rate_limit):
    """One server + two clients at one tick rate; return the server log text."""
    server_log_path = OUTPUT_DIR / f"server_{tick_rate}hz.log"
    with tempfile.TemporaryDirectory(prefix="optech2-net-") as temp_folder:
        temp_root = Path(temp_folder)
        homes = {name: temp_root / name for name in ("server", "alice", "bob")}
        for home in homes.values():
            (home / ".optech2" / "baseq2").mkdir(parents=True)

        server, server_log = start(
            [str(SERVER_BINARY), "-datadir", str(data_dir), "+set", "port", str(SERVER_PORT), "+set", "public", "0",
             "+set", "coop", "1", "+set", "deathmatch", "0", "+set", "cheats", "1", "+set", "maxclients", "4",
             "+set", "sv_tickrate", str(tick_rate), "+set", "sv_netstats", "1", "+map", "base1"],
            homes["server"], server_log_path)
        time.sleep(3)

        clients = []
        for name, spot, spawns in (("alice", ALICE_SPOT, True), ("bob", BOB_SPOT, False)):
            game_dir = homes[name] / ".optech2" / "baseq2"
            (game_dir / "net.cfg").write_text("\n".join(client_script(name, spot, spawns)) + "\n")
            process, log_file = start(
                [str(CLIENT_BINARY), "-datadir", str(data_dir), *CLIENT_SETTINGS, "+set", "rate", str(rate_limit),
                 "+set", "name", name, "+exec", "net.cfg"],
                homes[name], OUTPUT_DIR / f"{name}_{tick_rate}hz.log",
                {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"})
            clients.append((process, log_file))

        for process, log_file in clients:
            try:
                process.wait(timeout=GAME_TIMEOUT_SECONDS)
            except subprocess.TimeoutExpired:
                process.kill()
            log_file.close()
        server.terminate()
        server.wait(timeout=10)
        server_log.close()
    return server_log_path.read_text(errors="replace")


def summarize(server_log):
    """Average traffic per phase and player: {phase: {player: (down KB/s, up KB/s, drops/s)}}."""
    samples = {phase: {} for phase in PHASES}
    phase = None
    skip_next = False
    for line in server_log.splitlines():
        marker = re.search(r"alice: phase_(\w+)", line)
        if marker:
            phase = marker.group(1) if marker.group(1) in PHASES else None
            skip_next = True  # first sample after a marker straddles two phases
            continue
        stats = re.search(r"netstats (\w+) sent (\d+) B (\d+) pkt received (\d+) B (\d+) pkt ratedrops (\d+)", line)
        if not stats or phase is None:
            continue
        if skip_next:
            skip_next = (stats.group(1) != "bob")  # skip one line per player
            continue
        name = stats.group(1)
        sent, sent_packets, received, received_packets, drops = (int(stats.group(i)) for i in range(2, 7))
        down = (sent + UDP_IP_HEADER_BYTES * sent_packets) / 1024
        up = (received + UDP_IP_HEADER_BYTES * received_packets) / 1024
        samples[phase].setdefault(name, []).append((down, up, drops))

    averages = {}
    for phase, players in samples.items():
        averages[phase] = {}
        for name, values in players.items():
            count = len(values)
            averages[phase][name] = tuple(sum(value[i] for value in values) / count for i in range(3))
    return averages


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--rates", type=int, nargs="+", default=[10, 40, 60], help="tick rates to measure")
    parser.add_argument("--rate-limit", type=int, default=25000, help="clients' rate setting in bytes/s")
    arguments = parser.parse_args()

    for binary in (CLIENT_BINARY, SERVER_BINARY):
        if not binary.exists():
            sys.exit(f"Missing {binary}. Build first: cmake --workflow --preset default")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    print(f"{'tick rate':<10}{'phase':<7}{'player':<7}{'server->player KB/s':>21}{'player->server KB/s':>21}{'rate drops/s':>14}")
    missing = 0
    for tick_rate in arguments.rates:
        averages = summarize(run_session(tick_rate, arguments.datadir, arguments.rate_limit))
        for phase in PHASES:
            if not averages[phase]:
                missing += 1
                print(f"{tick_rate:<10}{phase:<7}no samples (see {OUTPUT_DIR}/server_{tick_rate}hz.log)")
            for name, (down, up, drops) in sorted(averages[phase].items()):
                print(f"{tick_rate:<10}{phase:<7}{name:<7}{down:>21.1f}{up:>21.1f}{drops:>14.1f}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
