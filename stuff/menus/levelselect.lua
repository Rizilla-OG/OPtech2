-- optech2: the level select page (levelselect.menu)
local campaigns = {}

local function shown()
	local index = math.floor(menu.value("ui_campaign"))

	if index < 0 or index >= #campaigns then
		return 0
	end

	return index
end

function on_open()
	campaigns = menu.campaigns()
	menu.set("ui_campaign", shown())
end

-- optech2: the campaign choice, left out when there is only one without a name (as stock)
function campaign_choice()
	if #campaigns == 0 or (#campaigns == 1 and campaigns[1].name == "(noname)") then
		return {}
	end

	local options = {}

	for _, campaign in ipairs(campaigns) do
		options[#options + 1] = campaign.name .. "=" .. campaign.index
	end

	return { { type = "choice", label = "campaign", cvar = "ui_campaign", options = table.concat(options, ";"),
		lua = "campaign_changed" } }
end

function campaign_changed()
	menu.reload()
end

function levels()
	local campaign = campaigns[shown() + 1]
	local lines = {}

	if not campaign then
		return lines
	end

	for level, name in ipairs(campaign.levels) do
		lines[#lines + 1] = { label = name, command = "menu_pickstart " .. campaign.index .. " " .. (level - 1) }
	end

	return lines
end
