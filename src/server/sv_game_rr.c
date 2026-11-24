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
 * optech2: hosts a Quake II re-release game library (game API 2023,
 * game_x64.so / game_x64.dll) on the classic server.
 *
 * The rest of the server keeps talking to a classic game_export_t (ge).
 * This file fills one whose functions call the re-release game, and keeps
 * a "shadow" array of engine edicts that mirrors the game's edicts. The
 * shadows carry what only the engine has (area links, clusters), so the
 * world, snapshot and network code work on them unchanged:
 *   - after every call into the game, the shadows are refreshed from the
 *     game's edicts (entity state, bounds, flags, player state);
 *   - linkentity links the shadow and copies the link results back;
 *   - world queries refresh each candidate's collision fields first, as
 *     the game may change them without relinking;
 *   - traces and entity lists hand the game its own edicts back.
 * Configstring numbers are mapped between the re-release layout and ours.
 *
 * Not done yet: saves (the game's JSON saves), the client side (player
 * state for prediction, the re-release Pmove), bots, debug drawing.
 *
 * =======================================================================
 */

#ifndef _WIN32
#define _GNU_SOURCE /* dladdr */
#include <dlfcn.h>
#endif
#include <limits.h>

/* optech2: where the game called a message-writing import from (logged for
   unreadable messages); MSVC has its own intrinsic for it */
#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define RR_CALLER() _ReturnAddress()
#else
#define RR_CALLER() __builtin_return_address(0)
#endif

#include "header/server.h"
#include "header/game_rerelease.h"
#include "../common/header/nav.h" /* optech2: GetPathToGoal */
#include "../common/header/debugdraw.h" /* optech2: the game's Draw_* calls */

void SCR_DebugGraph(float value, int color);

qboolean sv_rerelease_game; /* a re-release game library is loaded */

static rr_game_import_t rr_import;
static rr_game_export_t *rr_ge;
static game_export_t rr_classic; /* what the rest of the server calls */

static edict_t *rr_shadows; /* engine edicts mirroring the game's */
static gclient_t *rr_shadow_clients;
static int rr_shadow_count;

/* re-release configstrings that have no slot in our layout (weapon wheel,
   game style, ...): kept for the game, not sent yet */
#define RR_CS_MAX_STRING_LENGTH 96
#define RR_MAX_CONFIGSTRINGS_GUESS 0x7fff
static char **rr_extra_configstrings;
static int rr_extra_configstrings_count;

/* sets the game's unfinished message aside around our own (below) */
static void RR_SetAsidePending(void);
static void RR_RestorePending(void);


/* ======================================================================= */

/* edict numbers <-> the game's edicts and the shadows */

static rr_edict_t *
RR_EdictNum(int number)
{
	return (rr_edict_t *)((byte *)rr_ge->edicts + rr_ge->edict_size * number);
}

static int
RR_NumForEdict(const rr_edict_t *ent)
{
	return (int)(((const byte *)ent - (const byte *)rr_ge->edicts) / rr_ge->edict_size);
}

static edict_t *
RR_ShadowOf(const rr_edict_t *ent)
{
	int number;

	if (!ent || !rr_shadows)
	{
		return NULL;
	}

	/* optech2: only edicts we have a shadow for (a save can bring more) */
	number = RR_NumForEdict(ent);
	return ((number >= 0) && (number < rr_shadow_count)) ? rr_shadows + number : NULL;
}

static rr_edict_t *
RR_GameEdictOf(const edict_t *shadow)
{
	if (!shadow)
	{
		return NULL;
	}

	return RR_EdictNum((int)(shadow - rr_shadows));
}

/* ======================================================================= */

/* state conversion, game -> engine */

static short
RR_CoordToShort(float value)
{
	int scaled = (int)(value * 8.0f);

	if (scaled > 32767)
	{
		scaled = 32767;
	}
	else if (scaled < -32768)
	{
		scaled = -32768;
	}

	return (short)scaled;
}

static unsigned int
RR_ConvertRenderfx(uint32_t renderfx)
{
	/* the classic bits, except 6 (no origin lerp there, frame lerp here) and 14 */
	unsigned int converted = renderfx & ~(RR_RF_NO_ORIGIN_LERP | RR_RF_CASTSHADOW |
											RR_RF_SHELL_LITE_GREEN | 0xffc00000u);

	if (renderfx & RR_RF_CASTSHADOW)
	{
		converted |= RF_CASTSHADOW;
	}

	if (renderfx & RR_RF_SHELL_LITE_GREEN)
	{
		converted |= RF_SHELL_GREEN;
	}

	/* optech2: kept under our own bit */
	if (renderfx & RR_RF_NO_ORIGIN_LERP)
	{
		converted |= RF_NO_ORIGIN_LERP;
	}

	return converted;
}

static unsigned int
RR_ConvertHighEffects(uint64_t effects)
{
	unsigned int converted = 0;

	if (effects & RR_EF_HOLOGRAM)
	{
		converted |= EF_HOLOGRAM;
	}

	if (effects & RR_EF_FLASHLIGHT)
	{
		converted |= EF_FLASHLIGHT;
	}

	if (effects & RR_EF_TELEPORTER2)
	{
		converted |= EF_TELEPORTER2;
	}

	return converted;
}

/* refreshes what collision reads (also called for each world query candidate) */
static void
RR_PullCollision(edict_t *shadow, const rr_edict_t *ent)
{
	if (!shadow || !ent)
	{
		return;
	}

	shadow->inuse = ent->inuse;
	shadow->svflags = (int)ent->svflags; /* bits 0-2 are the classic ones */
	shadow->solid = (solid_t)ent->solid;
	shadow->clipmask = (int)ent->clipmask;
	shadow->owner = RR_ShadowOf(ent->owner);
	VectorCopy(ent->mins, shadow->mins);
	VectorCopy(ent->maxs, shadow->maxs);
	VectorCopy(ent->s.origin, shadow->s.origin);
	VectorCopy(ent->s.angles, shadow->s.angles);
	shadow->s.modelindex = ent->s.modelindex;
}

static void
RR_PullPlayerState(gclient_t *client, const rr_gclient_t *game_client)
{
	const rr_player_state_t *from = &game_client->ps;
	player_state_t *to = &client->ps;
	int i, pm_type;

	switch (from->pmove.pm_type)
	{
		case RR_PM_NOCLIP:
		case RR_PM_SPECTATOR:
			pm_type = PM_SPECTATOR;
			break;
		case RR_PM_DEAD:
			pm_type = PM_DEAD;
			break;
		case RR_PM_GIB:
			pm_type = PM_GIB;
			break;
		case RR_PM_FREEZE:
			pm_type = PM_FREEZE;
			break;
		default:
			pm_type = PM_NORMAL;
			break;
	}

	to->pmove.pm_type = pm_type;

	for (i = 0; i < 3; i++)
	{
		to->pmove.origin[i] = RR_CoordToShort(from->pmove.origin[i]);
		to->pmove.velocity[i] = RR_CoordToShort(from->pmove.velocity[i]);
		to->pmove.delta_angles[i] = (short)ANGLE2SHORT(from->pmove.delta_angles[i]);
	}

	/* bits 0-5 are the same; positional prediction off is our PMF_NO_PREDICTION */
	to->pmove.pm_flags = from->pmove.pm_flags & 63;

	if (from->pmove.pm_flags & RR_PMF_NO_POSITIONAL_PREDICTION)
	{
		to->pmove.pm_flags |= PMF_NO_PREDICTION;
	}

	/* milliseconds there, units of 8 ms here */
	to->pmove.pm_time = (from->pmove.pm_time / 8 > 255) ? 255 : from->pmove.pm_time / 8;
	to->pmove.gravity = from->pmove.gravity;

	VectorCopy(from->viewangles, to->viewangles);
	/* the re-release keeps the view height apart from the view offset */
	VectorCopy(from->viewoffset, to->viewoffset);
	to->viewoffset[2] += from->pmove.viewheight;
	VectorCopy(from->kick_angles, to->kick_angles);
	VectorCopy(from->gunangles, to->gunangles);
	VectorCopy(from->gunoffset, to->gunoffset);
	to->gunindex = from->gunindex;
	to->gunframe = from->gunframe;

	for (i = 0; i < 4; i++)
	{
		to->blend[i] = from->screen_blend[i];
	}

	to->fov = from->fov;
	to->rdflags = from->rdflags;

	for (i = 0; i < MAX_STATS; i++)
	{
		to->stats[i] = from->stats[i];
	}

	/* the protocol sends all 64; the stats keep the re-release meanings */
	for (i = 0; i < MAX_XSTATS && i < RR_MAX_STATS; i++)
	{
		client->ps_rrstats[i] = from->stats[i];
	}
}

