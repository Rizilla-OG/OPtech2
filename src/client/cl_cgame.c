/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * optech2: host for client game modules with the re-release cgame API
 * (header/cgame_rerelease.h). When a map starts, the client loads the game
 * library from the game folders (or the library named by "cl_cgame") and
 * calls its GetCGameAPI(). While a module is loaded it draws the HUD,
 * layouts, inventory and center prints; without one the built-in HUD
 * stays as it was.
 *
 * =======================================================================
 */

#include "header/client.h"
#include "header/cgame_rerelease.h"
#include "../common/header/optech2_ext.h"

/* module allocations live in their own zone tags, apart from the server's game */
#define CGAME_TAG_OFFSET 1000

static cvar_t *cl_cgame; /* library to load instead of the game folder's game library */
static void *cgame_library;
static cgame_export_t *cgame;
static const optech2_pmove_ext_t *cgame_pmove; /* the game library's player movement, or NULL */
static cgame_import_t cgame_import;

/* ------------------------------------------------------------------ */
/* imports */

static void
CG_Print(const char *msg)
{
	Com_Printf("%s", msg);
}

static void
CG_Error(const char *message)
{
	Com_Error(ERR_DROP, "client game: %s", message);
}

/* the module uses the re-release configstring numbering */
static const char *
CG_GetConfigString(int num)
{
	int ours;

	if (num < 0)
	{
		return "";
	}

	ours = P_ConvertConfigStringFrom(num, PROTOCOL_RR22_VERSION);

	if (ours < 0 || ours >= MAX_CONFIGSTRINGS)
	{
		return "";
	}

	return cl.configstrings[ours];
}

static void *
CG_TagMalloc(size_t size, int tag)
{
	void *block = Z_TagMalloc(size, (unsigned short)(tag + CGAME_TAG_OFFSET));

	memset(block, 0, size);
	return block;
}

static void
CG_TagFree(void *block)
{
	Z_Free(block);
}

static void
CG_FreeTags(int tag)
{
	Z_FreeTags((unsigned short)(tag + CGAME_TAG_OFFSET));
}

static cvar_t *
CG_Cvar(const char *name, const char *value, int flags)
{
	return Cvar_Get(name, value, flags);
}

static cvar_t *
CG_CvarSet(const char *name, const char *value)
{
	return Cvar_Set(name, value);
}

static cvar_t *
CG_CvarForceSet(const char *name, const char *value)
{
	return Cvar_ForceSet(name, value);
}

static void
CG_AddCommandString(const char *text)
{
	Cbuf_AddText(text);
}

/* OPtech2's own additions arrive here (M2 step 4) */
static void *
CG_GetExtension(const char *name)
{
	return NULL;
}

static bool
CG_FrameValid(void)
{
	return cl.frame.valid;
}

static float
CG_FrameTime(void)
{
	return cls.rframetime;
}

static uint64_t
CG_ClientTime(void)
{
	return (uint64_t)cl.time;
}

static uint64_t
CG_ClientRealTime(void)
{
	return (uint64_t)cls.realtime;
}

static int32_t
CG_ServerFrame(void)
{
	return cl.frame.serverframe;
}

static int32_t
CG_ServerProtocol(void)
{
	return cls.serverProtocol;
}

static const char *
CG_GetClientName(int32_t index)
{
	return (index >= 0 && index < MAX_CLIENTS) ? cl.clientinfo[index].name : "";
}

static const char *
CG_GetClientPic(int32_t index)
{
	return (index >= 0 && index < MAX_CLIENTS) ? cl.clientinfo[index].iconname : "";
}

static const char *
CG_GetClientDogtag(int32_t index)
{
	return "";
}

/* name of the first key bound to this command, or "" */
static const char *
CG_GetKeyBinding(const char *binding)
{
	int key;

	for (key = 0; key < K_LAST; key++)
	{
		if (keybindings[key] && !Q_stricmp(keybindings[key], binding))
		{
			return Key_KeynumToString(key);
		}
	}

	return "";
}

