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
- Dynamic lights raised from 32 to 128 (GL3, GL4, GL1 and software renderers; Vulkan still uses the first 32). The per-surface light mask is now 4 words, sent to the GL3/GL4 shaders as a uvec4.
- `cl_testlights N` spawns N test lights (1 still means all of them).
- Map loader: removed the unused MAX_MAP_* bounds. Submodels are allocated per map (limit is now the model configstrings, MAX_MODELS - 1, with a clear error). A `.ent` override file can be any size (was lump size + 256 KB, larger files overran the map memory). Maps with more than 256 areas now fail with a clear error instead of overflowing the network area bits.
- Mod list: no longer stops at 32 mods.
