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
 * OpenTech2 test mod: the Test Gun's beam, drawn on the client through the
 * game library's effects (src/game/cgame_fx): a purple and green double
 * helix from the gun to the hit point, a light and a burst at the impact.
 *
 * =======================================================================
 */

#include "../cgame_fx/fx_local.h"

#define TE_TESTGUN_BEAM 201 /* as testmod.h (server side) */

static void
TestGun_Beam(const vec3_t start, const vec3_t end)
{
	vec3_t dir, right, up;
	float length, along;
	optech2_dlight_t *light;
	int i;

	VectorSubtract(end, start, dir);
	length = VectorNormalize(dir);
	PerpendicularVector(right, dir);
	CrossProduct(dir, right, up);

	/* two strands, a full turn every 64 units */
	for (along = 0; along < length; along += 3)
	{
		for (i = 0; i < 2; i++)
		{
			cparticle_t *p = CL_AllocParticle();
			float angle = along * (2 * M_PI / 64) + i * M_PI;
			float c = cosf(angle) * 4, s = sinf(angle) * 4;

			if (!p)
			{
				return;
			}

			p->time = cl.time;
			p->color = i ? 0xff40ff40 : 0xffff40c0; /* green, purple */
			VectorMA(start, along, dir, p->org);
			VectorMA(p->org, c, right, p->org);
			VectorMA(p->org, s, up, p->org);
			VectorScale(right, c * 2, p->vel);
			VectorMA(p->vel, s * 2, up, p->vel);
			VectorClear(p->accel);
			p->alpha = 1.0f;
			p->alphavel = -1.0f / (0.6f + frandk() * 0.2f);
		}
	}

	/* a burst at the impact */
	for (i = 0; i < 40; i++)
	{
		cparticle_t *p = CL_AllocParticle();

		if (!p)
		{
			break;
		}

		p->time = cl.time;
		p->color = (i & 1) ? 0xff40ff40 : 0xffff40c0;
		VectorCopy(end, p->org);
		p->vel[0] = crandk() * 80;
		p->vel[1] = crandk() * 80;
		p->vel[2] = crandk() * 80;
		VectorSet(p->accel, 0, 0, -PARTICLE_GRAVITY);
		p->alpha = 1.0f;
		p->alphavel = -1.5f;
	}

	light = CL_AllocDlight(0);
	VectorCopy(end, light->origin);
	light->radius = 250;
	VectorSet(light->color, 0.8f, 0.3f, 1.0f);
	light->die = cl.time + 300;
	light->decay = 600;

	S_StartSound(end, 0, 0, S_RegisterSound("weapons/railgf1a.wav"), 0.6f, ATTN_NORM, 0);
}

/* the mod's own temp entity types; false for the others */
qboolean
TestMod_ParseTempEntity(int type)
{
	vec3_t start, end;

	switch (type)
	{
		case TE_TESTGUN_BEAM:
			MSG_ReadPos(&net_message, start, cls.serverProtocol);
			MSG_ReadPos(&net_message, end, cls.serverProtocol);
			TestGun_Beam(start, end);
			fxi->Printf("testmod: beam drawn from %.0f %.0f %.0f to %.0f %.0f %.0f\n",
				start[0], start[1], start[2], end[0], end[1], end[2]);
			return true;
	}

	return false;
}
