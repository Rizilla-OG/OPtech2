#!/usr/bin/env python3
"""Mod folder stack check for OpenTech2 (M2: several mod folders on top of
each other, in a set order).

Makes mod folders modA, modB and modC in a temporary home folder, each with
its own copy of layer.cfg (and modA one file only it has), then runs the
dedicated server with different fs_mods / game settings and checks:
  - a later folder in fs_mods overrides an earlier one, the game folder
    overrides them all, and files only in a lower folder are still found,
  - configs are written to the game folder (or baseq2), never a mod folder,
  - the game library comes from the highest folder that has one,
  - paths, baseq2, the game folder and duplicates in fs_mods are skipped,
  - changing fs_mods in the console rebuilds the stack,
  - a player joining a server gets the server's fs_mods.

Usage:
  stack_check.py
Needs build/release/OpenTech2-server and the game data.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
SERVER_BINARY = REPO_DIR / "build" / "release" / "OpenTech2-server"
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
GAME_LIBRARY = REPO_DIR / "build" / "release" / "baseq2" / "game.so"
OUTPUT_DIR = REPO_DIR / "build" / "stack-check"
# the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))


def make_mods(home):
    user_dir = Path(home) / ".opentech2"
    for name in ("modA", "modB", "modC"):
        folder = user_dir / name
        folder.mkdir(parents=True)
        (folder / "layer.cfg").write_text(f'echo "layer from {name}"\n')
    (user_dir / "modA" / "onlya.cfg").write_text('echo "only in modA"\n')
    shutil.copy(GAME_LIBRARY, user_dir / "modB" / "game.so")
    return user_dir


def run(name, arguments, commands):
    """Runs the dedicated server in a fresh home; returns its log."""
    with tempfile.TemporaryDirectory(prefix="opentech2-stack-") as home:
        user_dir = make_mods(home)
        (user_dir / "baseq2").mkdir()
        (user_dir / "baseq2" / "stacktest.cfg").write_text("\n".join(commands + ["quit"]) + "\n")
        console = ["+set", "developer", "1", "+exec", "stacktest.cfg"]  # developer: the library's path
        environment = dict(os.environ, HOME=home)
        server = subprocess.Popen(["stdbuf", "-oL", str(SERVER_BINARY), "-datadir", str(DATA_DIR)] + arguments + console,
                                  env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True)
        try:
            output, _ = server.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            server.kill()
            output, _ = server.communicate()
            output += "\nERROR: the server didn't quit\n"
        log = output.replace(home, "~")
    (OUTPUT_DIR / f"{name}.log").write_text(log)
    return log


def joined_client_log():
    """A client (no fs_mods of its own) joins a dedicated server running fs_mods modB."""
    with tempfile.TemporaryDirectory(prefix="opentech2-stack-") as home:
        user_dir = make_mods(home)
        (user_dir / "baseq2").mkdir()
        (user_dir / "baseq2" / "joined.cfg").write_text(
            'connect 127.0.0.1:27971\ndelay 9000 "exec layer.cfg"\ndelay 9500 quit\n')
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        server = subprocess.Popen(["stdbuf", "-oL", str(SERVER_BINARY), "-datadir", str(DATA_DIR), "+set", "port",
                                   "27971", "+set", "fs_mods", "modB", "+map", "base1"], env=environment,
                                  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            client = subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set",
                                     "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "s_initsound", "0",
                                     "+set", "port", "27972", "+exec", "joined.cfg"],
                                    env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=60, check=False)
            log = client.stdout + client.stderr
        except subprocess.TimeoutExpired as timeout:
            log = (timeout.stdout or b"").decode(errors="replace") + "\nERROR: the client didn't quit\n"
        finally:
            server.kill()
            server.wait()
    (OUTPUT_DIR / "joined-client.log").write_text(log)
    return log


def main():
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    log = run("stack", ["+set", "fs_mods", "modA,modB"], ["exec layer.cfg", "exec onlya.cfg", "path", "map base1"])
    report("layer from modB" in log and "layer from modA" not in log, "fs_mods \"modA,modB\": modB's file wins")
    report("only in modA" in log, "a file only in the lower folder (modA) is still found")
    report(re.search(r"Using '~/\.opentech2/baseq2' for writing", log) is not None,
           "no game folder: writes go to baseq2, not a mod folder")
    report(re.search(r"Loading library: ~/\.opentech2/modB/game\.so", log) is not None,
           "game library from the highest folder that has one (modB)")
    order = [folder for folder in re.findall(r"~/\.opentech2/(\w+)\n", log) if folder in ("modA", "modB", "baseq2")]
    report(order[:3] == ["modB", "modA", "baseq2"], f"path order, first searched first: {' > '.join(order[:3])}")

    log = run("game-on-top", ["+set", "fs_mods", "modA,modB", "+set", "game", "modC"], ["exec layer.cfg"])
    report("layer from modC" in log and "layer from modB" not in log, "game modC on top of fs_mods \"modA,modB\"")
    report(re.search(r"Using '~/\.opentech2/modC' for writing", log) is not None, "writes go to the game folder")

    # (";" also separates, but a server info cvar can't hold one)
    log = run("bad-names", [], ["set fs_mods \"../evil modA,modA baseq2 /tmp\"", "exec layer.cfg"])
    report("skipping \"../evil\"" in log and "skipping \"/tmp\"" in log, "paths in fs_mods are skipped")
    report(len(re.findall(r"Mod folder \d+:", log)) == 1 and "layer from modA" in log,
           "baseq2 and a repeated folder are skipped: one layer (modA)")

    log = run("change", [], ["exec layer.cfg", "set fs_mods modB", "exec layer.cfg", "set fs_mods \"\"",
                             "exec onlya.cfg"])
    report("layer from modB" in log and log.count("layer from") == 1,
           "set fs_mods in the console: modB's file found, none before")
    report("only in modA" not in log, "set fs_mods \"\": back to baseq2 only")

    log = joined_client_log()
    report(re.search(r"Mod folder 1: modB", log) is not None and "layer from modB" in log,
           "a player joining a server with fs_mods modB gets modB too")

    errors = [line for line in (OUTPUT_DIR / name for name in ("stack.log", "game-on-top.log"))
              for line in line.read_text().splitlines() if "ERROR" in line]
    report(not errors, "no errors" + (f" ({errors[0]})" if errors else ""))

    passed = all(results)
    print(f"\nMod stack check {'passed' if passed else 'FAILED'}. Logs in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
