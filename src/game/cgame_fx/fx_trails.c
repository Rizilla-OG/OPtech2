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

/* optech2: shared by the particle trails below (was in cl_effects.c) */
static vec3_t avelocities[NUMVERTEXNORMALS];

/* the engine's direction table (src/common/movemsg.c) */
static vec3_t bytedirs[NUMVERTEXNORMALS] = {
	{-0.525731, 0.000000, 0.850651},
	{-0.442863, 0.238856, 0.864188},
	{-0.295242, 0.000000, 0.955423},
	{-0.309017, 0.500000, 0.809017},
	{-0.162460, 0.262866, 0.951056},
	{0.000000, 0.000000, 1.000000},
	{0.000000, 0.850651, 0.525731},
	{-0.147621, 0.716567, 0.681718},
	{0.147621, 0.716567, 0.681718},
	{0.000000, 0.525731, 0.850651},
	{0.309017, 0.500000, 0.809017},
	{0.525731, 0.000000, 0.850651},
	{0.295242, 0.000000, 0.955423},
	{0.442863, 0.238856, 0.864188},
	{0.162460, 0.262866, 0.951056},
	{-0.681718, 0.147621, 0.716567},
	{-0.809017, 0.309017, 0.500000},
	{-0.587785, 0.425325, 0.688191},
	{-0.850651, 0.525731, 0.000000},
	{-0.864188, 0.442863, 0.238856},
	{-0.716567, 0.681718, 0.147621},
	{-0.688191, 0.587785, 0.425325},
	{-0.500000, 0.809017, 0.309017},
	{-0.238856, 0.864188, 0.442863},
	{-0.425325, 0.688191, 0.587785},
	{-0.716567, 0.681718, -0.147621},
	{-0.500000, 0.809017, -0.309017},
	{-0.525731, 0.850651, 0.000000},
	{0.000000, 0.850651, -0.525731},
	{-0.238856, 0.864188, -0.442863},
	{0.000000, 0.955423, -0.295242},
	{-0.262866, 0.951056, -0.162460},
	{0.000000, 1.000000, 0.000000},
	{0.000000, 0.955423, 0.295242},
	{-0.262866, 0.951056, 0.162460},
	{0.238856, 0.864188, 0.442863},
	{0.262866, 0.951056, 0.162460},
	{0.500000, 0.809017, 0.309017},
	{0.238856, 0.864188, -0.442863},
	{0.262866, 0.951056, -0.162460},
	{0.500000, 0.809017, -0.309017},
	{0.850651, 0.525731, 0.000000},
	{0.716567, 0.681718, 0.147621},
	{0.716567, 0.681718, -0.147621},
	{0.525731, 0.850651, 0.000000},
	{0.425325, 0.688191, 0.587785},
	{0.864188, 0.442863, 0.238856},
	{0.688191, 0.587785, 0.425325},
	{0.809017, 0.309017, 0.500000},
	{0.681718, 0.147621, 0.716567},
	{0.587785, 0.425325, 0.688191},
	{0.955423, 0.295242, 0.000000},
	{1.000000, 0.000000, 0.000000},
	{0.951056, 0.162460, 0.262866},
	{0.850651, -0.525731, 0.000000},
	{0.955423, -0.295242, 0.000000},
	{0.864188, -0.442863, 0.238856},
	{0.951056, -0.162460, 0.262866},
	{0.809017, -0.309017, 0.500000},
	{0.681718, -0.147621, 0.716567},
	{0.850651, 0.000000, 0.525731},
	{0.864188, 0.442863, -0.238856},
	{0.809017, 0.309017, -0.500000},
	{0.951056, 0.162460, -0.262866},
	{0.525731, 0.000000, -0.850651},
	{0.681718, 0.147621, -0.716567},
	{0.681718, -0.147621, -0.716567},
	{0.850651, 0.000000, -0.525731},
	{0.809017, -0.309017, -0.500000},
	{0.864188, -0.442863, -0.238856},
	{0.951056, -0.162460, -0.262866},
	{0.147621, 0.716567, -0.681718},
	{0.309017, 0.500000, -0.809017},
	{0.425325, 0.688191, -0.587785},
	{0.442863, 0.238856, -0.864188},
	{0.587785, 0.425325, -0.688191},
	{0.688191, 0.587785, -0.425325},
	{-0.147621, 0.716567, -0.681718},
	{-0.309017, 0.500000, -0.809017},
	{0.000000, 0.525731, -0.850651},
	{-0.525731, 0.000000, -0.850651},
	{-0.442863, 0.238856, -0.864188},
	{-0.295242, 0.000000, -0.955423},
	{-0.162460, 0.262866, -0.951056},
	{0.000000, 0.000000, -1.000000},
	{0.295242, 0.000000, -0.955423},
	{0.162460, 0.262866, -0.951056},
	{-0.442863, -0.238856, -0.864188},
	{-0.309017, -0.500000, -0.809017},
	{-0.162460, -0.262866, -0.951056},
	{0.000000, -0.850651, -0.525731},
	{-0.147621, -0.716567, -0.681718},
	{0.147621, -0.716567, -0.681718},
	{0.000000, -0.525731, -0.850651},
	{0.309017, -0.500000, -0.809017},
	{0.442863, -0.238856, -0.864188},
	{0.162460, -0.262866, -0.951056},
	{0.238856, -0.864188, -0.442863},
	{0.500000, -0.809017, -0.309017},
	{0.425325, -0.688191, -0.587785},
	{0.716567, -0.681718, -0.147621},
	{0.688191, -0.587785, -0.425325},
	{0.587785, -0.425325, -0.688191},
	{0.000000, -0.955423, -0.295242},
	{0.000000, -1.000000, 0.000000},
	{0.262866, -0.951056, -0.162460},
	{0.000000, -0.850651, 0.525731},
	{0.000000, -0.955423, 0.295242},
	{0.238856, -0.864188, 0.442863},
	{0.262866, -0.951056, 0.162460},
	{0.500000, -0.809017, 0.309017},
	{0.716567, -0.681718, 0.147621},
	{0.525731, -0.850651, 0.000000},
	{-0.238856, -0.864188, -0.442863},
	{-0.500000, -0.809017, -0.309017},
	{-0.262866, -0.951056, -0.162460},
	{-0.850651, -0.525731, 0.000000},
	{-0.716567, -0.681718, -0.147621},
	{-0.716567, -0.681718, 0.147621},
	{-0.525731, -0.850651, 0.000000},
	{-0.500000, -0.809017, 0.309017},
	{-0.238856, -0.864188, 0.442863},
	{-0.262866, -0.951056, 0.162460},
	{-0.864188, -0.442863, 0.238856},
	{-0.809017, -0.309017, 0.500000},
	{-0.688191, -0.587785, 0.425325},
	{-0.681718, -0.147621, 0.716567},
	{-0.442863, -0.238856, 0.864188},
	{-0.587785, -0.425325, 0.688191},
	{-0.309017, -0.500000, 0.809017},
	{-0.147621, -0.716567, 0.681718},
	{-0.425325, -0.688191, 0.587785},
	{-0.162460, -0.262866, 0.951056},
	{0.442863, -0.238856, 0.864188},
	{0.162460, -0.262866, 0.951056},
	{0.309017, -0.500000, 0.809017},
	{0.147621, -0.716567, 0.681718},
	{0.000000, -0.525731, 0.850651},
	{0.425325, -0.688191, 0.587785},
	{0.587785, -0.425325, 0.688191},
	{0.688191, -0.587785, 0.425325},
	{-0.955423, 0.295242, 0.000000},
	{-0.951056, 0.162460, 0.262866},
	{-1.000000, 0.000000, 0.000000},
	{-0.850651, 0.000000, 0.525731},
	{-0.955423, -0.295242, 0.000000},
	{-0.951056, -0.162460, 0.262866},
	{-0.864188, 0.442863, -0.238856},
	{-0.951056, 0.162460, -0.262866},
	{-0.809017, 0.309017, -0.500000},
	{-0.864188, -0.442863, -0.238856},
	{-0.951056, -0.162460, -0.262866},
	{-0.809017, -0.309017, -0.500000},
	{-0.681718, 0.147621, -0.716567},
	{-0.681718, -0.147621, -0.716567},
	{-0.850651, 0.000000, -0.525731},
	{-0.688191, 0.587785, -0.425325},
	{-0.587785, 0.425325, -0.688191},
	{-0.425325, 0.688191, -0.587785},
	{-0.425325, -0.688191, -0.587785},
	{-0.587785, -0.425325, -0.688191},
	{-0.688191, -0.587785, -0.425325}};

