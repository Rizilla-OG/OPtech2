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
 * Monster movement support functions. While unused by the CTF code
 * these functions must be here since they're referenced at several
 * points inside the game.so.
 *
 * =======================================================================
 */

#include "../../header/local.h"

#define DI_NODIR -1

/* optech2: how far monsters drop off ledges on their paths, and how fast
   they go off the edge */
#define MONSTER_DROP_HEIGHT 256
#define MONSTER_DROP_SPEED 100

int c_yes, c_no;

/* this is used for communications out of
 * sv_movestep to say what entity is blocking us */
edict_t *new_bad;

/*
 * Returns false if any part of the
 * bottom of the entity is off an edge
 * that is not a staircase.
 */

qboolean
M_CheckBottom(edict_t *ent)
{
	vec3_t mins, maxs, start, stop;
	trace_t trace;
	int x, y;
	float mid, bottom;

	if (!ent)
	{
		return false;
	}

	VectorAdd(ent->s.origin, ent->mins, mins);
	VectorAdd(ent->s.origin, ent->maxs, maxs);

	/* if all of the points under the corners are solid
	   world, don't bother with the tougher checks
	   the corners must be within 16 of the midpoint */
	start[2] = mins[2] - 1;

	if (ent->gravityVector[2] > 0)
	{
		start[2] = maxs[2] + 1;
	}

	for (x = 0; x <= 1; x++)
	{
		for (y = 0; y <= 1; y++)
		{
			start[0] = x ? maxs[0] : mins[0];
			start[1] = y ? maxs[1] : mins[1];

			if (gi.pointcontents(start) != CONTENTS_SOLID)
			{
				goto realcheck;
			}
		}
	}

	c_yes++;
	return true; /* we got out easy */

realcheck:
	c_no++;

	/* check it for real... */
	start[2] = mins[2];

	/* the midpoint must be within 16 of the bottom */
	start[0] = stop[0] = (mins[0] + maxs[0]) * 0.5;
	start[1] = stop[1] = (mins[1] + maxs[1]) * 0.5;

	if (ent->gravityVector[2] < 0)
	{
		start[2] = mins[2];
		stop[2] = start[2] - (2 * STEPSIZE);
	}
	else
	{
		start[2] = maxs[2];
		stop[2] = start[2] + (2 * STEPSIZE);
	}

	trace = gi.trace(start, vec3_origin, vec3_origin,
			stop, ent, MASK_MONSTERSOLID);

	if (trace.fraction == 1.0)
	{
		return false;
	}

	mid = bottom = trace.endpos[2];

	/* the corners must be within 16 of the midpoint */
	for (x = 0; x <= 1; x++)
	{
		for (y = 0; y <= 1; y++)
		{
			start[0] = stop[0] = x ? maxs[0] : mins[0];
			start[1] = stop[1] = y ? maxs[1] : mins[1];

			trace = gi.trace(start, vec3_origin, vec3_origin,
					stop, ent, MASK_MONSTERSOLID);

			if (ent->gravityVector[2] > 0)
			{
				if ((trace.fraction != 1.0) && (trace.endpos[2] < bottom))
				{
					bottom = trace.endpos[2];
				}

				if ((trace.fraction == 1.0) ||
					(trace.endpos[2] - mid > STEPSIZE))
				{
					return false;
				}
			}
			else
			{
				if ((trace.fraction != 1.0) && (trace.endpos[2] > bottom))
				{
					bottom = trace.endpos[2];
				}

				if ((trace.fraction == 1.0) ||
					(mid - trace.endpos[2] > STEPSIZE))
				{
					return false;
				}
			}
		}
	}

	c_yes++;
	return true;
}

static qboolean
IsBadAhead(edict_t *self, edict_t *bad, vec3_t move)
{
	vec3_t dir;
	vec3_t forward;
	float dp_bad, dp_move;
	vec3_t move_copy;

	if (!self || !bad)
	{
		return false;
	}

	VectorCopy(move, move_copy);

	VectorSubtract(bad->s.origin, self->s.origin, dir);
	VectorNormalize(dir);
	AngleVectors(self->s.angles, forward, NULL, NULL);
	dp_bad = DotProduct(forward, dir);

	VectorNormalize(move_copy);
	AngleVectors(self->s.angles, forward, NULL, NULL);
	dp_move = DotProduct(forward, move_copy);

	if ((dp_bad < 0) && (dp_move < 0))
	{
		return true;
	}

	if ((dp_bad > 0) && (dp_move > 0))
	{
		return true;
	}

	return false;
}

/*
 * Called by monster program code.
 * The move will be adjusted for slopes
 * and stairs, but if the move isn't
 * possible, no move is done, false is
 * returned, and pr_global_struct->trace_normal
 * is set to the normal of the blocking wall
 */
