#!/usr/bin/env python3
"""Data menu check for OpenTech2 (optech2: menus/<name>.menu pages).

A mod folder brings two pages:
  menus/test.menu      a slider, a choice, an action, a field and a label
  menus/options.menu   replaces the stock options menu
The game starts with no map, opens the pages from the console and drives
them with "presskey" (developer 1), then prints the cvars they changed:
  1. "menu test" draws the page (a shot differs from the empty screen)
  2. right arrow x3 on the slider: menutest_slider 3
  3. down, right on the choice: crosshair 1, and its command ran
  4. down, enter on the action: its command ran (menutest_action 1)
  5. a field takes the cvar's value and gives it back when the page closes
  6. "menu_options" opens the data page (same shot as "menu options"),
     "menu_builtin options" the stock one (a different shot)
  7. "menu nothere" says there is no such page; no errors in the logs

Usage: datamenu_check.py   (exit 1 on failure)
Logs and shots land in build/datamenu-check/.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO_DIR = Path(__file__).resolve().parent.parent.parent
CLIENT_BINARY = REPO_DIR / "build" / "release" / "OpenTech2"
OUTPUT_DIR = REPO_DIR / "build" / "datamenu-check"
# optech2: the data set: run_all.py --data sets OPTECH2_DATA_DIR (classic by default)
DATA_DIR = Path(os.environ.get("OPTECH2_DATA_DIR", Path.home() / ".local/share/Steam/steamapps/common/Quake 2"))

TEST_PAGE = """
// a page for the check
{
"title" "Menu test"
}
{
"type" "slider"
"label" "test slider"
"cvar" "menutest_slider"
"min" "0" "max" "10" "step" "1" "format" "%.0f"
}
{
"type" "choice"
"label" "crosshair"
"cvar" "crosshair"
"options" "none=0;cross=1;dot=2"
"command" "set menutest_choice_command 1"
}
{
"type" "action"
"label" "run the action"
"command" "set menutest_action 1"
"status" "sets menutest_action"
}
{
"type" "field"
"label" "name"
"cvar" "menutest_name"
"length" "16"
}
{
"type" "label"
"label" "a label"
"gap" "10"
}
"""

OPTIONS_PAGE = """
{
"banner" "m_banner_options"
}
{
"type" "toggle"
"label" "always run"
"cvar" "cl_run"
}
{
"type" "action"
"label" "customize controls"
"command" "menu_keys"
}
"""

results = []


def report(passed, text):
    results.append(passed)
    print(f"{'ok' if passed else 'FAIL':<6}{text}")


def run(name, script):
    """Runs the console script in the test mod; returns (log, [shots as arrays])."""
    with tempfile.TemporaryDirectory(prefix="optech2-datamenu-") as home:
        mod_dir = Path(home) / ".opentech2" / "menutest"
        (mod_dir / "menus").mkdir(parents=True)
        (mod_dir / "menus" / "test.menu").write_text(TEST_PAGE)
        (mod_dir / "menus" / "options.menu").write_text(OPTIONS_PAGE)
        game_dir = Path(home) / ".opentech2" / "baseq2"
        game_dir.mkdir(parents=True)
        lines = ["set developer 1"]
        for step, command in enumerate(script):
            lines.append(f'delay {1000 + step * 100} "{command}"')
        lines.append(f'delay {1000 + len(script) * 100 + 300} "quit"')
        (game_dir / "m.cfg").write_text("\n".join(lines) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        result = subprocess.run([str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set", "fs_mods", "menutest",
                                 "+set", "fixedtime", "16667", "+set", "vid_renderer", "soft", "+set", "r_mode", "4",
                                 "+set", "r_vsync", "0", "+set", "s_initsound", "0", "+exec", "m.cfg"],
                                env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                timeout=120, check=False)
        log = result.stdout + result.stderr
        (OUTPUT_DIR / f"{name}.log").write_text(log)
        shots = []
        for index, shot in enumerate(sorted((game_dir / "scrnshot").glob("*.png")) if (game_dir / "scrnshot").exists() else []):
            shutil.copy(shot, OUTPUT_DIR / f"{name}-{index}.png")
            shots.append(np.asarray(Image.open(shot).convert("RGB"), dtype=int))
        return log, shots


def cvar(log, name):
    found = re.findall(rf'"{name}" is "([^"]*)"', log)
    return found[-1] if found else None


def differing(first, second):
    return int((np.abs(first - second).sum(axis=2) > 0).sum())


def main():
    if not CLIENT_BINARY.exists():
        sys.exit(f"{CLIENT_BINARY} missing; build first")
    if OUTPUT_DIR.exists():
        shutil.rmtree(OUTPUT_DIR)
    OUTPUT_DIR.mkdir(parents=True)

    log, shots = run("drive", [
        "set crosshair 0", "set menutest_slider 0", "set menutest_name Tester",
        "screenshot png", "menu test", "screenshot png",
        "presskey rightarrow", "presskey rightarrow", "presskey rightarrow",
        "presskey downarrow", "presskey rightarrow",
        "presskey downarrow", "presskey enter",
        "set menutest_name Changed", "presskey escape",
        "menutest_slider", "crosshair", "menutest_choice_command", "menutest_action", "menutest_name",
        "menu nothere",
    ])
    if len(shots) < 2:
        report(False, "page drawn: a screenshot is missing (see drive.log)")
    else:
        report(differing(shots[0], shots[1]) > 500, f"page drawn ({differing(shots[0], shots[1])} pixels are the menu's)")
    report(cvar(log, "menutest_slider") == "3", f"slider: right x3 sets menutest_slider 3 (is {cvar(log, 'menutest_slider')})")
    report(cvar(log, "crosshair") == "1" and cvar(log, "menutest_choice_command") == "1",
           f"choice: right sets crosshair 1 and runs its command (crosshair {cvar(log, 'crosshair')}, "
           f"command {cvar(log, 'menutest_choice_command')})")
    report(cvar(log, "menutest_action") == "1", f"action: enter runs its command (menutest_action {cvar(log, 'menutest_action')})")
    # optech2: the field held "Tester" from when the page opened and commits it on close, over the console's "Changed"
    report(cvar(log, "menutest_name") == "Tester",
           f"field: keeps the cvar's value and commits it on close (menutest_name {cvar(log, 'menutest_name')})")
    report("No menu page menus/nothere.menu" in log, "a missing page is reported")

    # optech2: shots 1.5 s apart: the menu cursor animates in 15 frames of 100 ms, so they catch the same frame
    filler = ["echo"] * 14
    _, override_shots = run("override", [
        "menu options", "clear", "screenshot png", "presskey escape", "menu_options"] + filler[:11] + [
        "clear", "screenshot png", "presskey escape", "menu_builtin options"] + filler[:11] + [
        "clear", "screenshot png",
    ])
    if len(override_shots) < 3:
        report(False, "options override: a screenshot is missing (see override.log)")
    else:
        data, stock_command, builtin = override_shots
        report(differing(data, stock_command) == 0,
               f"menu_options opens menus/options.menu ({differing(data, stock_command)} pixels differ)")
        report(differing(data, builtin) > 500,
               f"menu_builtin options opens the stock menu ({differing(data, builtin)} pixels differ)")

    errors = [line for line in log.splitlines() if re.search(r"error|unknown item|expected \{|missing", line, re.I)]
    report(not errors, f"no errors in the logs{': ' + errors[0] if errors else ''}")

    print(f"\n{'PASS' if all(results) else 'FAIL'}: {sum(results)}/{len(results)} data menu checks")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