static bool
CG_RegisterPic(const char *name)
{
	return Draw_FindPic(name) != NULL;
}

static void
CG_GetPicSize(int *w, int *h, const char *name)
{
	*w = *h = 0;
	Draw_GetPicSize(w, h, name);
}

static void
CG_DrawChar(int x, int y, int scale, int num, bool shadow)
{
	Draw_CharScaled(x, y, num, (float)scale);
}

static void
CG_DrawPic(int x, int y, int w, int h, const char *name)
{
	Draw_StretchPic(x, y, w, h, name);
}

static void
CG_DrawColorPic(int x, int y, int w, int h, const char *name, const cg_rgba_t *color)
{
	int pic_w, pic_h;
	vec3_t tint;

	if (!color || (color->r == 255 && color->g == 255 && color->b == 255))
	{
		Draw_StretchPic(x, y, w, h, name);
		return;
	}

	/* tinted pictures can only be drawn scaled evenly */
	Draw_GetPicSize(&pic_w, &pic_h, name);
	VectorSet(tint, color->r / 255.0f, color->g / 255.0f, color->b / 255.0f);
	Draw_PicScaledCol(x, y, name, pic_w ? (float)w / pic_w : 1.0f, tint, NULL);
}

static void
CG_SetAltTypeface(bool enabled)
{
}

/* the re-release draws strings in its own font; here the console font */
static void
CG_DrawFontString(const char *str, int x, int y, int scale, const cg_rgba_t *color, bool shadow, int align)
{
	int width = (int)strlen(str) * 8 * scale;

	if (align == CG_ALIGN_CENTER)
	{
		x -= width / 2;
	}
	else if (align == CG_ALIGN_RIGHT)
	{
		x -= width;
	}

	Draw_StringScaled(x, y, (float)scale, false, str);
}

static cg_vec2_t
CG_MeasureFontString(const char *str, int scale)
{
	cg_vec2_t size;

	size.x = (float)(strlen(str) * 8 * scale);
	size.y = (float)(8 * scale);
	return size;
}

static float
CG_FontLineHeight(int scale)
{
	return (float)(10 * scale);
}

/* chat input stays with the engine's console */
static bool
CG_GetTextInput(const char **msg, bool *is_team)
{
	return false;
}

static int32_t
CG_GetWarnAmmoCount(int32_t weapon_id)
{
	return 0;
}

/* the localized text of a "$key" (localization/loc_<language>.txt), with
   its "{}" / "{N}" filled from args; other text as it is */
static const char *
CG_Localize(const char *base, const char **args, size_t num_args)
{
	static char localized[2][1024]; /* two, so a result can be an argument */
	static int which;
	const char *localized_args[16];
	char localized_arg_text[16][256];
	size_t i;

	if (!base)
	{
		return "";
	}

	if (num_args > 16)
	{
		num_args = 16;
	}

	for (i = 0; i < num_args; i++)
	{
		Q_strlcpy(localized_arg_text[i], SV_LocalizationMessage(args[i] ? args[i] : "", NULL),
			sizeof(localized_arg_text[i]));
		localized_args[i] = localized_arg_text[i];
	}

	which ^= 1;
	CL_LocalizeFormat(SV_LocalizationMessage(base, NULL), localized_args, (int)num_args,
		localized[which], sizeof(localized[which]));
	return localized[which];
}

static int32_t
CG_DrawBind(int32_t isplit, const char *binding, const char *purpose, int x, int y, int scale)
{
	const char *key = CG_GetKeyBinding(binding);
	char text[256];

	snprintf(text, sizeof(text), "[%s] %s", key[0] ? key : binding, purpose);
	Draw_StringScaled(x - (int)strlen(text) * 4 * scale, y, (float)scale, false, text);
	return 10 * scale;
}

static bool
CG_InAutoDemoLoop(void)
{
	return cl.attractloop;
}

/* ------------------------------------------------------------------ */
/* loading */

