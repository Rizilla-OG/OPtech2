# TODO

Known gaps and cleanups, beyond the milestone plan. Add to it when leaving
something unfinished; remove an entry in the commit that fixes it.

## Code quality

- **`cl_perf` overlay is Linux-only, GPU stats AMD-only** (`src/client/cl_stats.c`
  reads `/proc` and amdgpu's sysfs). Plan for one look on every platform:
  1. Split it: the overlay (layout and drawing, no OS code) and a stats layer
     per platform filling one shared record; a value a platform can't give
     shows a dim "-" in a fixed row, the layout never changes. First step:
     Linux only, same values, nothing visible changes.
  2. GPU frame time from the renderer itself (GL timer queries, Vulkan
     timestamp queries): works on every vendor and OS, the main GPU number;
     vendor load/VRAM/temperatures become extras where available.
  3. Windows stats layer: `GetProcessTimes` / `GetSystemTimes`, thread times,
     `GetProcessMemoryInfo`, DXGI `QueryVideoMemoryInfo` for VRAM (any
     vendor); test in Wine and CI.
  4. macOS stats layer: `getrusage` / `host_statistics`, `task_info`,
     `task_threads`; CI compiles it, nobody here can run it.
  Linux extras: NVML for NVIDIA load/VRAM beside amdgpu's sysfs.
  Look: a compact panel in the top right corner, in a smaller font, instead
  of covering the top half of the screen.
- **`func_eye` and `target_poi` are stand-ins** (`src/game/g_misc.c`): the eyes
  don't turn to the player, objective markers are removed instead of shown.
- **Re-release content fixes have no common home**: Quake II 64 changelevel
  flags, Q2Game.kpf / localization paths, map key handling. Fold them into the
  data-driven definitions.
- **`tests/run_all.py` isn't on CI yet**: add `run_all.py --headless` to the
  Linux workflow (needs the game data, so probably a self-hosted runner; the
  nightly check runs it locally meanwhile).
- **Definitions are numbers only**: weapons take numbers for the ten stock
  weapons and hand grenades (not fire rate, not the mission pack weapons);
  monsters take health, mass, skin, scale and damage, not speed, attacks or
  AI. A new monster's base must be a stock one.
- **Scripting API is small**: three events and player functions. Grow it with
  the data-driven definitions (entities, damage, think, spawn).
- **Monster navigation, phase 4**: lifts, ledge drops and the re-release
  `.nav` files' barrier and long jumps, ladders and teleporters are done
  (monsters climb with their run animation: there's no climb animation).
  Left: trains and doors as lifts, a per-monster drop height (every walking
  monster may drop 256), and `.nav` links onto spots narrower than a
  monster's mesh (crates, narrow ledges) that only bots can use.
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

- Jumping on a moving lift could miss a tick around takeoff and landing
  (client and server disagreeing for a tick). The `lift` check on the fixed
  test clock now shows under a unit both standing and jumping; confirm in play
  at a real frame rate before dropping this.

## Builds and releases

- Website (`docs/`): no screenshots yet of the barrier jumps, leaps, ladders,
  teleporters or the new console.
