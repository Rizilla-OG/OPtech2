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
  Left: trains and doors as lifts, and `.nav` links onto spots narrower than a
  monster's mesh (crates, narrow ledges) that only bots can use.
- **GL3/GL4 report every texture as having alpha**: solid and alpha formats
  are both `GL_RGBA`, so `GL3_Upload32`/`GL4_Upload32` always return true.
  Only `imagelist` shows it; return whether a pixel below 255 alpha was seen.

## Re-release support

- Re-release HUD pictures with partial alpha draw solid: no renderer has a
  see-through picture call (`Draw_FillColor` only does boxes). Fully
  transparent ones are skipped. Needs a renderer API addition (all five).
- Re-release bots (deferred, the last open item of the re-release parity
  list): the re-release keeps its bot brain in the engine (`Bot_RegisterEdict`,
  `Bot_MoveToPoint`, `Bot_FollowActor` and the game's `Bot_*` exports), so its
  games' bots need an engine-side bot AI on our navigation mesh (moving,
  following, picking items and targets). Only for re-release games' deathmatch
  and coop bots; our own game has its own bots. Pick it up with the next
  multiplayer work (after M3, or as part of M5), when the nav mesh has had the
  lift, jump and ladder work it needs for players too.
- Quake II 64: `hackflags` key ignored (monster/camera behaviour), camera
  scenes can't be skipped.
- Re-release `CS_CD_LOOP_COUNT` (map music loop count, "0" on Quake II 64
  maps) reaches the client but isn't used: its meaning isn't known (the
  original's cd_loopcount switched to track 11 after that many loops).
- Re-release pathfinding: requests carry no mover size, so every monster
  gets the player-sized mesh (a Tank may be sent through a gap only a soldier
  fits); the re-release's own nodes have the same limit.
- Debug drawing: `info_world_text` (world text) not tried on a map yet; text
  size and the static text's angles are ignored (always console size, facing
  the view); remote clients don't see it (kept in the local process).

## Effects (M2 step 4)

- Done: temp entities, muzzle flashes, trails, events, entity drawing
  (AddEntityModels, 2026-11-23), the view weapon (AddViewWeapon, with
  powerup shells) and shadow lights (AddShadowLight, 2026-11-24) all run in
  the game library.
- Re-release shadow lights (RF_CASTSHADOW, game library's AddShadowLight
  since fx ext 8) light as plain dlights with the map's intensity (1.3-5)
  as the dlight's radius, so they barely show (upstream did the same).
  Real shadow lights (radius, cone, shadow maps) belong to M3 renderer work.
- Flaky: `q2arpg` check failed once in the full suite (save loaded 15 units
  off the saved spot, 2026-11-24), passed 3/3 alone. Watch it.
- `fxtest` only sends position + direction; stock types with other data
  can't be sent with it.

## Menus (M2)

- Plan (riz, 2026-11-24): data menus (menu files in mod folders: pages of
  buttons, sliders, toggles, lists bound to cvars and commands; stock menus
  become data files) + sandboxed Lua hooks for live parts (save slots,
  server browser, conditions, custom widgets; needs a client-side Lua
  state), drawn by a restyled qmenu (TTF, scaling, mouse, gamepad, re-release
  look). Old C menus stay as the fallback.
