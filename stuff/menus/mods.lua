-- optech2: the mods page (mods.menu): a choice of the mod folders; apply switches.
local mods = {}

local function describe()
	for _, mod in ipairs(mods) do
		if mod.name == menu.cvar("ui_mod") then
			menu.status(mod.description)
			return
		end
	end

	menu.status("")
end

function on_open()
	mods = menu.modlist()
	local current = menu.cvar("game")

	if current == "" then
		current = "baseq2"
	end

	menu.set("ui_mod", current)
	describe()
end

function mod_choice()
	local options = {}

	for _, mod in ipairs(mods) do
		options[#options + 1] = mod.display .. "=" .. mod.name
	end

	return { { type = "choice", label = "mod", cvar = "ui_mod", options = table.concat(options, ";"),
		lua = "describe_choice" } }
end

-- optech2: the choice's lua: the status line follows it
function describe_choice()
	describe()
end

function apply()
	menu.command("menu_setmod " .. menu.cvar("ui_mod"))
end
