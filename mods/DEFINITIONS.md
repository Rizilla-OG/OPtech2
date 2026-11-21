# Item definitions

Mods can change items and add new ones with a text file, no compiler needed.
This is the first part of the data-driven definitions; weapons' firing and
monsters come next.

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
| `flags` | Kinds, separated by spaces: `weapon ammo armor key powerup health stay_coop melee not_giveable instant_use tech flag`. |
| `rotate` | `1` spins the item in the map, `0` doesn't. |
| `precaches` | Extra models, sounds and pictures to load, separated by spaces. |

Mistakes (an unknown field, a word where a number goes, a new item without
`base`) are printed to the console and skipped; the rest of the file still
loads.

## Things to know

- Item files are read once, when the game starts. Restart the game after
  changing one.
- Saves store items by number. Adding or removing items, or changing the mod
  folders, can mix up items in older saves.
- An ammo item made with `base` has its own count, separate from its base's:
  a new "ammo_shells_big" gives "Shell Box", not "Shells".
- Renaming a stock item's `pickup_name` also renames it for the weapons that
  use it as ammo: change their `ammo` too.
