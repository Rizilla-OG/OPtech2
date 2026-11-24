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
  8. optech2: a page with a script (luatest.lua): its open hook runs, a list
     makes items whose action functions and commands run, "if lua:" leaves
     an item out, an endless loop is stopped and the page carries on, and
     a script with a syntax error is reported while its page still opens
  9. optech2: ui_menustyle -1 picks the look by data set, and the stock C
     menus take the re-release look too (key bindings)
     optech2: the re-release style (ui_menustyle 1) looks different, and the
     mouse works it: menu_pointer + a click steps a choice and sets a slider
     from the pointer, right click goes back, a click on a key binding row
     binds the next key
 12. optech2: the load and save pages (saves.lua): save to a slot, turn the
     page, delete with del then y; they look like the stock menus
 11. optech2: an open page outlives more pages than there are slots, and
     reopening it drops back to it (it is at the bottom of the stack); list
     tables with hostile __len / __index neither abort nor hang the game
 10. optech2: the shipped pages (baseq2/menus: options, multiplayer, game,
     download options, deathmatch flags)
     draw exactly as the stock menus they replace (menu_builtin), and their
     choices show the cvars' values (crosshair colour from three cvars,
     invert mouse from m_pitch's sign)

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

# optech2: a page with a script (menus/luatest.lua): a list, Lua actions, "if lua:", an open hook
LUA_PAGE = """
{
"title" "Lua test"
"open" "on_open"
}
{
"type" "list"
"source" "entries"
}
{
"type" "action"
"label" "hidden"
"command" "set luatest_hidden 1"
"if" "lua:hidden"
}
{
"type" "action"
"label" "endless loop"
"lua" "runaway"
}
{
"type" "action"
"label" "after the loop"
"lua" "after"
"if" "lua:shown"
}
"""

LUA_SCRIPT = """
-- optech2: the check's page script
function on_open()
	menu.set("luatest_open", "1")
end

function entries()
	return {
		{ label = "first", action = function() menu.set("luatest_action", "first") end },
		{ label = "second", command = "set luatest_command second" },
	}
end

function shown() return true end
function hidden() return false end
function runaway() while true do end end

function after()
	menu.set("luatest_after", menu.value("luatest_after") + 1)
end
"""

BROKEN_PAGE = """
{
"title" "Broken script"
}
{
"type" "label"
"label" "the page still opens"
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

# optech2: many small pages, more than the 16 page slots, to check an open page is never reused
SLOT_PAGES = 18
SLOT_PAGE = """
{{
"title" "Slot page {index}"
}}
{{
"type" "action"
"label" "slot {index}"
"command" "set slot_page {index}"
}}
"""

# optech2: a list function returning tables with hostile metamethods (outside any pcall before)
META_PAGE = """
{
"title" "Metamethods"
}
{
"type" "list"
"source" "bad_length"
}
{
"type" "list"
"source" "bad_entry"
}
{
"type" "action"
"label" "still here"
"command" "set metatest_alive 1"
}
"""

META_SCRIPT = """
function bad_length()
	return setmetatable({}, { __len = function() error("length") end })
end

function bad_entry()
	return { setmetatable({}, { __index = function() while true do end end }) }