static qboolean
SV_movestep(edict_t *ent, vec3_t move, qboolean relink)
{
	vec3_t oldorg, neworg, end;
	trace_t trace;
	float stepsize;
	vec3_t test;
	int contents;
	edict_t *current_bad = NULL;

	if (!ent)
	{
		return false;
	}

	if (ent->health > 0)
	{
		current_bad = CheckForBadArea(ent);

		if (current_bad)
		{
			ent->bad_area = current_bad;

			if (ent->enemy && !strcmp(ent->enemy->classname, "tesla"))
			{
				/* if the tesla is in front of us, back up... */
				if (IsBadAhead(ent, current_bad, move))
				{
					VectorScale(move, -1, move);
				}
			}
		}
		else if (ent->bad_area)
		{
			/* if we're no longer in a bad area, get back to business. */
			ent->bad_area = NULL;

			if (ent->oldenemy)
			{
				ent->enemy = ent->oldenemy;
				ent->goalentity = ent->oldenemy;
				FoundTarget(ent);
				return true;
			}
		}
	}

	/* try the move */
	VectorCopy(ent->s.origin, oldorg);
	VectorAdd(ent->s.origin, move, neworg);

	/* flying monsters don't step up */
	if (ent->flags & (FL_SWIM | FL_FLY))
	{
		int i;

		/* try one move with vertical motion, then one without */
		for (i = 0; i < 2; i++)
		{
			VectorAdd(ent->s.origin, move, neworg);

			if ((i == 0) && ent->enemy)
			{
				float dz;

				if (!ent->goalentity)
				{
					ent->goalentity = ent->enemy;
				}

				dz = ent->s.origin[2] - ent->goalentity->s.origin[2];

				if (ent->goalentity->client)
				{
					float minheight;

					/* we want the carrier to stay a certain distance off the ground,
					   to help prevent him from shooting his fliers, who spawn in below him */
					if (!strcmp(ent->classname, "monster_carrier"))
					{
						minheight = 104;
					}
					else
					{
						minheight = 40;
					}

					if (dz > minheight)
					{
						neworg[2] -= 8;
					}

					if (!((ent->flags & FL_SWIM) && (ent->waterlevel < WATER_WAIST)))
					{
						if (dz < (minheight - 10))
						{
							neworg[2] += 8;
						}
					}
				}
				else
				{
					if (strcmp(ent->classname, "monster_fixbot") == 0)
					{
						if ((ent->s.frame >= 105) && (ent->s.frame <= 120))
						{
							if (dz > 12)
							{
								neworg[2]--;
							}
							else if (dz < -12)
							{
								neworg[2]++;
							}
						}
						else if ((ent->s.frame >= 31) && (ent->s.frame <= 88))
						{
							if (dz > 12)
							{
								neworg[2] -= 12;
							}
							else if (dz < -12)
							{
								neworg[2] += 12;
							}
						}
						else
						{
							if (dz > 12)
							{
								neworg[2] -= 8;
							}
							else if (dz < -12)
							{
								neworg[2] += 8;
							}
						}
					}
					else
					{
						if (dz > 8)
						{
							neworg[2] -= 8;
						}
						else if (dz > 0)
						{
							neworg[2] -= dz;
						}
						else if (dz < -8)
						{
							neworg[2] += 8;
						}
						else
						{
							neworg[2] += dz;
						}
					}
				}
			}

			trace = gi.trace(ent->s.origin, ent->mins, ent->maxs,
					neworg, ent, MASK_MONSTERSOLID);

			/* fly monsters don't enter water voluntarily */
			if (ent->flags & FL_FLY)
			{
				if (!ent->waterlevel)
				{
					test[0] = trace.endpos[0];
					test[1] = trace.endpos[1];
					test[2] = trace.endpos[2] + ent->mins[2] + 1;
					contents = gi.pointcontents(test);

					if (contents & MASK_WATER)
					{
						return false;
					}
				}
			}

			/* swim monsters don't exit water voluntarily */
			if (ent->flags & FL_SWIM)
			{
				if (ent->waterlevel < WATER_WAIST)
				{
					test[0] = trace.endpos[0];
					test[1] = trace.endpos[1];
					test[2] = trace.endpos[2] + ent->mins[2] + 1;
					contents = gi.pointcontents(test);

					if (!(contents & MASK_WATER))
					{
						return false;
					}
				}
			}

			if ((trace.fraction == 1) && (!trace.allsolid) && (!trace.startsolid))
			{
				VectorCopy(trace.endpos, ent->s.origin);

				if (!current_bad && CheckForBadArea(ent))
				{
					VectorCopy(oldorg, ent->s.origin);
				}
				else
				{
					if (relink)
					{
						gi.linkentity(ent);
						G_TouchTriggers(ent);
					}

					return true;
				}
			}

			if (!ent->enemy)
			{
				break;
			}
		}

		return false;
	}

	/* push down from a step height above the wished position */
	if (!(ent->monsterinfo.aiflags & AI_NOSTEP))
	{
		stepsize = STEPSIZE;
	}
	else
	{
		stepsize = 1;
	}

	/* trace from 1 stepsize gravityUp to 2 stepsize gravityDown. */
	VectorMA(neworg, -1 * stepsize, ent->gravityVector, neworg);
	VectorMA(neworg, 2 * stepsize, ent->gravityVector, end);

	trace = gi.trace(neworg, ent->mins, ent->maxs, end, ent, MASK_MONSTERSOLID);

	if (trace.allsolid)
	{
		return false;
	}

	if (trace.startsolid)
	{
		neworg[2] -= stepsize;
		trace = gi.trace(neworg, ent->mins, ent->maxs,
				end, ent, MASK_MONSTERSOLID);

		if (trace.allsolid || trace.startsolid)
		{
			return false;
		}
	}

	/* don't go in to water */
	if (ent->waterlevel == WATER_NONE)
	{
		test[0] = trace.endpos[0];
		test[1] = trace.endpos[1];

		if (ent->gravityVector[2] > 0)
		{
			test[2] = trace.endpos[2] + ent->maxs[2] - 1;
		}
		else
		{
			test[2] = trace.endpos[2] + ent->mins[2] + 1;
		}

		contents = gi.pointcontents(test);

		if (contents & MASK_WATER)
		{
			return false;
		}
	}

	if (trace.fraction == 1)
	{
		/* if monster had the ground pulled out, go ahead and fall */
		if (ent->flags & FL_PARTIALGROUND)
		{
			VectorAdd(ent->s.origin, move, ent->s.origin);

			if (relink)
			{
				gi.linkentity(ent);
				G_TouchTriggers(ent);
			}

			ent->groundentity = NULL;

			return true;
		}

		return false; /* walked off an edge */
	}

	/* check point traces down for dangling corners */
	VectorCopy(trace.endpos, ent->s.origin);

	if (ent->health > 0)
	{
		/* use AI_BLOCKED to tell the calling layer that we're now mad at a tesla */
		new_bad = CheckForBadArea(ent);

		if (!current_bad && new_bad)
		{
			if (new_bad->owner && !strcmp(new_bad->owner->classname, "tesla"))
			{
				/* A monster that is already angry at a tesla is left alone.
				   The original guarded that case with a misspelled "telsa",
				   so the branch never ran and the monster was retargeted at
				   the blocking tesla instead, which is the behaviour this
				   condition inherited when the chain was collapsed. */
				if (!ent->enemy || !ent->enemy->inuse ||
					(strcmp(ent->enemy->classname, "tesla") &&
					 (!ent->enemy->client || !visible(ent, ent->enemy))))
				{
					TargetTesla(ent, new_bad->owner);
					ent->monsterinfo.aiflags |= AI_BLOCKED;
				}
			}

			VectorCopy(oldorg, ent->s.origin);
			return false;
		}
	}

	if (!M_CheckBottom(ent))
	{
		if (ent->flags & FL_PARTIALGROUND)
		{
			/* entity had floor mostly pulled out
			   from underneath it and is trying to
			   correct */
			if (relink)
			{
				gi.linkentity(ent);
				G_TouchTriggers(ent);
			}

			return true;
		}

		VectorCopy(oldorg, ent->s.origin);
		return false;
	}

	if (ent->flags & FL_PARTIALGROUND)
	{
		ent->flags &= ~FL_PARTIALGROUND;
	}

	ent->groundentity = trace.ent;
	ent->groundentity_linkcount = trace.ent->linkcount;

	/* the move is ok */
	if (relink)
	{
		gi.linkentity(ent);

		/* [Paril-KEX] this is something N64 does to avoid doors opening
		 * at the start of a level, which triggers some monsters to spawn. */
		if (!level.is_n64 || level.time > level.ticktime) /* optech2: after the first server frame */
		{
			G_TouchTriggers(ent);
		}
	}

	return true;
}

