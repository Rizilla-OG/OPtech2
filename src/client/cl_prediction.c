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
 * This file implements interpolation between two frames. This is used
 * to smooth down network play
 *
 * =======================================================================
 */

#include <math.h>
#include "header/client.h"
#include "../common/header/rerelease.h" /* optech2: re-release movement */
#include "sound/header/local.h" /* optech2: underwater sound */

void
CL_CheckPredictionError(void)
{
	int frame;
	int delta[3];
	int len;

	if (!cl_predict->value ||
		(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION))
	{
		return;
	}

	/* calculate the last usercmd_t we sent that the server has processed */
	frame = cls.netchan.incoming_acknowledged;
	frame &= (CMD_BACKUP - 1);

	/* compare what the server returned with what we had predicted it to be */
	VectorSubtract(cl.frame.origin,
			cl.predicted_origins[frame], delta);

	/* optech2: a re-release game: its exact position, rounded as the
	   prediction rounds (the 1/8 one is truncated, so 24.1249 is 24.0) */
	if (cl.frame.playerstate.rr_pmove)
	{
		int i;

		for (i = 0; i < 3; i++)
		{
			delta[i] = (int)lroundf(cl.frame.playerstate.rr_origin[i] * 8) -
					   cl.predicted_origins[frame][i];
		}
	}

	/* save the prediction error for interpolation */
	len = abs(delta[0]) + abs(delta[1]) + abs(delta[2]);

	/* 80 world units */
	if (len > 640)
	{
		/* a teleport or something */
		VectorClear(cl.prediction_error);
	}
	else
	{
		size_t i;

		if (cl_showmiss->value && (delta[0] || delta[1] || delta[2]))
		{
			Com_Printf("prediction miss on %i: %i\n", cl.frame.serverframe,
					delta[0] + delta[1] + delta[2]);

			/* optech2: each axis, in 1/8 units */
			if (cl_showmiss->value > 1)
			{
				Com_Printf("  server %i %i %i predicted %i %i %i\n",
					cl.frame.origin[0], cl.frame.origin[1], cl.frame.origin[2],
					cl.predicted_origins[frame][0], cl.predicted_origins[frame][1],
					cl.predicted_origins[frame][2]);

				if (cl.frame.playerstate.rr_pmove)
				{
					Com_Printf("  exact %.4f %.4f %.4f\n", cl.frame.playerstate.rr_origin[0],
						cl.frame.playerstate.rr_origin[1], cl.frame.playerstate.rr_origin[2]);
				}
			}
		}

		VectorCopy(cl.frame.origin,
				cl.predicted_origins[frame]);

		/* optech2: a re-release game: store its exact position, rounded as
		   above, or a second frame acking the same command (no new command
		   sent in between) compares the exact one to the truncated one */
		if (cl.frame.playerstate.rr_pmove)
		{
			for (i = 0; i < 3; i++)
			{
				cl.predicted_origins[frame][i] =
					(int)lroundf(cl.frame.playerstate.rr_origin[i] * 8);
			}
		}

		/* save for error itnerpolation */
		for (i = 0; i < 3; i++)
		{
			cl.prediction_error[i] = delta[i] * 0.125f;
		}
	}
}

/*
 * optech2: lifts and other moving brush models in prediction. The server
 * runs a player's commands, then moves the lifts and pushes whoever stands
 * on them; the client replays the commands the server hasn't run yet against
 * the lift where the last frame left it, one tick behind (the snapping on
 * lifts). So while replaying, each moving brush model is moved on by its
 * speed for the time replayed so far, and the player is carried along with
 * the one under their feet. cl_predict_movers 0 turns this off.
 */
extern cvar_t *cl_predict_movers;
static float predicted_time; /* seconds of commands replayed so far */