- Steps: 1 loader + `menu` command + overrides (done 2026-11-24,
  src/client/menu/datamenu.c, check data-menus); 2 stock menus as data files
  (options, multiplayer, game, download options, deathmatch flags done
  2026-11-24; still stock: main (pics), keys/buttons (binding lists),
  load/save (slots), join/start server, address book, player setup (model
  preview), video, joy/gyro/sticks, mods, level select, credits, quit; the
  lists wait for step 3); 3 client Lua state +
  hooks (done 2026-11-24: menu_lua.c, lists, lua actions, if lua:, open;
  game page lists episodes; next lists to port: save/load slots, key
  bindings, servers, mods, level select — need menu.* functions for each); 4 restyle (done 2026-11-24 for data pages: ui_menustyle 1, panel,
  row bar, mouse; the stock C menus and the main menu keep the classic
  look until they're data pages); 5 modder docs (done 2026-11-24: mods/MENUS.md).
  Done 2026-11-24: every stock menu is a data page (load/save, all binding
  pages, video, address book, mods, level select, joy/gyro/sticks, quit,
  credits, start/join server, player setup) + options > game and HUD;
  stock C menus kept as menu_builtin fallbacks. Left: the main menu's
  classic picture page (still C; data main.menu only in the re-release
  look), the "searching" popup is still the classic text box.
- Later: game library menus in C through an extension hook (draw + keys),
  for libraries like q2arpg that want their own; engine/data menus stay the
  fallback for re-release games.

## Monster AI

- FindTarget keeps a noise or sight for FRAMETIME * tickrate framenums, and
  framenum counts at 10 Hz, so 0.6 s at 60 Hz (stock: one think). Narrowing
  it to 1-2 framenums breaks hearing (monster-nav check): the monster hears
  in the first frame and sets the noise as its enemy, but never reaches
  ai_run (the stand-to-run hand-off loses the sound target at 60 Hz; a
  re-hear within the window rescues it). Fix the hand-off, then narrow the
  window (code review, 2026-11-23).
- Clear-shot checks (M_CheckClearShot) are in for soldier, gunner, infantry,
  gladiator (and Xatrix's), iron maiden, parasite, tank and supertank. Left:
  shambler lightning offset search (our shambler is a different port),
  Gunner Commander (not in yet).
- Active dodging is in (M_CheckDodge, shared M_MonsterDodge for soldier,
  gunner, infantry, medic, iron maiden, brain). Left: the Xatrix soldiers
  (soldierh) keep their own duck-only dodge (no duck / sidestep callbacks);
  gekk and stalker keep theirs; berserker had the shared one already.
- Per-monster re-release tweaks done: soldier, tank, mutant, medic, iron
  maiden, hornet (incl. Quake II 64's), supertank grenades, stalker, carrier,
  gekk; infantry cocking and run-and-shoot, tank dropped arm (both only
  with the re-release data, detected at run time). Not ported: flyer pinning (part of the re-release's alternate flying), boss attack
  checks via M_CheckAttack_Base (same numbers as ours), Makron health bar
  handover (we have no boss health bars).

## Prediction

- Jumping on a moving lift could miss a tick around takeoff and landing
  (client and server disagreeing for a tick). The `lift` check on the fixed
  test clock now shows under a unit both standing and jumping; confirm in play
  at a real frame rate before dropping this.
- Re-release games, riding trains and lifts: the client carries the player
  at the mover's speed from its last two positions, so when the mover speeds
  up, slows, stops or turns the carry is a frame late: 2-5 unit hitches
  (101 of 591 misses on a train.bsp ride, the rest under 0.4 units). Fix:
  send the mover's exact velocity and next stop. Stock Quake 2 doesn't
  carry at all, so this is already better than stock.
- Re-release games, combat: 3-7 unit misses for a few frames while being
  shot (knockback?); not reproduced headless with monsters firing at a god
  mode player. Get `cl_showmiss 2` from a real fight.

## Renderer performance

- Vulkan is CPU-bound on big maps (riz, 2026-11-23): 16 fps on mgu2m1 (Call of
  the Machine, 22 MB QBSP, re-release data), render submit 63 ms/frame on one
  core, (AMD 6750 XT)GPU 0% busy; GL3 is fast at the same spot. ref_vk (vkQuake2) rebuilds
  and uploads world surface vertices on the CPU (AMD 5900X) every frame. Fix: keep world
  geometry in static GPU buffers (as GL3) and only record draws per frame;
  profile first (perf on ref_vk at that spot). Fits M3 (renderer work).

- Vulkan: a flat brown screen-aligned rectangle flickers in the lower right
  on base1 when standing still at the spawn (riz, 2026-11-23, re-release
  data, his config: fov 100, r_msaa_samples 8, vk_directrender 1, desktop
  fullscreen). Gone as soon as the player moves; caught by pausing over and
  over. Not seen in GL3. Not reproduced in 3 x 25 windowed screenshots (fresh
  and his config, paused and not). Suspect a 2D or translucent draw from a
  buffer reused while in flight. For the M3 renderer rework.
- Vulkan test run with riz's config died on SIGABRT while writing a 2560x1400
  PNG screenshot (inside miniz, main thread; 2026-11-23), right at the test's
  90 s timeout. Likely harness noise, but nothing in the game calls abort
  there: check if it comes back.

## Builds and releases

- TrueType console text looks blocky in GL4 at small window sizes (800x600, con_textscale 0.8); Vulkan draws it smoothly since 2026-11-22. Check the GL renderers' font sampling.

- Website (`docs/`): no screenshots yet of the barrier jumps, leaps, ladders,
  teleporters or the new console.