/* ============================================================================ */

void
M_ChangeYaw(edict_t *ent)
{
	float ideal;
	float current;
	float move;
	float speed;

	if (!ent)
	{
		return;
	}

	current = anglemod(ent->s.angles[YAW]);
	ideal = ent->ideal_yaw;

	if (current == ideal)
	{
		return;
	}

	move = ideal - current;
	speed = ent->yaw_speed * level.ticktime / FRAMETIME; /* optech2: yaw_speed is per 0.1 s */

	if (ideal > current)
	{
		if (move >= 180)
		{
			move = move - 360;
		}
	}
	else
	{
		if (move <= -180)
		{
			move = move + 360;
		}
	}

	if (move > 0)
	{
		if (move > speed)
		{
			move = speed;
		}
	}
	else
	{
		if (move < -speed)
		{
			move = -speed;
		}
	}

	ent->s.angles[YAW] = anglemod(current + move);
}

/*
 * Turns to the movement direction, and
 * walks the current distance if facing it.
 */
static qboolean
SV_StepDirection(edict_t *ent, float yaw, float dist)
{
	vec3_t move, oldorigin;

	if (!ent)
	{
		return false;
	}

	if (!ent->inuse)
	{
		return true;
	}

	ent->ideal_yaw = yaw;
	M_ChangeYaw(ent);

	yaw = yaw * M_PI * 2 / 360;
	move[0] = cos(yaw) * dist;
	move[1] = sin(yaw) * dist;
	move[2] = 0;

	VectorCopy(ent->s.origin, oldorigin);

	if (SV_movestep(ent, move, false))
	{
		float delta;

		ent->monsterinfo.aiflags &= ~AI_BLOCKED;

		if (!ent->inuse)
		{
			return true;
		}

		delta = ent->s.angles[YAW] - ent->ideal_yaw;

		if (strncmp(ent->classname, "monster_widow", 13))
		{
			if ((delta > 45) && (delta < 315))
			{
				/* not turned far enough, so don't take the step */
				VectorCopy(oldorigin, ent->s.origin);
			}
		}

		gi.linkentity(ent);
		G_TouchTriggers(ent);
		return true;
	}

	gi.linkentity(ent);
	G_TouchTriggers(ent);
	return false;
}

static void
SV_FixCheckBottom(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	ent->flags |= FL_PARTIALGROUND;
}

static void
SV_NewChaseDir(edict_t *actor, edict_t *enemy, float dist)
{
	float deltax, deltay;
	float d[3];
	float tdir, olddir, turnaround;

	if (!actor || !enemy)
	{
		return;
	}

	olddir = anglemod((int)(actor->ideal_yaw / 45) * 45);
	turnaround = anglemod(olddir - 180);

	deltax = enemy->s.origin[0] - actor->s.origin[0];
	deltay = enemy->s.origin[1] - actor->s.origin[1];

	if (deltax > 10)
	{
		d[1] = 0;
	}
	else if (deltax < -10)
	{
		d[1] = 180;
	}
	else
	{
		d[1] = DI_NODIR;
	}

	if (deltay < -10)
	{
		d[2] = 270;
	}
	else if (deltay > 10)
	{
		d[2] = 90;
	}
	else
	{
		d[2] = DI_NODIR;
	}

	/* try direct route */
	if ((d[1] != DI_NODIR) && (d[2] != DI_NODIR))
	{
		if (d[1] == 0)
		{
			tdir = d[2] == 90 ? 45 : 315;
		}
		else
		{
			tdir = d[2] == 90 ? 135 : 215;
		}

		if ((tdir != turnaround) && SV_StepDirection(actor, tdir, dist))
		{
			return;
		}
	}

	/* try other directions */
	if (((randk() & 3) & 1) || (fabsf(deltay) > fabsf(deltax)))
	{
		tdir = d[1];
		d[1] = d[2];
		d[2] = tdir;
	}

	if ((d[1] != DI_NODIR) && (d[1] != turnaround) &&
		SV_StepDirection(actor, d[1], dist))
	{
		return;
	}

	if ((d[2] != DI_NODIR) && (d[2] != turnaround) &&
		SV_StepDirection(actor, d[2], dist))
	{
		return;
	}

	if (actor->monsterinfo.blocked)
	{
		if ((actor->inuse) && (actor->health > 0))
		{
			if ((actor->monsterinfo.blocked)(actor, dist))
			{
				return;
			}
		}
	}

	/* there is no direct path to the player, so pick another direction */
	if ((olddir != DI_NODIR) && SV_StepDirection(actor, olddir, dist))
	{
		return;
	}

	if (randk() & 1) /* randomly determine direction of search */
	{
		for (tdir = 0; tdir <= 315; tdir += 45)
		{
			if ((tdir != turnaround) && SV_StepDirection(actor, tdir, dist))
			{
				return;
			}
		}
	}
	else
	{
		for (tdir = 315; tdir >= 0; tdir -= 45)
		{
			if ((tdir != turnaround) && SV_StepDirection(actor, tdir, dist))
			{
				return;
			}
		}
	}

	if ((turnaround != DI_NODIR) && SV_StepDirection(actor, turnaround, dist))
	{
		return;
	}

	actor->ideal_yaw = olddir; /* can't move */

	/* if a bridge was pulled out from underneath
	   a monster, it may not have a valid standing
	   position at all */
	if (!M_CheckBottom(actor))
	{
		SV_FixCheckBottom(actor);
	}
}

