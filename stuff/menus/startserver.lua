-- optech2: the start server page (startserver.menu)
local maps = {}

local function shown()
	local index = math.floor(menu.value("ui_startmap"))

	if index < 0 or index >= #maps then
		return 0
	end

	return index
end

function on_open()
	maps = menu.maps()
	menu.set("ui_startmap", shown())

	-- optech2: rules from the current game type, as the stock page
	if menu.value("coop") ~= 0 then
		menu.set("ui_rules", 1)
	elseif menu.value("ctf") ~= 0 then
		menu.set("ui_rules", 3)
	elseif menu.value("deathmatch") ~= 0 then
		menu.set("ui_rules", 0)
	else
		menu.set("ui_rules", 2)
	end

	-- optech2: a single player setting (1) offers 8 players
	if menu.value("maxclients") == 1 then
		menu.set("ui_maxclients", 8)
	else
		menu.set("ui_maxclients", menu.cvar("maxclients"))
	end
end

-- optech2: the map line: a choice of one, the shown map; left and right step through them
function map_line()
	local map = maps[shown() + 1]
	local line = map and map.line or "(no maps)"

	return { { type = "choice", label = "initial map", id = "map", gap = 18,
		options = line:gsub("[;=]", " ") .. "=0" } }
end

function keys(key, id)
	if id == "map" and #maps > 0 and (key == "leftarrow" or key == "rightarrow") then
		local step = (key == "leftarrow") and -1 or 1
		menu.set("ui_startmap", (shown() + step) % #maps)
		menu.reload()
		return true
	end

	return false
end

function begin()
	local map = maps[shown() + 1]

	if map then
		menu.command("menu_beginserver " .. map.name .. " " .. math.floor(menu.value("ui_rules")))
	end
end
