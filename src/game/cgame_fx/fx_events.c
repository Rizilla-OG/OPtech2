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
 * This file implements all specialized client side effects.  E.g.
 * weapon effects, enemy effects, flash, etc.
 *
 * =======================================================================
 */

#include "fx_local.h"

void
CL_ItemRespawnParticles(const vec3_t org)
{
	int i, j;
	cparticle_t *p;
	float time;

	time = (float)cl.time;

	for (i = 0; i < 64; i++)
	{
		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = CL_CombineColors(0xff2fa75f, 0xffffffff,
			(float)(randk() & 15) / 15.0);
		p->org[0] = org[0] + crandk() * 8;
		p->org[1] = org[1] + crandk() * 8;
		p->org[2] = org[2] + crandk() * 8;

		for (j = 0; j < 3; j++)
		{
			p->vel[j] = crandk() * 8;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY * 0.2;
		p->alpha = 1.0;

		p->alphavel = -1.0f / (1.0f + frandk() * 0.3f);
	}
}

/*
 * optech2: an entity event (was the engine's CL_EntityEvent): item respawn,
 * teleport, footsteps, falling. True when handled.
 */
qboolean
CL_EntityEvent(int number, int event, const vec3_t origin)
{
	switch (event)
	{
		case EV_ITEM_RESPAWN:
			S_StartSound(NULL, number, CHAN_WEAPON,
				S_RegisterSound("items/respawn1.wav"), 1, ATTN_IDLE, 0);
			CL_ItemRespawnParticles(origin);
			break;
		case EV_PLAYER_TELEPORT:
			S_StartSound(NULL, number, CHAN_WEAPON,
				S_RegisterSound("misc/tele1.wav"), 1, ATTN_IDLE, 0);
			CL_TeleportParticles(origin);
			break;
		case EV_FOOTSTEP:

			if (fxi->CvarValue("cl_footsteps"))
			{
				vec3_t mins = {0, 0, 0}, maxs = {0, 0, 0}, dir = {0, 0, -1000000};
				struct sfx_s *sfx = NULL;
				trace_t trace;
				trace = fxi->Trace(origin, mins, maxs, dir, MASK_DEADSOLID);

				/* ladder does not have separate material */
				if ((trace.contents & CONTENTS_LADDER))
				{
					char name[MAX_QPATH];
					int step;

					/* material has sometime 5 steps versions */
					step = randk() % 5;

					Com_sprintf(name, sizeof(name), "player/ladder%i.wav",
						step + 1);

					sfx = S_RegisterSound(name);
				}

				/* step sound based onb material */
				if (!sfx && trace.surface->material[0])
				{
					char name[MAX_QPATH];
					int step;

					/* material has sometime 5 steps versions */
					step = randk() % 5;

					Com_sprintf(name, sizeof(name), "player/%s%i.wav",
						trace.surface->material, step + 1);

					sfx = S_RegisterSound(name);
				}

				/* no material steps sound found */
				if (!sfx)
				{
					sfx = CL_RandomFootstepSfx();
				}

				S_StartSound(NULL, number, CHAN_BODY,
					sfx, 1, ATTN_NORM, 0);
			}

			break;
		case EV_FALLSHORT:
			S_StartSound(NULL, number, CHAN_AUTO,
				S_RegisterSound("player/land1.wav"), 1, ATTN_NORM, 0);
			break;
		case EV_FALL:
			S_StartSound(NULL, number, CHAN_AUTO,
				S_RegisterSound("*fall2.wav"), 1, ATTN_NORM, 0);
			break;
		case EV_FALLFAR:
			S_StartSound(NULL, number, CHAN_AUTO,
				S_RegisterSound("*fall1.wav"), 1, ATTN_NORM, 0);
			break;
		default:
			return false; /* optech2: not ours, the engine decides */
	}

	return true;
}