/* a brush model's speed in units per second from its last move */
static qboolean
CL_MoverVelocity(const entity_xstate_t *ent, vec3_t velocity)
{
	const centity_t *cent;

	if (!cl_predict_movers || !cl_predict_movers->value || ent->solid != 31)
	{
		return false;
	}

	cent = &cl_entities[ent->number];

	if (cent->serverframe != cl.frame.serverframe)
	{
		return false;
	}

	/* prev is the state before the entity's last move, move_lerp_msec the
	   time between them (cl_parse.c); only a lift that moved in this very
	   frame is still moving */
	if (cent->move_change_time != (int)((long long)cl.frame.serverframe * 1000 / cl.tickrate) ||
		cent->move_lerp_msec <= 0)
	{
		return false;
	}

	VectorSubtract(cent->current.origin, cent->prev.origin, velocity);

	if (VectorCompare(velocity, vec3_origin))
	{
		return false;
	}

	VectorScale(velocity, 1000.0f / cent->move_lerp_msec, velocity);
	return true;
}

/* the entity a prediction trace returned, if it is a frame entity */
static const entity_xstate_t *
CL_PredictionTraceEntity(const struct edict_s *traced)
{
	const entity_xstate_t *ent = (const entity_xstate_t *)traced;

	if (ent < cl_parse_entities || ent >= cl_parse_entities + MAX_PARSE_ENTITIES)
	{
		return NULL; /* nothing or the world */
	}

	return ent;
}

static void
CL_ClipMoveToEntities(const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, trace_t *tr, int contentmask, qboolean skip_players)
{
	int i;

	for (i = 0; i < cl.frame.num_entities; i++)
	{
		trace_t trace;
		int headnode;
		float *angles;
		int num;
		const cmodel_t *cmodel;
		vec3_t bmins, bmaxs;
		entity_xstate_t *ent;

		num = (cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1);
		ent = &cl_parse_entities[num];

		if (!ent->solid)
		{
			continue;
		}

		if (ent->number == cl.playernum + 1)
		{
			continue;
		}

		/* optech2: re-release movement may pass through other players */
		if (skip_players && (ent->number >= 1) && (ent->number <= MAX_CLIENTS) &&
			cl.configstrings[CS_PLAYERSKINS + ent->number - 1][0])
		{
			continue;
		}

		if (ent->solid == 31)
		{
			/* special value for bmodel */
			cmodel = cl.model_clip[ent->modelindex];

			if (!cmodel)
			{
				continue;
			}

			headnode = cmodel->headnode;
			angles = ent->angles;
		}
		else
		{
			int x, zd, zu;

			/* encoded bbox */
			x = 8 * (ent->solid & 31);
			zd = 8 * ((ent->solid >> 5) & 31);
			zu = 8 * ((ent->solid >> 10) & 63) - 32;

			bmins[0] = bmins[1] = -(float)x;
			bmaxs[0] = bmaxs[1] = (float)x;
			bmins[2] = -(float)zd;
			bmaxs[2] = (float)zu;

			headnode = CM_HeadnodeForBox(bmins, bmaxs);
			angles = vec3_origin; /* boxes don't rotate */
		}

		if (tr->allsolid)
		{
			return;
		}

		{
			vec3_t origin, velocity;

			/* optech2: a moving lift where it is after the replayed time */
			VectorCopy(ent->origin, origin);

			if (predicted_time > 0 && CL_MoverVelocity(ent, velocity))
			{
				VectorMA(origin, predicted_time, velocity, origin);
			}

			trace = CM_TransformedBoxTrace(start, end,
				mins, maxs, headnode, contentmask,
				origin, angles);
		}

		if (trace.allsolid || trace.startsolid ||
			(trace.fraction < tr->fraction))
		{
			trace.ent = (struct edict_s *)ent;

			if (tr->startsolid)
			{
				*tr = trace;
				tr->startsolid = true;
			}
			else
			{
				*tr = trace;
			}
		}
	}
}