static void
RR_PullEdict(int number)
{
	const rr_edict_t *ent;
	edict_t *shadow;
	float scale;

	/* optech2: only edicts we have a shadow for */
	if (!rr_shadows || (number < 0) || (number >= rr_shadow_count))
	{
		return;
	}

	ent = RR_EdictNum(number);
	shadow = rr_shadows + number;

	RR_PullCollision(shadow, ent);

	shadow->s.number = number;
	VectorCopy(ent->s.old_origin, shadow->s.old_origin);
	shadow->s.modelindex2 = ent->s.modelindex2;
	shadow->s.modelindex3 = ent->s.modelindex3;
	shadow->s.modelindex4 = ent->s.modelindex4;
	shadow->s.frame = ent->s.frame;
	shadow->s.skinnum = ent->s.skinnum;
	shadow->s.effects = (unsigned int)(ent->s.effects & 0xffffffffu);
	shadow->s.renderfx = (int)RR_ConvertRenderfx(ent->s.renderfx);
	shadow->s.sound = ent->s.sound;
	/* our events stop at EV_OTHER_TELEPORT; the new footsteps are footsteps */
	shadow->s.event = (ent->s.event > EV_OTHER_TELEPORT) ? EV_FOOTSTEP : ent->s.event;
	/* s.solid stays what linking packed into it */

	scale = (ent->s.scale > 0) ? ent->s.scale : 1.0f;
	VectorSet(shadow->rrs.scale, scale, scale, scale);
	shadow->rrs.effects = RR_ConvertHighEffects(ent->s.effects);
	shadow->rrs.alpha = ent->s.alpha;

	if (ent->client && (number >= 1) && (number <= rr_shadow_count) && (number <= MAX_CLIENTS) &&
		(number <= maxclients->value))
	{
		shadow->client = rr_shadow_clients + (number - 1);
		RR_PullPlayerState(shadow->client, ent->client);
	}
	else
	{
		shadow->client = NULL;
	}
}

/* refreshes every shadow from the game; after each call into the game */
static void
RR_PullAll(void)
{
	int i, count;

	count = (int)rr_ge->num_edicts;

	if (count > rr_shadow_count)
	{
		count = rr_shadow_count;
	}

	for (i = 0; i < count; i++)
	{
		RR_PullEdict(i);
	}

	rr_classic.num_edicts = count;
}

/* engine -> game: what the server writes into client edicts */
static void
RR_PushPings(void)
{
	int i;

	for (i = 1; i <= maxclients->value && i < rr_shadow_count; i++)
	{
		rr_edict_t *ent = RR_EdictNum(i);

		if (ent->client && rr_shadows[i].client)
		{
			ent->client->ping = rr_shadows[i].client->ping;
		}
	}
}

/* sv_world.c: refreshes a candidate's collision fields from the game */
static void
RR_WorldSync(edict_t *shadow)
{
	int number = (int)(shadow - rr_shadows);

	if ((number >= 0) && (number < rr_shadow_count))
	{
		RR_PullCollision(shadow, RR_EdictNum(number));
	}
}

/* ======================================================================= */

/* traces */

static rr_trace_t
RR_ConvertTrace(const trace_t *trace)
{
	return RR_TraceFromClassic(trace, trace->ent ? RR_GameEdictOf(trace->ent) : NULL);
}

static rr_trace_t
RR_Trace(const float *start, const float *mins, const float *maxs,
	const float *end, const rr_edict_t *passent, uint32_t contentmask)
{
	trace_t trace;
	edict_t *pass = NULL;

	if (passent)
	{
		pass = RR_ShadowOf(passent);
		RR_PullCollision(pass, passent);
	}

	trace = SV_Trace(start, mins, maxs, end, pass, (int)contentmask);
	return RR_ConvertTrace(&trace);
}

static rr_trace_t
RR_Clip(rr_edict_t *entity, const float *start, const float *mins,
	const float *maxs, const float *end, uint32_t contentmask)
{
	edict_t *shadow;
	trace_t trace;

	shadow = RR_ShadowOf(entity);

	if (!shadow)
	{
		/* optech2: nothing to clip against */
		memset(&trace, 0, sizeof(trace));
		trace.fraction = 1;
		VectorCopy(end, trace.endpos);
		return RR_ConvertTrace(&trace);
	}

	RR_PullCollision(shadow, entity);
	trace = SV_ClipToEntity(start, mins, maxs, end, shadow, (int)contentmask);
	return RR_ConvertTrace(&trace);
}

static uint32_t
RR_PointContents(const float *point)
{
	return (uint32_t)SV_PointContents(point);
}

/* ======================================================================= */

/* world links */

static void
RR_LinkEntity(rr_edict_t *ent)
{
	edict_t *shadow = RR_ShadowOf(ent);
	int number = RR_NumForEdict(ent);

	if ((number < 0) || (number >= rr_shadow_count))
	{
		return;
	}

	RR_PullEdict(number);
	SV_LinkEdict(shadow);

	ent->linked = (shadow->area.prev != NULL);
	ent->linkcount = shadow->linkcount;
	ent->areanum = shadow->areanum;
	ent->areanum2 = shadow->areanum2;
	VectorCopy(shadow->absmin, ent->absmin);
	VectorCopy(shadow->absmax, ent->absmax);
	VectorCopy(shadow->size, ent->size);
	VectorCopy(shadow->s.old_origin, ent->s.old_origin);
	ent->s.solid = (uint32_t)shadow->s.solid;
}

static void
RR_UnlinkEntity(rr_edict_t *ent)
{
	edict_t *shadow = RR_ShadowOf(ent);

	if (shadow)
	{
		SV_UnlinkEdict(shadow);
	}

	ent->linked = false;
}

static size_t
RR_BoxEdicts(const float *mins, const float *maxs, rr_edict_t **list,
	size_t maxcount, int32_t areatype, rr_box_edicts_filter_t filter,
	void *filter_data)
{
	static edict_t *found[MAX_EDICTS];
	int i, num;
	size_t count = 0;

	num = SV_AreaEdicts(mins, maxs, found, MAX_EDICTS, areatype);

	for (i = 0; i < num; i++)
	{
		rr_edict_t *ent = RR_GameEdictOf(found[i]);
		int32_t result = RR_BOXEDICTS_KEEP;

		if (filter)
		{
			result = filter(ent, filter_data);
		}

		if (!(result & RR_BOXEDICTS_SKIP))
		{
			if (count < maxcount)
			{
				list[count] = ent;
			}

			count++;
		}

		if (result & RR_BOXEDICTS_END)
		{
			break;
		}
	}

	/* the count stops at maxcount once a list is given */
	if (maxcount && (count > maxcount))
	{
		count = maxcount;
	}

	return count;
}

static void
RR_SetModel(rr_edict_t *ent, const char *name)
{
	if (!name)
	{
		Com_Printf("%s: Name is NULL\n", __func__);
		return;
	}

	ent->s.modelindex = SV_ModelIndex(name);

	/* if it is an inline model, get the size information for it */
	if (name[0] == '*')
	{
		cmodel_t *mod = CM_InlineModel(name);

		VectorCopy(mod->mins, ent->mins);
		VectorCopy(mod->maxs, ent->maxs);
		RR_LinkEntity(ent);
	}
}

static bool
RR_InPVS(const float *p1, const float *p2, bool portals)
{
	(void)portals; /* areaportals always block, as in the classic engine */
	return SV_InPVS(p1, p2);
}

static bool
RR_InPHS(const float *p1, const float *p2, bool portals)
{
	(void)portals;
	return SV_InPHS(p1, p2);
}

static void
RR_SetAreaPortalState(int portalnum, bool open)
{
	CM_SetAreaPortalState(portalnum, open);
}

static bool
RR_AreasConnected(int area1, int area2)
{
	return CM_AreasConnected(area1, area2);
}

/* ======================================================================= */

/* configstrings: the re-release layout <-> ours */

static int
RR_InternalConfigString(int index)
{
	int internal = P_ConvertConfigStringFrom(index, PROTOCOL_RR22_VERSION);

	if ((internal == CS_SKIP) || (internal < 0) || (internal >= MAX_CONFIGSTRINGS))
	{
		return -1;
	}

	return internal;
}