void
CL_BlasterTrail(const vec3_t start, const vec3_t end)
{
	vec3_t move;
	vec3_t vec;
	int len;
	int j;
	cparticle_t *p;
	int dec;
	float time;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = (int)VectorNormalize(vec);

	dec = 5;
	VectorScale(vec, 5, vec);

	while (len > 0)
	{
		len -= dec;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = time;

		p->alpha = 1.0;
		p->alphavel = -1.0f / (0.3f + frandk() * 0.2f);
		p->color = 0xff07abff;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + crandk();
			p->vel[j] = crandk() * 5;
			p->accel[j] = 0;
		}

		VectorAdd(move, vec, move);
	}
}

void
CL_FlagTrail(const vec3_t start, const vec3_t end, int color)
{
	vec3_t move;
	vec3_t vec;
	int len;
	int j;
	cparticle_t *p;
	int dec;
	float time;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = (int)VectorNormalize(vec);

	dec = 5;
	VectorScale(vec, 5, vec);

	while (len > 0)
	{
		len -= dec;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = time;

		p->alpha = 1.0;
		p->alphavel = -1.0f / (0.8f + frandk() * 0.2f);
		p->color = color;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + crandk() * 16;
			p->vel[j] = crandk() * 5;
			p->accel[j] = 0;
		}

		VectorAdd(move, vec, move);
	}
}