trace_t
CL_PMTrace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	trace_t t;

	/* check against world */
	t = CM_BoxTrace(start, end, mins, maxs, 0, MASK_PLAYERSOLID);

	if (t.fraction < 1.0)
	{
		// cppcheck-suppress intToPointerCast ; optech2: stock "hit something" marker, never dereferenced
		t.ent = (struct edict_s *)1;
	}

	/* check all other solid models */
	CL_ClipMoveToEntities(start, mins, maxs, end, &t, MASK_PLAYERSOLID, false);

	return t;
}

static int
CL_PMpointcontents(const vec3_t point)
{
	int i;
	int contents;

	contents = CM_PointContents(point, 0);

	for (i = 0; i < cl.frame.num_entities; i++)
	{
		entity_xstate_t *ent;
		int num;
		const cmodel_t *cmodel;

		num = (cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1);
		ent = &cl_parse_entities[num];

		if (ent->solid != 31) /* special value for bmodel */
		{
			continue;
		}

		cmodel = cl.model_clip[ent->modelindex];

		if (!cmodel)
		{
			continue;
		}

		contents |= CM_TransformedPointContents(point, cmodel->headnode,
				ent->origin, ent->angles);
	}

	return contents;
}

/*
 * optech2: underwater sound filter from the view position, each frame. It
 * was switched from inside player movement, which is game code now (and
 * did not run in demos or with prediction off).
 */
void
CL_UpdateUnderwaterSound(const vec3_t vieworg)
{
	static qboolean underwater;
	qboolean now_underwater;

	now_underwater = (cls.state == ca_active) &&
					 ((CL_PMpointcontents(vieworg) & MASK_WATER) != 0);

	if (now_underwater && !underwater)
	{
		underwater = true;
		snd_is_underwater = true;

#ifdef USE_OPENAL
		AL_Underwater();
#endif
	}
	else if (!now_underwater && underwater)
	{
		underwater = false;
		snd_is_underwater = false;

#ifdef USE_OPENAL
		AL_Overwater();
#endif
	}
}

/*
 * Sets cl.predicted_origin and cl.predicted_angles
 */
/*
 * optech2: prediction for re-release game libraries, with the library's
 * own player movement (re-release pmove_t, float state), so the client
 * moves the player exactly as the server does.
 */
static char cl_rr_world; /* stands for the world entity in prediction traces */

static rr_trace_t
CL_RR_PMTrace(const float *start, const float *mins, const float *maxs,
	const float *end, const rr_edict_t *passent, uint32_t contentmask)
{
	trace_t t;
	vec3_t zero = {0, 0, 0};

	(void)passent; /* always the player, skipped by number */

	if (!mins)
	{
		mins = zero;
	}

	if (!maxs)
	{
		maxs = zero;
	}

	t = CM_BoxTrace(start, end, mins, maxs, 0, (int)contentmask);
	/* the world: the movement code takes "no entity" as "not on the
	   ground", so it needs something (never read, only compared) */
	t.ent = (struct edict_s *)&cl_rr_world;

	CL_ClipMoveToEntities(start, mins, maxs, end, &t, (int)contentmask,
		!(contentmask & RR_CONTENTS_PLAYER));

	return RR_TraceFromClassic(&t, (rr_edict_t *)t.ent);
}

static rr_trace_t
CL_RR_PMClip(const float *start, const float *mins, const float *maxs,
	const float *end, uint32_t contentmask)
{
	trace_t t;
	vec3_t zero = {0, 0, 0};

	t = CM_BoxTrace(start, end, mins ? mins : zero, maxs ? maxs : zero, 0, (int)contentmask);
	return RR_TraceFromClassic(&t, (rr_edict_t *)&cl_rr_world);
}

static uint32_t
CL_RR_PMPointContents(const float *point)
{
	return (uint32_t)CL_PMpointcontents(point);
}