static char *
RR_ExtraConfigString(int index)
{
	if ((index < 0) || (index >= RR_MAX_CONFIGSTRINGS_GUESS))
	{
		return NULL;
	}

	if (index >= rr_extra_configstrings_count)
	{
		int count = index + 64;

		rr_extra_configstrings = Z_TagRealloc(rr_extra_configstrings,
			count * sizeof(*rr_extra_configstrings), 0);
		memset(rr_extra_configstrings + rr_extra_configstrings_count, 0,
			(count - rr_extra_configstrings_count) * sizeof(*rr_extra_configstrings));
		rr_extra_configstrings_count = count;
	}

	if (!rr_extra_configstrings[index])
	{
		rr_extra_configstrings[index] = Z_Malloc(RR_CS_MAX_STRING_LENGTH);
	}

	return rr_extra_configstrings[index];
}

static void
RR_Configstring(int index, const char *value)
{
	int internal;
	size_t length, space;
	char *stored;

	if (!value)
	{
		value = "";
	}

	internal = RR_InternalConfigString(index);

	if (internal < 0)
	{
		stored = RR_ExtraConfigString(index);

		if (stored)
		{
			Q_strlcpy(stored, value, RR_CS_MAX_STRING_LENGTH);
		}

		return;
	}

	length = strlen(value);
	stored = sv.configstrings[internal];

	/* statusbar code covers several configstring indices */
	if ((internal >= CS_STATUSBAR) && (internal < CS_STATUSBAR_END))
	{
		space = CS_STATUSBAR_SPACE(internal);
	}
	else
	{
		space = sizeof(sv.configstrings[internal]);
	}

	if (length >= space)
	{
		Com_Printf("%s: configstring %d too long: " YQ2_COM_PRIdS " >= " YQ2_COM_PRIdS "\n",
			__func__, index, length, space);
		return;
	}

	memcpy(stored, value, length + 1);

	if (sv.state != ss_loading)
	{
		/* send the update to everyone, by our numbering */
		RR_SetAsidePending();
		MSG_WriteChar(&sv.multicast, svc_configstring);
		MSG_WriteConfigString(&sv.multicast, internal, value);
		SV_Multicast(vec3_origin, MULTICAST_ALL_R);
		RR_RestorePending();
	}
}

static const char *
RR_GetConfigstring(int index)
{
	int internal = RR_InternalConfigString(index);
	const char *stored;

	if (internal >= 0)
	{
		return sv.configstrings[internal];
	}

	if ((index < 0) || (index >= rr_extra_configstrings_count))
	{
		return "";
	}

	stored = rr_extra_configstrings[index];
	return stored ? stored : "";
}

static void
RR_FreeExtraConfigStrings(void)
{
	int i;

	for (i = 0; i < rr_extra_configstrings_count; i++)
	{
		if (rr_extra_configstrings[i])
		{
			Z_Free(rr_extra_configstrings[i]);
		}
	}

	if (rr_extra_configstrings)
	{
		Z_Free(rr_extra_configstrings);
	}

	rr_extra_configstrings = NULL;
	rr_extra_configstrings_count = 0;
}

/* ======================================================================= */

/* printing and messages */

static int
RR_ClientNumber(const rr_edict_t *ent)
{
	int number;

	if (!ent)
	{
		return -1;
	}

	number = RR_NumForEdict(ent);

	if ((number < 1) || (number > maxclients->value))
	{
		return -1;
	}

	return number - 1;
}

static int
RR_PrintLevel(int32_t level)
{
	level &= 7;

	if ((level == RR_PRINT_TYPEWRITER) || (level == RR_PRINT_CENTER) ||
		(level == RR_PRINT_TTS))
	{
		return PRINT_HIGH;
	}

	return level;
}

static void
RR_BroadcastPrint(int32_t level, const char *message)
{
	SV_BroadcastPrintf(RR_PrintLevel(level), "%s", message);
}

static void
RR_ComPrint(const char *message)
{
	Com_Printf("%s", message);
}

/* The game builds its messages in sv.multicast and may call a sound, print
   or configstring in the middle of one; those write sv.multicast too. Each
   import that does sets the game's unfinished message aside first. */
static byte rr_pending_data[MAX_MSGLEN];
static int rr_pending_size;
static int rr_pending_depth;
static qboolean rr_pending_open;
static qboolean rr_message_open; /* the game has started a message */
static const void *rr_message_caller; /* game code that started it */

static void
RR_SetAsidePending(void)
{
	if (rr_pending_depth++ > 0)
	{
		return;
	}

	rr_pending_size = sv.multicast.cursize;

	if (rr_pending_size > (int)sizeof(rr_pending_data))
	{
		rr_pending_size = sizeof(rr_pending_data);
	}

	memcpy(rr_pending_data, sv.multicast.data, rr_pending_size);
	rr_pending_open = rr_message_open;
	SZ_Clear(&sv.multicast);
}

static void
RR_RestorePending(void)
{
	if (--rr_pending_depth > 0)
	{
		return;
	}

	SZ_Clear(&sv.multicast);
	SZ_Write(&sv.multicast, rr_pending_data, rr_pending_size);
	rr_pending_size = 0;
	rr_message_open = rr_pending_open;
}

static cvar_t *sv_rr_msglog; /* 1: log every message the game sends */
static cvar_t *sv_rr_pathlog; /* optech2: 1: log every path the game asks for */

static void
RR_LogMessage(const char *how, const char *note)
{
	const byte *data = sv.multicast.data;
	int i, size = sv.multicast.cursize;
	char hex[3 * 24 + 1];

	hex[0] = '\0';

	for (i = 0; i < size && i < 24; i++)
	{
		Com_sprintf(hex + i * 3, sizeof(hex) - i * 3, "%02x ", data[i]);
	}

	Com_Printf("rr message %s%s: svc %d, %d bytes: %s%s\n", how, note,
		size ? data[0] : -1, size, hex, (size > 24) ? "..." : "");
}

/* rewrites a message the game wrote where our protocol differs */
static void
RR_FixMessage(const char *how)
{
	const byte *data = sv.multicast.data;
	int size = sv.multicast.cursize;

	rr_message_open = false;

	if (size < 1)
	{
		return;
	}

	/* a message the client can't read: say so, whatever sv_rr_msglog is */
	if ((data[0] == svc_bad) || (data[0] > svc_achievement))
	{
		const char *where = "?";
#ifndef _WIN32
		Dl_info info;

		if (rr_message_caller && dladdr(rr_message_caller, &info) && info.dli_sname)
		{
			where = info.dli_sname;
		}
#endif
		RR_LogMessage(how, " (BAD: not a server message)");
		Com_Printf("rr message: started in %s\n", where);
	}
	else if (sv_rr_msglog && sv_rr_msglog->value)
	{
		RR_LogMessage(how, "");
	}

	/* monster muzzle flash 255: our protocol reads 255 as "add the next byte" */
	if ((data[0] == svc_muzzleflash2) && (size == 4) && (data[3] == 255))
	{
		MSG_WriteByte(&sv.multicast, 0);
	}
	/* inventory: the game sends 256 counts, our protocol MAX_ITEMS */
	else if ((data[0] == svc_inventory) && (size == 1 + 2 * RR_MAX_ITEMS))
	{
		int i;

		for (i = RR_MAX_ITEMS; i < MAX_ITEMS; i++)
		{
			MSG_WriteShort(&sv.multicast, 0);
		}
	}
}

static void
RR_Unicast(rr_edict_t *ent, bool reliable, uint32_t dupe_key)
{
	int number = RR_ClientNumber(ent);
	client_t *client;

	(void)dupe_key; /* no split screen */

	RR_FixMessage(reliable ? "unicast reliable" : "unicast");

	if (number < 0)
	{
		SZ_Clear(&sv.multicast);
		return;
	}

	client = svs.clients + number;

	if (client->state <= cs_zombie)
	{
		SZ_Clear(&sv.multicast);
		return;
	}

	if (reliable)
	{
		SZ_Write(&client->netchan.message, sv.multicast.data, sv.multicast.cursize);
	}
	else
	{
		SZ_Write(&client->datagram, sv.multicast.data, sv.multicast.cursize);
	}

	SZ_Clear(&sv.multicast);
}