static void
CL_CGame_FillImports(void)
{
	memset(&cgame_import, 0, sizeof(cgame_import));

	cgame_import.tick_rate = cl.tickrate ? cl.tickrate : 10;
	cgame_import.frame_time_s = 1.0f / cgame_import.tick_rate;
	cgame_import.frame_time_ms = 1000 / cgame_import.tick_rate;

	cgame_import.Com_Print = CG_Print;
	cgame_import.get_configstring = CG_GetConfigString;
	cgame_import.Com_Error = CG_Error;
	cgame_import.TagMalloc = CG_TagMalloc;
	cgame_import.TagFree = CG_TagFree;
	cgame_import.FreeTags = CG_FreeTags;
	cgame_import.cvar = CG_Cvar;
	cgame_import.cvar_set = CG_CvarSet;
	cgame_import.cvar_forceset = CG_CvarForceSet;
	cgame_import.AddCommandString = CG_AddCommandString;
	cgame_import.GetExtension = CG_GetExtension;
	cgame_import.CL_FrameValid = CG_FrameValid;
	cgame_import.CL_FrameTime = CG_FrameTime;
	cgame_import.CL_ClientTime = CG_ClientTime;
	cgame_import.CL_ClientRealTime = CG_ClientRealTime;
	cgame_import.CL_ServerFrame = CG_ServerFrame;
	cgame_import.CL_ServerProtocol = CG_ServerProtocol;
	cgame_import.CL_GetClientName = CG_GetClientName;
	cgame_import.CL_GetClientPic = CG_GetClientPic;
	cgame_import.CL_GetClientDogtag = CG_GetClientDogtag;
	cgame_import.CL_GetKeyBinding = CG_GetKeyBinding;
	cgame_import.Draw_RegisterPic = CG_RegisterPic;
	cgame_import.Draw_GetPicSize = CG_GetPicSize;
	cgame_import.SCR_DrawChar = CG_DrawChar;
	cgame_import.SCR_DrawPic = CG_DrawPic;
	cgame_import.SCR_DrawColorPic = CG_DrawColorPic;
	cgame_import.SCR_SetAltTypeface = CG_SetAltTypeface;
	cgame_import.SCR_DrawFontString = CG_DrawFontString;
	cgame_import.SCR_MeasureFontString = CG_MeasureFontString;
	cgame_import.SCR_FontLineHeight = CG_FontLineHeight;
	cgame_import.CL_GetTextInput = CG_GetTextInput;
	cgame_import.CL_GetWarnAmmoCount = CG_GetWarnAmmoCount;
	cgame_import.Localize = CG_Localize;
	cgame_import.SCR_DrawBind = CG_DrawBind;
	cgame_import.CL_InAutoDemoLoop = CG_InAutoDemoLoop;
}

/* opens a library as the game library; false when it won't open */
static qboolean
CL_CGame_Open(const char *path)
{
	void *library;

	/* with no symbol asked for, only the handle tells whether it opened */
	Sys_LoadLibrary(path, NULL, &library);

	if (!library)
	{
		return false;
	}

	cgame_library = library;
	return true;
}

/* the game library's own player movement for prediction, if it has one */
static void
CL_CGame_FindPmove(void)
{
	OPtech2_GetExtension_t get_extension =
		(OPtech2_GetExtension_t)Sys_GetProcAddress(cgame_library, "OPtech2_GetExtension");
	const optech2_pmove_ext_t *pmove_ext =
		get_extension ? get_extension(OPTECH2_EXT_PMOVE) : NULL;

	if (pmove_ext && pmove_ext->version == OPTECH2_PMOVE_EXT_VERSION &&
		pmove_ext->pmove_size == (int)sizeof(pmove_t) && pmove_ext->PmoveEx)
	{
		cgame_pmove = pmove_ext;
		Com_Printf("client game: player movement from the game library\n");
	}
	else if (pmove_ext)
	{
		Com_Printf("client game: player movement version %d / size %d don't match, using the engine's\n",
			pmove_ext->version, pmove_ext->pmove_size);
	}
}

