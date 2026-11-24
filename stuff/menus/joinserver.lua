-- optech2: the join server page (joinserver.menu): the stock menu's 8 lines
function servers()
	local lines = {}

	for index, server in ipairs(menu.servers()) do
		if index > 8 then
			break
		end

		lines[#lines + 1] = { label = server.name, status = server.address,
			command = "connect " .. server.address .. "; menu_close", disabled = server.address == "" }
	end

	return lines
end