static void
CL_PredictMovementRR(CL_RR_Pmove_t rr_pmove)
{
	const player_xstate_t *ps = &cl.frame.playerstate;
	int ack, current, i;
	rr_pmove_t pm;
	float old_z;

	ack = cls.netchan.incoming_acknowledged;
	current = cls.netchan.outgoing_sequence;

	/* if we are too far out of date, just freeze */
	if (current - ack >= CMD_BACKUP)
	{
		if (cl_showmiss->value)
		{
			Com_Printf("exceeded CMD_BACKUP\n");
		}

		return;
	}

	/* the state the server sent, back in the re-release's units */
	memset(&pm, 0, sizeof(pm));
	pm.s.pm_type = ps->rr_pm_type;
	pm.s.pm_flags = ps->rr_pm_flags;
	pm.s.pm_time = ps->rr_pm_time;
	pm.s.gravity = ps->pmove.gravity;
	pm.s.viewheight = (int8_t)ps->rr_viewheight;

	for (i = 0; i < 3; i++)
	{
		pm.s.origin[i] = ps->rr_origin[i];
		pm.s.velocity[i] = ps->rr_velocity[i];
		pm.s.delta_angles[i] = ps->rr_delta_angles[i];
		pm.viewoffset[i] = ps->viewoffset[i];
	}

	/* the server sent the view offset with the view height in it */
	pm.viewoffset[2] -= ps->rr_viewheight;

	pm.trace = CL_RR_PMTrace;
	pm.clip = CL_RR_PMClip;
	pm.pointcontents = CL_RR_PMPointContents;
	pm.player = NULL; /* traces skip the player by number */

	predicted_time = 0;

	/* run the commands the server hasn't answered yet */
	while (++ack <= current)
	{
		const usercmd_t *cmd;
		int frame;

		frame = ack & (CMD_BACKUP - 1);
		cmd = &cl.cmds[frame];

		if (!cmd->msec)
		{
			continue;
		}

		RR_ConvertUsercmd(cmd, &pm.cmd, 0);
		pm.touch.num = 0;
		rr_pmove(&pm);

		/* the server pushes the player along with the lift under them */
		{
			const entity_xstate_t *ground = CL_PredictionTraceEntity((const struct edict_s *)pm.groundentity);
			vec3_t velocity;

			if (ground && CL_MoverVelocity(ground, velocity))
			{
				VectorMA(pm.s.origin, cmd->msec * 0.001f, velocity, pm.s.origin);
			}

			predicted_time += cmd->msec * 0.001f;
		}

		/* for the prediction error check, in 1/8 units like the server's */
		for (i = 0; i < 3; i++)
		{
			cl.predicted_origins[frame][i] = (int)lroundf(pm.s.origin[i] * 8);
		}
	}

	predicted_time = 0;

	/* stairs: smooth a step up while walking */
	old_z = cl.predicted_origin[2];

	if ((pm.s.pm_flags & RR_PMF_ON_GROUND) &&
		((pm.s.origin[2] - old_z) > 0.125f) && ((pm.s.origin[2] - old_z) <= 18.0f) &&
		((pm.s.velocity[0] != 0) || (pm.s.velocity[1] != 0)))
	{
		cl.predicted_step = pm.s.origin[2] - old_z;
		cl.predicted_step_time = cls.realtime - (int)(cls.nframetime * 500);
	}

	VectorCopy(pm.s.velocity, cl.predicted_velocity);
	VectorCopy(pm.s.origin, cl.predicted_origin);
	VectorCopy(pm.viewangles, cl.predicted_angles);
}