end
"""

# optech2: the shipped pages compared with the stock menus they replace
SHIPPED_PAGES = ("options", "multiplayer", "game", "downloadoptions", "dmoptions", "keys", "multiplayer_keys",
                 "buttons", "altbuttons", "addressbook", "mods", "joy", "sticks", "gyro")

results = []


def report(passed, text):
    results.append(passed)
    print(f"{'ok' if passed else 'FAIL':<6}{text}")


def run(name, script, mods=True, style=0):
    """Runs the console script in the test mod; returns (log, [shots as arrays])."""
    with tempfile.TemporaryDirectory(prefix="optech2-datamenu-") as home:
        mod_dir = Path(home) / ".opentech2" / "menutest"
        (mod_dir / "menus").mkdir(parents=True)
        (mod_dir / "menus" / "test.menu").write_text(TEST_PAGE)
        (mod_dir / "menus" / "options.menu").write_text(OPTIONS_PAGE)
        (mod_dir / "menus" / "luatest.menu").write_text(LUA_PAGE)
        (mod_dir / "menus" / "luatest.lua").write_text(LUA_SCRIPT)
        (mod_dir / "menus" / "broken.menu").write_text(BROKEN_PAGE)
        (mod_dir / "menus" / "broken.lua").write_text("this is not lua (\n")
        (mod_dir / "menus" / "metatest.menu").write_text(META_PAGE)
        (mod_dir / "menus" / "metatest.lua").write_text(META_SCRIPT)
        for index in range(SLOT_PAGES):
            (mod_dir / "menus" / f"slot{index}.menu").write_text(SLOT_PAGE.format(index=index))
        game_dir = Path(home) / ".opentech2" / "baseq2"
        game_dir.mkdir(parents=True)
        # optech2: ui_menustyle 0 (classic) for the pixel comparisons with the stock menus
        lines = ["set developer 1", f"set ui_menustyle {style}"]
        for step, command in enumerate(script):
            # optech2: "#" steps only pass time: "delay" holds at most 128 commands
            if not command.startswith("#"):
                lines.append(f'delay {1000 + step * 100} "{command}"')
        lines.append(f'delay {1000 + len(script) * 100 + 300} "quit"')
        (game_dir / "m.cfg").write_text("\n".join(lines) + "\n")
        environment = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        result = subprocess.run([str(CLIENT_BINARY), "-datadir", str(DATA_DIR), "+set", "fs_mods", "menutest" if mods else "",
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
        # optech2: dmflags bits: falling damage yes -> no sets bit 8; teamplay (10th item) by skin adds 64
        "set dmflags 16656", "menu_dmoptions", "presskey leftarrow", "dmflags",
        "presskey downarrow", "presskey downarrow", "presskey downarrow", "presskey downarrow", "presskey downarrow",
        "presskey downarrow", "presskey downarrow", "presskey downarrow", "presskey downarrow", "presskey rightarrow",
        "presskey escape", "dmflags",
        # optech2: the Lua page: first (action function), second (command), the loop, after
        "menu luatest", "presskey enter", "presskey downarrow", "presskey enter",
        "presskey downarrow", "presskey enter", "presskey downarrow", "presskey enter", "presskey escape",
        "luatest_open", "luatest_action", "luatest_command", "luatest_hidden", "luatest_after",
        "menu broken", "presskey escape", "echo broken page done",
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
    dmflags = re.findall(r'"dmflags" is "([^"]*)"', log)
    report(dmflags[-2:] == ["16664", "16728"],
           f"dmflags bits: falling damage off sets 8, teamplay by skin sets 64 (dmflags {', '.join(dmflags[-2:])})")
    report(cvar(log, "luatest_open") == "1", f"Lua: the page's open hook ran (luatest_open {cvar(log, 'luatest_open')})")
    report(cvar(log, "luatest_action") == "first" and cvar(log, "luatest_command") == "second",
           f"Lua: a list's entries run their action function and command "
           f"({cvar(log, 'luatest_action')}, {cvar(log, 'luatest_command')})")
    report(cvar(log, "luatest_hidden") in (None, ""), "Lua: an item whose if lua: is false is left out")
    report("too many instructions" in log and cvar(log, "luatest_after") == "1",
           f"Lua: an endless loop is stopped and the page goes on (luatest_after {cvar(log, 'luatest_after')})")
    report("broken.lua" in log and "broken page done" in log,
           "Lua: a script with a syntax error is reported and its page still opens")
    report("No menu page menus/nothere.menu" in log, "a missing page is reported")

    # optech2: "test" stays open under 18 other pages opened and closed one by one (16 slots): it must still
    # work after; reopening the open "test" drops back to it instead of reloading it under the stack
    slot_script = ["set menutest_slider 0", "menu test"]
    for index in range(SLOT_PAGES):
        slot_script += [f"menu slot{index}", "presskey escape"]
    slot_script += ["presskey rightarrow", "menu slot0", "menu test", "presskey rightarrow", "menutest_slider",
                    "menu metatest", "set metatest_after 1", "metatest_after", "echo slots done"]
    slot_log, _ = run("slots", slot_script)
    report(cvar(slot_log, "menutest_slider") == "2",
           f"an open page outlives 18 other pages and a reopen (menutest_slider {cvar(slot_log, 'menutest_slider')})")
    report(cvar(slot_log, "metatest_after") == "1" and "slots done" in slot_log and "PANIC" not in slot_log,
           "Lua: hostile __len / __index in a list's tables neither abort nor hang the game")
    log += slot_log

    # optech2: the shipped load / save pages: enter on save3 saves there and closes the menus; the load page
    # turns its page (right, left), del then y deletes save3 (a later "load save3" finds nothing); the pages
    # line shows; the classic look matches the stock menus' (the cursor blinks, so only nearly)
    filler = ["#pause"] * 14
    saves_log, saves_shots = run("saves", ["map base1"] + filler[:10] + [
        "menu_savegame", "presskey downarrow", "presskey downarrow", "presskey downarrow", "presskey downarrow",
        "presskey enter", "#", "#", "menu_loadgame", "clear", "screenshot png", "presskey escape",
        "menu_builtin loadgame"] + filler[:11] + ["clear", "screenshot png", "presskey escape", "#",
        "menu_loadgame", "presskey rightarrow", "presskey leftarrow", "presskey downarrow", "presskey downarrow",
        "presskey downarrow", "presskey downarrow", "presskey downarrow", "presskey del", "presskey y", "#",
        "presskey escape", "load save3", "echo saves done"], mods=False)
    report("SV_CopySaveGame(current, save3)" in saves_log, "save page: enter on a slot saves there")
    report("No such savegame" in saves_log and "saves done" in saves_log,
           "load page: del then y deletes the save; left and right turn the page")
    if len(saves_shots) < 2:
        report(False, "load page: a screenshot is missing (see saves.log)")
    else:
        report(differing(saves_shots[0][:-40], saves_shots[1][:-40]) < 3000,
               f"load page draws as the stock menu, but for the cursor "
               f"({differing(saves_shots[0][:-40], saves_shots[1][:-40])} pixels differ)")
    log += saves_log

    # optech2: shots 1.5 s apart: the menu cursor animates in 15 frames of 100 ms, so they catch the same frame
    filler = ["#pause"] * 14
    _, override_shots = run("override", [
        "menu options", "clear", "screenshot png", "presskey escape", "menu_options"] + filler[:11] + [
        "clear", "screenshot png", "presskey escape", "menu_builtin options"] + filler[:11] + [
        "clear", "screenshot png",
    ])
    if len(override_shots) < 3:
        report(False, "options override: a screenshot is missing (see override.log)")
    else:
        # optech2: the bottom strip holds the version line, which can flicker between shots
        data, stock_command, builtin = (shot[:-40] for shot in override_shots)
        report(differing(data, stock_command) == 0,
               f"menu_options opens menus/options.menu ({differing(data, stock_command)} pixels differ)")
        report(differing(data, builtin) > 500,
               f"menu_builtin options opens the stock menu ({differing(data, builtin)} pixels differ)")

    # optech2: the re-release style at 640x480 (menu scale 1): the panel's rows start at 164 (under the
    # banner's place: 240 - 76), 14 high (slider at 164, choice at 178); the slider bar spans x 344-454
    style_log, style_shots = run("style", [
        "set crosshair 0", "set menutest_slider 0", "menu test", "clear", "screenshot png",
        "menu_pointer 320 184", "presskey mouse1", "menu_pointer 399 171", "presskey mouse1",
        "presskey mouse2", "crosshair", "menutest_slider", "echo style done",
    ], style=1)
    report(len(style_shots) == 1 and len(shots) >= 2 and differing(style_shots[0], shots[1]) > 500,
           "re-release style draws the page differently from classic")
    report(cvar(style_log, "crosshair") == "1", f"mouse: a click on a choice steps it (crosshair {cvar(style_log, 'crosshair')})")
    # optech2: the bar's place moves a little with the font's advance, so any middle value will do
    report(cvar(style_log, "menutest_slider") in ("3", "4", "5", "6", "7"),
           f"mouse: a click on the slider bar's middle sets it (menutest_slider {cvar(style_log, 'menutest_slider')})")
    report("style done" in style_log, "mouse: right click goes back without trouble")
    log += style_log

    # optech2: the shipped key binding page: enter then a key binds, escape cancels, del unbinds
    binds_log, _ = run("binds", ["unbind k", "menu_keys", "presskey downarrow", "presskey enter", "presskey k",
                                 "bind k", "presskey enter", "presskey escape", "bind k", "presskey del", "bind k"],
                       mods=False)
    bound = re.findall(r'"k" = "([^"]*)"|"k" is not bound', binds_log)
    report(bool(bound) and bound[0] == "weapnext" and bound[-1] == "",
           f"bind page: enter + key binds, del unbinds ({bound})")
    log += binds_log

    # optech2: the shipped video page: fov changes at once, vertical sync waits for apply, apply restarts
    video_log, _ = run("video", ["set r_vsync 0", "set fov 90", "menu_video"] + ["presskey downarrow"] * 4 + [
        "presskey rightarrow", "fov"] + ["presskey downarrow"] * 3 + ["presskey rightarrow", "r_vsync"] +
        ["presskey downarrow"] * 5 + ["presskey enter", "#", "#", "#", "#", "#", "r_vsync"], mods=False)
    vsync = re.findall(r'"r_vsync" is "([^"]*)"', video_log)
    report(cvar(video_log, "fov") == "91" and vsync == ["0", "1"] and video_log.count("as rendering backend") >= 2,
           f"video page: a slider changes at once, vsync waits for apply, apply restarts (fov "
           f"{cvar(video_log, 'fov')}, r_vsync {vsync})")
    log += video_log

    # optech2: the credits page scrolls as the stock credits; quit: classic look shows the stock picture,
    # n goes back, y quits (the game ends before the script's own quit)
    quit_log, quit_shots = run("quit", ["menu_quit", "clear", "screenshot png", "presskey n", "menu_builtin quit"] +
                               filler[:11] + ["clear", "screenshot png", "presskey escape", "#", "menu_credits", "clear",
                               "screenshot png", "presskey escape", "menu_builtin credits", "clear", "screenshot png",
                               "presskey escape", "echo before quit", "menu_quit", "presskey y", "#", "#",
                               "echo after quit"], mods=False)
    if len(quit_shots) < 4:
        report(False, "quit / credits: a screenshot is missing (see quit.log)")
    else:
        report(differing(quit_shots[0][:-40], quit_shots[1][:-40]) == 0, "quit page draws the stock quit picture")
        report(differing(quit_shots[2][:-40], quit_shots[3][:-40]) == 0, "credits page scrolls as the stock credits")
    report("before quit" in quit_log and "after quit" not in quit_log, "quit page: n goes back, y quits")
    log += quit_log

    # optech2: a click on a key binding row (re-release look) starts binding, as enter
    bind_log, _ = run("bind-click", ["unbind k", "menu_keys", "menu_pointer 320 171", "presskey mouse1",
                                     "presskey k", "presskey escape", "bind k"], mods=False, style=1)
    report('"k" = "+attack"' in bind_log, "mouse: a click on a key binding row binds the next key")
    log += bind_log

    # optech2: ui_menustyle -1 (the default) picks the look by data set: the re-release look on its data,
    # classic on the classic data; the stock C menus (key bindings here) take the look too
    keys_shots = {}
    for style in (-1, 0, 1):
        _, shots_for_style = run(f"keys-style{style}", ["menu_keys", "clear", "screenshot png"], mods=False, style=style)
        keys_shots[style] = shots_for_style[0][:-40] if shots_for_style else None
    if any(shot is None for shot in keys_shots.values()):
        report(False, "look by data set: a screenshot is missing (see keys-style*.log)")
    else:
        expected = 1 if DATA_DIR.name == "rerelease" else 0
        report(differing(keys_shots[-1], keys_shots[expected]) == 0,
               f"ui_menustyle -1 is the {'re-release' if expected else 'classic'} look on this data "
               f"({differing(keys_shots[-1], keys_shots[expected])} pixels differ)")
        report(differing(keys_shots[0], keys_shots[1]) > 500,
               f"a stock C menu (key bindings) takes the re-release look ({differing(keys_shots[0], keys_shots[1])} pixels)")

    # optech2: the shipped pages against the stock menus, shots 1.5 s apart (same cursor frame)
    # optech2: crosshair 0: "none" in both lists (the re-release's data names its own 29; stock's names are classic)
    shipped = ["set crosshair 0", "set crosshair_color_r 0", "set crosshair_color_g 1", "set crosshair_color_b 1",
               "set m_pitch -0.022", "set dmflags 16656"]  # optech2: some bits on, teamplay by skin
    for page in SHIPPED_PAGES:
        shipped += [f"menu_{page}", "clear", "screenshot png", "presskey escape", f"menu_builtin {page}"] + filler[:11] + [
                    "clear", "screenshot png", "presskey escape"] + filler[:12]
    # optech2: with several episodes (the re-release data) enter on the game page's first line asks for the
    # skill: the same skill menu as the stock page's (on the classic data it would start a game)
    episodes = DATA_DIR.name == "rerelease"
    if episodes:
        shipped += ["menu_game", "presskey enter", "clear", "screenshot png", "presskey escape", "presskey escape",
                    "menu_builtin game", "presskey enter"] + filler[:9] + ["clear", "screenshot png",
                    "presskey escape", "presskey escape"]
    # optech2: in two runs, each under the run's time limit
    pages_per_step = [f"menu_{page}" for page in SHIPPED_PAGES]
    split = shipped.index(pages_per_step[7])
    first_log, first_shots = run("shipped", shipped[:split], mods=False)
    second_log, second_shots = run("shipped2", shipped[:6] + shipped[split:], mods=False)
    shipped_log, shipped_shots = first_log + second_log, first_shots + second_shots
    for index, page in enumerate(SHIPPED_PAGES):
        if len(shipped_shots) < 2 * index + 2:
            report(False, f"shipped {page}: a screenshot is missing (see shipped.log)")
            continue
        # optech2: the bottom strip holds the version line, which can flicker between shots
        data, stock = shipped_shots[2 * index][:-40], shipped_shots[2 * index + 1][:-40]
        if page in ("joy", "gyro"):
            # optech2: the data pages mark items opening a page with ">" (the stock C ones don't)
            data, stock = data[:, :488], stock[:, :488]
        if page == "options":
            # optech2: options adds "game and HUD" under the stock lines (fixed y: nothing above moves)
            data, stock = data[:370], stock[:370]
        report(differing(data, stock) == 0,
               f"shipped menus/{page}.menu draws as the stock menu ({differing(data, stock)} pixels differ)")
    if episodes:
        index = len(SHIPPED_PAGES)
        if len(shipped_shots) < 2 * index + 2:
            report(False, "episode: a screenshot is missing (see shipped.log)")
        else:
            data, stock = shipped_shots[2 * index][:-40], shipped_shots[2 * index + 1][:-40]
            report(differing(data, stock) == 0,
                   f"an episode on the game page asks for the skill as the stock one ({differing(data, stock)} pixels differ)")
    log += shipped_log

    # optech2: with episodes, a level picked in level select asks for the skill and starts that level
    # (an episode line used to replace the pick); unit 2 starts with its cinematic
    if episodes:
        # optech2: and the data level select page: the second campaign's first level starts after the skill
        pick_log, _ = run("levelpick", ["map base1"] + filler[:10] + [
            "menu_game", "menu_levelselect", "presskey rightarrow", "presskey downarrow", "presskey enter",
            "presskey enter"] + filler[:14] + ["mapname"], mods=False)
        report(cvar(pick_log, "mapname") not in (None, "base1"),
               f"level select page: a campaign's level starts after the skill (mapname {cvar(pick_log, 'mapname')})")
        log += pick_log
        level_log, _ = run("levelselect", ["map base1"] + filler[:10] + [
            "menu_game", "menu_levelselect", "presskey downarrow", "presskey downarrow", "presskey enter",
            "presskey enter"] + filler[:14] + ["mapname"], mods=False)
        report(cvar(level_log, "mapname") in ("eou1_.cin", "ware1"),
               f"level select: the picked level starts after the skill (mapname {cvar(level_log, 'mapname')})")
        log += level_log

    # optech2: the Lua cases' own errors are expected
    errors = [line for line in log.splitlines() if re.search(r"error|unknown item|expected \{|missing", line, re.I)
              and "too many instructions" not in line and "broken.lua" not in line
              and "vert coodinates error" not in line]  # optech2: a re-release model's load note (level select run)
    report(not errors, f"no errors in the logs{': ' + errors[0] if errors else ''}")

    print(f"\n{'PASS' if all(results) else 'FAIL'}: {sum(results)}/{len(results)} data menu checks")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
