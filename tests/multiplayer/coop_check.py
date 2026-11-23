#!/usr/bin/env python3
"""Coop squad respawn check for OpenTech2 (our game, as the re-release's
g_coop_squad_respawn): a dedicated coop server on base1 and two hidden
clients. Alice walks away from the start and stays out of combat
(notarget); Bob kills himself. Checks that:
  1. both players join,
  2. Bob comes back about 3 s later next to Alice (within 256 units of
     her), not at the start,
  3. no errors.
Every process gets its own temporary HOME. Logs land in build/coop-check/.

Usage:
  coop_check.py
"""

import math
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mp_check  # noqa: E402  (shares binaries, settings and helpers)

OUTPUT_DIR = mp_check.REPO_DIR / "build" / "coop-check"
SERVER_PORT = 27998
ALICE_SPOT = (-99, -87, 24)  # base1, out of the start room's spawn spots
START = (128, -320)


def at(seconds, command):
    return mp_check.at(seconds, command)


def main():
    for binary in (mp_check.CLIENT_BINARY, mp_check.SERVER_BINARY):
        if not binary.exists():
            sys.exit(f"Missing {binary}. Build first: cmake --workflow --preset default")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    alice = [f"connect 127.0.0.1:{SERVER_PORT}", at(10, "notarget"),
             at(11, f"teleport {ALICE_SPOT[0]} {ALICE_SPOT[1]} {ALICE_SPOT[2]} 0 90 0"),
             at(30, "quit")]
    bob = [f"connect 127.0.0.1:{SERVER_PORT}", at(10, "notarget"), at(16, "kill"),
           at(17, "cmd playerinfo"), at(24, "cmd playerinfo"), at(30, "quit")]

    with tempfile.TemporaryDirectory(prefix="optech2-coop-") as temp_folder:
        temp_root = Path(temp_folder)
        homes = {name: temp_root / name for name in ("server", "alice", "bob")}
        for home in homes.values():
            (home / ".opentech2").mkdir(parents=True)

        server, server_log = mp_check.start(
            [str(mp_check.SERVER_BINARY), "-datadir", str(mp_check.DEFAULT_DATA_DIR),
             "+set", "port", str(SERVER_PORT), "+set", "public", "0", "+set", "coop", "1",
             "+set", "deathmatch", "0", "+set", "cheats", "1", "+set", "maxclients", "4", "+map", "base1"],
            homes["server"], OUTPUT_DIR / "server.log")
        import time
        time.sleep(3)

        hidden_window = {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
        clients = {}
        for name, script in (("alice", alice), ("bob", bob)):
            game_dir = mp_check.write_script(homes[name], name, script)
            process, log_file = mp_check.start(
                [str(mp_check.CLIENT_BINARY), "-datadir", str(mp_check.DEFAULT_DATA_DIR), *mp_check.CLIENT_SETTINGS,
                 "+set", "name", name, "+exec", f"{name}.cfg"],
                homes[name], OUTPUT_DIR / f"{name}.log", hidden_window)
            clients[name] = (process, log_file)

        for name, (process, log_file) in clients.items():
            try:
                process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                process.kill()
                results.append((False, f"{name} did not finish"))
            log_file.close()
        server.terminate()
        server.wait(timeout=10)
        server_log.close()

    logs = {name: (OUTPUT_DIR / f"{name}.log").read_text(errors="replace") for name in ("server", "alice", "bob")}

    for name in ("alice", "bob"):
        results.append((f"{name} entered the game" in logs["server"], f"{name} joined"))

    infos = re.findall(r"playerinfo time [\d.]+ health (-?\d+) ammo -?\d+ origin (-?[\d.]+) (-?[\d.]+)", logs["bob"])
    if len(infos) >= 2:
        dead_health = int(infos[0][0])
        health, x, y = int(infos[1][0]), float(infos[1][1]), float(infos[1][2])
        near_alice = math.hypot(x - ALICE_SPOT[0], y - ALICE_SPOT[1])
        at_start = math.hypot(x - START[0], y - START[1])
        results.append((dead_health <= 0, f"bob died (health {dead_health})"))
        results.append((health > 0 and near_alice < 256 and at_start > 64,
                        f"bob came back next to alice: {near_alice:.0f} units from her, {at_start:.0f} from the start"))
    else:
        results.append((False, f"bob's positions logged ({len(infos)})"))

    bad = [line for log in logs.values() for line in log.splitlines() if "ERROR" in line or "Game Error" in line]
    results.append((not bad, "no errors" + (f" ({bad[0].strip()[:90]})" if bad else "")))

    for passed, text in results:
        print(f"{'ok' if passed else 'FAIL':<6}{text}")
    passed = all(passed for passed, _ in results)
    print(f"\nCoop check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
