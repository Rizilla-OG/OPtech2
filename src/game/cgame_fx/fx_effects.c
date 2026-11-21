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

static int nuke_colortable[] = {
	0xffc3b79f, /* 0x10 */
	0xffa79773, /* 0x68 */
	0xff8b7747, /* 0xa8 */
	0xff6f5317, /* 0x90 */
};

static int default_colortable[] = {
	0xff234b63, /* 0x10 */
	0xff4f5bb3, /* 0x68 */
	0xff0f1b4f, /* 0xa8 */
	0xff7b9f97, /* 0x90 */
};

cparticle_t *
CL_AllocParticle(void)
{
	return fxi->AllocParticle(); /* optech2: the engine's particle list */
}

unsigned int
CL_CombineColors(unsigned int basecolor, unsigned int finalcolor, float scale)
{
	int a_beg, b_beg, c_beg, d_beg;
	int a_end, b_end, c_end, d_end;
	int a_step, b_step, c_step, d_step;

	// get colors
	a_beg = (basecolor >> 0) & 0xff;
	b_beg = (basecolor >> 8) & 0xff;
	c_beg = (basecolor >> 16) & 0xff;
	d_beg = (basecolor >> 24) & 0xff;

	a_end = (finalcolor >> 0) & 0xff;
	b_end = (finalcolor >> 8) & 0xff;
	c_end = (finalcolor >> 16) & 0xff;
	d_end = (finalcolor >> 24) & 0xff;

	a_step = (a_end - a_beg) * scale;
	b_step = (b_end - b_beg) * scale;
	c_step = (c_end - c_beg) * scale;
	d_step = (d_end - d_beg) * scale;

	return (((a_beg + a_step) << 0) & 0x000000ff) |
		   (((b_beg + b_step) << 8) & 0x0000ff00) |
		   (((c_beg + c_step) << 16) & 0x00ff0000) |
		   (((d_beg + d_step) << 24) & 0xff000000);
}

void
CL_ExplosionParticles(const vec3_t org)
{
	int i, j;
	cparticle_t *p;
	float time;

	time = (float)cl.time;

	for (i = 0; i < 256; i++)
	{
		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = CL_CombineColors(0xff07abff, 0xff002bab,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((randk() % 32) - 16);
			p->vel[j] = (randk() % 384) - 192;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0;

		p->alphavel = -0.8f / (0.5f + frandk() * 0.3f);
	}
}

void
CL_BigTeleportParticles(const vec3_t org)
{
	int i;
	float time;

	time = (float)cl.time;

	for (i = 0; i < 4096; i++)
	{
		float angle, dist;
		cparticle_t *p;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = default_colortable[randk() & 3];

		angle = M_PI * 2 * (randk() & 1023) / 1023.0f;
		dist = (float)(randk() & 31);
		p->org[0] = org[0] + (float)cos(angle) * dist;
		p->vel[0] = (float)cos(angle) * (70 + (randk() & 63));
		p->accel[0] = -(float)cos(angle) * 100;

		p->org[1] = org[1] + (float)sin(angle) * dist;
		p->vel[1] = (float)sin(angle) * (70 + (randk() & 63));
		p->accel[1] = -(float)sin(angle) * 100;

		p->org[2] = org[2] + 8 + (randk() % 90);
		p->vel[2] = -100 + (randk() & 31);
		p->accel[2] = PARTICLE_GRAVITY * 4;
		p->alpha = 1.0;

		p->alphavel = -0.3f / (0.5f + frandk() * 0.3f);
	}
}

/*
 *  Wall impact puffs
 */
void
CL_BlasterParticles(const vec3_t org, const vec3_t dir)
{
	int i, j;
	cparticle_t *p;
	int count;
	float time;

	time = (float)cl.time;

	count = 40;

	for (i = 0; i < count; i++)
	{
		float d;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = CL_CombineColors(0xff07abff, 0xff002bab,
			(float)(randk() & 15) / 15.0);
		d = randk() & 15;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((randk() & 7) - 4) + d * dir[j];
			p->vel[j] = dir[j] * 30 + crandk() * 40;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0;

		p->alphavel = -1.0f / (0.5f + frandk() * 0.3f);
	}
}

static void
MakeNormalVectors(vec3_t forward, vec3_t right, vec3_t up)
{
	float d;

	/* this rotate and negate guarantees a
	   vector not colinear with the original */
	right[1] = -forward[0];
	right[2] = forward[1];
	right[0] = forward[2];

	d = DotProduct(right, forward);
	VectorMA(right, -d, forward, right);
	VectorNormalize(right);
	CrossProduct(right, forward, up);
}

void
CL_RailTrail(const vec3_t start, const vec3_t end)
{
	vec3_t move, vec;
	vec3_t right, up;
	float time, dec;
	int i, len;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	MakeNormalVectors(vec, right, up);

	for (i = 0; i < len; i++)
	{
		cparticle_t *p;
		float d, c, s;
		vec3_t dir;
		int j;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		VectorClear(p->accel);

		d = i * 0.1f;
		c = (float)cos(d);
		s = (float)sin(d);

		VectorScale(right, c, dir);
		VectorMA(dir, s, up, dir);

		p->alpha = 1.0;
		p->alphavel = -1.0f / (1 + frandk() * 0.2f);
		p->color = CL_CombineColors(0xff6f5317, 0xff2b1f00,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + dir[j] * 3;
			p->vel[j] = dir[j] * 6;
		}

		VectorAdd(move, vec, move);
	}

	dec = 0.75;
	VectorScale(vec, dec, vec);
	VectorCopy(start, move);

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

		p->time = time;
		VectorClear(p->accel);

		p->alpha = 1.0;
		p->alphavel = -1.0f / (0.6f + frandk() * 0.2f);
		p->color = CL_CombineColors(0xff000000, 0xffebebeb,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + crandk() * 3;
			p->vel[j] = crandk() * 3;
			p->accel[j] = 0;
		}

		VectorAdd(move, vec, move);
	}
}