void
CL_PredictMovement(void)
{
	int ack, current, origin[3];
	vec3_t carried; /* optech2: lift carry not yet a whole 1/8 unit */
	pmove_t pm;
	int step;
	vec3_t tmp;

	if (cls.state != ca_active)
	{
		return;
	}

	if (cl_paused->value)
	{
		return;
	}

	if (!cl_predict->value ||
		(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION))
	{
		int i;

		/* just set angles */
		for (i = 0; i < 3; i++)
		{
			cl.predicted_angles[i] = cl.viewangles[i] + SHORT2ANGLE(
					cl.frame.playerstate.pmove.delta_angles[i]);
			/* optech2: the server's velocity for the speedometer */
			cl.predicted_velocity[i] = cl.frame.playerstate.pmove.velocity[i] * 0.125f;
		}

		return;
	}

	/* optech2: a re-release game: its own movement on its own state */
	if (cl.frame.playerstate.rr_pmove && CL_CGame_RRPmove())
	{
		static qboolean announced;

		if (!announced)
		{
			Com_Printf("client game: re-release movement for prediction\n");
			announced = true;
		}

		CL_PredictMovementRR(CL_CGame_RRPmove());
		return;
	}

	ack = cls.netchan.incoming_acknowledged;
	current = cls.netchan.outgoing_sequence;

	/* if we are too far out of date, just freeze */
	if (current - ack >= CMD_BACKUP)
	{
		if (cl_showmiss->value)
		{
			Com_Printf("exceeded CMD_BACKUP\n");
		}

		return;
	}

	/* copy current state to pmove */
	memset (&pm, 0, sizeof(pm));
	pm.trace = CL_PMTrace;
	pm.pointcontents = CL_PMpointcontents;
	pm.airaccelerate = atof(cl.configstrings[CS_AIRACCEL]);
	pm.s = cl.frame.playerstate.pmove;

	/* TODO: mins/maxs should be updated on current frame number */
	VectorCopy(cl.baseclientinfo.maxs, pm.maxs);
	VectorCopy(cl.baseclientinfo.mins, pm.mins);
	VectorCopy(cl.frame.origin, origin);

	predicted_time = 0;
	VectorClear(carried);

	/* run frames */
	while (++ack <= current)
	{
		const usercmd_t *cmd;
		int frame;

		frame = ack & (CMD_BACKUP - 1);
		cmd = &cl.cmds[frame];

		// Ignore null entries
		if (!cmd->msec)
		{
			continue;
		}

		pm.cmd = *cmd;
		CL_CGame_PmoveEx(&pm, origin); /* optech2: the game's movement */

		/* optech2: the server will push the player along with the lift
		   under their feet after this command */
		{
			const entity_xstate_t *ground = CL_PredictionTraceEntity(pm.groundentity);
			vec3_t velocity;

			if (ground && CL_MoverVelocity(ground, velocity))
			{
				/* in 1/8 units; the fraction carries over to the next
				   command, so many short commands don't lose it */
				for (int i = 0; i < 3; i++)
				{
					int whole;

					carried[i] += velocity[i] * cmd->msec * 0.001f * 8;
					whole = (int)carried[i];
					carried[i] -= whole;
					origin[i] += whole;
				}
			}

			predicted_time += cmd->msec * 0.001f;
		}

		/* save for debug checking */
		VectorCopy(origin, cl.predicted_origins[frame]);
	}

	predicted_time = 0; /* optech2: other traces see lifts where the frame has them */

	// step is used for movement prediction on stairs
	// (so moving up/down stairs is smooth)
	step = origin[2] - (int)(cl.predicted_origin[2] * 8);
	/* optech2: speedometer (velocity is in 1/8 units) */
	cl.predicted_velocity[0] = pm.s.velocity[0] * 0.125f;
	cl.predicted_velocity[1] = pm.s.velocity[1] * 0.125f;
	cl.predicted_velocity[2] = pm.s.velocity[2] * 0.125f;
	VectorCopy(pm.s.velocity, tmp);

	if (((step > 62 && step < 66) || (step > 94 && step < 98) || (step > 126 && step < 130))
		&& !VectorCompare(tmp, vec3_origin)
		&& (pm.s.pm_flags & PMF_ON_GROUND))
	{
		cl.predicted_step = step * 0.125f;
		cl.predicted_step_time = cls.realtime - (int)(cls.nframetime * 500);
	}

	/* copy results out for rendering */
	cl.predicted_origin[0] = origin[0] * 0.125f;
	cl.predicted_origin[1] = origin[1] * 0.125f;
	cl.predicted_origin[2] = origin[2] * 0.125f;

	VectorCopy(pm.viewangles, cl.predicted_angles);
}