void
CL_DiminishingTrail(const vec3_t start, const vec3_t end, centity_t *old, int flags)
{
	vec3_t move;
	vec3_t vec;
	int len;
	int j;
	cparticle_t *p;
	float dec;
	float orgscale;
	float velscale;
	float time;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	dec = 0.5;
	VectorScale(vec, dec, vec);

	if (old->trailcount > 900)
	{
		orgscale = 4;
		velscale = 15;
	}
	else if (old->trailcount > 800)
	{
		orgscale = 2;
		velscale = 10;
	}
	else
	{
		orgscale = 1;
		velscale = 5;
	}

	while (len > 0)
	{
		len -= dec;

		/* drop less particles as it flies */
		if ((randk() & 1023) < old->trailcount)
		{
			p = CL_AllocParticle(); /* optech2: the engine's particle list */
			if (!p)
			{
				return;
			}
			VectorClear(p->accel);

			p->time = time;

			if (flags & EF_GIB)
			{
				p->alpha = 1.0;
				p->alphavel = -1.0f / (1 + frandk() * 0.4f);
				p->color = CL_CombineColors(0xff001f9b, 0xff00001b,
					(float)(randk() & 15) / 15.0);

				for (j = 0; j < 3; j++)
				{
					p->org[j] = move[j] + crandk() * orgscale;
					p->vel[j] = crandk() * velscale;
					p->accel[j] = 0;
				}

				p->vel[2] -= PARTICLE_GRAVITY;
			}
			else if (flags & EF_GREENGIB)
			{
				p->alpha = 1.0;
				p->alphavel = -1.0f / (1 + frandk() * 0.4f);
				p->color = CL_CombineColors(0xff53ffff, 0xff007fef,
					(float)(randk() & 15) / 15.0);

				for (j = 0; j < 3; j++)
				{
					p->org[j] = move[j] + crandk() * orgscale;
					p->vel[j] = crandk() * velscale;
					p->accel[j] = 0;
				}

				p->vel[2] -= PARTICLE_GRAVITY;
			}
			else
			{
				p->alpha = 1.0;
				p->alphavel = -1.0f / (1 + frandk() * 0.2f);
				p->color = CL_CombineColors(0xff3f3f3f, 0xffababab,
					(float)(randk() & 15) / 15.0);

				for (j = 0; j < 3; j++)
				{
					p->org[j] = move[j] + crandk() * orgscale;
					p->vel[j] = crandk() * velscale;
				}

				p->accel[2] = 20;
			}
		}

		old->trailcount -= 5;

		if (old->trailcount < 100)
		{
			old->trailcount = 100;
		}

		VectorAdd(move, vec, move);
	}
}

