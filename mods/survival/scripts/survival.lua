-- survival.lua: a tougher start, a capped Quad, and a pickup tally.
-- An example OpenTech2 gameplay script (see mods/SCRIPTING.md).
-- Runs on every level; its variables start fresh each level and after a load.

print("survival loaded on " .. game.map())

-- settings, read from the console so a server can change them
local start_shells = tonumber(game.cvar("survival_shells")) or 10
local quad_limit = 2            -- Quads per player per level

local pickups = {}              -- player number -> items picked up this level
local quads = {}                -- player number -> Quads taken this level

game.on("level_start", function(map)
    game.bprint("Survival rules are on for " .. map)
end)

game.on("player_spawn", function(player)
    -- start weaker but armed
    game.set_health(player, 75)
    game.give(player, "Shotgun")
    game.give(player, "Shells", start_shells)
    game.use(player, "Shotgun")

    pickups[player] = 0
    game.centerprint(player, "Survive, " .. game.name(player) .. "!")
end)

-- asked on every touch (also for items the player can't take): only decide here
game.on("item_pickup", function(player, item)
    -- a Quad limit per level
    if item == "Quad Damage" and (quads[player] or 0) >= quad_limit then
        return false            -- refused: it stays on the floor
    end
end)

-- after an item was really taken: count here
game.on("item_taken", function(player, item)
    if item == "Quad Damage" then
        quads[player] = (quads[player] or 0) + 1
    end

    -- every 10th pickup earns a bonus
    pickups[player] = (pickups[player] or 0) + 1
    if pickups[player] % 10 == 0 then
        game.give(player, "Grenades", 2)
        game.cprint(player, string.format("%d pickups in %.0f s: 2 grenades for you",
            pickups[player], game.time()))
    end
end)
