#!/usr/bin/env python3
"""Two-client multiplayer check for OPtech2.

Starts a dedicated server (deathmatch, q2dm1) and two hidden clients on this
machine, then checks that:
  1. both players join the server,
  2. chat goes both ways (each client sees the other's message),
  3. each client sees the other player (screenshot with vs without them),
  4. a moving player shows up as moving (screenshot before vs after walking),
  5. no network warnings appear (overflows, stale deltas, errors).

Usage:
  mp_check.py              run the check (exit 1 on failure)
  mp_check.py --tickrate 10   same with the server at stock 10 frames per second

Clients run with the software renderer on SDL's dummy video driver, so no
windows open. Every process gets its own temporary HOME, so the user's own
config and savegames are never touched. Screenshots and logs land in
build/mp-check/.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image, ImageChops

CHECK_DIR = Path(__file__).resolve().parent
REPO_DIR = CHECK_DIR.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "optech2"
SERVER_BINARY = REPO_DIR / "build" / "release" / "optech2ded"
OUTPUT_DIR = REPO_DIR / "build" / "mp-check"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"

SERVER_PORT = 27999           # not the default 27910, so a real server on this machine is left alone
MAP_NAME = "q2dm1"
FRAMES_PER_SECOND = 60        # client frame rate
GAME_TIMEOUT_SECONDS = 180

# Fixed spots on q2dm1 (x y z pitch yaw roll). Alice stands on a deathmatch
# spawn at the end of a long straight corridor, looking down it (north).
# Bob is either far away or 120 units down the corridor, facing her.
ALICE_SPOT = "2016 -224 664 0 90 0"
BOB_HIDDEN_SPOT = "832 1744 792 0 270 0"   # another spawn, far away and out of Alice's view
BOB_IN_VIEW_SPOT = "2016 -104 664 0 270 0"  # 120 units down the corridor, facing Alice

ALICE_MESSAGE = "hello from alice"
BOB_MESSAGE = "hello from bob"

# A pixel counts as changed when its brightness differs by more than this,
# and a view counts as changed when more pixels than this changed. The corridor
# is dark and the player model small, so both are low. The control run
# (Bob never in view) must stay under it.
PIXEL_TOLERANCE = 12
MIN_CHANGED_PIXELS = 300

# Log lines that mean the network code had trouble.
WARNING_SIGNS = ["overflow", "too old", "ERROR", "Delta frame", "rest not sent", "Bad delta"]

CLIENT_SETTINGS = [
    "+set", "vid_renderer", "soft", "+set", "vid_fullscreen", "0", "+set", "r_mode", "4",  # 800x600
    "+set", "r_vsync", "0",  # with the dummy video driver vsync never returns
    "+set", "cl_maxfps", str(FRAMES_PER_SECOND), "+set", "s_initsound", "0",
    "+set", "cl_gun", "0", "+set", "crosshair", "0", "+set", "con_notifytime", "0",  # no chat text in shots
]


def at(seconds, command):
    """Run a console command this many seconds after the client starts.

    Uses the engine's "delay" command instead of "wait" chains: a long run of
    waits holds up the whole command buffer, including the commands the server
    sends while the client connects, so the client would never finish joining.
    """
    return f'delay {int(seconds * 1000)} "{command}"'


def alice_script():
    """Alice joins, then takes three shots: Bob away, Bob in front, Bob after walking."""
    return [
        f"connect 127.0.0.1:{SERVER_PORT}",
        at(14, f"teleport {ALICE_SPOT}"),       # both players are in by now; Bob is at his hidden spot
        at(17, "screenshot png"),               # shot 0: Bob not in view
        at(23, "screenshot png"),               # shot 1: Bob in front of Alice (he arrives at 20 s)
        at(24, f"say {ALICE_MESSAGE}"),
        at(29, "screenshot png"),               # shot 2: Bob after walking back (26-27 s)
        at(34, "quit"),                         # no "disconnect": it prints an ERROR line by design
    ]


def bob_script():
    """Bob joins, hides, steps in front of Alice, talks, then walks backwards."""
    return [
        f"connect 127.0.0.1:{SERVER_PORT}",
        at(14, f"teleport {BOB_HIDDEN_SPOT}"),
        at(20, f"teleport {BOB_IN_VIEW_SPOT}"),
        at(25, f"say {BOB_MESSAGE}"),
        at(26, "+back"),
        at(27, "-back"),
        at(30, "screenshot png"),               # Bob's view of Alice
        at(34, "quit"),
    ]


def write_script(home, name, lines):
    game_dir = home / ".optech2" / "baseq2"
    game_dir.mkdir(parents=True, exist_ok=True)
    (game_dir / f"{name}.cfg").write_text("\n".join(lines) + "\n")
    return game_dir


def start(command, home, log_path, extra_environment=None):
    environment = dict(os.environ, HOME=str(home), **(extra_environment or {}))
    log_file = open(log_path, "w")
    process = subprocess.Popen(["stdbuf", "-oL", *command], env=environment,
                               stdout=log_file, stderr=subprocess.STDOUT)
    return process, log_file


def changed_pixels(path_a, path_b):
    """Number of pixels that differ beyond the tolerance between two screenshots."""
    image_a = Image.open(path_a).convert("RGB")
    image_b = Image.open(path_b).convert("RGB")
    difference = ImageChops.difference(image_a, image_b).convert("L")
    return difference.point(lambda value: 255 if value > PIXEL_TOLERANCE else 0).histogram()[255]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", type=Path, default=DEFAULT_DATA_DIR, help="folder holding the stock baseq2/")
    parser.add_argument("--tickrate", type=int, default=60, help="server frames per second (sv_tickrate, default 60)")
    arguments = parser.parse_args()

    for binary in (CLIENT_BINARY, SERVER_BINARY):
        if not binary.exists():
            sys.exit(f"Missing {binary}. Build first: cmake --workflow --preset default")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    results = []  # (passed, description)
    with tempfile.TemporaryDirectory(prefix="optech2-mp-") as temp_folder:
        temp_root = Path(temp_folder)
        homes = {name: temp_root / name for name in ("server", "alice", "bob")}
        for home in homes.values():
            (home / ".optech2").mkdir(parents=True)

        server, server_log = start(
            [str(SERVER_BINARY), "-datadir", str(arguments.datadir),
             "+set", "port", str(SERVER_PORT), "+set", "public", "0", "+set", "deathmatch", "1",
             "+set", "cheats", "1", "+set", "maxclients", "4", "+set", "sv_tickrate", str(arguments.tickrate),
             "+set", "dmflags", "3",  # no items or health: rotating pickups would change the shots "+set", "timelimit", "0", "+set", "fraglimit", "0",
             "+map", MAP_NAME],
            homes["server"], OUTPUT_DIR / "server.log")
        time.sleep(3)  # let the server load the map before clients connect

        hidden_window = {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
        clients = {}
        for name, script in (("alice", alice_script()), ("bob", bob_script())):
            game_dir = write_script(homes[name], name, script)
            process, log_file = start(
                [str(CLIENT_BINARY), "-datadir", str(arguments.datadir), *CLIENT_SETTINGS,
                 "+set", "name", name, "+exec", f"{name}.cfg"],
                homes[name], OUTPUT_DIR / f"{name}.log", hidden_window)
            clients[name] = (process, log_file, game_dir)

        for name, (process, log_file, _game_dir) in clients.items():
            try:
                process.wait(timeout=GAME_TIMEOUT_SECONDS)
            except subprocess.TimeoutExpired:
                process.kill()
                results.append((False, f"{name} did not finish within {GAME_TIMEOUT_SECONDS} s"))
            log_file.close()
        server.terminate()
        server.wait(timeout=10)
        server_log.close()

        shots = {}
        for name, (_process, _log_file, game_dir) in clients.items():
            shots[name] = sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []
            for index, shot in enumerate(shots[name]):
                shutil.copy(shot, OUTPUT_DIR / f"{name}_{index}.png")

    logs = {name: (OUTPUT_DIR / f"{name}.log").read_text(errors="replace") for name in ("server", "alice", "bob")}

    # 1. both players joined
    for name in ("alice", "bob"):
        results.append((f"{name} connected" in logs["server"], f"server: {name} connected"))

    # 2. chat both ways
    results.append((f"bob: {BOB_MESSAGE}" in logs["alice"], "alice received bob's chat"))
    results.append((f"alice: {ALICE_MESSAGE}" in logs["bob"], "bob received alice's chat"))

    # 3 + 4. Alice sees Bob arrive, then sees him move
    alice_shots = [OUTPUT_DIR / f"alice_{index}.png" for index in range(3)]
    if all(path.exists() for path in alice_shots):
        arrived = changed_pixels(alice_shots[0], alice_shots[1])
        moved = changed_pixels(alice_shots[1], alice_shots[2])
        results.append((arrived > MIN_CHANGED_PIXELS, f"alice sees bob arrive ({arrived} pixels changed)"))
        results.append((moved > MIN_CHANGED_PIXELS, f"alice sees bob move ({moved} pixels changed)"))
    else:
        results.append((False, f"alice took {len(shots.get('alice', []))} of 3 screenshots"))
    results.append((len(shots.get("bob", [])) == 1, "bob took his screenshot"))

    # 5. no network trouble
    for name, text in logs.items():
        problems = [line.strip() for line in text.splitlines()
                    if any(sign in line for sign in WARNING_SIGNS) and "coodinates error" not in line]
        results.append((not problems, f"{name}: no network warnings" + (f" (first: {problems[0]})" if problems else "")))

    for passed, description in results:
        print(f"{'ok     ' if passed else 'FAIL   '} {description}")
    failures = sum(1 for passed, _ in results if not passed)
    print(f"\n{failures} failed. Logs and screenshots in {OUTPUT_DIR}" if failures
          else f"\nMultiplayer check passed. Logs and screenshots in {OUTPUT_DIR}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