void
CL_RocketTrail(const vec3_t start, const vec3_t end, centity_t *old)
{
	vec3_t move;
	vec3_t vec;
	int len;
	int j;
	cparticle_t *p;
	int dec;
	float time;

	time = (float)cl.time;

	/* smoke */
	CL_DiminishingTrail(start, end, old, EF_ROCKET);

	/* fire */
	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = (int)VectorNormalize(vec);

	dec = 1;
	VectorScale(vec, dec, vec);

	while (len > 0)
	{
		len -= dec;

		if ((randk() & 7) == 0)
		{
			p = CL_AllocParticle(); /* optech2: the engine's particle list */
			if (!p)
			{
				return;
			}

			VectorClear(p->accel);
			p->time = time;

			p->alpha = 1.0;
			p->alphavel = -1.0f / (1 + frandk() * 0.2f);
			p->color = CL_CombineColors(0xff27ffff, 0xff0fbfff,
				(float)(randk() & 15) / 15.0);

			for (j = 0; j < 3; j++)
			{
				p->org[j] = move[j] + crandk() * 5;
				p->vel[j] = crandk() * 20;
			}

			p->accel[2] = -PARTICLE_GRAVITY;
		}

		VectorAdd(move, vec, move);
	}
}

void
CL_IonripperTrail(const vec3_t start, const vec3_t end)
{
	vec3_t move;
	vec3_t vec;
	int len;
	int j;
	cparticle_t *p;
	int dec;
	int left = 0;
	float time;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = (int)VectorNormalize(vec);

	dec = 5;
	VectorScale(vec, 5, vec);

	while (len > 0)
	{
		len -= dec;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = time;
		p->alpha = 0.5;
		p->alphavel = -1.0f / (0.3f + frandk() * 0.2f);
		p->color = CL_CombineColors(0xff0057d3, 0xff002bab,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j];
			p->accel[j] = 0;
		}

		if (left)
		{
			left = 0;
			p->vel[0] = 10;
		}
		else
		{
			left = 1;
			p->vel[0] = -10;
		}

		p->vel[1] = 0;
		p->vel[2] = 0;

		VectorAdd(move, vec, move);
	}
}

static void
CL_FlyParticles(const vec3_t origin, int count)
{
	int i;
	float ltime;
	float time;

	time = (float)cl.time;

	if (count > NUMVERTEXNORMALS)
	{
		count = NUMVERTEXNORMALS;
	}

	if (!avelocities[0][0])
	{
		for (i = 0; i < NUMVERTEXNORMALS; i++)
		{
			avelocities[i][0] = (randk() & 255) * 0.01f;
			avelocities[i][1] = (randk() & 255) * 0.01f;
			avelocities[i][2] = (randk() & 255) * 0.01f;
		}
	}

	ltime = time / 1000.0f;

	for (i = 0; i < count; i += 2)
	{
		float sp, sy, cp, cy;
		vec3_t forward;
		cparticle_t *p;
		float angle;
		float dist;

		angle = ltime * avelocities[i][0];
		sy = (float)sin(angle);
		cy = (float)cos(angle);
		angle = ltime * avelocities[i][1];
		sp = (float)sin(angle);
		cp = (float)cos(angle);

		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;

		dist = (float)sin(ltime + i) * 64;
		p->org[0] = origin[0] + bytedirs[i][0] * dist + forward[0] * 16;
		p->org[1] = origin[1] + bytedirs[i][1] * dist + forward[1] * 16;
		p->org[2] = origin[2] + bytedirs[i][2] * dist + forward[2] * 16;

		VectorClear(p->vel);
		VectorClear(p->accel);

		p->color = 0xff000000; /* black */
		p->alpha = 1;
		p->alphavel = -100;
	}
}

void
CL_FlyEffect(centity_t *ent, const vec3_t origin)
{
	int n;
	int count;
	int starttime;

	if (ent->fly_stoptime < cl.time)
	{
		starttime = cl.time;
		ent->fly_stoptime = cl.time + 60000;
	}
	else
	{
		starttime = ent->fly_stoptime - 60000;
	}

	n = cl.time - starttime;

	if (n < 20000)
	{
		count = (int)n * 162 / 20000.0;
	}

	else
	{
		n = ent->fly_stoptime - cl.time;

		if (n < 20000)
		{
			count = (int)n * 162 / 20000.0;
		}

		else
		{
			count = 162;
		}
	}

	CL_FlyParticles(origin, count);
}

