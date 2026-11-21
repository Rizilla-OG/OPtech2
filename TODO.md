# TODO

Known gaps and cleanups, beyond the milestone plan. Add to it when leaving
something unfinished; remove an entry in the commit that fixes it.

## Code quality

- **`cl_perf` overlay is Linux-only, GPU stats AMD-only** (`src/client/cl_stats.c`
  reads `/proc` and amdgpu's sysfs). Fine as a developer tool; needs Windows
  (PDH / DXGI) and other GPU vendors, or a clear "unavailable" for each value.
- **Video decoder carries our changes inside `src/client/cinema/avdecode.h`**
  (repeat-frame packets, threads, silence at the end, buffer helper). Yamagi
  updates to that file will conflict; move our parts out or send them upstream.
- **`func_eye` and `target_poi` are stand-ins** (`src/game/g_misc.c`): the eyes
  don't turn to the player, objective markers are removed instead of shown.
- **Re-release content fixes have no common home**: Quake II 64 changelevel
  flags, Q2Game.kpf / localization paths, map key handling. Fold them into the
  data-driven definitions (M2) when those exist.
- **`tests/run_all.py` isn't on CI yet**: add `run_all.py --headless` to the
  Linux workflow (needs the game data, so probably a self-hosted runner).

- **Two checks are timing-sensitive under load**: `lift` (standing miss
  percentile) and `tickrate` (chaingun ammo at 60 Hz) failed once while the
  machine was busy, and pass when run alone. Make them robust to load.

## Re-release support

- Last ~0.25 s of FFmpeg videos is cut: the decoder isn't drained at end of file.
- `target_changelevel` flag 32 (fade out) isn't done.
- Quake II 64: `hackflags` key ignored (monster/camera behaviour), camera
  scenes can't be skipped.
- Multi-line localized texts get a stray space after each line break
  (`src/server/sv_translate.c` escape handling).
- Client game module: `NotifyMessage` / `ClearNotify` (kill feed, pickups) and
  the weapon wheel aren't wired; re-release mods (q2arpg) need the re-release
  `pmove_t` for `cgame->Pmove`.

## Effects (M2 step 4)

- Temp entities, muzzle flashes, entity trails and events run in the game
  library; still in the engine: how an entity's own model is drawn (powerup shells, translucency, rotation,
  linked weapon models) - the latter means the module drawing entities,
  Quake 3 style.
- `fxtest` only sends position + direction; stock types with other data
  can't be sent with it.

## Prediction

- Jumping on a moving lift still misses a tick around takeoff and landing
  (client and server disagree for a tick on standing); standing is fine.

## Builds and releases

- Windows: the game library now has C++ (re-release HUD code) and new files;
  not built on Windows yet. Makefile executable names still `quake2.exe`.
- Website (`docs/`): the M2 screenshots in `docs/screenshots/` aren't on the
  page yet.