static void
RR_Multicast(const float *origin, int32_t to, bool reliable)
{
	multicast_t where;

	RR_FixMessage(reliable ? "multicast reliable" : "multicast");

	switch (to)
	{
		case 1:
			where = reliable ? MULTICAST_PHS_R : MULTICAST_PHS;
			break;
		case 2:
			where = reliable ? MULTICAST_PVS_R : MULTICAST_PVS;
			break;
		default:
			where = reliable ? MULTICAST_ALL_R : MULTICAST_ALL;
			break;
	}

	SV_Multicast(origin ? origin : vec3_origin, where);
}

static void
RR_ClientPrint(rr_edict_t *ent, int32_t level, const char *message)
{
	int number;

	if (!ent)
	{
		Com_Printf("%s", message);
		return;
	}

	number = RR_ClientNumber(ent);

	if (number < 0)
	{
		return;
	}

	SV_ClientPrintf(svs.clients + number, RR_PrintLevel(level), "%s", message);
}

static void
RR_CenterPrintNow(rr_edict_t *ent, const char *message)
{
	if (RR_ClientNumber(ent) < 0)
	{
		return;
	}

	MSG_WriteByte(&sv.multicast, svc_centerprint);
	MSG_WriteString(&sv.multicast, message);
	RR_Unicast(ent, true, 0);
}

/* localizes base and args here (for prints svc_locprint can't carry) */
static void
RR_LocalizeHere(const char *base, const char **args, size_t num_args, char *text,
	size_t text_size)
{
	char arg_text[8][256];
	const char *localized_args[8];
	size_t i;

	if (num_args > 8)
	{
		num_args = 8; /* MAX_LOCALIZATION_ARGS */
	}

	for (i = 0; i < num_args; i++)
	{
		Q_strlcpy(arg_text[i], SV_LocalizationMessage(args[i] ? args[i] : "", NULL),
			sizeof(arg_text[i]));
		localized_args[i] = arg_text[i];
	}

	SV_LocalizeFormat(SV_LocalizationMessage(base, NULL), localized_args, (int)num_args,
		text, text_size);
}

/* localized print: the client localizes and fills in the arguments */
static void
RR_LocPrintNow(rr_edict_t *ent, int32_t level, const char *base, const char **args,
	size_t num_args)
{
	int print_level = level & 7;
	size_t i;

	if (!base)
	{
		return;
	}

	if ((print_level == RR_PRINT_CENTER) || (print_level == RR_PRINT_TYPEWRITER))
	{
		/* centerprints are localized here; they go as a localized print of
		   level 4 (typed out: mission objectives) or 5 (at once), so the
		   client game shows them as the re-release does */
		char text[1024];

		RR_LocalizeHere(base, args, num_args, text, sizeof(text));

		if (!(level & RR_PRINT_BROADCAST) && (RR_ClientNumber(ent) < 0))
		{
			return;
		}

		MSG_WriteByte(&sv.multicast, svc_locprint);
		MSG_WriteByte(&sv.multicast, print_level);
		MSG_WriteString(&sv.multicast, text);
		MSG_WriteByte(&sv.multicast, 0);

		if (level & RR_PRINT_BROADCAST)
		{
			SV_Multicast(vec3_origin, MULTICAST_ALL_R);
		}
		else
		{
			RR_Unicast(ent, true, 0);
		}

		return;
	}

	if (!ent && !(level & RR_PRINT_BROADCAST))
	{
		char text[1024];

		RR_LocalizeHere(base, args, num_args, text, sizeof(text));
		Com_Printf("%s", text);
		return;
	}

	MSG_WriteByte(&sv.multicast, svc_locprint);
	MSG_WriteByte(&sv.multicast, RR_PrintLevel(level));
	MSG_WriteString(&sv.multicast, base);
	MSG_WriteByte(&sv.multicast, (int)num_args);

	for (i = 0; i < num_args; i++)
	{
		MSG_WriteString(&sv.multicast, args[i] ? args[i] : "");
	}

	if (level & RR_PRINT_BROADCAST)
	{
		SV_Multicast(vec3_origin, MULTICAST_ALL_R);
	}
	else
	{
		RR_Unicast(ent, true, 0);
	}
}

/* the imports: with the game's unfinished message set aside */
static void
RR_CenterPrint(rr_edict_t *ent, const char *message)
{
	RR_SetAsidePending();
	RR_CenterPrintNow(ent, message);
	RR_RestorePending();
}

static void
RR_LocPrint(rr_edict_t *ent, int32_t level, const char *base, const char **args,
	size_t num_args)
{
	RR_SetAsidePending();
	RR_LocPrintNow(ent, level, base, args, num_args);
	RR_RestorePending();
}

YQ2_ATTR_NORETURN_FUNCPTR static void
RR_ComError(const char *message)
{
	Com_Error(ERR_DROP, "Game Error: %s", message);
}

/* ======================================================================= */

/* sounds */

static void
RR_Sound(rr_edict_t *ent, uint8_t channel, int soundindex, float volume,
	float attenuation, float timeofs)
{
	if (!ent)
	{
		return;
	}

	if (!RR_ShadowOf(ent))
	{
		return; /* optech2: an edict without a shadow */
	}

	RR_PullEdict(RR_NumForEdict(ent));
	RR_SetAsidePending();
	SV_StartSound(NULL, RR_ShadowOf(ent), channel, soundindex, volume,
		attenuation, timeofs);
	RR_RestorePending();
}

static void
RR_PositionedSound(const float *origin, rr_edict_t *ent, uint8_t channel,
	int soundindex, float volume, float attenuation, float timeofs)
{
	if (!ent)
	{
		return;
	}

	if (!RR_ShadowOf(ent))
	{
		return; /* optech2: an edict without a shadow */
	}

	RR_PullEdict(RR_NumForEdict(ent));
	RR_SetAsidePending();
	SV_StartSound(origin, RR_ShadowOf(ent), channel, soundindex, volume,
		attenuation, timeofs);
	RR_RestorePending();
}

/* a sound only one player hears: heard by everyone near until split screen */
static void
RR_LocalSound(rr_edict_t *target, const float *origin, rr_edict_t *ent,
	uint8_t channel, int soundindex, float volume, float attenuation,
	float timeofs, uint32_t dupe_key)
{
	(void)target;
	(void)dupe_key;
	RR_PositionedSound(origin, ent, channel, soundindex, volume, attenuation, timeofs);
}

/* ======================================================================= */

/* message writing */

/* debugging help: when the game starts a message, the buffer should be
   empty and the first byte a server command; if not, say who wrote it */

static void
RR_StartWrite(const void *caller, int first_byte)
{
	(void)first_byte;

	if (rr_message_open)
	{
		return;
	}

	rr_message_open = true;
	rr_message_caller = caller;

	if (sv.multicast.cursize > 0)
	{
		RR_LogMessage("left in the buffer", " (BAD: written but never sent)");
	}
}

static void
RR_WriteChar(int c)
{
	RR_StartWrite(RR_CALLER(), c & 255);
	MSG_WriteChar(&sv.multicast, c);
}

static void
RR_WriteByte(int c)
{
	RR_StartWrite(RR_CALLER(), c & 255);
	MSG_WriteByte(&sv.multicast, c);
}

static void
RR_WriteShort(int c)
{
	RR_StartWrite(RR_CALLER(), c & 255);
	MSG_WriteShort(&sv.multicast, c);
}

static void
RR_WriteLong(int c)
{
	RR_StartWrite(RR_CALLER(), c & 255);
	MSG_WriteLong(&sv.multicast, c);
}

static void
RR_WriteFloat(float f)
{
	RR_StartWrite(RR_CALLER(), -1);
	MSG_WriteFloat(&sv.multicast, f);
}

static void
RR_WriteString(const char *s)
{
	RR_StartWrite(RR_CALLER(), s ? (byte)s[0] : 0);
	MSG_WriteString(&sv.multicast, s);
}

static void
RR_WritePosition(const float *pos)
{
	RR_StartWrite(RR_CALLER(), -1);

	MSG_WritePos(&sv.multicast, pos, SV_GetRecomendedProtocol());
}

static void
RR_WriteDir(const float *dir)
{
	RR_StartWrite(RR_CALLER(), -1);

	MSG_WriteDir(&sv.multicast, dir);
}

static void
RR_WriteAngle(float f)
{
	RR_StartWrite(RR_CALLER(), -1);

	MSG_WriteAngle(&sv.multicast, f, SV_GetRecomendedProtocol());
}

