# Menus

Menu pages are text files: mods can change the stock menus and add their
own, no compiler needed. A page can come with a small Lua script for the
parts that change while you play (lists, buttons that do more than a
command).

## Where the files go

```
mymod/menus/<name>.menu      the page
mymod/menus/<name>.lua       its script (optional)
```

`menu <name>` in the console opens a page. The game ships the options,
multiplayer, single player, download options and deathmatch flags menus as
pages (`baseq2/menus/`): a page with the same name in a mod folder replaces
one, everywhere it opens from (the main menu, ESC, other menus). Files are
looked up the usual way, so the top folder of the stack wins (`fs_mods`,
then `game`). `menu_builtin <name>` opens the stock menu anyway; the stock
C menus (key bindings, load and save, servers, video, player setup...)
can be replaced by name too: `menus/keys.menu` for `menu_keys`.

A page is read again every time it opens: edit the file, close the menu and
open it again to see the change.

## The format

Blocks of `"key" "value"` pairs, written like map entities. `//` starts a
comment. The first block is the page, every block after it an item:

```
// mymod/menus/mymod.menu
{
"title" "My mod"
"status" "fov $fov"
}
{
"type" "slider"
"label" "field of view"
"cvar" "fov"
"min" "70" "max" "130" "step" "5" "format" "%.0f"
}
{
"type" "toggle"
"label" "always run"
"cvar" "cl_run"
}
{
"type" "action"
"label" "start the first map"
"command" "map base1"
}
```

### Page keys

| key | meaning |
|---|---|
| `banner` | a pic above the items (`m_banner_options`) |
| `title` | a line of text instead, when there's no banner |
| `status` | a status line; `$name` is a cvar's value (`dmflags = $dmflags`) |
| `if` | a condition (below): when it doesn't hold, the page isn't there and the stock menu of the same name opens instead |
| `open` | a function of the page's script, run before the items are built |
| `x`, `y` | where the classic look places it: pixels (menu scale) from the screen's middle; centered without `y` |

### Items

| type | what it is | its keys |
|---|---|---|
| `slider` | a number between two values | `cvar`, `min`, `max`, `step`, `format` (`%.1f`) |
| `choice` | one of several values | `cvar`, `options` (`name=value;name=value`), `command` (run after a change), `mask` |
| `toggle` | a choice of `no=0;yes=1` | `cvar` |
| `invert` | yes makes the cvar negative, keeping its size (invert mouse: `m_pitch`) | `cvar` |
| `action` | runs a command on enter | `command`, `lua` |
| `field` | text typed into a cvar, saved when the page closes | `cvar`, `length` |
| `label` | text only (a section heading) | |
| `space` | a blank line | |
| `list` | items made by the page's script | `source` |

Every item takes `label`, `alttext` (a localization key for the label,
`$m_sound_volume`), `status` (its status line), `gap` (extra pixels above
it), `x` (pixels right of the item column, classic look), `if` (left out
when it doesn't hold) and, for actions, `align` (`right` lines the label up
with the others in the classic look).

**Choices with several cvars:** a space separated `cvar` takes space
separated values, one each. While the page is open the choice shows the
option nearest to the cvars' values:

```
{
"type" "choice"
"label" "crosshair color"
"cvar" "crosshair_color_r crosshair_color_g crosshair_color_b"
"options" "white=1 1 1;red=1 0 0;orange=1 0.5 0"
}
```

**Bits of a cvar:** with `mask` a choice changes only those bits, as the
deathmatch flags do (`dmflags` 8 is "no falling damage"):

```
{
"type" "choice"
"label" "falling damage"
"cvar" "dmflags"
"mask" "8"
"options" "no=8;yes=0"
}
```

### Conditions

`if` takes names separated by spaces; all must hold, `!name` for not:

| name | holds when |
|---|---|
| `ingame` | a game is running |
| `gamepad` | a gamepad is connected (and the screen has room) |
| `tall` | the screen has room for the long options page |
| `episodes` | the game data has several episodes (the re-release) |
| `levelselect`, `mods` | the stock menus would offer them |
| `curl` | the engine was built with HTTP downloads |
| `game=<name>` | the `game` cvar is that (`game=ctf`; `game=baseq2` also when it's empty) |
| `lua:<fn>` | the page script's function returns true |
| anything else | a cvar of that name, not 0 |

## Scripts

`menus/<name>.lua` runs each time its page opens, in an environment of its
own: globals it sets are its own, the standard functions and the `menu`
table are read through. It runs in the same sandbox as the gameplay
scripts (no files: no `dofile`, `loadfile`, `require`; `load` takes text
only), and each call stops after 1,000,000 instructions. An error is
printed in the console and the page goes on without that part.

The page names the script's functions:

```
// mymod/menus/maps.menu
{
"title" "Pick a map"
}
{
"type" "list"
"source" "maps"
}
{
"type" "action"
"label" "count the clicks"
"lua" "click"
}
```

```lua
-- mymod/menus/maps.lua
function maps()
	local lines = {}

	for _, name in ipairs({ "base1", "base2", "base3" }) do
		lines[#lines + 1] = { label = name, command = "map " .. name, status = "start " .. name }
	end

	return lines
end

clicks = 0

function click()
	clicks = clicks + 1
	menu.print("clicked " .. clicks .. " times")
end
```

A `list` function returns entries, each one becoming an action: `label`,
`command`, `alttext`, `status`, and `action`, a function run on enter.

The `menu` table:

| function | what it does |
|---|---|
| `menu.cvar(name)` | a cvar's value as text (`""` if unset) |
| `menu.value(name)` | as a number |
| `menu.set(name, value)` | sets it (a whole number is written as one: `1`, not `1.0`) |
| `menu.command(text)` | runs a console command (after the call returns) |
| `menu.open(page)` | opens a page, a data one or a stock one (`options`) |
| `menu.condition(name)` | a condition name from the table above (not cvars) |
| `menu.episodes()` | `{ { name = "Quake II", index = 0 }, ... }`; `menu_episode <index>` asks for the skill |
| `menu.mods()` | the mod folder names the mods menu offers |
| `menu.print(...)` | to the console |

The shipped single player page is a small example: `baseq2/menus/game.menu`
and `game.lua` (in the source: `stuff/menus/`).

## Commands for pages

| command | what it does |
|---|---|
| `menu <name>` | opens a page |
| `menu_builtin <name>` | opens a stock menu even when a page replaces it |
| `menu_newgame <skill>` | a new single player game (0 easy to 3 hard+) |
| `menu_episode <index>` | picks an episode, then asks for the skill |
| `menu_console` | closes the menus and opens the console |
| `menu_oggenable` | starts or stops OGG music after `ogg_enable` changed |

## The look

`ui_menustyle 1` (the default) draws pages the re-release's way: a dark
panel, a bar on the selected row, values on the right, sliders as bars.
The mouse works it: hover selects, a click picks (a choice steps to its
next option, a click on a slider's bar sets it), the right button goes
back, the wheel moves. `ui_menustyle 0` draws pages in the classic Quake II
look. A page too tall for the screen is drawn smaller.

## Checking a page

`python3 tests/menus/datamenu_check.py` drives test pages with
`presskey <key>` and `menu_pointer <x> <y>` (both need `developer 1`) and
compares the shipped pages with the stock menus.