static qboolean
SV_CloseEnough(const edict_t *ent, const edict_t *goal, float dist)
{
	int i;

	if (!ent || !goal)
	{
		return false;
	}

	for (i = 0; i < 3; i++)
	{
		if (goal->absmin[i] > ent->absmax[i] + dist)
		{
			return false;
		}

		if (goal->absmax[i] < ent->absmin[i] - dist)
		{
			return false;
		}
	}

	return true;
}

/* optech2: a step toward to_point that may leave the floor (SV_movestep
   refuses to walk off an edge). Once nothing is under it, it falls, carried
   on toward where the path lands, and asks for a path when it lands. */
static qboolean
M_NavWalkOff(edict_t *ent, const vec3_t to_point, float dist)
{
	vec3_t direction, end, below_end; // optech2: not "below", that is a function
	trace_t trace;
	float delta;

	direction[0] = to_point[0];
	direction[1] = to_point[1];
	direction[2] = 0;

	if (VectorNormalize(direction) < 1)
	{
		return false;
	}

	/* turned toward it first */
	delta = anglemod(ent->s.angles[YAW] - ent->ideal_yaw);

	if ((delta > 45) && (delta < 315))
	{
		return true;
	}

	VectorMA(ent->s.origin, dist, direction, end);
	trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, end, ent, MASK_MONSTERSOLID);

	if (trace.allsolid || trace.startsolid || (trace.fraction < 1))
	{
		return false;
	}

	VectorCopy(end, ent->s.origin);
	VectorCopy(end, below_end);
	below_end[2] -= 1;
	trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, below_end, ent, MASK_MONSTERSOLID);

	if (trace.fraction == 1)
	{
		/* off the edge */
		ent->groundentity = NULL;
		VectorScale(direction, MONSTER_DROP_SPEED, ent->velocity);
		ent->monsterinfo.nav_link_type = OPTECH2_PATH_LINK_WALK;
		ent->monsterinfo.nav_retry_time = 0;

		if (g_monster_nav->value >= 2)
		{
			gi.dprintf("%s walks off a ledge at %.0f %.0f %.0f\n", ent->classname, ent->s.origin[0],
				ent->s.origin[1], ent->s.origin[2]);
		}
	}

	gi.linkentity(ent);
	G_TouchTriggers(ent);
	return true;
}

/* optech2: how long a monster waits for a lift before it asks for another
   way (the lift may be held by a player, or broken) */
#define MONSTER_LIFT_WAIT 8

/* optech2: is point over the lift, or within reach of its sides? */
static qboolean
M_NavNearLift(const edict_t *lift, const vec3_t point, float reach)
{
	return (point[0] >= lift->absmin[0] - reach) && (point[0] <= lift->absmax[0] + reach) &&
		   (point[1] >= lift->absmin[1] - reach) && (point[1] <= lift->absmax[1] + reach);
}

/* optech2: a lift the navigation mesh has (func_plats nothing targets) */
static qboolean
M_NavIsLift(const edict_t *lift)
{
	return lift->inuse && lift->classname && !strcmp(lift->classname, "func_plat") && !lift->targetname;
}

/* optech2: the lift between from and to (near both; the nearest if there
   are several), NULL if there's none. The mesh puts a lift's far end up
   to its size's half width + 40 past its sides: reach covers the largest. */
static edict_t *
M_NavFindLift(const vec3_t from, const vec3_t to)
{
	const float reach = 64 + 40 + 24;
	edict_t *best = NULL;
	float best_distance = 0;
	vec3_t middle;
	int i;

	VectorAdd(from, to, middle);
	VectorScale(middle, 0.5f, middle);

	for (i = game.maxclients + 1; i < globals.num_edicts; i++)
	{
		edict_t *lift = &g_edicts[i];

		if (M_NavIsLift(lift) && M_NavNearLift(lift, from, reach) && M_NavNearLift(lift, to, reach))
		{
			const float dx = (lift->absmin[0] + lift->absmax[0]) * 0.5f - middle[0];
			const float dy = (lift->absmin[1] + lift->absmax[1]) * 0.5f - middle[1];
			const float distance = dx * dx + dy * dy;

			if (!best || (distance < best_distance))
			{
				best = lift;
				best_distance = distance;
			}
		}
	}

	return best;
}

/* optech2: the lift whose resting place point is on, if it isn't there now
   (an open shaft), else NULL */
static edict_t *
M_NavLiftAway(const vec3_t point)
{
	int i;

	for (i = game.maxclients + 1; i < globals.num_edicts; i++)
	{
		edict_t *lift = &g_edicts[i];

		if (M_NavIsLift(lift) && (lift->moveinfo.state != STATE_BOTTOM) && M_NavNearLift(lift, point, 0) &&
			(fabsf(point[2] - (lift->pos2[2] + lift->maxs[2])) <= 2 * STEPSIZE))
		{
			return lift;
		}
	}

	return NULL;
}