/*
 * Opens the game's library on the client side (the game folders' game
 * library, like the server's, or the one cl_cgame names) and takes what it
 * offers: its player movement for prediction and its client game module
 * for the HUD. Either can be missing; with neither the library is closed.
 */
void
CL_CGame_Load(void)
{
#if defined(_WIN32)
	const char *game_library_name = "game.dll";
#elif defined(__APPLE__)
	const char *game_library_name = "game.dylib";
#else
	const char *game_library_name = "game.so";
#endif
	GetCGameAPI_t entry;
	const char *path = NULL;
	char name[MAX_OSPATH];

	if (cgame_library)
	{
		return;
	}

	if (!cl_cgame)
	{
		cl_cgame = Cvar_Get("cl_cgame", "", 0);
	}

	if (cl_cgame->string[0])
	{
		if (!CL_CGame_Open(cl_cgame->string))
		{
			Com_Printf("cl_cgame: can't open %s\n", cl_cgame->string);
		}
	}
	else
	{
		/* the game library of the game folders, like the server's */
		while ((path = FS_NextPath(path)))
		{
			FILE *f;

			snprintf(name, sizeof(name), "%s/%s", path, game_library_name);
			f = Q_fopen(name, "rb");

			if (f)
			{
				fclose(f);
				CL_CGame_Open(name);
				break; /* the first game library found is the game's */
			}
		}
	}

	if (!cgame_library)
	{
		return;
	}

	CL_CGame_FindPmove();

	entry = (GetCGameAPI_t)Sys_GetProcAddress(cgame_library, "GetCGameAPI");

	if (entry)
	{
		CL_CGame_FillImports();
		cgame = entry(&cgame_import);

		if (!cgame || cgame->apiversion != CGAME_API_VERSION)
		{
			Com_Printf("client game: API version %d, need %d; using the built-in HUD\n",
				cgame ? cgame->apiversion : -1, CGAME_API_VERSION);
			cgame = NULL;
		}
	}

	if (!cgame)
	{
		/* no client game module: keep the library only for its movement */
		if (!cgame_pmove)
		{
			Sys_FreeLibrary(cgame_library);
			cgame_library = NULL;
		}

		return;
	}

	Com_Printf("client game: loaded (cgame API %d)\n", CGAME_API_VERSION);

	cgame->Init();

	if (cgame->TouchPics)
	{
		cgame->TouchPics();
	}
}

void
CL_CGame_Unload(void)
{
	cgame_pmove = NULL;

	if (cgame)
	{
		cgame->Shutdown();
		cgame = NULL;
	}

	if (cgame_library)
	{
		Sys_FreeLibrary(cgame_library);
		cgame_library = NULL;
	}
}

qboolean
CL_CGame_Active(void)
{
	return cgame != NULL;
}

/* player movement for prediction: the game library's when it has its own,
   else the engine's copy of the shared code */
void
CL_CGame_PmoveEx(pmove_t *pmove, int *origin)
{
	if (cgame_pmove)
	{
		cgame_pmove->PmoveEx(pmove, origin);
	}
	else
	{
		PmoveEx(pmove, origin);
	}
}

/* ------------------------------------------------------------------ */
/* calls into the module */

