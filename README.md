# OPtech2

OPtech2 ("Open Tech 2") is a Quake II derived engine for building an original game:
no arbitrary limits for mappers and modders, a multi-threaded renderer, and per-pixel
lighting with real-time shadows, while staying playable on modest hardware.

Project page: https://rizilla-og.github.io/OPtech2/

It is a fork of [Yamagi Quake II Remaster](https://github.com/yquake2/yquake2remaster)
(see [README.yamagi.md](README.yamagi.md) for the upstream readme). Stock Quake II maps
and assets keep loading and serve as test content.

## Status

- **M0 Foundation:** done. One-command build, screenshot regression check, benchmark.
- **M1 Remove limits:** in progress. Done so far: raised client render, particle, light
  and sound limits; 1024 packet entities; bigger configstring and image limits (protocol
  2025); 60 Hz server tick with client smoothing; re-release AI and weapon timing ports;
  rate limiter fixed for any tick rate; network packet splitting; an oversized test map
  (beyond +-4096 units, over 256 models and sounds, extended QBSP with BSPX lightmaps); 8192
  entities and a ~3800 entity stress map at 60 Hz.

Changes are listed in [CHANGES.md](CHANGES.md).

## Build

```
cmake --workflow --preset default
```

Output lands in `build/release/` (`optech2`, `optech2ded`). Point it at a Quake II install:

```
build/release/optech2 -datadir "<path to Quake 2>"
```

## Map tools

Maps are compiled with [ericw-tools](https://github.com/ericwa/ericw-tools) (qbsp, vis, light).
OPtech2 is tested with **ericw-tools 2.0.0-alpha11**. To get exactly that version (official release,
sha256 checked) into `build/tools/ericw-tools/`:

```
python3 tools/get_ericw_tools.py
```

Compile for Quake II's extended format with `qbsp -qbism -subdivide 240` (the software renderer needs
faces subdivided like stock maps); `light -world_units_per_luxel N` adds decoupled lightmaps.
`tests/maps/make_limits_map.py` shows the full command lines.

## Models

OPtech2's model format is **IQM** (Inter-Quake Model, version 2): one binary file holding the mesh,
skeleton and animations. Export it from Blender with the [IQM exporter](https://github.com/lsalzman/iqm)
(add-ons up to Blender 5.0). Each animation shows up as a frame group named after it. Materials name
the skin image. Stock MD2 and the other formats keep loading.

## Test maps to play

```
python3 tools/install_test_content.py [--extra your_model.iqm]
build/release/optech2 -datadir "<path to Quake 2>" +map optech2_iqm
```

`optech2_iqm` shows IQM models still and animated, `optech2_limits` goes past the stock limits,
`optech2_stress` has ~3800 entities. The script only adds `maps/optech2_*` and `models/optech2/` to your
game folder.

## Savegames

Saves store every field by name, so they keep loading after the game code changes. After adding,
removing or changing a field of a saved struct (`edict_t`, `gclient_t`, `level_locals_t`,
`game_locals_t` and what they contain), run `python3 tools/gen_save_fields.py` and commit the table
it writes.

## Checks

All checks use a temporary HOME, so your own config and saves are never touched.

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
- `python3 tests/tickrate/tick_check.py`, `damage_check.py`: 10 Hz vs 60 Hz behaviour

## License

GPL version 2, as Quake II and Yamagi Quake II. See [LICENSE](LICENSE).
