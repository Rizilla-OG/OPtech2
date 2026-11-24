-- optech2: the gyro page (gyro.menu)
function on_open()
	if menu.condition("uncalibrated") then
		menu.status("Calibration required")
	end
end

function local_space()
	return menu.value("gyro_space") == 0
end

-- optech2: the upper threshold never below the lower one (as the stock page)
function keep_thresholds()
	local lower = menu.value("gyro_accel_lower_thresh")

	if menu.value("gyro_accel_upper_thresh") < lower then
		menu.set("gyro_accel_upper_thresh", lower)
	end
end