static void
RR_WriteEntity(const rr_edict_t *ent)
{
	RR_StartWrite(RR_CALLER(), -1);

	MSG_WriteShort(&sv.multicast, ent ? RR_NumForEdict(ent) : 0);
}

/* ======================================================================= */

/* memory, cvars, commands */

static void *
RR_TagMalloc(size_t size, int tag)
{
	if ((tag > USHRT_MAX) || (tag < 0))
	{
		tag = 0;
	}

	return Z_TagMalloc((int)size, (unsigned short)tag);
}

static void
RR_FreeTags(int tag)
{
	if ((tag > USHRT_MAX) || (tag <= 0))
	{
		return;
	}

	Z_FreeTags((unsigned short)tag);
}

static cvar_t *
RR_Cvar(const char *name, const char *value, uint32_t flags)
{
	/* archive, userinfo, serverinfo, noset, latch are the classic bits */
	return Cvar_Get(name, value, (int)(flags & 31));
}

static int
RR_Argc(void)
{
	return Cmd_Argc();
}
static const char *
RR_Argv(int n)
{
	return Cmd_Argv(n);
}
static const char *
RR_Args(void)
{
	return Cmd_Args();
}
static void
RR_AddCommandString(const char *text)
{
	Cbuf_AddText(text);
}

static void
RR_DebugGraph(float value, int color)
{
#ifndef DEDICATED_ONLY
	SCR_DebugGraph(value, color);
#endif
}

static void *
RR_GetExtension(const char *name)
{
	(void)name;
	return NULL;
}

/* ======================================================================= */

/* not hosted yet: bots, path finding, debug drawing */

static void
RR_BotRegisterEdict(const rr_edict_t *edict)
{
	(void)edict;
}
static void
RR_BotUnRegisterEdict(const rr_edict_t *edict)
{
	(void)edict;
}

static int32_t
RR_BotMoveToPoint(const rr_edict_t *bot, const float *point, const float tolerance)
{
	(void)bot;
	(void)point;
	(void)tolerance;
	return 0; /* GoalReturnCode Error */
}

static int32_t
RR_BotFollowActor(const rr_edict_t *bot, const rr_edict_t *actor)
{
	(void)bot;
	(void)actor;
	return 0;
}

/* optech2: the monster asking for a path: the re-release's request carries
   no mover, but its monsters ask from their own origin; NULL if none is there */
static const rr_edict_t *
RR_PathMover(const float *start)
{
	uint32_t i;

	if (!rr_ge || !rr_ge->edicts)
	{
		return NULL;
	}

	for (i = 1; i < rr_ge->num_edicts; i++)
	{
		const rr_edict_t *ent = RR_EdictNum((int)i);

		if (ent->inuse && (ent->svflags & RR_SVF_MONSTER) && (fabsf(ent->s.origin[0] - start[0]) < 1.0f) &&
			(fabsf(ent->s.origin[1] - start[1]) < 1.0f) && (fabsf(ent->s.origin[2] - start[2]) < 1.0f))
		{
			return ent;
		}
	}

	return NULL;
}

/* optech2: the game's pathfinding (its monsters' M_NavPathToGoal, the
   compass, g_debug_monster_kills) on our navigation mesh: the request and
   answer are the re-release's, field for field, in our own types. A
   monster's path is on the mesh for its size (a Tank isn't sent through a
   gap only a soldier fits); others are player-sized, as the re-release's
   own nodes are. */
static bool
RR_GetPathToGoal(const rr_path_request_t *request, rr_path_info_t *info)
{
	optech2_path_request_t nav_request;
	optech2_path_info_t nav_info;
	float mover_radius = 0, mover_height = 0; /* optech2: 0: player-sized */
	int found;

	memset(&nav_request, 0, sizeof(nav_request));
	VectorCopy(request->start, nav_request.start);
	VectorCopy(request->goal, nav_request.goal);
	nav_request.path_flags = request->pathFlags;
	nav_request.move_dist = request->moveDist;
	nav_request.draw_time = request->debugging.drawTime;
	nav_request.ignore_node_flags = request->nodeSearch.ignoreNodeFlags ? 1 : 0;
	nav_request.min_height = request->nodeSearch.minHeight;
	nav_request.max_height = request->nodeSearch.maxHeight;
	nav_request.radius = request->nodeSearch.radius;
	nav_request.drop_height = request->traversals.dropHeight;
	nav_request.jump_height = request->traversals.jumpHeight;
	nav_request.points = request->pathPoints.array;
	nav_request.point_count = request->pathPoints.array ? request->pathPoints.count : 0;

	/* the re-release's long jumps carry no distance: allow the longest
	   leap the navigation knows */
	if (request->pathFlags & OPTECH2_PATH_LONG_JUMP)
	{
		nav_request.leap_distance = 320;
	}

	memset(&nav_info, 0, sizeof(nav_info));

	{
		const rr_edict_t *mover = RR_PathMover(request->start);

		if (mover)
		{
			mover_radius = Q_max(Q_max(-mover->mins[0], mover->maxs[0]), Q_max(-mover->mins[1], mover->maxs[1]));
			mover_height = mover->maxs[2] - mover->mins[2];
			found = Nav_GetPathToGoalSized(&nav_request, &nav_info, mover_radius, mover_height);
		}
		else
		{
			found = Nav_GetPathToGoal(&nav_request, &nav_info);
		}
	}

	info->numPathPoints = nav_info.point_count;
	info->pathDistSqr = nav_info.path_dist_sqr;
	VectorCopy(nav_info.first_move_point, info->firstMovePoint);
	VectorCopy(nav_info.second_move_point, info->secondMovePoint);

	/* our ladder and teleporter links aren't in the re-release's list:
	   walk to them */
	info->pathLinkType = (nav_info.link_type > OPTECH2_PATH_LINK_ELEVATOR) ? OPTECH2_PATH_LINK_WALK : nav_info.link_type;
	info->returnCode = nav_info.return_code;

	if (sv_rr_pathlog && sv_rr_pathlog->value)
	{
		Com_Printf("rr_path %.0f %.0f %.0f -> %.0f %.0f %.0f: code %d, %d points, next %.0f %.0f %.0f, mover %.0f x %.0f\n",
			request->start[0], request->start[1], request->start[2], request->goal[0], request->goal[1],
			request->goal[2], info->returnCode, info->numPathPoints, info->firstMovePoint[0],
			info->firstMovePoint[1], info->firstMovePoint[2], mover_radius, mover_height);
	}

	return found != 0;
}

/* optech2: the game's debug drawing (g_debug_monster_kills, info_world_text,
   bot debugging): shapes become lines in the local debug draw store, which
   the client draws over its view */
static unsigned int
RR_DrawColor(const rr_rgba_t *c)
{
	return ((unsigned int)c->a << 24) | ((unsigned int)c->r << 16) | ((unsigned int)c->g << 8) | c->b;
}

#define RR_DRAW_CIRCLE_SEGMENTS 24

/* a ring around origin in the plane of the two axes (0 x, 1 y, 2 z) */
static void
RR_DrawRing(const float *origin, float radius, int axis_a, int axis_b, unsigned int color, float seconds,
	qboolean depth_test)
{
	vec3_t previous, point;
	int i;

	for (i = 0; i <= RR_DRAW_CIRCLE_SEGMENTS; i++)
	{
		float angle = (float)i / RR_DRAW_CIRCLE_SEGMENTS * 2.0f * (float)M_PI;

		VectorCopy(origin, point);
		point[axis_a] += cosf(angle) * radius;
		point[axis_b] += sinf(angle) * radius;

		if (i > 0)
		{
			DebugDraw_Line(previous, point, color, seconds, depth_test);
		}

		VectorCopy(point, previous);
	}
}

/* an arrowhead at end, pointing away from start */
static void
RR_DrawArrowHead(const float *start, const float *end, float size, unsigned int color, float seconds,
	qboolean depth_test)
{
	vec3_t direction, right, up, back, tip;
	int i;

	VectorSubtract(end, start, direction);

	if (VectorNormalize(direction) <= 0)
	{
		return;
	}

	PerpendicularVector(right, direction);
	CrossProduct(direction, right, up);

	for (i = 0; i < 4; i++)
	{
		const float *side = (i < 2) ? right : up;
		float sign = (i % 2) ? -1.0f : 1.0f;

		VectorMA(end, -size, direction, back);
		VectorMA(back, sign * size * 0.5f, side, tip);
		DebugDraw_Line(end, tip, color, seconds, depth_test);
	}
}

