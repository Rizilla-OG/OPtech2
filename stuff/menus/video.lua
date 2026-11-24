-- optech2: the video page (video.menu). The settings that restart the video are
-- copied into ui_v_* cvars when the page opens and written back by apply().
local PENDING = {
	-- ui_v_ cvar, the real one; restart: changing it needs vid_restart
	{ "ui_v_renderer", "vid_renderer", restart = true },
	{ "ui_v_mode", "r_mode", restart = true },
	{ "ui_v_fullscreen", "vid_fullscreen", restart = true },
	{ "ui_v_vsync", "r_vsync", restart = true },
	{ "ui_v_anisotropic", "r_anisotropic", restart = true },
	{ "ui_v_msaa", "r_msaa_samples", restart = true },
}

local SCALES = { "r_hudscale", "r_consolescale", "r_menuscale", "crosshair_scale" }

local function renderer_family()
	local renderer = menu.cvar("vid_renderer")

	if renderer == "gles1" then return "gl1" end
	if renderer == "gles3" then return "gl3" end
	return renderer
end

-- optech2: the ui scale shown: one number for all four, else "custom"
local function current_uiscale()
	local hud = menu.value("r_hudscale")

	for _, name in ipairs(SCALES) do
		if menu.value(name) ~= hud then
			return "custom"
		end
	end

	if hud < 0 then
		return "-1"
	end

	if hud >= 1 and hud <= 6 and hud == math.floor(hud) then
		return tostring(math.floor(hud))
	end

	return "custom"
end

-- optech2: the nearest power of two at most the value, as the stock list shows it (0 = off)
local function power_of_two(value)
	local shown = 0

	for _, step in ipairs({ 2, 4, 8, 16 }) do
		if value >= step then
			shown = step
		end
	end

	return shown
end

function on_open()
	for _, setting in ipairs(PENDING) do
		menu.set(setting[1], menu.cvar(setting[2]))
	end

	menu.set("ui_v_uiscale", current_uiscale())
	menu.set("ui_v_anisotropic", power_of_two(menu.value("r_anisotropic")))
	menu.set("ui_v_msaa", power_of_two(menu.value("r_msaa_samples")))
	menu.set("ui_v_vsync", menu.value("r_vsync") ~= 0 and 1 or 0)
	gl1_before = { menu.cvar("gl1_intensity"), menu.cvar("gl1_minlight") }
end

function renderer()
	local options = {}

	for _, entry in ipairs(menu.renderers()) do
		options[#options + 1] = entry.name .. "=" .. entry.value
	end

	return { { type = "choice", label = "renderer", alttext = "$m_graphics_api", cvar = "ui_v_renderer",
		options = table.concat(options, ";") } }
end

local function slider(label, cvar, min, max, step, format, status)
	return { type = "slider", label = label, cvar = cvar, min = min, max = max, step = step or 0.1,
		format = format, status = status }
end

local function toggle(label, cvar, status)
	return { type = "toggle", label = label, cvar = cvar, status = status }
end

-- optech2: what each renderer has, as the stock menu
function renderer_settings()
	local family = renderer_family()

	if family == "gl3" or family == "gl4" then
		return {
			slider("color intensity", family .. "_intensity", 0.1, 5.0),
			slider("overbrights", family .. "_overbrightbits", 0.1, 5.0),
			toggle("color light", family .. "_colorlight"),
			toggle("bloom", "r_bloom"),
			toggle("dynamic light", "r_dynamic", "lights from shots and explosions"),
		}
	elseif family == "vk" then
		return {
			slider("color intensity", "vk_intensity", 0, 5, 1, "%.0f"),
			slider("overbrights", "vk_overbrightbits", 0.1, 5.0),
			toggle("dynamic light", "r_dynamic", "lights from shots and explosions"),
		}
	elseif family == "gl1" then
		return {
			slider("color intensity", "gl1_intensity", 1.0, 10.0, 0.1, nil, "applies after apply"),
			slider("overbrights", "gl1_overbrightbits", 0, 2, 1, "%.0f"),
			slider("min. light level", "gl1_minlight", 0, 32, 4, "%.0f", "applies after apply"),
			toggle("dynamic light", "r_dynamic", "lights from shots and explosions"),
		}
	end

	return {}
end

-- optech2: the sample counts the GPU gives (vid_msaa_max, from the renderer; -1 = not known)
function msaa()
	local max = menu.value("vid_msaa_max")
	local options = { "off=0" }

	for _, samples in ipairs({ 2, 4, 8, 16 }) do
		if max < 0 or samples <= max then
			options[#options + 1] = samples .. "x=" .. samples
		end
	end

	return { { type = "choice", label = "multisampling", cvar = "ui_v_msaa", options = table.concat(options, ";"),
		status = max > 0 and ("your GPU allows up to " .. math.floor(max) .. "x") or nil } }
end

function filter()
	local family = renderer_family()

	if family == "gl1" or family == "gl3" then
		return { { type = "choice", label = "texture filter", cvar = "gl_texturemode",
			options = "pixelated=GL_NEAREST;linear=GL_LINEAR;standard=GL_LINEAR_MIPMAP_NEAREST;"
				.. "trilinear=GL_LINEAR_MIPMAP_LINEAR" } }
	elseif family == "soft" then
		return { { type = "choice", label = "texture filter", cvar = "sw_texture_filtering", options = "off=0;on=1" } }
	end

	return {}
end

function apply()
	local restart = false

	for _, setting in ipairs(PENDING) do
		local pending = menu.cvar(setting[1])

		if pending ~= "" and menu.value(setting[1]) ~= menu.value(setting[2]) or
			(setting[2] == "vid_renderer" and pending ~= menu.cvar("vid_renderer")) then
			menu.set(setting[2], pending)
			restart = restart or setting.restart
		end
	end

	-- optech2: a fullscreen window always takes the desktop's size
	if menu.value("vid_fullscreen") == 2 then
		menu.set("r_mode", -2)
	end

	local scale = menu.cvar("ui_v_uiscale")

	if scale ~= "custom" then
		for _, name in ipairs(SCALES) do
			menu.set(name, tonumber(scale))
		end
	end

	if renderer_family() == "gl1" and (gl1_before[1] ~= menu.cvar("gl1_intensity") or
		gl1_before[2] ~= menu.cvar("gl1_minlight")) then
		restart = true
	end

	if restart then
		menu.command("vid_restart")
	end

	menu.command("menu_close")
end
