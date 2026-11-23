# OpenTech2

[![Linux](https://github.com/Rizilla-OG/OPtech2/actions/workflows/linux_x86_64.yml/badge.svg)](https://github.com/Rizilla-OG/OPtech2/actions/workflows/linux_x86_64.yml)
[![Windows](https://github.com/Rizilla-OG/OPtech2/actions/workflows/win_mingw.yml/badge.svg)](https://github.com/Rizilla-OG/OPtech2/actions/workflows/win_mingw.yml)
[![macOS](https://github.com/Rizilla-OG/OPtech2/actions/workflows/macos.yml/badge.svg)](https://github.com/Rizilla-OG/OPtech2/actions/workflows/macos.yml)

OpenTech2 is a Quake II derived engine for building an original game:
no arbitrary limits for mappers and modders, a multi-threaded renderer, and per-pixel
lighting with real-time shadows, while staying playable on modest hardware.

Project page: https://rizilla-og.github.io/OPtech2/

It is a fork of [Yamagi Quake II Remaster](https://github.com/yquake2/yquake2remaster)
(see [README.yamagi.md](README.yamagi.md) for the upstream readme). Stock Quake II maps
and assets keep loading and serve as test content.

## Status

- **M0 Foundation:** done. One-command build, screenshot regression check, benchmark.
- **M1 Remove limits:** done. Raised client render, particle, light and sound limits; 1024 packet
  entities, 8192 entities; bigger configstring and image limits (protocol 2025); 60 Hz server tick
  with client smoothing; network packet splitting; maps past +-4096 units with extended QBSP and BSPX
  lightmaps; IQM models, true-color textures, more sound formats; saves that load across builds.
- **M2 Modding architecture:** in progress. Done so far: the client game module (re-release client
  game API, id's re-release HUD, effects), player movement as game code shared with prediction, the
  re-release episodes in the Game menu, stacked mod folders, sandboxed Lua scripts, item, weapon and
  monster definitions in text files, and monster navigation (paths, three monster sizes, ledge drops,
  lifts, jumps, leaps, ladders and teleporters from the re-release's navigation files), re-release
  game libraries (game API 2023: id's game and mods built on it, such as q2arpg) with their saves,
  level changes and movement prediction, weapon and powerup wheels (hold E / Q), kill feed and chat,
  colored text, autosave and Restart level, WASD default keys, a modern console (see-through panel,
  Hack TrueType font), the Vulkan renderer in the default build, and the re-release's game features in
  the stock game: compass trail, co-op squad respawn and lives, end-of-unit screen, re-release weapon and
  monster balance (`g_classic_balance 1` for the original), monsters that see before they listen, and
  footsteps by floor material. Two full code reviews with every finding fixed. Network protocol 2027.

Downloads (pre-releases, Windows and Linux): https://github.com/Rizilla-OG/OPtech2/releases

Changes are listed in [CHANGES.md](CHANGES.md).

## Build

```
cmake --workflow --preset default
```

Output lands in `build/release/` (`OpenTech2`, `OpenTech2-server`). Point it at a Quake II install:

```
build/release/OpenTech2 -datadir "<path to Quake 2>"
```

## Re-release episodes

Point `-datadir` at the 2023 re-release's `rerelease` folder:

```
build/release/OpenTech2 -datadir "<path to Quake 2>/rerelease"
```

The Game menu then lists Quake II, The Reckoning, Ground Zero, Call of the Machine and Quake II 64,
each starting with its own intro.

Game libraries built for the re-release (its game API 2023) load too: a game or mod folder with
`game_x64.so` (`game_x64.dll` on Windows) and no `game.so` gets it, for both server and client game.
Saves, loads and level changes use the game's own save files. `tools/rerelease/stock_game` builds
id's stock game for Linux.

## Map tools

Maps are compiled with [ericw-tools](https://github.com/ericwa/ericw-tools) (qbsp, vis, light).
OpenTech2 is tested with **ericw-tools 2.0.0-alpha11**. To get exactly that version (official release,
sha256 checked) into `build/tools/ericw-tools/`:

```
python3 tools/get_ericw_tools.py
```

Compile for Quake II's extended format with `qbsp -qbism -subdivide 240` (the software renderer needs
faces subdivided like stock maps); `light -world_units_per_luxel N` adds decoupled lightmaps.
`tests/maps/make_limits_map.py` shows the full command lines.

## Models

OpenTech2's model format is **IQM** (Inter-Quake Model, version 2): one binary file holding the mesh,
skeleton and animations. Export it from Blender with the [IQM exporter](https://github.com/lsalzman/iqm)
(add-ons up to Blender 5.0). Each animation shows up as a frame group named after it. Materials name
the skin image. Stock MD2 and the other formats keep loading.

## Test maps to play

```
python3 tools/install_test_content.py [--extra your_model.iqm]
build/release/OpenTech2 -datadir "<path to Quake 2>" +map optech2_iqm
```

`optech2_iqm` shows IQM models still and animated, `optech2_limits` goes past the stock limits,
`optech2_stress` has ~3800 entities. The script only adds `maps/optech2_*` and `models/optech2/` to your
game folder.

## Savegames

Saves store every field by name, so they keep loading after the game code changes. After adding,
removing or changing a field of a saved struct (`edict_t`, `gclient_t`, `level_locals_t`,
`game_locals_t` and what they contain), run `python3 tools/gen_save_fields.py` and commit the table
it writes.

## Mods

Stack mod folders on top of the game, lowest first: `+set fs_mods hires,mymod` (commas, no spaces, on
the command line). A later folder's files replace the same files in an earlier one, `+set game` goes
on top of them all, and saves and settings still go to the game folder. Players joining a server get
its list.

Each mod folder can run a gameplay script in sandboxed Lua 5.4, `scripts/<folder>.lua`: see
[mods/SCRIPTING.md](mods/SCRIPTING.md). Items can be changed or added in a text file,
`defs/<folder>.items` (weapons take damage, spread and speed numbers there too), and monsters in
`defs/<folder>.monsters`: see [mods/DEFINITIONS.md](mods/DEFINITIONS.md). [mods/survival](mods/survival/README.md)
is a small example script mod. For a mod with its own game library (new weapons, effects,
HUD, movement), see [mods/testmod](mods/testmod/README.md).

## Monster navigation

Every map gets a navigation mesh when it loads (Recast/Detour; one per monster size, built in under
a second, cached in `navigation/<map>.otnav` in the game folder). Walking monsters that can't walk
straight at you follow it: around walls, off ledges up to 256 units high, and onto lifts
(`func_plat`), which they call and ride. With the re-release's data, the barrier jumps in its bot
navigation files (`bots/navigation/<map>.nav`) are added too: monsters that can jump (Berserker,
Gunner, Infantry, Mutant, Parasite) jump up onto ledges, over barriers and across gaps where those
files mark it; player-sized monsters climb their ladders, and any walking monster takes their
teleporters when its path goes through one.

- `g_monster_nav 0` turns it off (the stock chase), `2` logs ledge drops, lift rides, jumps, climbs and teleports, `3` also
  draws each monster's path (single player)
- `cl_shownav 1`, `2`, `3` draw the small, medium and large mesh
- `nav_info`, `nav_path x1 y1 z1 x2 y2 z2 [small|medium|large]`, `nav_rebuild` in the console

## Checks

All checks use a temporary HOME, so your own config and saves are never touched. Most run on a fixed
clock (`+set fixedtime 16667`: every frame is one 60 Hz tick), so they play out the same way on every run
and finish faster than real time; the ones that time real speed or run a separate server keep the real
clock. `run_all.py --headless` runs only the checks that need no window or sound device.

- `python3 tests/screenshots/screenshot_check.py`: fixed viewpoints vs. reference images
- `python3 tests/benchmark/benchmark.py`: timedemo against the saved baseline
- `python3 tests/multiplayer/mp_check.py`: two clients on a dedicated server
- `python3 tests/multiplayer/net_check.py`: traffic per player at several tick rates
- `python3 tests/multiplayer/split_check.py`: packet splitting, clean and with loss
- `python3 tests/maps/make_limits_map.py`, then `tests/maps/limits_check.py`: oversized test map
- `python3 tests/maps/make_stress_map.py`, then `tests/maps/stress_check.py`: ~3800 entities, frame times
- `python3 tests/models/iqm_check.py`: IQM test model (two bones, bend animation) in a small map
- `python3 tests/savegame/save_check.py`: save and load, player and every entity compared
- `python3 tests/cgame/cgame_check.py`: loads a test client game module and checks the engine's imports
- `python3 tests/pmove/pmove_check.py`: player movement runs from the game library; a scripted walk on base1
- `python3 tests/maps/make_lift_map.py`, then `tests/pmove/lift_check.py`: prediction while riding a lift
- `python3 tests/maps/texture_check.py`: true-color textures
- `python3 tests/sound/sound_check.py`: the extra sound formats
- `python3 tests/tickrate/tick_check.py`, `damage_check.py`: 10 Hz vs 60 Hz behaviour
- `python3 tests/mod/mod_check.py`: the test mod against the stock game (weapon, effect, HUD, movement)
- `python3 tests/mod/stack_check.py`: stacked mod folders, override order, joining players
- `python3 tests/mod/script_check.py`: Lua scripts from three mods, events, sandbox limits
- `python3 tests/mod/defs_check.py`: item definitions from two mods, a new weapon, mistakes reported
- `python3 tests/mod/weapon_defs_check.py`: weapon damage, pellets and speed from a definition, against stock
- `python3 tests/mod/monster_check.py`: a new monster from a definition, damage and death events, drops
- `python3 tests/maps/make_nav_map.py`, then `tests/nav/nav_check.py`: navigation mesh, paths around a wall, cache
- `python3 tests/nav/monster_nav_check.py`: a Berserker paths around the wall to the player (and stock, to compare); hearing at 60 Hz
- `python3 tests/maps/make_ledge_map.py`, then `tests/nav/ledge_check.py`: ledge drops, one way, not from too high; a Berserker walks off a ledge to the player
- `python3 tests/maps/make_plat_map.py`, then `tests/nav/plat_check.py`: monsters (Berserker, Tank) ride a lift up and down to the player
- `python3 tests/maps/make_jump_map.py`, then `tests/nav/jump_check.py`: a Berserker jumps up a ledge through a gap, using a re-release navigation file's barrier jump
- `python3 tests/maps/make_leap_map.py`, then `tests/nav/leap_check.py`: a Berserker leaps a pit, using a re-release navigation file's long jump
- `python3 tests/maps/make_ladder_map.py`, then `tests/nav/ladder_check.py`: a Berserker climbs a ladder to a balcony and takes a teleporter into a vault
- `python3 tests/run_all.py`: all of the above

## Code scanning

Every push to `main` runs CodeQL and cppcheck (full rule set) on GitHub; both are clean, with bundled
libraries (Recast/Detour, Lua, miniz, stb, glad, volk, pl_mpeg, minimp3, HandmadeMath) left out. To run
the same scans locally before pushing:

- `tools/ci/run_cppcheck.sh`: no output means clean (cppcheck 2.13 or newer)
- `tools/ci/run_codeql.sh`: builds into `build/codeql` and lists the results (needs the CodeQL CLI)

The pre-commit hook (`git config core.hooksPath .githooks`) runs cppcheck on the staged source files and
stops the commit on a finding. `tools/nightly_check.sh` builds, runs every check and cppcheck, reads the GitHub CI and alert state and
writes a report to `build/nightly/`; it can run from a systemd user timer and skips while a game is running.

## License

GPL version 2, as Quake II and Yamagi Quake II. See [LICENSE](LICENSE).
