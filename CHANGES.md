# OPtech2 changes

Changes made on top of Yamagi Quake II Remaster. Upstream history is in `CHANGELOG`.

## Unreleased

- Forked from Yamagi Quake II Remaster (511b7b0e).
- Project named OPtech2 (Open Tech 2).
- One-command build: `cmake --workflow --preset default` (Vulkan renderer off).
- Screenshots from the GL3 renderer are captured at the end of the next frame, before the buffer swap. Before, they could come out black or stale.
- `teleport x y z pitch yaw roll`: the cheat command can now also set the view direction.
- Screenshot regression check: `python3 tests/screenshots/screenshot_check.py` (add `--update` to save new references).
- Timedemo now also prints frame-time stats (average, median, 95th and 99th percentile, worst) in ms.
- Benchmark: `python3 tests/benchmark/benchmark.py` plays the stock demos and compares with `tests/benchmark/baseline.json` (`--save-baseline` to replace it).
- Map loader accepts only Quake 2 (IBSP 38, extended QBSP/XBSP, BSPX) and Quake 3 (IBSP 46) maps. Quake 1, Hexen 2, Half-Life, Daikatana, SiN, Heretic 2, Kingpin and Doom map support removed.
- Fixed an uninitialized lump count when a map file has an unknown format.
- Pack files: only Quake 2 PAK and PK3/ZIP (pk2, pkz) are loaded. Quake 1/Half-Life WAD, Anachronox DAT, SiN and Daikatana pack formats removed.
- Network protocols 25 (Quake 2 test demos) and 51 (Heretic 2 demo) removed. Stock Quake 2 (26, 31, 32, 34), re-release (2022, 2023) and OPtech2 (2024) remain.
- Renamed to OPtech2 (v0.1.0-dev): executables `optech2` and `optech2ded`, window titles, console banner and crash report. User data now lives in `~/.local/share/OPtech2` (was `YamagiQ2`).
- Raised client limits: render entities 512 to 4096, particles 4096 to 16384, explosions, beams and lasers 64 to 256, sustained effects 32 to 128, sound channels 32 to 128, queued sounds 128 to 512.
- Code style: `.clang-format` (id style: tabs, Allman braces, return type on its own line) and a pre-commit hook in `.githooks/` that formats only the changed lines. Enable once per clone: `git config core.hooksPath .githooks`.
- Dynamic lights raised from 32 to 128 (GL3, GL4, GL1 and software renderers; Vulkan still uses the first 32). The per-surface light mask is now 4 words, sent to the GL3/GL4 shaders as a uvec4.
- `cl_testlights N` spawns N test lights (1 still means all of them).
- Map loader: removed the unused MAX_MAP_* bounds. Submodels are allocated per map (limit is now the model configstrings, MAX_MODELS - 1, with a clear error). A `.ent` override file can be any size (was lump size + 256 KB, larger files overran the map memory). Maps with more than 256 areas now fail with a clear error instead of overflowing the network area bits.
- Mod list: no longer stops at 32 mods.
- Entities per client frame raised from 256 to 1024, and now enforced: before, a frame with more than 256 visible entities could overwrite older frames the server deltas from. Client entity buffer raised from 1024 to 16384 to hold 16 such frames.
- Window title and renderer banner show the OPtech2 version (were still showing the upstream 8.71RR16).
- Multiplayer check: `python3 tests/multiplayer/mp_check.py` runs a dedicated server and two hidden clients on q2dm1 and checks joining, chat both ways, seeing each other and movement (~40 s, no windows).
- `delay <ms> <command>` console command: runs a command later without holding up the command buffer (a long `wait` chain blocks the commands a server sends while connecting).
- Fixed: two clients started in the same second on one machine got the same qport, and the server treated them as one player. The qport now mixes in the process id.
- Images (HUD pics, icons) per map raised from 256 to 2048. This moves the configstrings, so the network protocol is now 2025 (was 2024); older OPtech2 builds and saves will not match.
- Fixed: the server only sent the first 32 player stats; all 64 the game provides now reach the client.
- `sv_tickrate` (server frames per second, default 10, applies from the next map): the server steps at that rate and protocol 2025 tells clients, which time interpolation by it. The game code still assumes 10, so leave it at 10 until the game is converted.
- Game runs at any `sv_tickrate`: physics steps by the real frame time, while thinks, monster and weapon frames and player animation keep their 0.1 s pace (`level.framenum` is now a 10 Hz counter, `level.ticktime` is one server frame). Lava/slime damage stays per 0.1 s. At 60 Hz, monsters still move in 0.1 s steps (client smoothing comes next). `sv_tickrate` is latched: set it before starting a game.
- Multiplayer check takes `--tickrate N`.
- Client smoothing for high tick rates: monsters and other things that move, turn or animate every 0.1 s have each change spread over the time since their previous one (at most 100 ms), so they move every frame at 60 Hz instead of jumping every 6th frame. Movement and animation frames are timed separately. Same result as before at 10 Hz. `cl_smoothmovers 0` turns it off.
- Fixed: doors, lifts and trains lost distance at high tick rates (a door stopped 3 units short of open and closed at 60 Hz). Pushes are still rounded to 1/8 unit, but the rounded-off part now carries into the next push.
- Smoother view at high tick rates: view bob, view and gun offsets, gun sway and damage/pickup flashes now update every server frame (their per-step amounts scaled to the frame time) instead of every 0.1 s. Weapon model animation frames are spread over 0.1 s like monsters (`cl_smoothmovers`). Lava/drowning and falling damage, player model animation, weapon kick and menu updates keep the 0.1 s pace.
- Fixed: in single player the server only stepped on client network frames. With vsync on a 144 Hz monitor that was ~48 per second, so a 60 Hz server lost frames: game time ran ~6% slow and monsters stuttered. The local server now steps on its own clock (game time 4.98 s per 5 s real, was 4.68).
- Fixed: gun model jittered while turning at high tick rates. The sway used one server frame of view turn scaled up 6x; how many inputs land in a frame varies (0-3), so it jumped between 0 and 52 degrees. It is now smoothed over 0.1 s (21-28 degrees in the same turn, like stock at 10 Hz).
- `playerinfo` cheat command: prints game time, health, ammo and position on one line (for tests).
- Tick rate check: `python3 tests/tickrate/tick_check.py` runs base1 at 10 and 60 Hz and compares door speed, machine gun fire rate, rocket speed and berserker run speed (~40 s, no windows).