static void
RR_DrawLine(const float *a, const float *b, const rr_rgba_t *c, const float t, const bool d)
{
	DebugDraw_Line(a, b, RR_DrawColor(c), t, d);
}

static void
RR_DrawPoint(const float *a, const float s, const rr_rgba_t *c, const float t, const bool d)
{
	float half = 2.0f * ((s > 1.0f) ? s : 1.0f);
	int axis;

	/* a small cross on all three axes */
	for (axis = 0; axis < 3; axis++)
	{
		vec3_t from, to;

		VectorCopy(a, from);
		VectorCopy(a, to);
		from[axis] -= half;
		to[axis] += half;
		DebugDraw_Line(from, to, RR_DrawColor(c), t, d);
	}
}

static void
RR_DrawCircle(const float *a, const float r, const rr_rgba_t *c, const float t, const bool d)
{
	RR_DrawRing(a, r, 0, 1, RR_DrawColor(c), t, d);
}

static void
RR_DrawBounds(const float *a, const float *b, const rr_rgba_t *c, const float t, const bool d)
{
	const unsigned int color = RR_DrawColor(c);
	vec3_t corners[8];
	int i;

	/* corner i takes x from bit 0, y from bit 1, z from bit 2 */
	for (i = 0; i < 8; i++)
	{
		corners[i][0] = (i & 1) ? b[0] : a[0];
		corners[i][1] = (i & 2) ? b[1] : a[1];
		corners[i][2] = (i & 4) ? b[2] : a[2];
	}

	/* the 12 edges: corners that differ in exactly one bit */
	for (i = 0; i < 8; i++)
	{
		int bit;

		for (bit = 1; bit < 8; bit <<= 1)
		{
			if (!(i & bit))
			{
				DebugDraw_Line(corners[i], corners[i | bit], color, t, d);
			}
		}
	}
}

static void
RR_DrawSphere(const float *a, const float r, const rr_rgba_t *c, const float t, const bool d)
{
	const unsigned int color = RR_DrawColor(c);

	RR_DrawRing(a, r, 0, 1, color, t, d);
	RR_DrawRing(a, r, 0, 2, color, t, d);
	RR_DrawRing(a, r, 1, 2, color, t, d);
}

static void
RR_DrawOrientedWorldText(const float *a, const char *x, const rr_rgba_t *c, const float s, const float t, const bool d)
{
	(void)s; /* drawn at the console's text size */
	DebugDraw_Text(a, x, RR_DrawColor(c), t, d);
}

static void
RR_DrawStaticWorldText(const float *a, const float *g, const char *x, const rr_rgba_t *c, const float s, const float t, const bool d)
{
	(void)g; /* drawn facing the view, like oriented text */
	(void)s;
	DebugDraw_Text(a, x, RR_DrawColor(c), t, d);
}

static void
RR_DrawCylinder(const float *a, const float h, const float r, const rr_rgba_t *c, const float t, const bool d)
{
	const unsigned int color = RR_DrawColor(c);
	vec3_t top, bottom;
	int i;

	VectorCopy(a, top);
	VectorCopy(a, bottom);
	top[2] += h;
	bottom[2] -= h;
	RR_DrawRing(top, r, 0, 1, color, t, d);
	RR_DrawRing(bottom, r, 0, 1, color, t, d);

	for (i = 0; i < 4; i++)
	{
		vec3_t up, down;
		float angle = i * 0.5f * (float)M_PI;

		VectorCopy(top, up);
		VectorCopy(bottom, down);
		up[0] += cosf(angle) * r;
		up[1] += sinf(angle) * r;
		down[0] += cosf(angle) * r;
		down[1] += sinf(angle) * r;
		DebugDraw_Line(up, down, color, t, d);
	}
}

static void
RR_DrawRay(const float *a, const float *b, const float l, const float s, const rr_rgba_t *c, const float t, const bool d)
{
	const unsigned int color = RR_DrawColor(c);
	vec3_t end;

	VectorMA(a, l, b, end);
	DebugDraw_Line(a, end, color, t, d);
	RR_DrawArrowHead(a, end, (s > 2.0f) ? s : 2.0f, color, t, d);
}

static void
RR_DrawArrow(const float *a, const float *b, const float s, const rr_rgba_t *c, const rr_rgba_t *c2, const float t, const bool d)
{
	DebugDraw_Line(a, b, RR_DrawColor(c), t, d);
	RR_DrawArrowHead(a, b, (s > 2.0f) ? s : 2.0f, RR_DrawColor(c2), t, d);
}

/* the game wrote match details (teams, scores) for the platform scoreboard;
   we have none, so they are dropped, not left for the next message */
static void
RR_ReportMatchDetails(bool is_end)
{
	(void)is_end;
	SZ_Clear(&sv.multicast);
	rr_message_open = false;
}

static uint32_t
RR_ServerFrame(void)
{
	return (uint32_t)sv.framenum;
}

#ifndef DEDICATED_ONLY
int IN_SetClipboardText(const char *s);
#endif

/* optech2: the game's "where" command puts the player's position on the
   clipboard; on this machine (a local game), not a dedicated server's */
static void
RR_SendToClipBoard(const char *text)
{
#ifndef DEDICATED_ONLY
	if (text && !dedicated->value)
	{
		IN_SetClipboardText(text);
	}
#else
	(void)text;
#endif
}

/* info strings */

static size_t
RR_InfoValueForKey(const char *s, const char *key, char *buffer, size_t buffer_len)
{
	const char *value = Info_ValueForKey((char *)s, (char *)key);

	if (buffer_len)
	{
		Q_strlcpy(buffer, value, buffer_len);
	}

	return strlen(value);
}

static bool
RR_InfoRemoveKey(char *s, const char *key)
{
	Info_RemoveKey(s, key);
	return true;
}

static bool
RR_InfoSetValueForKey(char *s, const char *key, const char *value)
{
	Info_SetValueForKey(s, key, value);
	return true;
}

/* ======================================================================= */

/* the classic export the server calls */

static void
RR_AllocShadows(void)
{
	rr_shadow_count = (int)rr_ge->max_edicts;

	if (rr_shadow_count > MAX_EDICTS)
	{
		rr_shadow_count = MAX_EDICTS;
	}

	rr_shadows = Z_Malloc(rr_shadow_count * sizeof(*rr_shadows));
	rr_shadow_clients = Z_Malloc(MAX_CLIENTS * sizeof(*rr_shadow_clients));

	rr_classic.edicts = rr_shadows;
	rr_classic.edict_size = sizeof(edict_t);
	rr_classic.max_edicts = rr_shadow_count;
	rr_classic.num_edicts = 0;
}

static void
Classic_Init(void)
{
	rr_ge->Init();
	RR_AllocShadows();
	RR_PullAll();
}

static void
Classic_Shutdown(void)
{
	rr_ge->Shutdown();

	if (rr_shadows)
	{
		Z_Free(rr_shadows);
		Z_Free(rr_shadow_clients);
	}

	rr_shadows = NULL;
	rr_shadow_clients = NULL;
	rr_shadow_count = 0;

	RR_FreeSurfaces();
	RR_FreeExtraConfigStrings();
	SV_WorldSyncEdict = NULL;
	sv_rerelease_game = false;
	rr_ge = NULL; /* optech2: the library unloads next; nothing may call into it */
	Cmd_RemoveCommand("rr_edicts");
}

static void
Classic_SpawnEntities(const char *mapname, char *entstring, const char *spawnpoint)
{
	/* a new map: every shadow starts unlinked and empty */
	memset(rr_shadows, 0, rr_shadow_count * sizeof(*rr_shadows));

	rr_ge->SpawnEntities(mapname, entstring, spawnpoint);
	RR_PullAll();
}

/* saves: the game writes and reads JSON; the server hands us file names
   relative to the save folder it made the work directory */

static void
RR_WriteJsonFile(const char *filename, char *json, size_t size)
{
	FILE *f;

	if (!json)
	{
		Com_Printf("%s: the game wrote nothing\n", filename);
		return;
	}

	f = Q_fopen(filename, "wb");

	if (!f)
	{
		Com_Printf("Couldn't write %s\n", filename);
	}
	else
	{
		fwrite(json, 1, size, f);
		fclose(f);
	}

	Z_Free(json); /* TagMalloc'd by the game */
}

