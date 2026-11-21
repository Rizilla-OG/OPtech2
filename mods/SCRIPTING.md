# Gameplay scripts (Lua)

OpenTech2 runs gameplay scripts written in [Lua 5.4](https://www.lua.org/manual/5.4/).
Scripts need no compiler and no game library of their own, and several mods'
scripts run together.

For a complete example, see [survival](survival/README.md): a tougher start,
a Quad limit and a pickup bonus in one short script.

This is the first version of the scripting API. It will grow with the
data-driven definitions (items, weapons and monsters in text files).

## Where scripts go

Each mod folder runs one script, named after the folder, in lower case:

```
mymod/scripts/mymod.lua
```

Mod folders are stacked with `fs_mods` and `game` (see the README):

```
OpenTech2 +set fs_mods hires,mymod +set game othermod
```

Scripts run in the same order the folders are stacked, lowest first:
`baseq2`, then each `fs_mods` folder, then the `game` folder. A script can also
come from a pack file (`.pak`, `.pk3`) in its folder.

Scripts start again on every level and after loading a save. Their own
variables aren't saved in save games yet, so keep anything that must last in
the game's own state (a player's items and health).

## A first script

```lua
print("mymod loaded")

-- every player starts with extra shells
game.on("player_spawn", function(player)
    game.give(player, "Shells", 20)
    game.centerprint(player, "Welcome, " .. game.name(player))
end)

-- no armor shards on this server
game.on("item_pickup", function(player, item)
    if item == "Armor Shard" then
        return false
    end
end)

-- count what players actually take
game.on("item_taken", function(player, item)
    print(game.name(player) .. " took " .. item)
end)
```

## Events

Register a function with `game.on(event, function)`. Each event can have
several functions, from any number of scripts: they run in the order they were
added.

| Event | Arguments | Notes |
| --- | --- | --- |
| `level_start` | map name | After the level's entities spawned. Not after loading a save. |
| `player_spawn` | player | A player entered the level or respawned. |
| `item_pickup` | player, item's pickup name | Before the pickup. Return `false` to refuse it. It's asked on every touch, every frame the player stands on an item, also for items they can't take (full ammo or health): don't count pickups here. |
| `item_taken` | player, item's pickup name | After the player took an item: count pickups here. |

## Functions

Players are numbers, from 1 to `maxclients`.

| Function | Does |
| --- | --- |
| `game.on(event, function)` | Runs the function on the event. |
| `game.time()` | Level time in seconds. |
| `game.map()` | Map name. |
| `game.cvar(name)` | A console variable's value, as a string (`""` if it doesn't exist). |
| `game.bprint(text)` | Prints to every player. |
| `game.cprint(player, text)` | Prints to one player's console. |
| `game.centerprint(player, text)` | Prints in the middle of one player's screen. |
| `game.name(player)` | The player's name. |
| `game.health(player)` | The player's health. |
| `game.set_health(player, n)` | Sets the health of a living player (1 to 10000). |
| `game.give(player, item, n)` | Adds `n` (default 1) of an item, by its pickup name: `"Shells"`, `"Health"`, `"Armor Shard"`. |
| `game.count(player, item)` | How many of an item the player has. |
| `game.weapon(player)` | The pickup name of the weapon in the player's hand (`""` if none). |

`print(...)` writes to the server console. With `+set cheats 1`, the console
command `lua <code>` runs one line of Lua, handy for trying things out:

```
lua print(game.map(), game.health(1))
```

## The sandbox

Scripts can use Lua's `string`, `table`, `math`, `utf8` and `coroutine`
libraries and the basic functions. To keep players safe when they install a
mod, scripts can't touch files or the system:

- no `io`, `os`, `package` or `debug` libraries, no `require`, `dofile` or
  `loadfile`
- `load` takes Lua source only, never compiled bytecode
- one call (a script loading, or an event function) is stopped after about 10
  million instructions, so an endless loop can't freeze the game
- all scripts together can use up to 64 MB of memory

An error in a script is printed to the console (`script error in ...`) and the
game carries on.
