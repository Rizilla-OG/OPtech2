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

- **Definitions are numbers only**: weapons take numbers for the ten stock
  weapons and hand grenades (not fire rate, not the mission pack weapons);
  monsters take health, mass, skin, scale and damage, not speed, attacks or
  AI. A new monster's base must be a stock one.
- **Scripting API is small**: three events and player functions. Grow it with
  the data-driven definitions (entities, damage, think, spawn).
- **Monster navigation, phase 4**: lifts, ledge drops and the re-release
  `.nav` files' barrier jumps are done. Left: their long jumps (gaps; the
  re-release gives them to flyers only), ladders and teleporters (stock
  monsters can't use either), trains and doors as lifts, a per-monster drop
  height (every walking monster may drop 256), and `.nav` jumps onto spots
  narrower than a monster's mesh (crates, ledges) that bots can use.
- **`savegame` keeps the real clock**: with `cl_loadpaused` the pause starts
  only once the client has connected, so on the fixed test clock the server
  runs a frame or two after a load (a falling gib moves). Pause the local
  server at the start of a single-player load, then move the check to
  `fixedtime` like the others.
- **`cgame` check rewrites `docs/screenshots/m2_test_cgame_hud.png`** on every
  run (its particles differ each time): write it under `build/` and copy to
  `docs/` only on purpose.
- **GL3/GL4 report every texture as having alpha**: solid and alpha formats
  are both `GL_RGBA`, so `GL3_Upload32`/`GL4_Upload32` always return true.
  Only `imagelist` shows it; return whether a pixel below 255 alpha was seen.

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
