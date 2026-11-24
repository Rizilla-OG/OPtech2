-- optech2: the player setup page (playerconfig.menu)
local models = {}

local function find(list, wanted)
	for index, name in ipairs(list) do
		if name:lower() == wanted:lower() then
			return index - 1
		end
	end

	return 0
end

function on_open()
	models = menu.playermodels()

	-- optech2: "male/grunt" -> model and skin, as the stock page reads the skin cvar
	local model_name, skin_name = menu.cvar("skin"):match("^([^/\\]+)[/\\](.+)$")

	if not model_name then
		model_name, skin_name = "male", "grunt"
	end

	local names = {}

	for _, model in ipairs(models) do
		names[#names + 1] = model.name
	end

	local model = find(names, model_name)
	menu.set("ui_pmodel", model)
	menu.set("ui_pskin", models[model + 1] and find(models[model + 1].skins, skin_name) or 0)

	if menu.value("hand") < 0 or menu.value("hand") > 2 then
		menu.set("hand", 0)
	end
end

local function store_skin()
	local model = models[math.floor(menu.value("ui_pmodel")) + 1]

	if model then
		local skin = model.skins[math.floor(menu.value("ui_pskin")) + 1]

		if skin then
			menu.set("skin", model.name .. "/" .. skin)
		end
	end
end

local function indexed(list)
	local options = {}

	for index, name in ipairs(list) do
		options[#options + 1] = name .. "=" .. (index - 1)
	end

	return table.concat(options, ";")
end

function model_choice()
	local names = {}

	for _, model in ipairs(models) do
		names[#names + 1] = model.name
	end

	return { { type = "choice", cvar = "ui_pmodel", options = indexed(names), x = -56, cursor = -48,
		lua = "model_changed" } }
end

function model_changed()
	menu.set("ui_pskin", 0)
	store_skin()
	menu.reload()
end

function skin_choice()
	local model = models[math.floor(menu.value("ui_pmodel")) + 1]

	return { { type = "choice", cvar = "ui_pskin", options = model and indexed(model.skins) or "none=0",
		x = -56, cursor = -48, lua = "skin_changed" } }
end

function skin_changed()
	store_skin()
end

-- optech2: the stock speeds; another rate shows as user defined (picking that keeps it)
function rate_choice()
	local rate = math.floor(menu.value("rate"))
	local options = "28.8 Modem=2500;33.6 Modem=3200;Single ISDN=5000;Dual ISDN/Cable=10000;T1/LAN=25000"
	local known = { [2500] = true, [3200] = true, [5000] = true, [10000] = true, [25000] = true }

	if not known[rate] then
		options = options .. ";User defined=" .. rate
	else
		options = options .. ";User defined=" .. rate .. " "
	end

	return { { type = "choice", cvar = "rate", options = options, x = -56, cursor = -48 } }
end