void
CL_BubbleTrail(const vec3_t start, const vec3_t end)
{
	vec3_t move;
	vec3_t vec;
	int len;
	int i, j;
	cparticle_t *p;
	float dec;
	float time;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	dec = 32;
	VectorScale(vec, dec, vec);

	for (i = 0; i < len; i += 32)
	{
		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		VectorClear(p->accel);
		p->time = time;

		p->alpha = 1.0;
		p->alphavel = -1.0f / (1 + frandk() * 0.2f);
		p->color = CL_CombineColors(0xff3f3f3f, 0xffababab,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + crandk() * 2;
			p->vel[j] = crandk() * 5;
		}

		p->vel[2] += 6;

		VectorAdd(move, vec, move);
	}
}

void
CL_BFGExplosionParticles(const vec3_t org)
{
	int i;
	float time;

	time = (float)cl.time;

	for (i = 0; i < 256; i++)
	{
		cparticle_t *p;
		int j;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = CL_CombineColors(0xff00ff00, 0xffffffff,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((randk() % 32) - 16);
			p->vel[j] = (randk() % 384) - 192;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0;

		p->alphavel = -0.8f / (0.5f + frandk() * 0.3f);
	}
}

void
CL_TeleportParticles(const vec3_t org)
{
	float time;
	int i;

	time = (float)cl.time;

	for (i = -16; i <= 16; i += 4)
	{
		int j;

		for (j = -16; j <= 16; j += 4)
		{
			int k;

			for (k = -16; k <= 32; k += 4)
			{
				cparticle_t *p;
				vec3_t dir;
				float vel;

				p = CL_AllocParticle();
				if (!p)
				{
					return;
				}

				p->time = time;
				p->color = CL_CombineColors(0xff6b6b6b, 0xffdbdbdb,
					(float)(randk() & 15) / 15.0);
				p->alpha = 1.0;
				p->alphavel = -1.0f / (0.3f + (randk() & 7) * 0.02f);

				p->org[0] = org[0] + i + (randk() & 3);
				p->org[1] = org[1] + j + (randk() & 3);
				p->org[2] = org[2] + k + (randk() & 3);

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

void
CL_FlameEffects(const vec3_t origin)
{
	int n, count;

	count = randk() & 0xF;

	/* Particles going down */
	for (n = 0; n < count; n++)
	{
		cparticle_t *p;
		int j;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		VectorClear(p->accel);
		p->time = cl.time;

		p->alpha = 1.0;
		p->alphavel = -1.0 / (1 + frandk() * 0.2);
		p->color = CL_CombineColors(0xff007fef, 0xff003bb7,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = origin[j] + crandk() * 5;
			p->vel[j] = crandk() * 5;
		}

		p->vel[2] = crandk() * -10;
		p->accel[2] = -PARTICLE_GRAVITY;
	}

	count = randk() & 0x7;

	/* Particles go up */
	for (n = 0; n < count; n++)
	{
		cparticle_t *p;
		int j;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}
		VectorClear(p->accel);

		p->time = cl.time;

		p->alpha = 1.0;
		p->alphavel = -1.0 / (1 + frandk() * 0.5);
		p->color = CL_CombineColors(0xff0000ff, 0xff002f2f,
			(float)(randk() & 15) / 15.0);
		for (j = 0; j < 3; j++)
		{
			p->org[j] = origin[j] + crandk() * 3;
		}
		p->vel[2] = 20 + crandk() * 5;
	}
}

void
CL_Flashlight(int ent, const vec3_t pos)
{
	cdlight_t *dl;

	dl = CL_AllocDlight(ent);
	VectorCopy(pos, dl->origin);
	dl->radius = 400;
	dl->minlight = 250;
	dl->die = cl.time + 100;
	dl->color[0] = 1;
	dl->color[1] = 1;
	dl->color[2] = 1;
}

void
CL_ColorFlash(vec3_t pos, int ent, float intensity, float r, float g, float b)
{
	cdlight_t *dl;

	dl = CL_AllocDlight(ent);
	VectorCopy(pos, dl->origin);
	dl->radius = intensity;
	dl->minlight = 250;
	dl->die = cl.time + 100;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
}

void
CL_DebugTrail(const vec3_t start, const vec3_t end)
{
	vec3_t move;
	vec3_t vec;
	float len;
	cparticle_t *p;
	float dec;
	vec3_t right, up;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	MakeNormalVectors(vec, right, up);

	dec = 3;
	VectorScale(vec, dec, vec);
	VectorCopy(start, move);

	while (len > 0)
	{
		len -= dec;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = (float)cl.time;
		VectorClear(p->accel);
		VectorClear(p->vel);
		p->alpha = 1.0;
		p->alphavel = -0.1f;
		p->color = CL_CombineColors(0xff6f5317, 0xff2b1f00,
			(float)(randk() & 15) / 15.0);
		VectorCopy(move, p->org);
		VectorAdd(move, vec, move);
	}
}

void
CL_ForceWall(const vec3_t start, const vec3_t end, int color)
{
	vec3_t move;
	vec3_t vec;
	int j;
	cparticle_t *p;

	float len, time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	VectorScale(vec, 4, vec);

	time = (float)cl.time;

	while (len > 0)
	{
		len -= 4;

		if (frandk() > 0.3)
		{
			p = CL_AllocParticle();
			if (!p)
			{
				return;
			}

			VectorClear(p->accel);

			p->time = time;

			p->alpha = 1.0;
			p->alphavel = -1.0f / (3.0 + frandk() * 0.5f);
			p->color = color;

			for (j = 0; j < 3; j++)
			{
				p->org[j] = move[j] + crandk() * 3;
				p->accel[j] = 0;
			}

			p->vel[0] = 0;
			p->vel[1] = 0;
			p->vel[2] = -40 - (crandk() * 10);
		}

		VectorAdd(move, vec, move);
	}
}

/*
 * CL_BubbleTrail2 (lets you control the # of bubbles
 * by setting the distance between the spawns)
 */
void
CL_BubbleTrail2(vec3_t start, vec3_t end, int dist)
{
	vec3_t move;
	vec3_t vec;
	float len, time;
	int i;
	int j;
	cparticle_t *p;

	time = (float)cl.time;

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	VectorScale(vec, dist, vec);

	for (i = 0; i < len; i += dist)
	{
		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		VectorClear(p->accel);
		p->time = time;

		p->alpha = 1.0;
		p->alphavel = -1.0f / (1 + frandk() * 0.1f);
		p->color = CL_CombineColors(0xff3f3f3f, 0xffababab,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = move[j] + crandk() * 2;
			p->vel[j] = crandk() * 10;
		}

		p->org[2] -= 4;
		p->vel[2] += 20;
		VectorAdd(move, vec, move);
	}
}

void
CL_Heatbeam(vec3_t start, vec3_t forward)
{
	vec3_t move;
	vec3_t vec;
	float len;
	int j;
	cparticle_t *p;
	vec3_t right, up;
	float i;
	vec3_t dir;
	float ltime;
	float step = 32.0, rstep;
	float start_pt;
	float rot;
	float time;
	vec3_t end;

	VectorMA(start, 4096, forward, end);

	VectorCopy(start, move);
	VectorSubtract(end, start, vec);
	len = VectorNormalize(vec);

	VectorCopy(cl.v_right, right);
	VectorCopy(cl.v_up, up);

	VectorMA(move, -0.5, right, move);
	VectorMA(move, -0.5, up, move);

	time = (float)cl.time;

	ltime = (float)cl.time / 1000.0f;
	start_pt = (float)fmod(ltime * 96.0f, step);
	VectorMA(move, start_pt, vec, move);

	VectorScale(vec, step, vec);

	rstep = M_PI / 10.0f;

	for (i = start_pt; i < len; i += step)
	{
		if (i > step * 5) /* don't bother after the 5th ring */
		{
			break;
		}

		for (rot = 0; rot < M_PI * 2; rot += rstep)
		{
			float variance;
			float c, s;

			p = CL_AllocParticle();
			if (!p)
			{
				return;
			}

			p->time = time;
			VectorClear(p->accel);
			variance = 0.5;
			c = (float)cos(rot) * variance;
			s = (float)sin(rot) * variance;

			/* trim it so it looks like it's starting at the origin */
			if (i < 10)
			{
				VectorScale(right, c * (i / 10.0f), dir);
				VectorMA(dir, s * (i / 10.0f), up, dir);
			}
			else
			{
				VectorScale(right, c, dir);
				VectorMA(dir, s, up, dir);
			}

			p->alpha = 0.5;
			p->alphavel = -1000.0;
			p->color = CL_CombineColors(0xff0fbfff, 0xff003bb7,
				(float)(randk() & 15) / 15.0);

			for (j = 0; j < 3; j++)
			{
				p->org[j] = move[j] + dir[j] * 3;
				p->vel[j] = 0;
			}
		}

		VectorAdd(move, vec, move);
	}
}

/*
 *Puffs with velocity along direction, with some randomness thrown in
 */
void
CL_ParticleSteamEffect(vec3_t org, vec3_t dir, unsigned int basecolor, unsigned int finalcolor,
	int count, int magnitude)
{
	int i;
	float time;
	vec3_t r, u;

	time = (float)cl.time;
	MakeNormalVectors(dir, r, u);

	for (i = 0; i < count; i++)
	{
		cparticle_t *p;
		float d;
		int j;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = CL_CombineColors(basecolor, finalcolor,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + magnitude * 0.1f * crandk();
		}

		VectorScale(dir, magnitude, p->vel);
		d = crandk() * magnitude / 3;
		VectorMA(p->vel, d, r, p->vel);
		d = crandk() * magnitude / 3;
		VectorMA(p->vel, d, u, p->vel);

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY / 2;
		p->alpha = 1.0;

		p->alphavel = -1.0f / (0.5f + frandk() * 0.3f);
	}
}

void
CL_ParticleSteamEffect2(cl_sustain_t *self)
{
	int i;
	vec3_t r, u;
	vec3_t dir;

	VectorCopy(self->dir, dir);
	MakeNormalVectors(dir, r, u);

	for (i = 0; i < self->count; i++)
	{
		cparticle_t *p;
		float d;
		int j;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = cl.time;
		p->color = CL_CombineColors(self->basecolor, self->finalcolor,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = self->org[j] + self->magnitude * 0.1 * crandk();
		}

		VectorScale(dir, self->magnitude, p->vel);
		d = crandk() * self->magnitude / 3;
		VectorMA(p->vel, d, r, p->vel);
		d = crandk() * self->magnitude / 3;
		VectorMA(p->vel, d, u, p->vel);

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY / 2;
		p->alpha = 1.0;

		p->alphavel = -1.0 / (0.5 + frandk() * 0.3);
	}

	self->nextthink += self->thinkinterval;
}

void
CL_MonsterPlasma_Shell(vec3_t origin)
{
	vec3_t dir;
	int i;
	cparticle_t *p;
	float time;

	time = (float)cl.time;

	for (i = 0; i < 40; i++)
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
		p->color = 0xff07abff;
		dir[0] = crandk();
		dir[1] = crandk();
		dir[2] = crandk();
		VectorNormalize(dir);

		VectorMA(origin, 10, dir, p->org);
	}
}

void
CL_Widowbeamout(cl_sustain_t *self)
{
	vec3_t dir;
	int i;
	cparticle_t *p;
	float ratio;
	float time;

	ratio = 1.0f - (((float)self->endtime - (float)cl.time) / 2100.0f);
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
		p->color = default_colortable[randk() & 3];
		dir[0] = crandk();
		dir[1] = crandk();
		dir[2] = crandk();
		VectorNormalize(dir);

		VectorMA(self->org, (45.0 * ratio), dir, p->org);
	}
}

void
CL_Nukeblast(cl_sustain_t *self)
{
	vec3_t dir;
	int i;
	cparticle_t *p;
	float ratio;
	float time;

	ratio = 1.0f - (((float)self->endtime - (float)cl.time) / 1000.0f);
	time = (float)cl.time;

	for (i = 0; i < 700; i++)
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
		p->color = nuke_colortable[randk() & 3];
		dir[0] = crandk();
		dir[1] = crandk();
		dir[2] = crandk();
		VectorNormalize(dir);

		VectorMA(self->org, (200.0 * ratio), dir, p->org);
	}
}

void
CL_WidowSplash(const vec3_t org)
{
	int i;
	float time;

	time = (float)cl.time;

	for (i = 0; i < 256; i++)
	{
		cparticle_t *p;
		vec3_t dir;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = default_colortable[randk() & 3];
		dir[0] = crandk();
		dir[1] = crandk();
		dir[2] = crandk();
		VectorNormalize(dir);
		VectorMA(org, 45.0, dir, p->org);
		VectorMA(vec3_origin, 40.0, dir, p->vel);

		p->accel[0] = p->accel[1] = 0;
		p->alpha = 1.0;

		p->alphavel = -0.8f / (0.5f + frandk() * 0.3f);
	}
}

void
CL_PowerSplash(void)
{
	static const unsigned colortable[4] = {
		0xff00ff00,
		0xff0fe723,
		0xff1bd33f,
		0xff27bb53};
	int entnum, type, i;
	vec3_t entity_origin, entity_angles;

	entnum = MSG_ReadShort(&net_message);
	type = MSG_ReadByte(&net_message);

	/* optech2: the entity's current position from the engine */
	if (!fxi->EntityState(entnum, entity_origin, entity_angles))
	{
		return;
	}

	for (i = 0; i < 256; i++)
	{
		cparticle_t *p;
		vec3_t dir;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = cl.time;
		if (type)
		{
			p->color = colortable[randk() & 3];
		}
		else
		{
			p->color = default_colortable[randk() & 3];
		}

		dir[0] = crandk();
		dir[1] = crandk();
		dir[2] = crandk();
		VectorNormalize(dir);
		/* 20 radius of effect, should be model size */
		VectorMA(entity_origin, 20, dir, p->org);
		VectorScale(dir, 40.0f, p->vel);

		VectorClear(p->accel);
		p->alpha = 1.0f;

		p->alphavel = -1.0f / (0.5f + frandk() * 0.3f);
	}
}

void
CL_ColorExplosionParticles(const vec3_t org, unsigned int basecolor, unsigned int finalcolor)
{
	int i;
	int j;
	cparticle_t *p;
	float time;

	time = (float)cl.time;

	for (i = 0; i < 128; i++)
	{
		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = CL_CombineColors(basecolor, finalcolor,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((randk() % 32) - 16);
			p->vel[j] = (randk() % 256) - 128;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0;

		p->alphavel = -0.4f / (0.6f + frandk() * 0.2f);
	}
}

/*
 * Like the steam effect, but unaffected by gravity
 */
void
CL_ParticleSmokeEffect(const vec3_t org, vec3_t dir, unsigned int basecolor, unsigned int finalcolor,
	int count, int magnitude)
{
	int i, j;
	cparticle_t *p;
	vec3_t r, u;
	float time;

	time = (float)cl.time;

	MakeNormalVectors(dir, r, u);

	for (i = 0; i < count; i++)
	{
		float d;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = CL_CombineColors(basecolor, finalcolor,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + magnitude * 0.1f * crandk();
		}

		VectorScale(dir, magnitude, p->vel);
		d = crandk() * magnitude / 3;
		VectorMA(p->vel, d, r, p->vel);
		d = crandk() * magnitude / 3;
		VectorMA(p->vel, d, u, p->vel);

		p->accel[0] = p->accel[1] = p->accel[2] = 0;
		p->alpha = 1.0;

		p->alphavel = -1.0f / (0.5f + frandk() * 0.3f);
	}
}

/*
 * Wall impact puffs (Green)
 */
void
CL_BlasterParticles2(const vec3_t org, const vec3_t dir, unsigned int basecolor, unsigned int finalcolor)
{
	int i, j;
	cparticle_t *p;
	int count;
	float time;

	time = (float)cl.time;

	count = 40;

	for (i = 0; i < count; i++)
	{
		float d;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		d = (float)(randk() & 15);
		p->color = CL_CombineColors(basecolor, finalcolor,
			(float)(randk() & 15) / 15.0);

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((randk() & 7) - 4) + d * dir[j];
			p->vel[j] = dir[j] * 30 + crandk() * 40;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0;

		p->alphavel = -1.0f / (0.5f + frandk() * 0.3f);
	}
}

void
CL_ParticleEffect(vec3_t org, vec3_t dir, unsigned int basecolor, unsigned int finalcolor, int count)
{
	int i;

	for (i = 0; i < count; i++)
	{
		cparticle_t *p;
		float d;
		int j;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = cl.time;
		p->color = CL_CombineColors(basecolor, finalcolor,
			(float)(randk() & 15) / 15.0);
		d = randk() & 31;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((randk() & 7) - 4) + d * dir[j];
			p->vel[j] = crandk() * 20;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY + 0.2f;
		p->alpha = 1.0;

		p->alphavel = -1.0 / (0.5 + frandk() * 0.3);
	}
}

void
CL_ParticleEffect2(vec3_t org, vec3_t dir, unsigned int basecolor, unsigned int finalcolor, int count)
{
	float time;
	int i;

	time = (float)cl.time;

	for (i = 0; i < count; i++)
	{
		cparticle_t *p;
		int j;
		float d;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = CL_CombineColors(basecolor, finalcolor,
			(float)(randk() & 15) / 15.0);

		d = randk() & 7;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((randk() & 7) - 4) + d * dir[j];
			p->vel[j] = crandk() * 20;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -PARTICLE_GRAVITY;
		p->alpha = 1.0;

		p->alphavel = -1.0f / (0.5f + frandk() * 0.3f);
	}
}

void
CL_ParticleEffect3(vec3_t org, vec3_t dir, unsigned int color, int count)
{
	float time;
	int i;

	time = (float)cl.time;

	for (i = 0; i < count; i++)
	{
		int j;
		cparticle_t *p;
		float d;

		p = CL_AllocParticle();
		if (!p)
		{
			return;
		}

		p->time = time;
		p->color = color;

		d = randk() & 7;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((randk() & 7) - 4) + d * dir[j];
			p->vel[j] = crandk() * 20;
		}

		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = PARTICLE_GRAVITY;
		p->alpha = 1.0;

		p->alphavel = -1.0f / (0.5f + frandk() * 0.3f);
	}
}
