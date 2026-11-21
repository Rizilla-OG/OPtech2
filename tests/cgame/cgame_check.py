#!/usr/bin/env python3
"""Client game module check for OpenTech2: builds tests/cgame/test_cgame.c (a
small module with the re-release cgame API 2022), loads it with cl_cgame on
base1 and checks what the module reports about the engine's imports:
  - it loads, Init runs, DrawHUD is called every frame, Shutdown on quit,
  - tick rate and frame time,
  - a cvar it creates: the integer field follows the value and a change is
    counted (the re-release cvar_t layout),
  - the map name through the re-release configstring numbering,
  - health from the player state, frame valid, server frame, client time,
  - a picture's size, a key binding,
  - the HUD rect in HUD units at r_hudscale 2 (pixels / scale),
  - Localize of "$key" texts with arguments (with the re-release data).
The screenshot (module HUD instead of the built-in one) lands in
build/cgame-check/ and docs/screenshots/.

Usage:
  cgame_check.py
Needs a C compiler (cc). Hidden software renderer, temporary HOME.
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
MODULE_SOURCE = REPO_DIR / "tests" / "cgame" / "test_cgame.c"
OUTPUT_DIR = REPO_DIR / "build" / "cgame-check"
DOC_SHOT = REPO_DIR / "docs" / "screenshots" / "m2_test_cgame_hud.png"
DEFAULT_DATA_DIR = Path.home() / ".local/share/Steam/steamapps/common/Quake 2"
RERELEASE_DATA_DIR = DEFAULT_DATA_DIR / "rerelease"  # has localization/ (Q2Game.kpf); used when present


def main():
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)
    module = OUTPUT_DIR / "test_cgame.so"
    build = subprocess.run(["cc", "-shared", "-fPIC", "-Wall", f"-I{REPO_DIR}", '-DYQ2OSTYPE="Linux"',
                            '-DYQ2ARCH="x86_64"', "-o", str(module), str(MODULE_SOURCE)], capture_output=True, text=True)
    if build.returncode != 0:
        sys.exit(f"module build failed:\n{build.stderr}")

    results = []

    def report(passed, text):
        results.append(passed)
        print(f"{'ok' if passed else 'FAIL':<6}{text}")

    with tempfile.TemporaryDirectory(prefix="optech2-cgame-") as home:
        game_dir = Path(home) / ".opentech2" / "baseq2"
        game_dir.mkdir(parents=True)
        (game_dir / "c.cfg").write_text('map base1\ndelay 6000 "screenshot png"\ndelay 7000 "quit"\n')
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        log_path = OUTPUT_DIR / "client.log"
        with open(log_path, "w") as log_file:
            data_dir = RERELEASE_DATA_DIR if (RERELEASE_DATA_DIR / "Q2Game.kpf").exists() else DEFAULT_DATA_DIR
            subprocess.run(["stdbuf", "-oL", str(CLIENT_BINARY), "-datadir", str(data_dir),
                            "+set", "vid_renderer", "soft", "+set", "r_vsync", "0", "+set", "s_initsound", "0", "+set", "r_hudscale", "2",
                            "+set", "cl_cgame", str(module), "+exec", "c.cfg"],
                           env=environment, stdout=log_file, stderr=subprocess.STDOUT, timeout=60, check=False)
        shots = sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []
        if shots:
            shutil.copy(shots[0], OUTPUT_DIR / "hud.png")
            DOC_SHOT.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(shots[0], DOC_SHOT)

    log = log_path.read_text(errors="replace")
    report("client game: loaded (cgame API 2022)" in log, "module loaded through cl_cgame")

    init = re.search(r"cgtest init: tick_rate (\d+) frame_time_ms (\d+) cvar integer (-?\d+) value (\S+) change counted (\w+)", log)
    if init:
        report(init.group(1) == "60" and init.group(2) == "16", f"tick rate {init.group(1)}, frame time {init.group(2)} ms")
        report(init.group(3) == "43" and init.group(4) == "43",
               f"cvar integer {init.group(3)} follows value {init.group(4)} (re-release cvar_t layout)")
        report(init.group(5) == "yes", "cvar change counted (modified_count)")
    else:
        report(False, "Init ran (no 'cgtest init' line)")

    localized = re.search(r"cgtest localize: '([^']*)' \| '([^']*)'", log)
    if (RERELEASE_DATA_DIR / "Q2Game.kpf").exists():
        report(bool(localized) and localized.group(1) == "Primary Objective" and "Tester" in localized.group(2)
               and "$" not in localized.group(2),
               f"Localize: {localized.groups() if localized else 'no line'}")
    hud = re.search(r"cgtest hud: map '([^']*)' health (-?\d+) frame_valid (\d) server_frame (\d+) client_time (\d+) "
                    r"scale (\d+) screen (\d+)x(\d+) playernum (-?\d+) pic i_health (\d+)x(\d+) registered (\d) "
                    r"attack key '([^']*)'", log)
    if hud:
        report(hud.group(1) == "Outer Base", f"map name '{hud.group(1)}' through re-release configstring 0")
        report(hud.group(2) == "100", f"health {hud.group(2)} from the player state")
        report(hud.group(3) == "1" and int(hud.group(4)) > 0 and int(hud.group(5)) > 0,
               f"frame valid, server frame {hud.group(4)}, client time {hud.group(5)} ms")
        report(hud.group(10) == "24" and hud.group(12) == "1", f"picture i_health {hud.group(10)}x{hud.group(11)}")
        report(hud.group(13) != "", f"+attack bound to '{hud.group(13)}'")
        # hud_vrect is in HUD units (pixels / scale), as the re-release passes it
        shot_size = None
        if (OUTPUT_DIR / "hud.png").exists():
            png = (OUTPUT_DIR / "hud.png").read_bytes()
            shot_size = (int.from_bytes(png[16:20], "big"), int.from_bytes(png[20:24], "big"))
        scale = int(hud.group(6))
        rect = (int(hud.group(7)), int(hud.group(8)))
        report(scale == 2 and shot_size is not None and rect == (shot_size[0] // scale, shot_size[1] // scale),
               f"scale {scale}, HUD rect {rect[0]}x{rect[1]} (screen {shot_size} / scale)")
    else:
        report(False, "DrawHUD ran (no 'cgtest hud' line)")

    draws = re.search(r"cgtest shutdown after (\d+) HUD draws", log)
    report(bool(draws) and int(draws.group(1)) > 100, f"Shutdown ran after {draws.group(1) if draws else 0} HUD draws")
    errors = [line for line in log.splitlines() if "ERROR" in line]
    report(not errors, "no errors" + (f" ({errors[0].strip()})" if errors else ""))

    passed = all(results)
    print(f"\nClient game check {'passed' if passed else 'FAILED'}. Log and screenshot in {OUTPUT_DIR}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