/* our player state in the re-release layout */
static void
CL_CGame_PlayerState(cg_player_state_t *out)
{
	const player_xstate_t *ps = &cl.frame.playerstate;
	int i;

	memset(out, 0, sizeof(*out));

	/* the re-release numbers player types differently (it adds grapple and noclip) */
	switch (ps->pmove.pm_type)
	{
		case PM_SPECTATOR:
			out->pmove.pm_type = 3;
			break;
		case PM_DEAD:
			out->pmove.pm_type = 4;
			break;
		case PM_GIB:
			out->pmove.pm_type = 5;
			break;
		case PM_FREEZE:
			out->pmove.pm_type = 6;
			break;
		default:
			out->pmove.pm_type = 0;
			break;
	}

	for (i = 0; i < 3; i++)
	{
		out->pmove.origin[i] = cl.frame.origin[i] * 0.125f;
		out->pmove.velocity[i] = ps->pmove.velocity[i] * 0.125f;
		out->pmove.delta_angles[i] = SHORT2ANGLE(ps->pmove.delta_angles[i]);
		out->viewangles[i] = ps->viewangles[i];
		out->viewoffset[i] = ps->viewoffset[i];
		out->kick_angles[i] = ps->kick_angles[i];
		out->gunangles[i] = ps->gunangles[i];
		out->gunoffset[i] = ps->gunoffset[i];
	}

	out->pmove.pm_flags = ps->pmove.pm_flags;
	out->pmove.pm_time = ps->pmove.pm_time;
	out->pmove.gravity = ps->pmove.gravity;
	out->pmove.viewheight = (int8_t)ps->viewoffset[2];

	out->gunindex = ps->gunindex;
	out->gunframe = ps->gunframe;
	out->gunrate = cl.tickrate;

	for (i = 0; i < 4; i++)
	{
		out->screen_blend[i] = ps->blend[i];
	}

	out->fov = ps->fov;
	out->rdflags = (uint8_t)ps->rdflags;

	for (i = 0; i < CG_MAX_STATS && i < MAX_XSTATS; i++)
	{
		out->stats[i] = ps->stats[i];
	}
}

void
CL_CGame_DrawHUD(void)
{
	static cg_server_data_t data;
	cg_player_state_t ps;
	cg_vrect_t screen, safe_margin;
	int i, scale;

	if (!cgame || !cl.frame.valid)
	{
		return;
	}

	Q_strlcpy(data.layout, cl.layout, sizeof(data.layout));

	for (i = 0; i < CG_MAX_ITEMS; i++)
	{
		data.inventory[i] = (int16_t)cl.inventory[i];
	}

	CL_CGame_PlayerState(&ps);

	scale = (int)(SCR_GetHUDScale() + 0.5f);
	scale = scale < 1 ? 1 : scale;

	/* optech2: the re-release passes the HUD rect in HUD units (pixels
	   divided by scale; the module multiplies by scale) and the safe zone as
	   a pixel margin. Passing pixels drew the HUD off-screen at scale 2 and
	   up (1440p and 4K). */
	screen.x = scr_vrect.x / scale;
	screen.y = scr_vrect.y / scale;
	screen.width = scr_vrect.width / scale;
	screen.height = scr_vrect.height / scale;

	memset(&safe_margin, 0, sizeof(safe_margin));

	cgame->DrawHUD(0, &data, screen, safe_margin, scale, cl.playernum, &ps);
}

void
CL_CGame_CenterPrint(const char *text)
{
	static int last_center_time = -100000;
	int on_screen_msec;
	qboolean replace;

	if (!cgame || !cgame->ParseCenterPrint)
	{
		return;
	}

	/* optech2: the re-release HUD queues center prints, each typed out and
	   shown for scr_centertime before the next. Messages that come faster
	   (a map's "5... 4... 3..." countdown) fell seconds behind the game.
	   A message that arrives while the previous one may still be up
	   replaces it at once, as center prints always did in Quake II. Two
	   sent together (within 0.2 s: "Pump online." + "Valve opened.") still
	   queue, so the first isn't lost. */
	{
		int since_last = cls.realtime - last_center_time;

		on_screen_msec = (int)(Cvar_VariableValue("scr_centertime") * 1000) + 1000; /* + typing */
		replace = (since_last >= 200) && (since_last < on_screen_msec);
	}

	last_center_time = cls.realtime;

	cgame->ParseCenterPrint(text, 0, replace);
}

/* a configstring changed (our numbering); the module gets its own */
void
CL_CGame_ConfigString(int index, const char *text)
{
	if (cgame && cgame->ParseConfigString)
	{
		cgame->ParseConfigString(P_ConvertConfigStringTo(index, PROTOCOL_RR22_VERSION), text);
	}
}
