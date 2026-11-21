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

#include <stdarg.h>
#include "header/client.h"
#include "header/cgame_rerelease.h"
#include "../common/header/optech2_ext.h"

/* module allocations live in their own zone tags, apart from the server's game */
#define CGAME_TAG_OFFSET 1000

static cvar_t *cl_cgame; /* library to load instead of the game folder's game library */
static void *cgame_library;
static cgame_export_t *cgame;
static const optech2_pmove_ext_t *cgame_pmove; /* the game library's player movement, or NULL */
static const optech2_fx_ext_t *cgame_fx; /* the game library's effects, or NULL */
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

/* optech2: stats that hold a configstring index (the layout's stat_string)
   go to the module in the re-release numbering. Strings it has no slot
   for (item names past its 256 items: mission pack, Quake II 64 and mod
   items) are parked in the last 8 re-release general strings. The game only
   writes general strings for player names (CS_GENERAL + player number), so
   the end of the range is free. */
#define CG_MAX_GENERAL 512 /* MAX_GENERAL of the re-release API (MAX_CLIENTS * 2) */
#define CG_PARKED_STRINGS 8
static int cg_parked_index[CG_PARKED_STRINGS]; /* re-release index of each parked string */
static int cg_parked_from[CG_PARKED_STRINGS]; /* our configstring index it shows */
static int cg_parked_count; /* slots in use */
static int cg_parked_next; /* slot to reuse when all are in use (oldest first) */

static int
CG_RRGeneralEnd(void)
{
	return P_ConvertConfigStringTo(CS_GENERAL, PROTOCOL_RR22_VERSION) + CG_MAX_GENERAL;
}

/* our configstring index as the module will ask for it */
static int
CG_ConfigStringIndexForModule(int ours)
{
	int rr, i;

	if (ours < 0 || ours >= MAX_CONFIGSTRINGS)
	{
		return ours;
	}

	rr = P_ConvertConfigStringTo(ours, PROTOCOL_RR22_VERSION);

	if (rr >= 0 && P_ConvertConfigStringFrom(rr, PROTOCOL_RR22_VERSION) == ours)
	{
		return rr; /* has a re-release slot */
	}

	for (i = 0; i < cg_parked_count; i++)
	{
		if (cg_parked_from[i] == ours)
		{
			return cg_parked_index[i];
		}
	}

	if (cg_parked_count < CG_PARKED_STRINGS)
	{
		cg_parked_from[cg_parked_count] = ours;
		cg_parked_index[cg_parked_count] = CG_RRGeneralEnd() - 1 - cg_parked_count;
		return cg_parked_index[cg_parked_count++];
	}

	/* all in use: reuse the oldest slot rather than hand back an index the
	   module would read as a different string */
	i = cg_parked_next;
	cg_parked_next = (cg_parked_next + 1) % CG_PARKED_STRINGS;
	cg_parked_from[i] = ours;
	return cg_parked_index[i];
}

/* stats the status bar or layout shows as text ("stat_string N",
   "loc_stat_string N"...): a bit per stat */
static void
CG_FindStringStats(const char *layout, qboolean *string_stats)
{
	const char *p = layout;

	while (p && (p = strstr(p, "stat_string")))
	{
		int stat;

		p += strlen("stat_string");

		while (*p && *p != ' ')
		{
			p++; /* "_multiline" and the like */
		}

		stat = atoi(p);

		if (stat >= 0 && stat < CG_MAX_STATS)
		{
			string_stats[stat] = true;
		}
	}
}

