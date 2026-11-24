-- optech2: the load and save pages (loadgame.menu, savegame.menu): save slots,
-- 14 a page over 4 pages (save0 - save55), as the stock menus.
SLOTS_PER_PAGE = 14
PAGES = 4

-- optech2: the page shown, kept in a cvar (the script runs afresh on each reload)
function save_page()
	local page = math.floor(menu.value("ui_savepage"))

	if page < 0 or page >= PAGES then
		return 0
	end

	return page
end

function page_status()
	local text = "pages: "

	for page = 0, PAGES - 1 do
		if page == save_page() then
			text = text .. "[" .. (page + 1) .. "]"
		else
			text = text .. " " .. (page + 1) .. " "
		end
	end

	return text
end

function show_pages()
	menu.status(page_status())
end

-- optech2: one entry per slot of the page shown; make(slot, line, valid) says what enter does
local function slot_entries(make)
	local entries = {}
	local first = save_page() * SLOTS_PER_PAGE

	for index = first, first + SLOTS_PER_PAGE - 1 do
		local slot = "save" .. index
		local line, valid = menu.save(slot)
		local entry = make(slot, line, valid)
		entry.label = line
		entry.id = slot
		entries[#entries + 1] = entry
	end

	return entries
end

function load_quick()
	local line, valid = menu.save("quick")
	return { { label = line, id = "quick", command = "load quick; menu_close", disabled = not valid } }
end

function load_slots()
	return slot_entries(function(slot, line, valid)
		return { command = "load " .. slot .. "; menu_close", disabled = not valid }
	end)
end

local function reserved(text)
	return function() menu.status(text) end
end

function save_quick()
	local line = menu.save("quick")
	return { { label = line, id = "quick", action = reserved("this slot is kept for quicksaving") } }
end

function save_slots()
	return slot_entries(function(slot, line, valid)
		if slot == "save0" then
			return { action = reserved("this slot is kept for autosaving") }
		end

		return { command = "save " .. slot .. "; menu_close" }
	end)
end

-- optech2: left/right turn the page, del (or backspace) asks, then y deletes
local deleting = nil

function save_keys(key, id)
	if deleting then
		local slot = deleting
		deleting = nil

		if key == "y" or key == "enter" then
			menu.command("menu_deletesave " .. slot)
			menu.reload()
		else
			menu.status(page_status())
		end

		return true
	end

	if key == "leftarrow" or key == "rightarrow" then
		local step = (key == "leftarrow") and -1 or 1
		menu.set("ui_savepage", (save_page() + step) % PAGES)
		menu.reload()
		return true
	end

	if (key == "del" or key == "backspace") and id ~= "" then
		local _, valid = menu.save(id)

		if valid then
			deleting = id
			menu.status("are you sure you want to delete? y")
		end

		return true
	end

	return false
end