void
CL_BfgParticles(const entity_t *ent)
{
	int i;
	vec3_t forward;
	vec3_t v;
	float ltime;
	float time;

	time = (float)cl.time;

	if (!avelocities[0][0])
	{
		for (i = 0; i < NUMVERTEXNORMALS; i++)
		{
			avelocities[i][0] = (randk() & 255) * 0.01f;
			avelocities[i][1] = (randk() & 255) * 0.01f;
			avelocities[i][2] = (randk() & 255) * 0.01f;
		}
	}

	ltime = time / 1000.0;

	for (i = 0; i < NUMVERTEXNORMALS; i++)
	{
		float sp, sy, cp, cy;
		float angle, dist;
		cparticle_t *p;

		angle = ltime * avelocities[i][0];
		sy = (float)sin(angle);
		cy = (float)cos(angle);
		angle = ltime * avelocities[i][1];
		sp = (float)sin(angle);
		cp = (float)cos(angle);

		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;

		dist = (float)sin(ltime + i) * 64;
		p->org[0] = ent->origin[0] + bytedirs[i][0] * dist + forward[0] * 16;
		p->org[1] = ent->origin[1] + bytedirs[i][1] * dist + forward[1] * 16;
		p->org[2] = ent->origin[2] + bytedirs[i][2] * dist + forward[2] * 16;

		VectorClear(p->vel);
		VectorClear(p->accel);

		VectorSubtract(p->org, ent->origin, v);
		dist = VectorLength(v) / 90.0f;
		p->color = CL_CombineColors(0xff00ff00, 0xffffffff, dist);
		p->alpha = 1.0f - dist;
		p->alphavel = -100;
	}
}

void
CL_TrapParticles(entity_t *ent)
{
	vec3_t move;
	vec3_t vec;
	vec3_t start, end;
	int len;
	int dec;
	float time;

	time = (float)cl.time;

	ent->origin[2] -= 14;
	VectorCopy(ent->origin, start);
	VectorCopy(ent->origin, end);
	end[2] += 64;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = (int)VectorNormalize(vec);

	dec = 5;
	VectorScale(vec, 5, vec);

	while (len > 0)
	{
		cparticle_t *p;
		int j;

		len -= dec;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = time;

		p->alpha = 1.0;
		p->alphavel = -1.0f / (0.3f + frandk() * 0.2f);
		p->color = 0xff07abff;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + crandk();
			p->vel[j] = crandk() * 15;
			p->accel[j] = 0;
		}

		p->accel[2] = PARTICLE_GRAVITY;

		VectorAdd(move, vec, move);
	}

	{
		vec3_t org;
		int i;

		ent->origin[2] += 14;
		VectorCopy(ent->origin, org);

		for (i = -2; i <= 2; i += 4)
		{
			int j;

			for (j = -2; j <= 2; j += 4)
			{
				int k;

				for (k = -2; k <= 4; k += 4)
				{
					cparticle_t *p;
					vec3_t dir;
					float vel;

					p = CL_AllocParticle(); /* optech2: the engine's particle list */
					if (!p)
					{
						return;
					}

					p->time = time;
					p->color = CL_CombineColors(0xff07abff, 0xff006be3,
						(float)(randk() & 15) / 15.0);
					p->alpha = 1.0;
					p->alphavel = -1.0f / (0.3f + (randk() & 7) * 0.02f);

					p->org[0] = org[0] + i + ((randk() & 23) * crandk());
					p->org[1] = org[1] + j + ((randk() & 23) * crandk());
					p->org[2] = org[2] + k + ((randk() & 23) * crandk());

					dir[0] = j * 8.0f;
					dir[1] = i * 8.0f;
					dir[2] = k * 8.0f;

					VectorNormalize(dir);
					vel = (float)(50 + (randk() & 63));
					VectorScale(dir, vel, p->vel);

					p->accel[0] = p->accel[1] = 0;
					p->accel[2] = -PARTICLE_GRAVITY;
				}
			}
		}
	}
}