/* the module uses the re-release configstring numbering */
static const char *
CG_GetConfigString(int num)
{
	int ours, i;

	if (num < 0)
	{
		return "";
	}

	for (i = 0; i < cg_parked_count; i++)
	{
		if (cg_parked_index[i] == num)
		{
			return cl.configstrings[cg_parked_from[i]];
		}
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

/* ------------------------------------------------------------------ */
/* effects extension: calls the game library's effect code can make */

extern cparticle_t *active_particles, *free_particles;

/* the extension's structs must be the engine's, byte for byte */
_Static_assert(sizeof(optech2_particle_t) == sizeof(cparticle_t), "optech2_particle_t layout");
_Static_assert(offsetof(optech2_particle_t, alphavel) == offsetof(cparticle_t, alphavel), "optech2_particle_t layout");
_Static_assert(sizeof(optech2_dlight_t) == sizeof(cdlight_t), "optech2_dlight_t layout");
_Static_assert(offsetof(optech2_dlight_t, minlight) == offsetof(cdlight_t, minlight), "optech2_dlight_t layout");
_Static_assert(sizeof(optech2_entity_t) == sizeof(entity_t), "optech2_entity_t layout");
_Static_assert(offsetof(optech2_entity_t, skin) == offsetof(entity_t, skin), "optech2_entity_t layout");
_Static_assert(offsetof(optech2_entity_t, flags) == offsetof(entity_t, flags), "optech2_entity_t layout");

static void
FX_Printf(const char *fmt, ...)
{
	char text[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	Com_Printf("%s", text);
}

static void
FX_Error(const char *fmt, ...)
{
	char text[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	Com_Error(ERR_DROP, "%s", text);
}

/* the client state effects read, for the frame being drawn */
static void
FX_GetState(optech2_fx_state_t *state)
{
	frame_t *oldframe;
	player_xstate_t *ps, *ops;
	int i;

	state->time = cl.time;
	state->servertime = cl.frame.servertime;
	state->lerpfrac = cl.lerpfrac;
	state->playernum = cl.playernum;
	VectorCopy(cl.refdef.vieworg, state->vieworg);
	VectorCopy(cl.v_forward, state->v_forward);
	VectorCopy(cl.v_right, state->v_right);
	VectorCopy(cl.v_up, state->v_up);

	/* gun position: view origin plus the gun offset, lerped between the
	   previous and the current frame (was AdjustToWeapon) */
	ps = &cl.frame.playerstate;
	i = (cl.frame.serverframe - 1) & UPDATE_MASK;
	oldframe = &cl.frames[i];

	if ((oldframe->serverframe != cl.frame.serverframe - 1) || !oldframe->valid)
	{
		oldframe = &cl.frame; /* previous frame was dropped or invalid */
	}

	ops = &oldframe->playerstate;

	for (i = 0; i < 3; i++)
	{
		state->gunorigin[i] = cl.refdef.vieworg[i] + ops->gunoffset[i] + cl.lerpfrac * (ps->gunoffset[i] - ops->gunoffset[i]);
	}
}

static qboolean
FX_EntityState(int entnum, vec3_t origin, vec3_t angles)
{
	centity_t *ent;

	if ((entnum < 1) || (entnum > MAX_CL_ENTNUM))
	{
		return false;
	}

	ent = CL_AllocEntity(entnum);

	if (!ent)
	{
		return false;
	}

	VectorCopy(ent->current.origin, origin);
	VectorCopy(ent->current.angles, angles);
	return true;
}

static float
FX_CvarValue(const char *name)
{
	return Cvar_VariableValue(name);
}
static unsigned
FX_PaletteColor(byte index)
{
	return VID_PaletteColor(index);
}
static void
FX_RegisterPic(const char *name)
{
	Draw_FindPic(name);
}
static void
FX_AddEntity(const optech2_entity_t *ent)
{
	V_AddEntity((const entity_t *)ent);
}

static void
FX_AddLight(const vec3_t org, float intensity, float r, float g, float b)
{
	vec3_t where;

	VectorCopy(org, where);
	V_AddLight(where, intensity, r, g, b);
}

static int
FX_ReadByte(void)
{
	return MSG_ReadByte(&net_message);
}
static int
FX_ReadShort(void)
{
	return MSG_ReadShort(&net_message);
}
static int
FX_ReadLong(void)
{
	return MSG_ReadLong(&net_message);
}
static float
FX_ReadFloat(void)
{
	return MSG_ReadFloat(&net_message);
}
static void
FX_ReadPos(vec3_t pos)
{
	MSG_ReadPos(&net_message, pos, cls.serverProtocol);
}
static void
FX_ReadDir(vec3_t dir)
{
	MSG_ReadDir(&net_message, dir);
}
static int
FX_Time(void)
{
	return cl.time;
}
static int
FX_Rand(void)
{
	return randk() & 0x7fff;
}

static optech2_particle_t *
FX_AllocParticle(void)
{
	cparticle_t *p;

	if (!free_particles)
	{
		return NULL;
	}

	p = free_particles;
	free_particles = p->next;
	p->next = active_particles;
	active_particles = p;

	return (optech2_particle_t *)p;
}

static optech2_dlight_t *
FX_AllocDlight(int key)
{
	return (optech2_dlight_t *)CL_AllocDlight(key);
}

static void
FX_StartSound(const vec3_t origin, int entnum, int channel, struct sfx_s *sfx,
	float volume, float attenuation, float timeofs)
{
	vec3_t where;

	if (origin)
	{
		VectorCopy(origin, where);
	}

	S_StartSound(origin ? where : NULL, entnum, channel, sfx, volume, attenuation, timeofs);
}

static trace_t
FX_Trace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int contentmask)
{
	return CM_BoxTrace(start, end, mins ? mins : vec3_origin, maxs ? maxs : vec3_origin, 0, contentmask);
}

static const optech2_fx_import_t fx_import = {
	OPTECH2_FX_EXT_VERSION,
	FX_Printf, FX_Error,
	FX_ReadByte, FX_ReadShort, FX_ReadLong, FX_ReadFloat, FX_ReadPos, FX_ReadDir,
	FX_Time, FX_Rand,
	FX_AllocParticle, FX_AllocDlight,
	S_RegisterSound, FX_StartSound,
	FX_Trace,
	FX_GetState, FX_EntityState, FX_CvarValue, FX_PaletteColor,
	R_RegisterModel, FX_RegisterPic, FX_AddEntity, FX_AddLight};

/* the game library's effects, if it has them */
static void
CL_CGame_FindFx(OPtech2_GetExtension_t get_extension)
{
	const optech2_fx_ext_t *fx_ext = get_extension ? get_extension(OPTECH2_EXT_FX) : NULL;

	if (!fx_ext)
	{
		return;
	}

	if (fx_ext->version != OPTECH2_FX_EXT_VERSION)
	{
		Com_Printf("client game: effects version %d, the engine needs %d; using the engine's effects\n",
			fx_ext->version, OPTECH2_FX_EXT_VERSION);
		return;
	}

	if (!fx_ext->Init || !fx_ext->ParseTempEntity || !fx_ext->ParseMuzzleFlash || !fx_ext->EntityTrails ||
		!fx_ext->EntityEvent || !fx_ext->RegisterMedia || !fx_ext->AddFrameEffects || !fx_ext->ClearEffects)
	{
		Com_Printf("client game: the effects extension leaves an entry point empty; using the engine's effects\n");
		return;
	}

	cgame_fx = fx_ext;
	cgame_fx->Init(&fx_import); /* its media is registered right after (CL_CGame_RegisterEffects) */
	Com_Printf("client game: effects from the game library\n");
}

/* a new level: forget the parked strings of the last one */
void
CL_CGame_NewLevel(void)
{
	cg_parked_count = 0;
	cg_parked_next = 0;
}

/* models and sounds of the game library's effects, at each level load */
void
CL_CGame_RegisterEffects(void)
{
	if (cgame_fx)
	{
		cgame_fx->RegisterMedia();
	}
}

/* the game library's effects for this frame */
void
CL_CGame_AddEffects(void)
{
	if (cgame_fx)
	{
		cgame_fx->AddFrameEffects();
	}
}

/* forget the game library's effects (level change, disconnect) */
void
CL_CGame_ClearEffects(void)
{
	if (cgame_fx)
	{
		cgame_fx->ClearEffects();
	}
}

/* a muzzle flash (kind 1-3: svc_muzzleflash, 2, 3): true when the game
   library drew it */
qboolean
CL_CGame_ParseMuzzleFlash(int kind)
{
	return cgame_fx && cgame_fx->ParseMuzzleFlash(kind);
}

/* an entity's trail and flying light: true when the game library drew them */
qboolean
CL_CGame_EntityTrails(centity_t *cent, const entity_xstate_t *s1, unsigned int effects, entity_t *ent)
{
	optech2_fx_entity_t info;

	if (!cgame_fx)
	{
		return false;
	}

	info.number = s1->number;
	info.effects = effects;
	info.renderfx = s1->renderfx;
	info.frame = s1->frame;
	VectorCopy(cent->lerp_origin, info.oldorigin);
	VectorCopy(ent->origin, info.origin);
	VectorCopy(ent->angles, info.angles);
	info.trailcount = cent->trailcount;
	info.fly_stoptime = cent->fly_stoptime;

	if (!cgame_fx->EntityTrails(&info))
	{
		return false;
	}

	VectorCopy(info.origin, ent->origin);
	cent->trailcount = info.trailcount;
	cent->fly_stoptime = info.fly_stoptime;
	return true;
}

/* an entity event: true when the game library played it */
qboolean
CL_CGame_EntityEvent(const entity_xstate_t *ent)
{
	return cgame_fx && cgame_fx->EntityEvent(ent->number, ent->event, ent->origin);
}

/* a temp entity of this type arrived: true when the game library drew it */
qboolean
CL_CGame_ParseTempEntity(int type)
{
	return cgame_fx && cgame_fx->ParseTempEntity(type);
}

/* the game library's own player movement for prediction, if it has one */
static void
CL_CGame_FindPmove(void)
{
	OPtech2_GetExtension_t get_extension =
		(OPtech2_GetExtension_t)Sys_GetProcAddress(cgame_library, "OPtech2_GetExtension");
	const optech2_pmove_ext_t *pmove_ext =
		get_extension ? get_extension(OPTECH2_EXT_PMOVE) : NULL;

	CL_CGame_FindFx(get_extension);

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
		/* no client game module: keep the library for its movement / effects */
		if (!cgame_pmove && !cgame_fx)
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
	cgame_fx = NULL;
	CL_CGame_NewLevel();

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

	{
		qboolean string_stats[CG_MAX_STATS] = {false};

		CG_FindStringStats(cl.configstrings[CS_STATUSBAR], string_stats);
		CG_FindStringStats(cl.layout, string_stats);

		for (i = 0; i < CG_MAX_STATS && i < MAX_XSTATS; i++)
		{
			/* optech2: text stats in the module's configstring numbering */
			out->stats[i] = string_stats[i] ? (int16_t)CG_ConfigStringIndexForModule(ps->stats[i]) : ps->stats[i];
		}
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
