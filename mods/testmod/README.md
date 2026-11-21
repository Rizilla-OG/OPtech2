# OpenTech2 test mod

A small mod that proves the engine can be modded without changing it: it
adds a new weapon, the **Test Gun**, with its own visual effect.

- `src/game/testmod/testmod_weapon.c`: the weapon (server side). Hitscan, no
  ammo, 20 damage, blaster models. Each shot sends a temporary entity type the
  engine doesn't know, `TE_TESTGUN_BEAM` (201).
- `src/game/testmod/testmod_fx.c`: its effect (client side, in the game
  library's effects, `src/game/cgame_fx/`): a purple and green double helix
  from the gun to the hit point, with a light and a burst at the impact.
- `testmod.patch`: the few changes to stock game files: the item entry in
  `g_items.c`, the mod's effects hooked into the game library's temp entity
  code, and the two files added to the build.

Nothing under `src/client`, `src/server`, `src/common` or the renderers is
changed: the engine binary stays the same.

## Build and play

```
python3 tests/mod/mod_check.py   # builds it into build/mod-check/ and tests it
```

or by hand: copy the source tree, `patch -p1 < mods/testmod/testmod.patch`,
copy `mods/testmod/src` over it, build the `game` target, and put the
resulting `game.so` / `game.dll` in a `testmod` folder next to `baseq2` in your
game folder. Then start the game with `+set game testmod` and `give test gun`
(cheats) or `use test gun`.
