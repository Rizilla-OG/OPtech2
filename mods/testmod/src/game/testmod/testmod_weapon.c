/*
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
 * =======================================================================
 *
 * OpenTech2 test mod: the Test Gun, a hitscan weapon (blaster models, no
 * ammo, 20 damage). Each shot sends its own temporary entity type,
 * TE_TESTGUN_BEAM, which the mod's client effect code draws
 * (testmod_fx.c). Nothing in the engine knows this weapon or its effect.
 *
 * =======================================================================
 */

#include "../header/local.h"
#include "testmod.h"

#define TESTGUN_DAMAGE 20
#define TESTGUN_RANGE 8192

static void
Weapon_TestGun_Fire(edict_t *ent)
{
	vec3_t forward, right, start, end, offset;
	trace_t trace;

	if (!ent || !ent->client)
	{
		return;
	}

	AngleVectors(ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 24, 8, ent->viewheight - 8);
	P_ProjectSource(ent, offset, forward, right, start);
	VectorMA(start, TESTGUN_RANGE, forward, end);

	trace = gi.trace(start, NULL, NULL, end, ent, MASK_SHOT);

	if (trace.ent && trace.ent->takedamage)
	{
		T_Damage(trace.ent, ent, ent, forward, trace.endpos, trace.plane.normal,
			TESTGUN_DAMAGE, 0, DAMAGE_ENERGY, MOD_BLASTER);
	}

	/* the beam: from the gun to where the shot hit */
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TESTGUN_BEAM);
	gi.WritePosition(start);
	gi.WritePosition(trace.endpos);
	gi.multicast(start, MULTICAST_PHS);

	VectorScale(forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	PlayerNoise(ent, start, PNOISE_WEAPON);
	ent->client->ps.gunframe++;
}

void
Weapon_TestGun(edict_t *ent)
{
	/* the blaster's frames: activate 0-4, fire 5-8, idle 9-52, put away 53-55 */
	static const int pause_frames[] = {19, 32, 0};
	static const int fire_frames[] = {5, 0};

	Weapon_Generic(ent, 4, 8, 52, 55, pause_frames, fire_frames, Weapon_TestGun_Fire);
}
