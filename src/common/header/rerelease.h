/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (c) ZeniMax Media Inc.
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
 * optech2: the re-release player movement types (pmove_t, trace_t and
 * what they hold), in C, shared by the server's game bridge and the
 * client's prediction. Same layout rules as server/header/game_rerelease.h
 * (byte for byte id's game.h; checked by tests/rerelease/layout_check.py).
 *
 * =======================================================================
 */

#ifndef COMMON_RERELEASE_H
#define COMMON_RERELEASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "shared.h" /* usercmd_t */

#define RR_MAXTOUCH 32

typedef struct rr_edict_s rr_edict_t;

/* contents_t bits the classic engine does not have */
#define RR_CONTENTS_NO_WATERJUMP (1u << 13)
#define RR_CONTENTS_PROJECTILECLIP (1u << 14)
#define RR_CONTENTS_PLAYER (1u << 30)
#define RR_CONTENTS_PROJECTILE (1u << 31)

/* pmtype_t */
enum
{
	RR_PM_NORMAL,
	RR_PM_GRAPPLE,
	RR_PM_NOCLIP,
	RR_PM_SPECTATOR,
	RR_PM_DEAD,
	RR_PM_GIB,
	RR_PM_FREEZE
};

/* pmflags_t (uint16) */
#define RR_PMF_DUCKED (1 << 0)
#define RR_PMF_JUMP_HELD (1 << 1)
#define RR_PMF_ON_GROUND (1 << 2)
#define RR_PMF_TIME_WATERJUMP (1 << 3)
#define RR_PMF_TIME_LAND (1 << 4)
#define RR_PMF_TIME_TELEPORT (1 << 5)
#define RR_PMF_NO_POSITIONAL_PREDICTION (1 << 6)
#define RR_PMF_ON_LADDER (1 << 7)
#define RR_PMF_NO_ANGULAR_PREDICTION (1 << 8)
#define RR_PMF_IGNORE_PLAYER_COLLISION (1 << 9)
#define RR_PMF_TIME_TRICK (1 << 10)

/* button_t (uint8) */
#define RR_BUTTON_ATTACK (1 << 0)
#define RR_BUTTON_USE (1 << 1)
#define RR_BUTTON_HOLSTER (1 << 2)
#define RR_BUTTON_JUMP (1 << 3)
#define RR_BUTTON_CROUCH (1 << 4)
#define RR_BUTTON_ANY (1 << 7)

typedef struct
{
	float normal[3];
	float dist;
	uint8_t type;
	uint8_t signbits;
	uint8_t pad[2];
} rr_cplane_t;

typedef struct
{
	char name[32];
	uint32_t flags;
	int32_t value;
	uint32_t id; /* unique texinfo ID, offset by 1 (0 is 'null') */
	char material[16];
} rr_csurface_t;

typedef struct
{
	bool allsolid;
	bool startsolid;
	float fraction;
	float endpos[3];
	rr_cplane_t plane;
	rr_csurface_t *surface;
	uint32_t contents;
	rr_edict_t *ent;
	rr_cplane_t plane2;
	rr_csurface_t *surface2;
} rr_trace_t;

typedef struct
{
	int32_t pm_type;
	float origin[3];
	float velocity[3];
	uint16_t pm_flags;
	uint16_t pm_time; /* milliseconds */
	int16_t gravity;
	float delta_angles[3];
	int8_t viewheight;
} rr_pmove_state_t;

typedef struct
{
	uint8_t msec;
	uint8_t buttons;
	float angles[3];
	float forwardmove, sidemove;
	uint32_t server_frame;
} rr_usercmd_t;

typedef struct
{
	uint32_t num;
	rr_trace_t traces[RR_MAXTOUCH];
} rr_touch_list_t;

typedef struct
{
	rr_pmove_state_t s;

	rr_usercmd_t cmd;
	bool snapinitial;

	rr_touch_list_t touch;

	float viewangles[3];
	float mins[3], maxs[3];

	rr_edict_t *groundentity;
	rr_cplane_t groundplane;
	uint32_t watertype;
	uint8_t waterlevel;

	rr_edict_t *player;

	rr_trace_t (*trace)(const float *start, const float *mins, const float *maxs,
		const float *end, const rr_edict_t *passent, uint32_t contentmask);
	rr_trace_t (*clip)(const float *start, const float *mins, const float *maxs,
		const float *end, uint32_t contentmask);
	uint32_t (*pointcontents)(const float *point);

	float viewoffset[3];

	float screen_blend[4];
	uint8_t rdflags;
	bool jump_sound;
	bool step_clip;
	float impact_delta;
} rr_pmove_t;

/* a classic command as the re-release game takes it: jump and crouch are
   buttons there (upmove here), angles are degrees. The server and the
   client's prediction must convert the same way. */
static inline void
RR_ConvertUsercmd(const usercmd_t *from, rr_usercmd_t *to, uint32_t server_frame)
{
	int i;

	memset(to, 0, sizeof(*to));
	to->msec = from->msec;
	to->buttons = from->buttons & (RR_BUTTON_ATTACK | RR_BUTTON_USE | RR_BUTTON_ANY);

	if (from->upmove > 0)
	{
		to->buttons |= RR_BUTTON_JUMP;
	}
	else if (from->upmove < 0)
	{
		to->buttons |= RR_BUTTON_CROUCH;
	}

	for (i = 0; i < 3; i++)
	{
		to->angles[i] = (float)SHORT2ANGLE(from->angles[i]);
	}

	to->forwardmove = from->forwardmove;
	to->sidemove = from->sidemove;
	to->server_frame = server_frame;
}

/* rerelease.c: a trace in the re-release's form (surfaces of the loaded map) */
rr_trace_t RR_TraceFromClassic(const trace_t *trace, rr_edict_t *ent);
void RR_FreeSurfaces(void);

#endif /* COMMON_RERELEASE_H */