/* the whole file, zero terminated (free it), or NULL */
static char *
RR_ReadJsonFile(const char *filename)
{
	FILE *f;
	long size;
	char *text;

	f = Q_fopen(filename, "rb");

	if (!f)
	{
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0)
	{
		fclose(f);
		return NULL;
	}

	text = malloc(size + 1);

	if (!text)
	{
		fclose(f);
		return NULL;
	}

	if (fread(text, 1, size, f) != (size_t)size)
	{
		free(text);
		fclose(f);
		return NULL;
	}

	text[size] = '\0';
	fclose(f);
	return text;
}

/* the game is about to replace all its edicts: forget the shadows' world
   links (the server cleared the world, or is about to) */
static void
RR_ForgetShadows(void)
{
	memset(rr_shadows, 0, rr_shadow_count * sizeof(*rr_shadows));
}

static void
Classic_WriteGame(const char *filename, qboolean autosave)
{
	size_t size = 0;
	char *json = rr_ge->WriteGameJson(autosave, &size);

	RR_WriteJsonFile(filename, json, size);
}

static void
Classic_ReadGame(const char *filename)
{
	char *json = RR_ReadJsonFile(filename);

	if (!json)
	{
		Com_Error(ERR_DROP, "Couldn't read %s", filename);
		return;
	}

	RR_ForgetShadows();
	rr_ge->ReadGameJson(json);
	free(json);

	/* optech2: the save's game may have room for another number of edicts:
	   a shadow for each again (the old ones were forgotten above) */
	if (((int)rr_ge->max_edicts != rr_shadow_count) && (rr_shadow_count < MAX_EDICTS ||
														   (int)rr_ge->max_edicts < MAX_EDICTS))
	{
		Z_Free(rr_shadows);
		Z_Free(rr_shadow_clients);
		RR_AllocShadows();
	}

	RR_PullAll();
}

static void
Classic_WriteLevel(const char *filename)
{
	size_t size = 0;
	/* leaving the level: the players go on with the player, not with it */
	char *json = rr_ge->WriteLevelJson(sv_level_transition, &size);

	RR_WriteJsonFile(filename, json, size);
}

static void
Classic_ReadLevel(const char *filename)
{
	char *json = RR_ReadJsonFile(filename);

	if (!json)
	{
		Com_Error(ERR_DROP, "Couldn't read %s", filename);
		return;
	}

	RR_ForgetShadows();
	rr_ge->ReadLevelJson(json);
	free(json);
	RR_PullAll();
}

/* SV_BuildClientFrame: the client's re-release movement state */
void
SV_RR_FillPlayerState(int edict_number, player_xstate_t *ps)
{
	const rr_edict_t *ent;
	const rr_pmove_state_t *pmove;

	if (!rr_ge || (edict_number < 1) || (edict_number >= rr_shadow_count))
	{
		return;
	}

	ent = RR_EdictNum(edict_number);

	if (!ent->client)
	{
		return;
	}

	pmove = &ent->client->ps.pmove;
	ps->rr_pmove = true;
	ps->rr_pm_type = pmove->pm_type;
	ps->rr_pm_flags = pmove->pm_flags;
	ps->rr_pm_time = pmove->pm_time;
	ps->rr_viewheight = pmove->viewheight;
	VectorCopy(pmove->origin, ps->rr_origin);
	VectorCopy(pmove->velocity, ps->rr_velocity);
	VectorCopy(pmove->delta_angles, ps->rr_delta_angles);
}

/* optech2: an entity the game shows each player on their own (SVF_INSTANCED:
   coop items a player already took stay for the others); false if this
   client shouldn't see it */
qboolean
SV_RR_VisibleTo(const edict_t *shadow, const edict_t *client_shadow)
{
	rr_edict_t *ent;

	if (!rr_ge || !rr_ge->Entity_IsVisibleToPlayer || !(shadow->svflags & RR_SVF_INSTANCED))
	{
		return true;
	}

	ent = RR_GameEdictOf(shadow);
	return rr_ge->Entity_IsVisibleToPlayer(ent, RR_GameEdictOf(client_shadow));
}

/* optech2: the game asks for slow time (its help computer is open) */
qboolean
SV_RR_SlowTime(void)
{
	return rr_ge && (rr_ge->server_flags & RR_SERVER_FLAG_SLOW_TIME);
}

/* optech2: a shadow's looping sound volume and attenuation, from the
   game's entity; left alone for other entities */
void
SV_RR_LoopSound(const edict_t *shadow, float *volume, float *attenuation)
{
	const rr_edict_t *ent;

	if (!rr_ge || !rr_shadows || shadow < rr_shadows || shadow >= rr_shadows + rr_shadow_count)
	{
		return;
	}

	ent = RR_GameEdictOf(shadow);
	*volume = ent->s.loop_volume;
	*attenuation = ent->s.loop_attenuation;
}

/* optech2: the client slot a connecting player should take, as the game
   picks it (coop: the slot of the player with that name, so they get their
   inventory back); -1 for the engine's own pick */
int
SV_RR_ChooseSlot(const char *userinfo)
{
	rr_edict_t *ignore[MAX_CLIENTS];
	const rr_edict_t *chosen;
	size_t ignore_count = 0;
	int i, slot;

	if (!rr_ge || !rr_ge->ClientChooseSlot)
	{
		return -1;
	}

	/* slots the engine already has a client in */
	for (i = 0; i < maxclients->value && i < MAX_CLIENTS; i++)
	{
		if (svs.clients[i].state != cs_free)
		{
			ignore[ignore_count++] = RR_EdictNum(i + 1);
		}
	}

	chosen = rr_ge->ClientChooseSlot(userinfo, "", false, ignore, ignore_count, sv.state == ss_cinematic);

	if (!chosen)
	{
		return -1;
	}

	slot = RR_NumForEdict(chosen) - 1;
	return (slot >= 0 && slot < maxclients->value && svs.clients[slot].state == cs_free) ? slot : -1;
}

/* the game may refuse a save (intermission, deathmatch, dead player...) */
qboolean
SV_RR_CanSave(void)
{
	return !rr_ge || !rr_ge->CanSave || rr_ge->CanSave();
}

static qboolean
Classic_ClientConnect(edict_t *ent, char *userinfo)
{
	bool allowed;

	allowed = rr_ge->ClientConnect(RR_GameEdictOf(ent), userinfo, "", false);
	RR_PullAll();
	return allowed;
}

static void
Classic_ClientBegin(edict_t *ent)
{
	rr_ge->ClientBegin(RR_GameEdictOf(ent));
	RR_PullAll();
}

static void
Classic_ClientUserinfoChanged(edict_t *ent, char *userinfo)
{
	rr_ge->ClientUserinfoChanged(RR_GameEdictOf(ent), userinfo);
	RR_PullAll();
}

static void
Classic_ClientDisconnect(edict_t *ent)
{
	rr_ge->ClientDisconnect(RR_GameEdictOf(ent));
	RR_PullAll();
}

/* optech2: chat. Re-release game libraries built for the re-release
   engine (KEX_Q2_GAME, as id ships them) leave say and say_team to the
   engine's lobby system and answer them with "invalid game command". No
   teams here: say_team is said in brackets, as classic games do without
   teams. */
static void
RR_Say(const edict_t *ent, qboolean team)
{
	char text[256];
	const char *said;
	int client_number = (int)NUM_FOR_EDICT(ent) - 1;
	size_t length;

	if ((client_number < 0) || (client_number >= maxclients->value) || (Cmd_Argc() < 2))
	{
		return;
	}

	said = Cmd_Args();

	/* say "text" arrives with the quotes */
	if (*said == '"')
	{
		said++;
	}

	Com_sprintf(text, sizeof(text), team ? "(%s): %s" : "%s: %s",
		svs.clients[client_number].name, said);

	length = strlen(text);

	if (length && (text[length - 1] == '"'))
	{
		text[--length] = 0;
	}

	if (length > 150)
	{
		text[150] = 0;
	}

	SV_BroadcastPrintf(PRINT_CHAT, "%s\n", text);
}

static void
Classic_ClientCommand(edict_t *ent)
{
	if (!Q_stricmp(Cmd_Argv(0), "say") || !Q_stricmp(Cmd_Argv(0), "say_team"))
	{
		RR_Say(ent, !Q_stricmp(Cmd_Argv(0), "say_team"));
		return;
	}

	rr_ge->ClientCommand(RR_GameEdictOf(ent));
	RR_PullAll();
}