/* optech2: waiting for a lift: true while it may wait, false (and the
   path dropped for a while) once it has waited MONSTER_LIFT_WAIT */
static qboolean
M_NavLiftWait(edict_t *ent)
{
	if (!ent->monsterinfo.nav_lift_wait_end)
	{
		ent->monsterinfo.nav_lift_wait_end = level.time + MONSTER_LIFT_WAIT;
	}
	else if (level.time >= ent->monsterinfo.nav_lift_wait_end)
	{
		if (g_monster_nav->value >= 2)
		{
			gi.dprintf("%s gives up waiting for a lift\n", ent->classname);
		}

		ent->monsterinfo.nav_lift_wait_end = 0;
		ent->monsterinfo.nav_has_point = false;
		ent->monsterinfo.nav_link_type = OPTECH2_PATH_LINK_WALK;
		ent->monsterinfo.nav_retry_time = level.time + 2.0f; /* stock chase for a while */
		return false;
	}

	ent->monsterinfo.nav_retry_time = level.time + 0.5f; /* hold the path while waiting */
	return true;
}

/* optech2: a step toward the lift's middle; false if blocked */
static qboolean
M_NavStepOntoLift(edict_t *ent, const edict_t *lift, float dist)
{
	vec3_t to_point;

	to_point[0] = (lift->absmin[0] + lift->absmax[0]) * 0.5f - ent->s.origin[0];
	to_point[1] = (lift->absmin[1] + lift->absmax[1]) * 0.5f - ent->s.origin[1];
	to_point[2] = 0;

	if (!SV_StepDirection(ent, vectoyaw(to_point), dist))
	{
		ent->monsterinfo.nav_retry_time = 0;
		return false;
	}

	return true;
}

/* optech2: the path rides a lift to nav_move_point, the floor beside its
   other end. Onto it when it's at this end (else wait for it, or call it
   from the other end), all the way on, send it, stand still while it
   moves, then step off. Holds the path (no new one while riding: off the
   mesh); gives up after waiting MONSTER_LIFT_WAIT. */
static qboolean
M_NavLift(edict_t *ent, float dist)
{
	edict_t *lift = M_NavFindLift(ent->s.origin, ent->monsterinfo.nav_move_point);
	const float rise = ent->monsterinfo.nav_move_point[2] - (ent->s.origin[2] + ent->mins[2]);
	const qboolean going_up = (rise > 0);
	const int here = going_up ? STATE_BOTTOM : STATE_TOP; /* where the lift must be to get on */

	if (!lift)
	{
		ent->monsterinfo.nav_link_type = OPTECH2_PATH_LINK_WALK;
		ent->monsterinfo.nav_retry_time = 0;
		return false;
	}

	/* at the other end: step off */
	if (fabsf(rise) <= STEPSIZE)
	{
		vec3_t to_point;

		ent->monsterinfo.nav_lift_wait_end = 0;
		VectorSubtract(ent->monsterinfo.nav_move_point, ent->s.origin, to_point);

		if (!SV_StepDirection(ent, vectoyaw(to_point), dist))
		{
			ent->monsterinfo.nav_retry_time = 0;
			return false;
		}

		return true;
	}

	if (ent->groundentity == lift)
	{
		if ((lift->moveinfo.state == STATE_UP) || (lift->moveinfo.state == STATE_DOWN))
		{
			/* riding */
			ent->monsterinfo.nav_lift_wait_end = 0;
			ent->monsterinfo.nav_retry_time = level.time + 0.5f;
			return true;
		}

		if (lift->moveinfo.state == here)
		{
			/* all the way on before it goes (a box over the edge would hit
			   the shaft) */
			if ((ent->absmin[0] < lift->absmin[0]) || (ent->absmax[0] > lift->absmax[0]) ||
				(ent->absmin[1] < lift->absmin[1]) || (ent->absmax[1] > lift->absmax[1]))
			{
				if (!M_NavStepOntoLift(ent, lift, dist))
				{
					return M_NavLiftWait(ent);
				}

				ent->monsterinfo.nav_retry_time = level.time + 0.5f;
				return true;
			}

			if (g_monster_nav->value >= 2)
			{
				gi.dprintf("%s rides a lift %s\n", ent->classname, going_up ? "up" : "down");
			}

			ent->monsterinfo.nav_lift_wait_end = 0;
			ent->monsterinfo.nav_retry_time = level.time + 0.5f;

			if (going_up)
			{
				plat_go_up(lift);
			}
			else
			{
				plat_go_down(lift);
			}

			return true;
		}

		return M_NavLiftWait(ent);
	}

	if (lift->moveinfo.state == here)
	{
		/* onto it */
		if (!M_NavStepOntoLift(ent, lift, dist))
		{
			return M_NavLiftWait(ent);
		}

		ent->monsterinfo.nav_retry_time = level.time + 0.5f;
		return true;
	}

	/* at the other end: from the top, call it up (from the bottom it comes
	   back down on its own) */
	if (!going_up && (lift->moveinfo.state == STATE_BOTTOM))
	{
		plat_go_up(lift);
	}

	return M_NavLiftWait(ent);
}

/*
 * optech2: a step along the engine's navigation path to goal (walking
 * monsters only, when the engine offers paths and g_monster_nav is on), on
 * the mesh for the monster's size.
 * The path is asked for again every 0.25 s (staggered between monsters),
 * when its next corner is reached, or when a step fails. False when there's
 * no path to follow: the stock chase takes over.
 */
/* optech2: on a path's ladder, going up (off the ground while it climbs) */
static qboolean
M_NavClimbing(const edict_t *ent)
{
	return ent->monsterinfo.nav_has_point && (ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_LADDER) &&
		   (ent->monsterinfo.nav_move_point[2] > ent->s.origin[2] + ent->mins[2] - STEPSIZE);
}

