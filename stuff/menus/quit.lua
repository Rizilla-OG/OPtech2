-- optech2: the quit page (quit.menu): y quits, n goes back, as the stock one
function keys(key, id)
	if key == "y" then
		menu.command("quit")
		return true
	end

	if key == "n" then
		menu.command("menu_back")
		return true
	end

	return false
end
