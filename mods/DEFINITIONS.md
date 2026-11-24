# Item and monster definitions

Mods can change items and monsters and add new ones with text files, no
compiler needed: pickups, ammo, how hard weapons hit, and how tough monsters
are.

## Where the file goes

Each mod folder can have one item file, named after the folder, in lower case:

```
mymod/defs/mymod.items
```

Files are read in the order the folders are stacked (`baseq2`, then each
`fs_mods` folder, then the `game` folder), so a later folder's definitions win.
The file can also be in a pack (`.pak`, `.pk3`) in that folder.

## The format

Blocks of `"key" "value"` pairs, written like map entities. `//` starts a
comment.

```
// shells boxes give 20 instead of 10
{
"classname" "ammo_shells"
"quantity" "20"
}

// a new weapon: the shotgun, but it fires bullets
{
"classname" "weapon_boomstick"
"base" "weapon_shotgun"
"pickup_name" "Boomstick"
"ammo" "Bullets"
}
```

**Changing an item:** a block whose `classname` is an existing item changes
only the fields it lists.

**Adding an item:** a new `classname` needs `"base"`, an existing item it starts
as a copy of, behaviour included: how it's picked up, used and, for a weapon,
how it fires. Then the listed fields replace the copy's. Place it in a map by
its classname, like any item. Give it its own `pickup_name`: that's the name
the HUD shows and `give` and `use` take.

## Fields

| Field | Meaning |
| --- | --- |
| `classname` | The item's map name (`ammo_shells`, `weapon_shotgun`, ...). |
| `base` | New items only: the item to copy. |
| `pickup_name` | The name on the HUD and for `give` / `use` (`"Shells"`). Weapons find their ammo by this name. |
| `pickup_sound` | Sound played on pickup (`"misc/am_pkup.wav"`). |
| `world_model` | Model lying in the map. |
| `view_model` | Weapons: the model in the player's hand. |
| `icon` | HUD icon picture name (`"a_shells"`). |
| `count_width` | Digits shown next to the icon. |
| `quantity` | Ammo: how much a pickup gives. Weapons: ammo used per shot. |
| `ammo` | Weapons: the `pickup_name` of their ammo. |
| `gives` | Ammo: picking it up adds to this item instead (its `pickup_name`), e.g. a bigger box that gives `"Shells"`. |
| `flags` | Kinds, separated by spaces: `weapon ammo armor key powerup health stay_coop melee not_giveable instant_use tech flag`. |
| `rotate` | `1` spins the item in the map, `0` doesn't. |
| `precaches` | Extra models, sounds and pictures to load, separated by spaces. |

## Weapon numbers

Weapons also take the numbers their firing code uses. A field a weapon
doesn't use is ignored; fields left out keep the stock values. A new weapon
made with `base` fires like its base, with these numbers.

```
// a shotgun with a wide spray of weak pellets
{
"classname" "weapon_shotgun"
"pellets" "30"
"damage" "2"
"spread_h" "1000"
}
```

| Field | Meaning |
| --- | --- |
| `damage` | Damage of a hit (per pellet for shotguns; a rocket adds 0 to 19 to it). |
| `kick` | How hard a hit knocks the target back. |
| `pellets` | Shotguns: pellets per shot (the Super Shotgun splits them between its barrels). |
| `spread_h`, `spread_v` | Bullets and pellets: sideways and up-down spread. |
| `speed` | Projectiles: flight speed in units per second. |
| `radius_damage` | Rockets: the most splash damage. |
| `damage_radius` | Explosions: how far the splash reaches (for grenades, also the splash damage's reach). |

Which weapon uses which:

| Weapon | Fields |
| --- | --- |
| Blaster, HyperBlaster | `damage`, `speed` |
| Machinegun, Chaingun | `damage`, `kick`, `spread_h`, `spread_v` |
| Shotgun, Super Shotgun | `damage`, `kick`, `pellets`, `spread_h`, `spread_v` |
| Railgun | `damage`, `kick` |
| Grenade Launcher | `damage`, `speed`, `damage_radius` |
| Hand Grenades | `damage`, `damage_radius` |
| Rocket Launcher | `damage`, `speed`, `radius_damage`, `damage_radius` |
| BFG10K | `damage`, `speed`, `damage_radius` |

Quad Damage still multiplies the numbers. How fast a weapon fires comes from
its animation and isn't a field yet. The mission pack weapons don't read
these fields yet.

Mistakes (an unknown field, a word where a number goes, a new item without
`base`) are printed to the console and skipped; the rest of the file still
loads.

## Monsters

Monsters go in a second file next to the items, `defs/<folder>.monsters`, in
the same format:

```
// a tougher soldier, in its own skin, that hits three times as hard
{
"classname" "monster_soldier_elite"
"base" "monster_soldier"
"health" "300"
"skin" "2"
"damage_percent" "300"
}
```

A block for a stock monster (`monster_soldier`, `monster_tank`, ...) changes
every one of them; a new `classname` with `"base"` spawns as that stock
monster, with the changes, and can be placed in maps or spawned by scripts
(`game.spawn`). A base has to be a stock monster.

| Field | Meaning |
| --- | --- |
| `health` | Health, replacing the stock value (and its skill scaling). |
| `gib_health` | How far below 0 health the body gibs (`-40` or `40` both mean 40 below). |
| `mass` | Weight: how far hits knock it back. |
| `skin` | Skin number of its model (stock monsters have 0 and 1 for the pain skin; some have more). |
| `scale` | Drawn size, 0.1 to 10. Only the look: the size it takes up in the world stays. |
| `damage_percent` | How hard its attacks hit, in percent (`300` = three times, `0` = none). |
| `drop_height` | How far it drops off a ledge when it follows a path, 0 to 256 (`0` = never; ledges come in steps of 64). Stock: 256 for most, 192 for Gladiators, Medics, Brains and Iron Maidens, 128 for Tanks, Shamblers and Arachnids, 64 for the Supertank, 0 for bosses. A new monster drops as its base. |

Its behaviour, attacks and speed are its base's. Code that checks a monster
by classname (a few stock special cases) sees the new name.

## Things to know

- Item and monster files are read once, when the game starts. Restart the
  game after changing one.
- Saves match items by classname, so a save keeps working when mods add or
  remove items. An item the game no longer has is dropped from the save, with
  a message in the console (a weapon in hand becomes the Blaster). Saves made
  before this version still need the same items as when they were saved.
- An ammo item made with `base` has its own count, separate from its base's,
  unless it has `gives`: a new "ammo_shells_big" with `"gives" "Shells"` adds
  to Shells.
- Renaming a stock item's `pickup_name` also renames it for the weapons that
  use it as ammo: change their `ammo` too.