/* optech2: climbs a path's ladder: at its foot (the path gives the link once
   the monster is there), up at a steady speed and pressed toward the top,
   then over the lip onto the floor at the top. Stock monsters have no climb:
   they rise with their run animation. False when the ladder goes down
   (walk off it like a ledge) or it's up already. */
static qboolean
M_NavClimb(edict_t *ent)
{
	const float feet = ent->s.origin[2] + ent->mins[2];
	vec3_t across;
	float distance;

	if (ent->monsterinfo.nav_move_point[2] <= feet + STEPSIZE && ent->groundentity)
	{
		return false;
	}

	VectorSubtract(ent->monsterinfo.nav_move_point, ent->s.origin, across);
	across[2] = 0;
	distance = VectorNormalize(across);

	if (distance < 1.0f)
	{
		vec3_t angles = {0, ent->s.angles[YAW], 0};

		AngleVectors(angles, across, NULL, NULL);
	}
	else
	{
		ent->ideal_yaw = vectoyaw(across);
		M_ChangeYaw(ent);
	}

	if (ent->monsterinfo.nav_move_point[2] > feet + STEPSIZE)
	{
		/* (starting: on the ground, or not rising at climbing speed yet) */
		if ((ent->groundentity || ent->velocity[2] < 100.0f) && g_monster_nav->value >= 2)
		{
			gi.dprintf("%s climbs a ladder at %.0f %.0f %.0f\n", ent->classname, ent->s.origin[0],
				ent->s.origin[1], ent->s.origin[2]);
		}

		/* climbing */
		VectorScale(across, 40.0f, ent->velocity);
		ent->velocity[2] = 200.0f;
	}
	else
	{
		/* at the top: over the lip */
		VectorScale(across, 150.0f, ent->velocity);
		ent->velocity[2] = Q_max(ent->velocity[2], 60.0f);
	}

	ent->groundentity = NULL;
	ent->monsterinfo.nav_retry_time = level.time + 0.3f; /* keep this link until it's up */
	return true;
}

/* optech2: the monsters with a jump of their own (they jump when blocked):
   how high they jump onto a ledge or over a barrier on a path (the
   re-release's jump_height) and how far they leap across a gap (takeoff to
   landing). Others don't jump on paths. */
static const struct
{
	const char *classname;
	float height;
	float leap;
} nav_jumpers[] = {
	{"monster_berserk", 40, 192},
	{"monster_guncmdr", 40, 192},
	{"monster_gunner", 40, 192},
	{"monster_infantry", 40, 192},
	{"monster_mutant", 68, 320},
	{"monster_parasite", 68, 256},
};

/* optech2: how far monsters drop off a ledge on a path (the mesh has drops of
   64 to 256, in steps of 64). The heavy ones drop less, the bosses never;
   every other walker 256. A monster definition's "drop_height" wins. */
static const struct
{
	const char *classname;
	int height;
} nav_drop_heights[] = {
	{"monster_arachnid", 128},
	{"monster_boss5", 64},
	{"monster_brain", 192},
	{"monster_chick", 192},
	{"monster_chick_heat", 192},
	{"monster_gladb", 192},
	{"monster_gladiator", 192},
	{"monster_guardian", 0},
	{"monster_jorg", 0},
	{"monster_makron", 0},
	{"monster_medic", 192},
	{"monster_medic_commander", 192},
	{"monster_ogre", 128},
	{"monster_shambler", 128},
	{"monster_supertank", 64},
	{"monster_tank", 128},
	{"monster_tank_commander", 128},
	{"monster_tankv", 128},
	{"monster_widow", 0},
	{"monster_widow2", 0},
};

static int
M_NavDropHeightOf(const char *classname)
{
	for (int i = 0; i < (int)(sizeof(nav_drop_heights) / sizeof(nav_drop_heights[0])); i++)
	{
		if (!Q_stricmp(classname, nav_drop_heights[i].classname))
		{
			return nav_drop_heights[i].height;
		}
	}

	return MONSTER_DROP_HEIGHT;
}

static int
M_NavDropHeight(const edict_t *ent)
{
	const monster_def_t *def;

	if (!ent->classname)
	{
		return MONSTER_DROP_HEIGHT;
	}

	def = Defs_FindMonster(ent->classname);

	if (def && (def->set & MDEF_DROP_HEIGHT))
	{
		return def->drop_height;
	}

	/* optech2: a new monster from a definition drops as its base */
	if (def && def->base[0])
	{
		return M_NavDropHeightOf(def->base);
	}

	return M_NavDropHeightOf(ent->classname);
}

static int
M_NavJumper(const edict_t *ent)
{
	if (!ent->classname || !ent->monsterinfo.blocked)
	{
		return -1;
	}

	for (int i = 0; i < (int)(sizeof(nav_jumpers) / sizeof(nav_jumpers[0])); i++)
	{
		if (!Q_stricmp(ent->classname, nav_jumpers[i].classname))
		{
			return i;
		}
	}

	return -1;
}

