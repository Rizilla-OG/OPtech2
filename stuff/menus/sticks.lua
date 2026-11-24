-- optech2: the sticks page (sticks.menu): flick stick layouts only with a gyro (or when already set)
function on_open()
end

function layout()
	local options = "default=0;southpaw=1;legacy=2;legacy southpaw=3"

	if menu.condition("gyro") or menu.value("joy_layout") > 3 then
		options = options .. ";flick stick=4;flick stick spaw=5"
	end

	return { { type = "choice", label = "layout", cvar = "joy_layout", options = options } }
end