void
CL_TrackerTrail(const vec3_t start, const vec3_t end, unsigned int color)
{
	vec3_t move;
	vec3_t vec;
	vec3_t forward, right, up, angle_dir;
	float len;
	int j;
	cparticle_t *p;
	int dec;
	float time;

	time = (float)cl.time;
	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	VectorCopy(vec, forward);
	AngleVectors2(forward, angle_dir);
	AngleVectors(angle_dir, forward, right, up);

	dec = 3;
	VectorScale(vec, 3, vec);

	while (len > 0)
	{
		float dist;

		len -= dec;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = time;

		p->alpha = 1.0;
		p->alphavel = -2.0;
		p->color = color;
		dist = DotProduct(move, forward);
		VectorMA(move, 8 * cos(dist), up, p->org);

		for (j = 0; j < 3; j++)
		{
			p->vel[j] = 0;
			p->accel[j] = 0;
		}

		p->vel[2] = 5;

		VectorAdd(move, vec, move);
	}
}

void
CL_Tracker_Shell(const vec3_t origin)
{
	vec3_t dir;
	int i;
	cparticle_t *p;
	float time;

	time = (float)cl.time;

	for (i = 0; i < 300; i++)
	{
		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = time;

		p->alpha = 1.0;
		p->alphavel = INSTANT_PARTICLE;
		p->color = 0xff000000; /* black */
		dir[0] = crandk();
		dir[1] = crandk();
		dir[2] = crandk();
		VectorNormalize(dir);

		VectorMA(origin, 40, dir, p->org);
	}
}

void
CL_TagTrail(const vec3_t start, const vec3_t end, int color)
{
	vec3_t move;
	vec3_t vec;
	float len;
	int j;
	cparticle_t *p;
	int dec;
	float time;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	dec = 5;
	VectorScale(vec, 5, vec);

	while (len >= 0)
	{
		len -= dec;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = time;

		p->alpha = 1.0;
		p->alphavel = -1.0f / (0.8f + frandk() * 0.2f);
		p->color = color;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + crandk() * 16;
			p->vel[j] = crandk() * 5;
			p->accel[j] = 0;
		}

		VectorAdd(move, vec, move);
	}
}

/*
 * Green!
 */
void
CL_BlasterTrail2(const vec3_t start, const vec3_t end)
{
	vec3_t move;
	vec3_t vec;
	float len;
	int j;
	cparticle_t *p;
	int dec;
	float time;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	dec = 5;
	VectorScale(vec, 5, vec);

	while (len > 0)
	{
		len -= dec;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = time;

		p->alpha = 1.0;
		p->alphavel = -1.0f / (float)(0.3f + frandk() * 0.2f);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + crandk();
			p->vel[j] = crandk() * 5;
			p->accel[j] = 0;
		}

		VectorAdd(move, vec, move);
	}
}

/*
 * optech2: an entity's trail and flying light (was the end of the engine's
 * CL_AddPacketEntities, now CL_EntityTrails there).
 */