static qboolean
M_NavStep(edict_t *ent, const edict_t *goal, float dist)
{
	vec3_t to_point;

	if (!gi_optech2 || !gi_optech2->GetPathToGoalSized || !g_monster_nav || !g_monster_nav->value ||
		(ent->flags & (FL_FLY | FL_SWIM)) || (!ent->groundentity && !M_NavClimbing(ent)))
	{
		return false;
	}

	/* the corner is reached: the next one */
	if (ent->monsterinfo.nav_has_point)
	{
		VectorSubtract(ent->monsterinfo.nav_move_point, ent->s.origin, to_point);
		to_point[2] = 0;

		/* (a lift's far end may be right above: reached at its height) */
		if ((VectorLength(to_point) <= dist + 8) &&
			((ent->monsterinfo.nav_link_type != OPTECH2_PATH_LINK_ELEVATOR) ||
				(fabsf(ent->monsterinfo.nav_move_point[2] - (ent->s.origin[2] + ent->mins[2])) <= STEPSIZE)))
		{
			ent->monsterinfo.nav_retry_time = 0;
		}
	}

	if (ent->monsterinfo.nav_retry_time <= level.time)
	{
		optech2_path_request_t request;
		optech2_path_info_t info;

		memset(&request, 0, sizeof(request));
		VectorCopy(ent->s.origin, request.start);
		request.start[2] += ent->mins[2]; /* the feet */
		VectorCopy(goal->s.origin, request.goal);
		request.goal[2] += goal->mins[2];
		request.path_flags = OPTECH2_PATH_WALK | OPTECH2_PATH_ELEVATOR | OPTECH2_PATH_TELEPORT;
		request.drop_height = (float)M_NavDropHeight(ent);

		if (request.drop_height > 0)
		{
			request.path_flags |= OPTECH2_PATH_WALK_OFF_LEDGE;
		}

		/* player-sized monsters climb ladders */
		if (Q_max(Q_max(-ent->mins[0], ent->maxs[0]), Q_max(-ent->mins[1], ent->maxs[1])) <= 16.5f &&
			ent->maxs[2] - ent->mins[2] <= 56.5f)
		{
			request.path_flags |= OPTECH2_PATH_LADDER;
		}

		{
			const int jumper = M_NavJumper(ent);

			if (jumper >= 0)
			{
				request.path_flags |= OPTECH2_PATH_BARRIER_JUMP | OPTECH2_PATH_LONG_JUMP;
				request.jump_height = nav_jumpers[jumper].height;
				request.leap_distance = nav_jumpers[jumper].leap;
			}
		}

		request.move_dist = dist;

		/* g_monster_nav 3: draw the path until the next one (single player) */
		if (g_monster_nav->value >= 3)
		{
			request.draw_time = 0.3f;
		}

		/* the mesh for its size: half its width, its height */
		gi_optech2->GetPathToGoalSized(&request, &info, Q_max(Q_max(-ent->mins[0], ent->maxs[0]), Q_max(-ent->mins[1], ent->maxs[1])), ent->maxs[2] - ent->mins[2]);
		ent->monsterinfo.nav_has_point = (info.return_code == OPTECH2_PATH_IN_PROGRESS);
		ent->monsterinfo.nav_link_type = info.link_type;

		if (ent->monsterinfo.nav_has_point)
		{
			VectorCopy(info.first_move_point, ent->monsterinfo.nav_move_point);
			ent->monsterinfo.nav_retry_time = level.time + 0.25f + (ent - g_edicts) % 5 * 0.02f;

			/* walking off a ledge: past the mesh's edge, where a new path
			   would turn it back; it asks again once it falls */
			if (info.link_type == OPTECH2_PATH_LINK_WALK_OFF_LEDGE)
			{
				ent->monsterinfo.nav_retry_time = level.time + 2.0f;
			}
		}
		else
		{
			ent->monsterinfo.nav_retry_time = level.time + 1.0f; /* no path: stock chase for a while */
		}
	}

	if (!ent->monsterinfo.nav_has_point)
	{
		return false;
	}

	if (ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_ELEVATOR)
	{
		return M_NavLift(ent, dist);
	}

	/* the next corner is where a lift rests, and it's up (an open shaft):
	   wait at the edge for it */
	if (M_NavLiftAway(ent->monsterinfo.nav_move_point))
	{
		return M_NavLiftWait(ent);
	}

	ent->monsterinfo.nav_lift_wait_end = 0;
	VectorSubtract(ent->monsterinfo.nav_move_point, ent->s.origin, to_point);

	/* a ladder up: climb it */
	if ((ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_LADDER) && M_NavClimb(ent))
	{
		return true;
	}

	/* at a barrier the path jumps or a gap it leaps (its takeoff, back from
	   the edge): face across it and jump right away (the monster's own
	   jump, through its blocked callback; a leap gets the speed that lands
	   it). Walking into a barrier first would stop the jump's forward speed
	   against it. */
	if (((ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_BARRIER_JUMP) ||
			(ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_LONG_JUMP)) &&
		ent->monsterinfo.blocked)
	{
		const qboolean leap = (ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_LONG_JUMP);
		const float since_jump = level.time - ent->monsterinfo.nav_jump_time;
		vec3_t to_landing;

		VectorSubtract(ent->monsterinfo.nav_move_point, ent->s.origin, to_landing);
		to_landing[2] = 0;

		/* landed there (the old path still names the jump): it made it */
		if (ent->monsterinfo.nav_jump_time > 0 && since_jump >= 1.0f &&
			ent->s.origin[2] + ent->mins[2] >= ent->monsterinfo.nav_move_point[2] - STEPSIZE &&
			VectorLength(to_landing) < 96.0f)
		{
			ent->monsterinfo.nav_jump_time = 0;
			ent->monsterinfo.nav_retry_time = 0;
			return false;
		}

		/* just jumped: keep asking for the jump until it's in the air (its
		   jump animation only takes over on a later frame) */
		if (ent->monsterinfo.nav_jump_time > 0 && since_jump < 1.0f)
		{
			ent->monsterinfo.blocked(ent, dist);
			return true;
		}

		/* back at the same jump soon after jumping: it didn't make it (a
		   blocked landing, another monster): the stock chase for a while */
		if (ent->monsterinfo.nav_jump_time > 0 && since_jump < 4.0f)
		{
			if (g_monster_nav->value >= 2)
			{
				gi.dprintf("%s gives up a jump at %.0f %.0f %.0f\n", ent->classname, ent->s.origin[0],
					ent->s.origin[1], ent->s.origin[2]);
			}

			ent->monsterinfo.nav_jump_time = 0;
			ent->monsterinfo.nav_has_point = false;
			ent->monsterinfo.nav_retry_time = level.time + 5.0f;
			return false;
		}

		ent->ideal_yaw = vectoyaw(to_point);

		if (!FacingIdeal(ent))
		{
			M_ChangeYaw(ent);
			return true;
		}

		/* (facing is within 45 degrees: square up, the jump goes where it faces) */
		ent->s.angles[YAW] = ent->ideal_yaw;

		/* (a leap: the jump's takeoff frame gets the speed to land there) */
		ent->monsterinfo.nav_leaping = leap;
		VectorCopy(ent->monsterinfo.nav_move_point, ent->monsterinfo.nav_leap_target);

		if (ent->monsterinfo.blocked(ent, dist))
		{
			if (g_monster_nav->value >= 2)
			{
				gi.dprintf("%s %s at %.0f %.0f %.0f\n", ent->classname, leap ? "leaps" : "jumps",
					ent->s.origin[0], ent->s.origin[1], ent->s.origin[2]);
			}

			ent->monsterinfo.nav_jump_time = level.time;
			ent->monsterinfo.nav_retry_time = level.time + 1.5f; /* in the air: ask again once landed */
			return true;
		}

		ent->monsterinfo.nav_leaping = false;
	}

	if (!SV_StepDirection(ent, vectoyaw(to_point), dist))
	{
		/* at a ledge the path drops from (or a ladder it goes down): step off it */
		if (((ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_WALK_OFF_LEDGE) ||
				(ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_LADDER)) &&
			M_NavWalkOff(ent, to_point, dist))
		{
			return true;
		}

		ent->monsterinfo.nav_retry_time = 0; /* blocked (another monster?): ask again */
		return false;
	}

	return true;
}

