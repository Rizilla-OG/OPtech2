-- optech2: the single player page's first lines (menus/game.menu, "list").
-- With several episodes (the re-release data) one line each, which asks
-- for the skill next; else the four skills, which start the game.
function first_lines()
	local episodes = menu.episodes()

	if #episodes > 1 then
		local lines = {}

		for _, episode in ipairs(episodes) do
			lines[#lines + 1] = { label = episode.name, command = "menu_episode " .. episode.index }
		end

		return lines
	end

	return {
		{ label = "easy", alttext = "$m_easy", command = "menu_newgame 0" },
		{ label = "medium", alttext = "$m_medium", command = "menu_newgame 1" },
		{ label = "hard", alttext = "$m_hard", command = "menu_newgame 2" },
		{ label = "hard+", alttext = "$m_nightmare", command = "menu_newgame 3" },
	}
end