static void
Classic_ClientThink(edict_t *ent, usercmd_t *cmd)
{
	rr_usercmd_t rr_cmd;

	RR_ConvertUsercmd(cmd, &rr_cmd, (uint32_t)sv.framenum);

	RR_PushPings();
	rr_ge->ClientThink(RR_GameEdictOf(ent), &rr_cmd);
	RR_PullAll();
}

static void
Classic_RunFrame(void)
{
	RR_PushPings();
	rr_ge->RunFrame(true);
	RR_PullAll();
}

static void
Classic_ServerCommand(void)
{
	rr_ge->ServerCommand();
	RR_PullAll();
}

/* "rr_edicts [classname]": the game's entities as its bot info sees them
   (number, class, health, origin); for checks and debugging */
static void
RR_Edicts_f(void)
{
	const char *filter = (Cmd_Argc() > 1) ? Cmd_Argv(1) : NULL;
	uint32_t i;
	int listed = 0;

	if (!rr_ge || !rr_ge->edicts)
	{
		Com_Printf("no re-release game running\n");
		return;
	}

	for (i = 1; i < rr_ge->num_edicts; i++)
	{
		const rr_edict_t *ent = RR_EdictNum((int)i);
		const char *classname = ent->sv.classname ? ent->sv.classname : "";

		if (!ent->inuse || !ent->sv.init)
		{
			continue;
		}

		if (filter && Q_stricmp(filter, classname))
		{
			continue;
		}

		Com_Printf("rr_edict %u %s health %d origin %.0f %.0f %.0f\n", i, classname,
			ent->sv.health, ent->s.origin[0], ent->s.origin[1], ent->s.origin[2]);
		listed++;
	}

	Com_Printf("rr_edicts: %d listed\n", listed);
}

/* SV_PrepWorldFrame: events last one message; the game clears its own */
void
SV_RR_PrepFrame(void)
{
	if (rr_ge && rr_ge->PrepFrame)
	{
		rr_ge->PrepFrame();
	}
}

/* ======================================================================= */

void *
SV_RR_GameImport(void)
{
	rr_game_import_t *gi = &rr_import;
	/* the game library loads before sv.tickrate is set; it is latched, so the same */
	int tickrate = Q_clamp((int)sv_tickrate->value, 10, 120);

	memset(gi, 0, sizeof(*gi));

	gi->tick_rate = (uint32_t)tickrate;
	gi->frame_time_s = 1.0f / (float)tickrate;
	gi->frame_time_ms = (uint32_t)(1000 / tickrate);

	gi->Broadcast_Print = RR_BroadcastPrint;
	gi->Com_Print = RR_ComPrint;
	gi->Client_Print = RR_ClientPrint;
	gi->Center_Print = RR_CenterPrint;

	gi->sound = RR_Sound;
	gi->positioned_sound = RR_PositionedSound;
	gi->local_sound = RR_LocalSound;

	gi->configstring = RR_Configstring;
	gi->get_configstring = RR_GetConfigstring;

	gi->Com_Error = RR_ComError;

	gi->modelindex = SV_ModelIndex;
	gi->soundindex = SV_SoundIndex;
	gi->imageindex = SV_ImageIndex;

	gi->setmodel = RR_SetModel;

	gi->trace = RR_Trace;
	gi->clip = RR_Clip;
	gi->pointcontents = RR_PointContents;
	gi->inPVS = RR_InPVS;
	gi->inPHS = RR_InPHS;
	gi->SetAreaPortalState = RR_SetAreaPortalState;
	gi->AreasConnected = RR_AreasConnected;

	gi->linkentity = RR_LinkEntity;
	gi->unlinkentity = RR_UnlinkEntity;
	gi->BoxEdicts = RR_BoxEdicts;

	gi->multicast = RR_Multicast;
	gi->unicast = RR_Unicast;

	gi->WriteChar = RR_WriteChar;
	gi->WriteByte = RR_WriteByte;
	gi->WriteShort = RR_WriteShort;
	gi->WriteLong = RR_WriteLong;
	gi->WriteFloat = RR_WriteFloat;
	gi->WriteString = RR_WriteString;
	gi->WritePosition = RR_WritePosition;
	gi->WriteDir = RR_WriteDir;
	gi->WriteAngle = RR_WriteAngle;
	gi->WriteEntity = RR_WriteEntity;

	gi->TagMalloc = RR_TagMalloc;
	gi->TagFree = Z_Free;
	gi->FreeTags = RR_FreeTags;

	gi->cvar = RR_Cvar;
	gi->cvar_set = Cvar_Set;
	gi->cvar_forceset = Cvar_ForceSet;

	gi->argc = RR_Argc;
	gi->argv = RR_Argv;
	gi->args = RR_Args;

	gi->AddCommandString = RR_AddCommandString;
	gi->DebugGraph = RR_DebugGraph;
	gi->GetExtension = RR_GetExtension;

	gi->Bot_RegisterEdict = RR_BotRegisterEdict;
	gi->Bot_UnRegisterEdict = RR_BotUnRegisterEdict;
	gi->Bot_MoveToPoint = RR_BotMoveToPoint;
	gi->Bot_FollowActor = RR_BotFollowActor;
	gi->GetPathToGoal = RR_GetPathToGoal;

	gi->Loc_Print = RR_LocPrint;

	gi->Draw_Line = RR_DrawLine;
	gi->Draw_Point = RR_DrawPoint;
	gi->Draw_Circle = RR_DrawCircle;
	gi->Draw_Bounds = RR_DrawBounds;
	gi->Draw_Sphere = RR_DrawSphere;
	gi->Draw_OrientedWorldText = RR_DrawOrientedWorldText;
	gi->Draw_StaticWorldText = RR_DrawStaticWorldText;
	gi->Draw_Cylinder = RR_DrawCylinder;
	gi->Draw_Ray = RR_DrawRay;
	gi->Draw_Arrow = RR_DrawArrow;

	gi->ReportMatchDetails_Multicast = RR_ReportMatchDetails;
	gi->ServerFrame = RR_ServerFrame;
	gi->SendToClipBoard = RR_SendToClipBoard;

	gi->Info_ValueForKey = RR_InfoValueForKey;
	gi->Info_RemoveKey = RR_InfoRemoveKey;
	gi->Info_SetValueForKey = RR_InfoSetValueForKey;

	return gi;
}

/* takes the export GetGameAPI returned; gives the classic one the server
   uses, or NULL (game library unloaded) when it is not API 2023 */
game_export_t *
SV_RR_Connect(void *game_export)
{
	rr_ge = (rr_game_export_t *)game_export;

	if (!rr_ge)
	{
		return NULL;
	}

	if (rr_ge->apiversion != RR_GAME_API_VERSION)
	{
		Com_Printf("re-release game library is version %i, not %i\n",
			rr_ge->apiversion, RR_GAME_API_VERSION);
		rr_ge = NULL;
		return NULL;
	}

	memset(&rr_classic, 0, sizeof(rr_classic));
	rr_classic.apiversion = GAME_API_VERSION;
	rr_classic.Init = Classic_Init;
	rr_classic.Shutdown = Classic_Shutdown;
	rr_classic.SpawnEntities = Classic_SpawnEntities;
	rr_classic.WriteGame = Classic_WriteGame;
	rr_classic.ReadGame = Classic_ReadGame;
	rr_classic.WriteLevel = Classic_WriteLevel;
	rr_classic.ReadLevel = Classic_ReadLevel;
	rr_classic.ClientConnect = Classic_ClientConnect;
	rr_classic.ClientBegin = Classic_ClientBegin;
	rr_classic.ClientUserinfoChanged = Classic_ClientUserinfoChanged;
	rr_classic.ClientDisconnect = Classic_ClientDisconnect;
	rr_classic.ClientCommand = Classic_ClientCommand;
	rr_classic.ClientThink = Classic_ClientThink;
	rr_classic.RunFrame = Classic_RunFrame;
	rr_classic.ServerCommand = Classic_ServerCommand;

	sv_rerelease_game = true;
	SV_WorldSyncEdict = RR_WorldSync;
	Cmd_AddCommand("rr_edicts", RR_Edicts_f);
	sv_rr_msglog = Cvar_Get("sv_rr_msglog", "0", 0);
	sv_rr_pathlog = Cvar_Get("sv_rr_pathlog", "0", 0);

	/* may change maxclients etc. before Init */
	if (rr_ge->PreInit)
	{
		rr_ge->PreInit();
	}

	Com_Printf("re-release game library (game API %i)\n", RR_GAME_API_VERSION);
	return &rr_classic;
}