void
M_MoveToGoal(edict_t *ent, float dist)
{
	edict_t *goal;
	qboolean randomBump; /* optech2 */

	if (!ent)
	{
		return;
	}

	goal = ent->goalentity;

	/* (optech2: or climbing a path's ladder) */
	if (!ent->groundentity && !(ent->flags & (FL_FLY | FL_SWIM)) && !M_NavClimbing(ent))
	{
		return;
	}

	/* if the next step hits the enemy, return immediately
	   (optech2: dist is a share of the 0.1 s step, check against the whole step) */
	if (ent->enemy && SV_CloseEnough(ent, ent->enemy, dist * FRAMETIME / level.ticktime))
	{
		return;
	}

	/* optech2 (from the re-release): with a clear line to the goal, step
	   straight at it instead of following the 8-way chase directions
	   (the zig-zag). If that step fails, use the stock chase for 5 s. */
	if (goal && ((ent->monsterinfo.bad_move_time <= level.time) || (ent->monsterinfo.aiflags & AI_CHARGING)) &&
		!(ent->monsterinfo.nav_has_point && ((ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_ELEVATOR) ||
												(ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_LADDER) ||
												(ent->monsterinfo.nav_link_type == OPTECH2_PATH_LINK_TELEPORT))))
	{
		trace_t straightTrace;

		if (!FacingIdeal(ent))
		{
			M_ChangeYaw(ent);
			return;
		}

		straightTrace = gi.trace(ent->s.origin, NULL, NULL, goal->s.origin, ent, MASK_MONSTERSOLID);

		if ((straightTrace.fraction == 1.0f) || (straightTrace.ent == goal))
		{
			vec3_t toGoal;

			VectorSubtract(goal->s.origin, ent->s.origin, toGoal);

			if (SV_StepDirection(ent, vectoyaw(toGoal), dist))
			{
				return;
			}
		}

		/* no straight step: don't try for a while, unless going to a path corner */
		if (goal->classname && strcmp(goal->classname, "path_corner") &&
			strcmp(goal->classname, "point_combat"))
		{
			ent->monsterinfo.bad_move_time = level.time + 5;
			ent->monsterinfo.aiflags &= ~AI_CHARGING;
		}
	}

	/* optech2: no straight line to the goal: follow the navigation mesh */
	if (goal && M_NavStep(ent, goal, dist))
	{
		return;
	}

	/* optech2: roll for a random direction change at most once per 0.1 s,
	   as stock did once per frame */
	randomBump = false;

	if (ent->monsterinfo.random_change_time <= level.time + 0.001f)
	{
		randomBump = ((randk() & 3) == 1);
		ent->monsterinfo.random_change_time = level.time + FRAMETIME;
	}

	/* bump around... */
	if ((randomBump &&
			!(ent->monsterinfo.aiflags & AI_CHARGING)) ||
		!SV_StepDirection(ent, ent->ideal_yaw, dist))
	{
		if (ent->monsterinfo.aiflags & AI_BLOCKED)
		{
			ent->monsterinfo.aiflags &= ~AI_BLOCKED;
			return;
		}

		if (ent->inuse)
		{
			SV_NewChaseDir(ent, goal, dist);
		}
	}
}

/* optech2: re-release ai_check_move: could the monster step dist units
   straight ahead (no wall, no ledge)? Doesn't move it */
qboolean
ai_check_move(edict_t *self, float dist)
{
	vec3_t move, old_origin;
	float yaw;
	qboolean possible;

	if (!self)
	{
		return false;
	}

	yaw = self->s.angles[YAW] * M_PI * 2 / 360;
	VectorSet(move, cos(yaw) * dist, sin(yaw) * dist, 0);
	VectorCopy(self->s.origin, old_origin);

	possible = SV_movestep(self, move, false);

	VectorCopy(old_origin, self->s.origin);
	gi.linkentity(self);
	return possible;
}

qboolean
M_walkmove(edict_t *ent, float yaw, float dist)
{
	vec3_t move;
	qboolean retval;

	if (!ent)
	{
		return false;
	}

	if (!ent->groundentity && !(ent->flags & (FL_FLY | FL_SWIM)))
	{
		return false;
	}

	yaw = yaw * M_PI * 2 / 360;

	move[0] = cos(yaw) * dist;
	move[1] = sin(yaw) * dist;
	move[2] = 0;

	retval = SV_movestep(ent, move, true);
	ent->monsterinfo.aiflags &= ~AI_BLOCKED;
	return retval;
}
