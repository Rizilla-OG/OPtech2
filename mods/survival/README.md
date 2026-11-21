# Survival (example script mod)

A small example of a gameplay script, with no game library and nothing to
compile: one Lua file, `scripts/survival.lua`.

- Players spawn with 75 health, a Shotgun in the inventory and 10 shells
  (`+set survival_shells 20` for more).
- Each player can take two Quad Damage per level; more stay on the floor.
- Every 10th pickup gives 2 grenades.

## Try it

Copy the `survival` folder into your OpenTech2 user folder
(`~/.local/share/OpenTech2/` on Linux, next to `baseq2`), then start with:

```
OpenTech2 +set fs_mods survival +map base1
```

It stacks with other mods: `+set fs_mods survival,othermod`.

The scripting API is in [../SCRIPTING.md](../SCRIPTING.md).
`tests/mod/script_check.py` plays this mod to make sure the example keeps
working.