qboolean
FX_EntityTrails(optech2_fx_entity_t *info)
{
	centity_t entity_state;
	centity_t *cent = &entity_state;
	entity_t entity;
	entity_t ent;
	unsigned int effects = info->effects;

	memset(&entity_state, 0, sizeof(entity_state));
	VectorCopy(info->oldorigin, cent->lerp_origin);
	cent->trailcount = info->trailcount;
	cent->fly_stoptime = info->fly_stoptime;

	memset(&entity, 0, sizeof(entity));
	VectorCopy(info->origin, entity.origin);
	VectorCopy(info->angles, entity.angles);
	entity.frame = info->frame;
	ent = entity;

	/* add automatic particle trails */
	if ((effects & ~EF_ROTATE))
	{
		if (effects & EF_ROCKET)
		{
			CL_RocketTrail(cent->lerp_origin, ent.origin, cent);

			if (fxi->CvarValue("cl_r1q2_lightstyle"))
			{
				V_AddLight(ent.origin, 200, 1, 0.23f, 0);
			}
			else
			{
				V_AddLight(ent.origin, 200, 1, 1, 0);
			}
		}

		/* Do not reorder EF_BLASTER and EF_HYPERBLASTER.
		   EF_BLASTER | EF_TRACKER is a special case for
		   EF_BLASTER2 */
		else if (effects & EF_BLASTER)
		{
			if (effects & EF_TRACKER)
			{
				CL_BlasterTrail2(cent->lerp_origin, ent.origin);
				V_AddLight(ent.origin, 200, 0, 1, 0);
			}
			else
			{
				CL_BlasterTrail(cent->lerp_origin, ent.origin);
				V_AddLight(ent.origin, 200, 1, 1, 0);
			}
		}
		else if (effects & EF_HYPERBLASTER)
		{
			if (effects & EF_TRACKER)
			{
				V_AddLight(ent.origin, 200, 0, 1, 0);
			}
			else
			{
				V_AddLight(ent.origin, 200, 1, 1, 0);
			}
		}
		else if (effects & EF_GIB)
		{
			CL_DiminishingTrail(cent->lerp_origin, ent.origin,
				cent, effects);
		}
		else if (effects & EF_GRENADE)
		{
			CL_DiminishingTrail(cent->lerp_origin, ent.origin,
				cent, effects);
		}
		else if (effects & EF_FLIES)
		{
			CL_FlyEffect(cent, ent.origin);
		}
		else if (effects & EF_BFG)
		{
			static const int bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};
			static int bfg_lightramp_max = (int)ARRLEN(bfg_lightramp);
			int i;

			if (effects & EF_ANIM_ALLFAST)
			{
				CL_BfgParticles(&ent);
				i = 200;
			}
			else
			{
				if (info->frame >= 0 && info->frame < bfg_lightramp_max)
				{
					i = bfg_lightramp[info->frame];
				}
				else
				{
					i = bfg_lightramp[0];
				}
			}

			V_AddLight(ent.origin, i, 0, 1, 0);
		}
		else if (effects & EF_TRAP)
		{
			int i;

			ent.origin[2] += 32;
			CL_TrapParticles(&ent);
			i = (randk() % 100) + 100;
			V_AddLight(ent.origin, i, 1, 0.8f, 0.1f);
		}
		else if (effects & EF_FLAG1)
		{
			CL_FlagTrail(cent->lerp_origin, ent.origin, 0xff0000ff);
			V_AddLight(ent.origin, 225, 1, 0.1f, 0.1f);
		}
		else if (effects & EF_FLAG2)
		{
			CL_FlagTrail(cent->lerp_origin, ent.origin, 0xff7f672f);
			V_AddLight(ent.origin, 225, 0.1f, 0.1f, 1);
		}
		else if (effects & EF_TAGTRAIL)
		{
			CL_TagTrail(cent->lerp_origin, ent.origin, 0xff27ffff);
			V_AddLight(ent.origin, 225, 1.0, 1.0, 0.0);
		}
		else if (effects & EF_TRACKERTRAIL)
		{
			if (effects & EF_TRACKER)
			{
				float intensity;

				intensity = 50 + (500 * ((float)sin(cl.time / 500.0f) + 1.0f));
				V_AddLight(ent.origin, intensity, -1.0, -1.0, -1.0);
			}
			else
			{
				CL_Tracker_Shell(cent->lerp_origin);
				V_AddLight(ent.origin, 155, -1.0, -1.0, -1.0);
			}
		}
		else if (effects & EF_TRACKER)
		{
			CL_TrackerTrail(cent->lerp_origin, ent.origin, 0xff000000);
			V_AddLight(ent.origin, 200, -1, -1, -1);
		}
		else if (effects & EF_IONRIPPER)
		{
			CL_IonripperTrail(cent->lerp_origin, ent.origin);
			V_AddLight(ent.origin, 100, 1, 0.5, 0.5);
		}
		else if (effects & EF_BLUEHYPERBLASTER)
		{
			V_AddLight(ent.origin, 200, 0, 0, 1);
		}
		else if (effects & EF_PLASMA)
		{
			if (effects & EF_ANIM_ALLFAST)
			{
				CL_BlasterTrail(cent->lerp_origin, ent.origin);
			}

			V_AddLight(ent.origin, 130, 1, 0.5, 0.5);
		}
	}

	VectorCopy(ent.origin, info->origin);
	info->trailcount = cent->trailcount;
	info->fly_stoptime = cent->fly_stoptime;
	return true;
}
