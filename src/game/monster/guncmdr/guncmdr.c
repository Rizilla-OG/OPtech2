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
 * optech2: Gunner Commander, ported from the re-release (m_guncmdr.cpp).
 * A bigger gunner with a flechette chaingun, mortar and grenade volleys,
 * an ion ripper burst from a crouch counter, a kick and its own pains
 * and deaths. Uses the commander frames of the re-release gunner model;
 * without them (classic data) monster_guncmdr stays the gunner it was.
 *
 * =======================================================================
 */

#include "../../header/local.h"
#include "guncmdr.h"

#define SPAWNFLAG_GUNCMDR_NOJUMPING 8

#define MORTAR_SPEED 850.0f
#define GRENADE_SPEED 600.0f
#define RANGE_GUNCMDR_MELEE 20.0f /* box to box: kick */
#define RANGE_GUNCMDR_GRENADE 100.0f /* never grenades this close */
#define RANGE_GUNCMDR_MORTAR 525.0f /* always mortar from here */
#define RANGE_GUNCMDR_CHAIN_RUN 400.0f /* run at the enemy while firing */

static int sound_pain;
static int sound_pain2;
static int sound_death;
static int sound_idle;
static int sound_open;
static int sound_search;
static int sound_sight;
static int sound_step;
static int sound_step2;

void guncmdr_stand(edict_t *self);
void guncmdr_run(edict_t *self);
void guncmdr_attack(edict_t *self);
void guncmdr_sidestep(edict_t *self);
static void guncmdr_fidget(edict_t *self);
static void guncmdr_footstep(edict_t *self);
static void guncmdr_footstep_shrink(edict_t *self);
static void guncmdr_idlesound(edict_t *self);
static void guncmdr_dead(edict_t *self);
static void guncmdr_shrink(edict_t *self);
static void guncmdr_pain5_to_death1(edict_t *self);
static void guncmdr_pain5_to_death2(edict_t *self);
static void guncmdr_pain6_to_death6(edict_t *self);
static void guncmdr_opengun(edict_t *self);
static void GunnerCmdrFire(edict_t *self);
static void GunnerCmdrGrenade(edict_t *self);
static void GunnerCmdrCounter(edict_t *self);
static void guncmdr_fire_chain(edict_t *self);
static void guncmdr_refire_chain(edict_t *self);
static void guncmdr_grenade_mortar_resume(edict_t *self);
static void guncmdr_grenade_back_dodge_resume(edict_t *self);
static void guncmdr_kick(edict_t *self);
static void guncmdr_kick_finished(edict_t *self);
static void guncmdr_jump_now(edict_t *self);
static void guncmdr_jump2_now(edict_t *self);
static void guncmdr_jump_wait_land(edict_t *self);

extern mmove_t guncmdr_move_jump;
extern mmove_t guncmdr_move_jump2;
extern mmove_t guncmdr_move_duck_attack;

static void
guncmdr_footstep(edict_t *self)
{
	if (!self || !g_monsterfootsteps->value)
	{
		return;
	}

	/* lazy loading for savegame compatibility */
	if (sound_step == 0 || sound_step2 == 0)
	{
		sound_step = gi.soundindex("gunner/step1.wav");
		sound_step2 = gi.soundindex("gunner/step2.wav");
	}

	gi.sound(self, CHAN_BODY, (randk() & 1) ? sound_step : sound_step2, 1, ATTN_NORM, 0);
}

static void
guncmdr_footstep_shrink(edict_t *self)
{
	guncmdr_footstep(self);
	guncmdr_shrink(self);
}

static void
guncmdr_idlesound(edict_t *self)
{
	gi.sound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

void
guncmdr_sight(edict_t *self, edict_t *other /* unused */)
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

void
guncmdr_search(edict_t *self)
{
	gi.sound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

/* distance between the two boxes (the re-release's range_to) */
static float
guncmdr_range_to(const edict_t *self, const edict_t *other)
{
	float distance_squared = 0;
	int i;

	for (i = 0; i < 3; i++)
	{
		float gap = 0;

		if (other->absmin[i] > self->absmax[i])
		{
			gap = other->absmin[i] - self->absmax[i];
		}
		else if (self->absmin[i] > other->absmax[i])
		{
			gap = self->absmin[i] - other->absmax[i];
		}

		distance_squared += gap * gap;
	}

	return sqrtf(distance_squared);
}

static mframe_t guncmdr_frames_fidget[] = {
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, guncmdr_idlesound},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, guncmdr_idlesound},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}};

mmove_t guncmdr_move_fidget = {
	FRAME_c_stand201,
	FRAME_c_stand254,
	guncmdr_frames_fidget,
	guncmdr_stand};
static void
guncmdr_fidget(edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		return;
	}

	if (self->enemy)
	{
		return;
	}

	if (random() <= 0.05f)
	{
		self->monsterinfo.currentmove = &guncmdr_move_fidget;
	}
}

static mframe_t guncmdr_frames_stand[] = {
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, guncmdr_fidget},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, guncmdr_fidget},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, guncmdr_fidget},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, guncmdr_fidget}};

mmove_t guncmdr_move_stand = {
	FRAME_c_stand101,
	FRAME_c_stand140,
	guncmdr_frames_stand,
	NULL};
void
guncmdr_stand(edict_t *self)
{
	self->monsterinfo.currentmove = &guncmdr_move_stand;
}

static mframe_t guncmdr_frames_walk[] = {
	{ai_walk, 1.5f, guncmdr_footstep},
	{ai_walk, 2.5f, NULL},
	{ai_walk, 3.0f, NULL},
	{ai_walk, 2.5f, NULL},
	{ai_walk, 2.3f, NULL},
	{ai_walk, 3.0f, NULL},
	{ai_walk, 2.8f, guncmdr_footstep},
	{ai_walk, 3.6f, NULL},
	{ai_walk, 2.8f, NULL},
	{ai_walk, 2.5f, NULL},
	{ai_walk, 2.3f, NULL},
	{ai_walk, 4.3f, NULL},
	{ai_walk, 3.0f, guncmdr_footstep},
	{ai_walk, 1.5f, NULL},
	{ai_walk, 2.5f, NULL},
	{ai_walk, 3.3f, NULL},
	{ai_walk, 2.8f, NULL},
	{ai_walk, 3.0f, NULL},
	{ai_walk, 2.0f, guncmdr_footstep},
	{ai_walk, 2.0f, NULL},
	{ai_walk, 3.3f, NULL},
	{ai_walk, 3.6f, NULL},
	{ai_walk, 3.4f, NULL},
	{ai_walk, 2.8f, NULL}};

mmove_t guncmdr_move_walk = {
	FRAME_c_walk101,
	FRAME_c_walk124,
	guncmdr_frames_walk,
	NULL};
void
guncmdr_walk(edict_t *self)
{
	self->monsterinfo.currentmove = &guncmdr_move_walk;
}

static mframe_t guncmdr_frames_run[] = {
	{ai_run, 15.f, monster_done_dodge},
	{ai_run, 16.f, guncmdr_footstep},
	{ai_run, 20.f, NULL},
	{ai_run, 18.f, NULL},
	{ai_run, 24.f, guncmdr_footstep},
	{ai_run, 13.5f, NULL}};

mmove_t guncmdr_move_run = {
	FRAME_c_run101,
	FRAME_c_run106,
	guncmdr_frames_run,
	NULL};
void
guncmdr_run(edict_t *self)
{
	monster_done_dodge(self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.currentmove = &guncmdr_move_stand;
	}
	else
	{
		self->monsterinfo.currentmove = &guncmdr_move_run;
	}
}

/* standing pains */
static mframe_t guncmdr_frames_pain1[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_pain1 = {
	FRAME_c_pain101,
	FRAME_c_pain104,
	guncmdr_frames_pain1,
	guncmdr_run};
static mframe_t guncmdr_frames_pain2[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_pain2 = {
	FRAME_c_pain201,
	FRAME_c_pain204,
	guncmdr_frames_pain2,
	guncmdr_run};
static mframe_t guncmdr_frames_pain3[] = {
	{ai_move, -3.0f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_pain3 = {
	FRAME_c_pain301,
	FRAME_c_pain304,
	guncmdr_frames_pain3,
	guncmdr_run};
static mframe_t guncmdr_frames_pain4[] = {
	{ai_move, -17.1f, NULL},
	{ai_move, -3.2f, NULL},
	{ai_move, 0.9f, NULL},
	{ai_move, 3.6f, NULL},
	{ai_move, -2.6f, NULL},
	{ai_move, 1.0f, NULL},
	{ai_move, -5.1f, NULL},
	{ai_move, -6.7f, NULL},
	{ai_move, -8.8f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -2.1f, NULL},
	{ai_move, -2.3f, NULL},
	{ai_move, -2.5f, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_pain4 = {
	FRAME_c_pain401,
	FRAME_c_pain415,
	guncmdr_frames_pain4,
	guncmdr_run};
static mframe_t guncmdr_frames_death1[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 4.0f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_death1 = {
	FRAME_c_death101,
	FRAME_c_death118,
	guncmdr_frames_death1,
	guncmdr_dead};
static void
guncmdr_pain5_to_death1(edict_t *self)
{
	if (self->health < 0)
	{
		self->monsterinfo.currentmove = &guncmdr_move_death1;
	}
}

static mframe_t guncmdr_frames_death2[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_death2 = {
	FRAME_c_death201,
	FRAME_c_death204,
	guncmdr_frames_death2,
	guncmdr_dead};
static void
guncmdr_pain5_to_death2(edict_t *self)
{
	if ((self->health < 0) && (randk() & 1))
	{
		self->monsterinfo.currentmove = &guncmdr_move_death2;
	}
}

static mframe_t guncmdr_frames_pain5[] = {
	{ai_move, -29.f, NULL},
	{ai_move, -5.f, NULL},
	{ai_move, -5.f, NULL},
	{ai_move, -3.f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, guncmdr_pain5_to_death2},
	{ai_move, 9.f, NULL},
	{ai_move, 3.f, NULL},
	{ai_move, 0, guncmdr_pain5_to_death1},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -4.6f, NULL},
	{ai_move, -4.8f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 9.5f, NULL},
	{ai_move, 3.4f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -2.4f, NULL},
	{ai_move, -9.0f, NULL},
	{ai_move, -5.0f, NULL},
	{ai_move, -3.6f, NULL}};

mmove_t guncmdr_move_pain5 = {
	FRAME_c_pain501,
	FRAME_c_pain524,
	guncmdr_frames_pain5,
	guncmdr_run};
static void
guncmdr_dead(edict_t *self)
{
	VectorSet(self->mins, -16, -16, -24);
	VectorSet(self->maxs, 16, 16, -8);
	monster_sync_scale_mins_maxs(self);
	monster_dynamic_dead(self);
}

static void
guncmdr_shrink(edict_t *self)
{
	self->maxs[2] = -4 * ((self->rrs.scale[2] > 0) ? self->rrs.scale[2] : 1.0f);
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity(self);
}

static mframe_t guncmdr_frames_death6[] = {
	{ai_move, 0, guncmdr_shrink},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_death6 = {
	FRAME_c_death601,
	FRAME_c_death614,
	guncmdr_frames_death6,
	guncmdr_dead};
static void
guncmdr_pain6_to_death6(edict_t *self)
{
	if (self->health < 0)
	{
		self->monsterinfo.currentmove = &guncmdr_move_death6;
	}
}

static mframe_t guncmdr_frames_pain6[] = {
	{ai_move, 16.f, NULL},
	{ai_move, 16.f, NULL},
	{ai_move, 12.f, NULL},
	{ai_move, 5.5f, monster_duck_down},
	{ai_move, 3.0f, NULL},
	{ai_move, -4.7f, NULL},
	{ai_move, -6.0f, guncmdr_pain6_to_death6},
	{ai_move, 0, NULL},
	{ai_move, 1.8f, NULL},
	{ai_move, 0.7f, NULL},
	{ai_move, 0, NULL},
	{ai_move, -2.1f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -6.1f, NULL},
	{ai_move, 10.5f, NULL},
	{ai_move, 4.3f, NULL},
	{ai_move, 4.7f, monster_duck_up},
	{ai_move, 1.4f, NULL},
	{ai_move, 0, NULL},
	{ai_move, -3.2f, NULL},
	{ai_move, 2.3f, NULL},
	{ai_move, -4.4f, NULL},
	{ai_move, -4.4f, NULL},
	{ai_move, -2.4f, NULL}};

mmove_t guncmdr_move_pain6 = {
	FRAME_c_pain601,
	FRAME_c_pain632,
	guncmdr_frames_pain6,
	guncmdr_run};
static mframe_t guncmdr_frames_pain7[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_pain7 = {
	FRAME_c_pain701,
	FRAME_c_pain714,
	guncmdr_frames_pain7,
	guncmdr_run};
/* pain skin on the commander skins (2 and 3) */
static void
guncmdr_setskin(edict_t *self)
{
	if (self->health < (self->max_health / 2))
	{
		self->s.skinnum |= 1;
	}
	else
	{
		self->s.skinnum &= ~1;
	}
}

/* the re-release dodges a hit it still gets pain for (no trace here: a sidestep) */
static void
guncmdr_pain_dodge(edict_t *self)
{
	if ((random() >= 0.3f) || (self->monsterinfo.aiflags & (AI_STAND_GROUND | AI_DODGING)) || (self->health < 1))
	{
		return;
	}

	self->monsterinfo.lefty = randk() & 1;
	self->monsterinfo.aiflags |= AI_DODGING;
	guncmdr_sidestep(self);
}

void
guncmdr_pain(edict_t *self, edict_t *other, float kick /* unused */, int damage)
{
	vec3_t forward, from_other;

	if (!self)
	{
		return;
	}

	guncmdr_setskin(self);
	monster_done_dodge(self);

	if ((self->monsterinfo.currentmove == &guncmdr_move_jump) ||
		(self->monsterinfo.currentmove == &guncmdr_move_jump2) ||
		(self->monsterinfo.currentmove == &guncmdr_move_duck_attack))
	{
		return;
	}

	if (level.time < self->pain_debounce_time)
	{
		guncmdr_pain_dodge(self);
		return;
	}

	self->pain_debounce_time = level.time + 3;

	gi.sound(self, CHAN_VOICE, (randk() & 1) ? sound_pain : sound_pain2, 1, ATTN_NORM, 0);

	if (skill->value == SKILL_HARDPLUS)
	{
		guncmdr_pain_dodge(self);
		return; /* no pain anims in nightmare */
	}

	AngleVectors(self->s.angles, forward, NULL, NULL);
	VectorClear(from_other);

	if (other)
	{
		VectorSubtract(other->s.origin, self->s.origin, from_other);
		from_other[2] = 0;
		VectorNormalize(from_other);
	}

	if (damage < 35)
	{
		/* small pain */
		int which = randk() % 5;

		if (which == 0)
		{
			self->monsterinfo.currentmove = &guncmdr_move_pain3;
		}
		else if (which == 1)
		{
			self->monsterinfo.currentmove = &guncmdr_move_pain2;
		}
		else if (which == 2)
		{
			self->monsterinfo.currentmove = &guncmdr_move_pain1;
		}
		else
		{
			self->monsterinfo.currentmove = &guncmdr_move_pain7;
		}
	}
	else if (DotProduct(from_other, forward) < -0.40f)
	{
		/* large pain from behind */
		self->monsterinfo.currentmove = &guncmdr_move_pain6;
		self->pain_debounce_time += 1.5f;
	}
	else
	{
		self->monsterinfo.currentmove = (randk() & 1) ? &guncmdr_move_pain4 : &guncmdr_move_pain5;
		self->pain_debounce_time += 1.5f;
	}

	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	if (self->monsterinfo.aiflags & AI_DUCKED)
	{
		monster_duck_up(self);
	}
}

static mframe_t guncmdr_frames_death3[] = {
	{ai_move, 20.f, NULL},
	{ai_move, 10.f, NULL},
	{ai_move, 10.f, guncmdr_footstep_shrink},
	{ai_move, 0.f, guncmdr_footstep},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_death3 = {
	FRAME_c_death301,
	FRAME_c_death321,
	guncmdr_frames_death3,
	guncmdr_dead};
static mframe_t guncmdr_frames_death7[] = {
	{ai_move, 30.f, NULL},
	{ai_move, 20.f, NULL},
	{ai_move, 16.f, guncmdr_footstep_shrink},
	{ai_move, 5.f, guncmdr_footstep},
	{ai_move, -6.f, NULL},
	{ai_move, -7.f, guncmdr_footstep},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0.f, guncmdr_footstep},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0.f, guncmdr_footstep},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_death7 = {
	FRAME_c_death701,
	FRAME_c_death730,
	guncmdr_frames_death7,
	guncmdr_dead};
static mframe_t guncmdr_frames_death4[] = {
	{ai_move, -20.f, NULL},
	{ai_move, -16.f, NULL},
	{ai_move, -26.f, guncmdr_footstep_shrink},
	{ai_move, 0.f, guncmdr_footstep},
	{ai_move, -12.f, NULL},
	{ai_move, 16.f, NULL},
	{ai_move, 9.2f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_death4 = {
	FRAME_c_death401,
	FRAME_c_death436,
	guncmdr_frames_death4,
	guncmdr_dead};
static mframe_t guncmdr_frames_death5[] = {
	{ai_move, -14.f, NULL},
	{ai_move, -2.7f, NULL},
	{ai_move, -2.5f, NULL},
	{ai_move, -4.6f, guncmdr_footstep},
	{ai_move, -4.0f, guncmdr_footstep},
	{ai_move, -1.5f, NULL},
	{ai_move, 2.3f, NULL},
	{ai_move, 2.5f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 3.5f, NULL},
	{ai_move, 12.9f, guncmdr_footstep},
	{ai_move, 3.8f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -2.1f, NULL},
	{ai_move, -1.3f, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 3.4f, NULL},
	{ai_move, 5.7f, NULL},
	{ai_move, 11.2f, NULL},
	{ai_move, 0, guncmdr_footstep}};

mmove_t guncmdr_move_death5 = {
	FRAME_c_death501,
	FRAME_c_death528,
	guncmdr_frames_death5,
	guncmdr_dead};
static void
guncmdr_skinned_gib(edict_t *self, const char *name, int damage, gibtype_t type)
{
	edict_t *gib = ThrowGib(self, name, damage, type);

	if (gib)
	{
		gib->s.skinnum = self->s.skinnum;
	}
}

void
guncmdr_die(edict_t *self, edict_t *inflictor, edict_t *attacker /* unused */,
	int damage, const vec3_t point)
{
	vec3_t forward, from_inflictor;
	int which;

	if (!self)
	{
		return;
	}

	guncmdr_setskin(self);

	/* check for gib */
	if (self->health <= self->gib_health)
	{
		int n;
		const char *head_gib = (self->monsterinfo.currentmove != &guncmdr_move_death5) ? "models/objects/gibs/sm_meat/tris.md2" : "models/monsters/gunner/gibs/head.md2";

		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);
		self->s.skinnum /= 2; /* the gib models' commander skin */

		for (n = 0; n < 2; n++)
		{
			ThrowGib(self, "models/objects/gibs/bone/tris.md2", damage, GIB_ORGANIC);
			ThrowGib(self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		}

		ThrowGib(self, "models/objects/gibs/gear/tris.md2", damage, GIB_METALLIC);
		guncmdr_skinned_gib(self, "models/monsters/gunner/gibs/chest.md2", damage, GIB_ORGANIC);
		guncmdr_skinned_gib(self, "models/monsters/gunner/gibs/garm.md2", damage, GIB_ORGANIC);
		guncmdr_skinned_gib(self, "models/monsters/gunner/gibs/gun.md2", damage, GIB_METALLIC);
		guncmdr_skinned_gib(self, "models/monsters/gunner/gibs/foot.md2", damage, GIB_ORGANIC);
		ThrowHead(self, head_gib, damage, GIB_ORGANIC);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
	{
		return;
	}

	/* regular death */
	gi.sound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	/* these pains turn into deaths on their own: keep going */
	if ((self->monsterinfo.currentmove == &guncmdr_move_pain5) && (self->s.frame < FRAME_c_pain508))
	{
		return;
	}

	if ((self->monsterinfo.currentmove == &guncmdr_move_pain6) && (self->s.frame < FRAME_c_pain607))
	{
		return;
	}

	AngleVectors(self->s.angles, forward, NULL, NULL);
	VectorClear(from_inflictor);

	if (inflictor)
	{
		VectorSubtract(inflictor->s.origin, self->s.origin, from_inflictor);
		from_inflictor[2] = 0;
		VectorNormalize(from_inflictor);
	}

	if ((fabsf((self->s.origin[2] + self->viewheight) - point[2]) <= 4) && (self->velocity[2] < 65.0f))
	{
		/* off with his head */
		edict_t *head;

		self->monsterinfo.currentmove = &guncmdr_move_death5;
		head = ThrowGib(self, "models/monsters/gunner/gibs/head.md2", damage, GIB_ORGANIC);

		if (head)
		{
			vec3_t head_direction;

			VectorCopy(self->s.angles, head->s.angles);
			VectorCopy(self->s.origin, head->s.origin);
			head->s.origin[2] += 24;
			VectorClear(head_direction);

			if (inflictor)
			{
				VectorSubtract(self->s.origin, inflictor->s.origin, head_direction);
				VectorNormalize(head_direction);
			}

			VectorScale(head_direction, 100, head->velocity);
			head->velocity[2] = 200;
			VectorScale(head->avelocity, 0.15f, head->avelocity);
			gi.linkentity(head);
		}
	}
	else if (DotProduct(from_inflictor, forward) < -0.40f)
	{
		/* from behind: a backwards death */
		which = randk() % ((self->monsterinfo.currentmove == &guncmdr_move_pain6) ? 2 : 3);

		if (which == 0)
		{
			self->monsterinfo.currentmove = &guncmdr_move_death3;
		}
		else if (which == 1)
		{
			self->monsterinfo.currentmove = &guncmdr_move_death7;
		}
		else
		{
			self->monsterinfo.currentmove = &guncmdr_move_pain6;
		}
	}
	else
	{
		which = randk() % ((self->monsterinfo.currentmove == &guncmdr_move_pain5) ? 1 : 2);
		self->monsterinfo.currentmove = (which == 0) ? &guncmdr_move_death4 : &guncmdr_move_pain5;
	}
}

static void
guncmdr_opengun(edict_t *self)
{
	gi.sound(self, CHAN_VOICE, sound_open, 1, ATTN_IDLE, 0);
}

static void
GunnerCmdrFire(edict_t *self)
{
	vec3_t start, forward, right, aim;
	int flash_number, i;

	if (!self || !self->enemy || !self->enemy->inuse)
	{
		return;
	}

	if ((self->s.frame >= FRAME_c_attack401) && (self->s.frame <= FRAME_c_attack505))
	{
		flash_number = MZ2_GUNCMDR_CHAINGUN_2;
	}
	else
	{
		flash_number = MZ2_GUNCMDR_CHAINGUN_1;
	}

	AngleVectors(self->s.angles, forward, right, NULL);
	M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right, start);
	PredictAim(self->enemy, start, 800, false, random() * 0.3f, aim, NULL);

	for (i = 0; i < 3; i++)
	{
		aim[i] += crandom() * 0.025f;
	}

	fire_flechette(self, start, aim, 4, 800, 2);
	monster_muzzleflash2(self, start, flash_number);
}

static mframe_t guncmdr_frames_attack_chain[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, guncmdr_opengun},
	{ai_charge, 0, NULL}};

mmove_t guncmdr_move_attack_chain = {
	FRAME_c_attack101,
	FRAME_c_attack106,
	guncmdr_frames_attack_chain,
	guncmdr_fire_chain};
static mframe_t guncmdr_frames_fire_chain[] = {
	{ai_charge, 0, GunnerCmdrFire},
	{ai_charge, 0, GunnerCmdrFire},
	{ai_charge, 0, GunnerCmdrFire},
	{ai_charge, 0, GunnerCmdrFire},
	{ai_charge, 0, GunnerCmdrFire},
	{ai_charge, 0, GunnerCmdrFire}};

mmove_t guncmdr_move_fire_chain = {
	FRAME_c_attack107,
	FRAME_c_attack112,
	guncmdr_frames_fire_chain,
	guncmdr_refire_chain};
static mframe_t guncmdr_frames_fire_chain_run[] = {
	{ai_charge, 15.f, GunnerCmdrFire},
	{ai_charge, 16.f, GunnerCmdrFire},
	{ai_charge, 20.f, GunnerCmdrFire},
	{ai_charge, 18.f, GunnerCmdrFire},
	{ai_charge, 24.f, GunnerCmdrFire},
	{ai_charge, 13.5f, GunnerCmdrFire}};

mmove_t guncmdr_move_fire_chain_run = {
	FRAME_c_run201,
	FRAME_c_run206,
	guncmdr_frames_fire_chain_run,
	guncmdr_refire_chain};
static mframe_t guncmdr_frames_fire_chain_dodge_right[] = {
	{ai_charge, 5.1f * 2.0f, GunnerCmdrFire},
	{ai_charge, 9.0f * 2.0f, GunnerCmdrFire},
	{ai_charge, 3.5f * 2.0f, GunnerCmdrFire},
	{ai_charge, 3.6f * 2.0f, GunnerCmdrFire},
	{ai_charge, -1.0f * 2.0f, GunnerCmdrFire}};

mmove_t guncmdr_move_fire_chain_dodge_right = {
	FRAME_c_attack401,
	FRAME_c_attack405,
	guncmdr_frames_fire_chain_dodge_right,
	guncmdr_refire_chain};
static mframe_t guncmdr_frames_fire_chain_dodge_left[] = {
	{ai_charge, 5.1f * 2.0f, GunnerCmdrFire},
	{ai_charge, 9.0f * 2.0f, GunnerCmdrFire},
	{ai_charge, 3.5f * 2.0f, GunnerCmdrFire},
	{ai_charge, 3.6f * 2.0f, GunnerCmdrFire},
	{ai_charge, -1.0f * 2.0f, GunnerCmdrFire}};

mmove_t guncmdr_move_fire_chain_dodge_left = {
	FRAME_c_attack501,
	FRAME_c_attack505,
	guncmdr_frames_fire_chain_dodge_left,
	guncmdr_refire_chain};
static mframe_t guncmdr_frames_endfire_chain[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, guncmdr_opengun},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL}};

mmove_t guncmdr_move_endfire_chain = {
	FRAME_c_attack118,
	FRAME_c_attack124,
	guncmdr_frames_endfire_chain,
	guncmdr_run};
static void
GunnerCmdrGrenade(edict_t *self)
{
	vec3_t start, forward, right, up, aim, target;
	int flash_number;
	float spread;
	qboolean mortar, crouch;

	if (!self || !self->enemy || !self->enemy->inuse)
	{
		return;
	}

	switch (self->s.frame)
	{
		case FRAME_c_attack205:
			spread = -0.1f;
			flash_number = MZ2_GUNCMDR_GRENADE_MORTAR_1;
			break;
		case FRAME_c_attack208:
			spread = 0.0f;
			flash_number = MZ2_GUNCMDR_GRENADE_MORTAR_2;
			break;
		case FRAME_c_attack211:
			spread = 0.1f;
			flash_number = MZ2_GUNCMDR_GRENADE_MORTAR_3;
			break;
		case FRAME_c_attack304:
			spread = -0.1f;
			flash_number = MZ2_GUNCMDR_GRENADE_FRONT_1;
			break;
		case FRAME_c_attack307:
			spread = 0.0f;
			flash_number = MZ2_GUNCMDR_GRENADE_FRONT_2;
			break;
		case FRAME_c_attack310:
			spread = 0.1f;
			flash_number = MZ2_GUNCMDR_GRENADE_FRONT_3;
			break;
		case FRAME_c_attack911:
			spread = 0.25f;
			flash_number = MZ2_GUNCMDR_GRENADE_CROUCH_1;
			break;
		case FRAME_c_attack912:
			spread = 0.0f;
			flash_number = MZ2_GUNCMDR_GRENADE_CROUCH_2;
			break;
		case FRAME_c_attack913:
			spread = -0.25f;
			flash_number = MZ2_GUNCMDR_GRENADE_CROUCH_3;
			break;
		default:
			return;
	}

	mortar = (flash_number >= MZ2_GUNCMDR_GRENADE_MORTAR_1) && (flash_number <= MZ2_GUNCMDR_GRENADE_MORTAR_3);
	crouch = (flash_number >= MZ2_GUNCMDR_GRENADE_CROUCH_1) && (flash_number <= MZ2_GUNCMDR_GRENADE_CROUCH_3);

	/* firing blind and still can't see the enemy: at the blind fire target */
	if ((self->monsterinfo.aiflags & AI_MANUAL_STEERING) && !visible(self, self->enemy))
	{
		if (VectorCompare(self->monsterinfo.blind_fire_target, vec3_origin))
		{
			return;
		}

		VectorCopy(self->monsterinfo.blind_fire_target, target);
	}
	else
	{
		VectorCopy(self->enemy->s.origin, target);
	}

	AngleVectors(self->s.angles, forward, right, up);
	M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right, start);

	if (!crouch)
	{
		float dist, pitch;

		VectorSubtract(target, self->s.origin, aim);
		dist = VectorLength(aim);

		/* aim up if they're on the same level and far away */
		if ((dist > 512) && (aim[2] < 64) && (aim[2] > -64))
		{
			aim[2] += (dist - 512);
		}

		VectorNormalize(aim);
		pitch = Q_clamp(aim[2], -0.5f, 0.4f);

		if (mortar && ((self->enemy->absmin[2] - self->absmax[2]) > 16.0f))
		{
			pitch += 0.5f;
		}

		if (!mortar)
		{
			pitch -= 0.05f; /* front grenades */
		}

		VectorMA(forward, spread, right, aim);
		VectorMA(aim, pitch, up, aim);
		VectorNormalize(aim);
	}
	else
	{
		PredictAim(self->enemy, start, 800, false, 0.0f, aim, NULL);
		VectorMA(aim, spread, right, aim);
		VectorNormalize(aim);
	}

	if (crouch)
	{
		/* three ion rippers fanned out */
		const float inner_spread = 0.125f;
		int i;

		for (i = 0; i < 3; i++)
		{
			vec3_t dir;

			VectorMA(aim, -(inner_spread * 2) + (inner_spread * (i + 1)), right, dir);
			fire_ionripper(self, start, dir, 15, 800, EF_IONRIPPER);
		}

		monster_muzzleflash2(self, start, flash_number);
	}
	else
	{
		float speed = mortar ? MORTAR_SPEED : GRENADE_SPEED;

		/* a pitch that lands it, if there is one */
		M_CalculatePitchToFire(self, target, start, aim, speed, 2.5f, mortar, false);
		monster_fire_grenade(self, start, aim, 50, speed, flash_number);
	}
}

static mframe_t guncmdr_frames_attack_mortar[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, GunnerCmdrGrenade},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, GunnerCmdrGrenade},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, GunnerCmdrGrenade},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, monster_duck_up},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL}};

mmove_t guncmdr_move_attack_mortar = {
	FRAME_c_attack201,
	FRAME_c_attack221,
	guncmdr_frames_attack_mortar,
	guncmdr_run};
static void
guncmdr_grenade_mortar_resume(edict_t *self)
{
	self->monsterinfo.currentmove = &guncmdr_move_attack_mortar;
	self->monsterinfo.attack_state = AS_STRAIGHT;
	self->s.frame = self->count;
}

static mframe_t guncmdr_frames_attack_mortar_dodge[] = {
	{ai_charge, 11.f, NULL},
	{ai_charge, 12.f, NULL},
	{ai_charge, 16.f, NULL},
	{ai_charge, 16.f, NULL},
	{ai_charge, 12.f, NULL},
	{ai_charge, 11.f, NULL}};

mmove_t guncmdr_move_attack_mortar_dodge = {
	FRAME_c_duckstep01,
	FRAME_c_duckstep06,
	guncmdr_frames_attack_mortar_dodge,
	guncmdr_grenade_mortar_resume};
static mframe_t guncmdr_frames_attack_back[] = {
	{ai_charge, -2.f, NULL},
	{ai_charge, -1.5f, NULL},
	{ai_charge, -0.5f, GunnerCmdrGrenade},
	{ai_charge, -6.0f, NULL},
	{ai_charge, -4.f, NULL},
	{ai_charge, -2.5f, GunnerCmdrGrenade},
	{ai_charge, -7.0f, NULL},
	{ai_charge, -3.5f, NULL},
	{ai_charge, -1.1f, GunnerCmdrGrenade},
	{ai_charge, -4.6f, NULL},
	{ai_charge, 1.9f, NULL},
	{ai_charge, 1.0f, NULL},
	{ai_charge, -4.5f, NULL},
	{ai_charge, 3.2f, NULL},
	{ai_charge, 4.4f, NULL},
	{ai_charge, -6.5f, NULL},
	{ai_charge, -6.1f, NULL},
	{ai_charge, 3.0f, NULL},
	{ai_charge, -0.7f, NULL},
	{ai_charge, -1.0f, NULL}};

mmove_t guncmdr_move_attack_grenade_back = {
	FRAME_c_attack302,
	FRAME_c_attack321,
	guncmdr_frames_attack_back,
	guncmdr_run};
static void
guncmdr_grenade_back_dodge_resume(edict_t *self)
{
	self->monsterinfo.currentmove = &guncmdr_move_attack_grenade_back;
	self->monsterinfo.attack_state = AS_STRAIGHT;
	self->s.frame = self->count;
}

static mframe_t guncmdr_frames_attack_grenade_back_dodge_right[] = {
	{ai_charge, 5.1f * 2.0f, NULL},
	{ai_charge, 9.0f * 2.0f, NULL},
	{ai_charge, 3.5f * 2.0f, NULL},
	{ai_charge, 3.6f * 2.0f, NULL},
	{ai_charge, -1.0f * 2.0f, NULL}};

mmove_t guncmdr_move_attack_grenade_back_dodge_right = {
	FRAME_c_attack601,
	FRAME_c_attack605,
	guncmdr_frames_attack_grenade_back_dodge_right,
	guncmdr_grenade_back_dodge_resume};
static mframe_t guncmdr_frames_attack_grenade_back_dodge_left[] = {
	{ai_charge, 5.1f * 2.0f, NULL},
	{ai_charge, 9.0f * 2.0f, NULL},
	{ai_charge, 3.5f * 2.0f, NULL},
	{ai_charge, 3.6f * 2.0f, NULL},
	{ai_charge, -1.0f * 2.0f, NULL}};

mmove_t guncmdr_move_attack_grenade_back_dodge_left = {
	FRAME_c_attack701,
	FRAME_c_attack705,
	guncmdr_frames_attack_grenade_back_dodge_left,
	guncmdr_grenade_back_dodge_resume};
static void
guncmdr_kick_finished(edict_t *self)
{
	self->monsterinfo.melee_debounce_time = level.time + 3;
	self->monsterinfo.attack(self);
}

static void
guncmdr_kick(edict_t *self)
{
	vec3_t aim = {MELEE_DISTANCE, 0, -32};

	if (fire_hit(self, aim, 15, 400))
	{
		if (self->enemy && self->enemy->client && (self->enemy->velocity[2] < 270.0f))
		{
			self->enemy->velocity[2] = 270.0f;
		}
	}
}

static mframe_t guncmdr_frames_attack_kick[] = {
	{ai_charge, -7.7f, NULL},
	{ai_charge, -4.9f, NULL},
	{ai_charge, 12.6f, guncmdr_kick},
	{ai_charge, 0, NULL},
	{ai_charge, -3.0f, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, -4.1f, NULL},
	{ai_charge, 8.6f, NULL}};

mmove_t guncmdr_move_attack_kick = {
	FRAME_c_attack801,
	FRAME_c_attack808,
	guncmdr_frames_attack_kick,
	guncmdr_kick_finished};
void
guncmdr_attack(edict_t *self)
{
	vec3_t forward, right, start, aim;
	float distance;

	if (!self || !self->enemy)
	{
		return;
	}

	monster_done_dodge(self);

	distance = guncmdr_range_to(self, self->enemy);
	AngleVectors(self->s.angles, forward, right, NULL);
	VectorSubtract(self->enemy->s.origin, self->s.origin, aim);
	VectorNormalize(aim);

	if (!self->bad_area && (distance < RANGE_GUNCMDR_MELEE) && (self->monsterinfo.melee_debounce_time < level.time))
	{
		/* kick close enemies */
		self->monsterinfo.currentmove = &guncmdr_move_attack_kick;
		return;
	}

	if (self->bad_area || (((distance <= RANGE_GUNCMDR_GRENADE) || (randk() & 1)) &&
							  M_CheckClearShot(self, monster_flash_offset[MZ2_GUNCMDR_CHAINGUN_1], NULL)))
	{
		self->monsterinfo.currentmove = &guncmdr_move_attack_chain;
		return;
	}

	/* far away, or far above or below: mortar */
	M_ProjectFlashSource(self, monster_flash_offset[MZ2_GUNCMDR_GRENADE_MORTAR_1], forward, right, start);

	if (((distance >= RANGE_GUNCMDR_MORTAR) || (fabsf(self->absmin[2] - self->enemy->absmax[2]) > 64.0f)) &&
		M_CheckClearShot(self, monster_flash_offset[MZ2_GUNCMDR_GRENADE_MORTAR_1], NULL) &&
		M_CalculatePitchToFire(self, self->enemy->s.origin, start, aim, MORTAR_SPEED, 2.5f, true, false))
	{
		self->monsterinfo.currentmove = &guncmdr_move_attack_mortar;
		monster_duck_down(self);
		return;
	}

	VectorSubtract(self->enemy->s.origin, self->s.origin, aim);
	VectorNormalize(aim);
	M_ProjectFlashSource(self, monster_flash_offset[MZ2_GUNCMDR_GRENADE_FRONT_1], forward, right, start);

	if (M_CheckClearShot(self, monster_flash_offset[MZ2_GUNCMDR_GRENADE_FRONT_1], NULL) &&
		!(self->monsterinfo.aiflags & AI_STAND_GROUND) &&
		M_CalculatePitchToFire(self, self->enemy->s.origin, start, aim, GRENADE_SPEED, 2.5f, false, false))
	{
		self->monsterinfo.currentmove = &guncmdr_move_attack_grenade_back;
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.currentmove = &guncmdr_move_attack_chain;
	}
}

static qboolean
guncmdr_runs_while_firing(edict_t *self)
{
	return !(self->monsterinfo.aiflags & AI_STAND_GROUND) && self->enemy &&
		   (guncmdr_range_to(self, self->enemy) > RANGE_GUNCMDR_CHAIN_RUN) && ai_check_move(self, 8.0f);
}

static void
guncmdr_fire_chain(edict_t *self)
{
	self->monsterinfo.currentmove = guncmdr_runs_while_firing(self) ? &guncmdr_move_fire_chain_run : &guncmdr_move_fire_chain;
}

static void
guncmdr_refire_chain(edict_t *self)
{
	monster_done_dodge(self);
	self->monsterinfo.attack_state = AS_STRAIGHT;

	if (self->enemy && (self->enemy->health > 0) && visible(self, self->enemy) && (random() <= 0.5f))
	{
		guncmdr_fire_chain(self);
		return;
	}

	self->monsterinfo.currentmove = &guncmdr_move_endfire_chain;
}

static void
guncmdr_jump_now(edict_t *self)
{
	vec3_t forward, up;

	monster_jump_start(self);
	AngleVectors(self->s.angles, forward, NULL, up);

	/* a path's leap gets the speed that lands it; else the stock jump */
	if (!monster_nav_leap_velocity(self))
	{
		VectorMA(self->velocity, 100, forward, self->velocity);
		VectorMA(self->velocity, 300, up, self->velocity);
	}
}

static void
guncmdr_jump2_now(edict_t *self)
{
	vec3_t forward, up;

	monster_jump_start(self);
	AngleVectors(self->s.angles, forward, NULL, up);

	if (!monster_nav_leap_velocity(self))
	{
		VectorMA(self->velocity, 150, forward, self->velocity);
		VectorMA(self->velocity, 400, up, self->velocity);
	}
}

static void
guncmdr_jump_wait_land(edict_t *self)
{
	if ((self->groundentity == NULL) && !monster_jump_finished(self))
	{
		self->monsterinfo.nextframe = self->s.frame;
	}
	else
	{
		self->monsterinfo.nextframe = self->s.frame + 1;
	}
}

static mframe_t guncmdr_frames_jump[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, guncmdr_jump_now},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, guncmdr_jump_wait_land},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_jump = {
	FRAME_c_jump01,
	FRAME_c_jump10,
	guncmdr_frames_jump,
	guncmdr_run};
static mframe_t guncmdr_frames_jump2[] = {
	{ai_move, -8, NULL},
	{ai_move, -4, NULL},
	{ai_move, -4, NULL},
	{ai_move, 0, guncmdr_jump2_now},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, guncmdr_jump_wait_land},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}};

mmove_t guncmdr_move_jump2 = {
	FRAME_c_jump01,
	FRAME_c_jump10,
	guncmdr_frames_jump2,
	guncmdr_run};
static void
guncmdr_jump(edict_t *self)
{
	if (!self->enemy)
	{
		return;
	}

	monster_done_dodge(self);

	if (monster_jump_target_z(self) > self->absmin[2]) /* the path's jump, else the enemy */
	{
		self->monsterinfo.currentmove = &guncmdr_move_jump2;
	}
	else
	{
		self->monsterinfo.currentmove = &guncmdr_move_jump;
	}
}

/* the berserker's slam damage (re-release): falls off with the square of
   the distance to the box, players in the air are left alone, those hit
   are knocked up */
static void
guncmdr_slam_radius_damage(const vec3_t point, edict_t *inflictor, edict_t *attacker,
	float damage, float kick, const edict_t *ignore, float radius)
{
	edict_t *ent = NULL;

	while ((ent = findradius(ent, inflictor->s.origin, radius * 2.0f)) != NULL)
	{
		vec3_t closest, offset, dir, hit_point;
		float amount;
		int i;

		if ((ent == ignore) || !ent->takedamage || !CanDamage(ent, inflictor))
		{
			continue;
		}

		if (ent->client && !ent->groundentity)
		{
			continue;
		}

		for (i = 0; i < 3; i++)
		{
			closest[i] = Q_clamp(point[i], ent->s.origin[i] + ent->mins[i], ent->s.origin[i] + ent->maxs[i]);
		}

		VectorSubtract(closest, point, offset);
		amount = Q_min(1.0f, 1.0f - (VectorLength(offset) / radius));

		if (amount <= 0)
		{
			continue;
		}

		amount *= amount;

		VectorSubtract(ent->s.origin, point, dir);
		VectorNormalize(dir);

		/* the point at their feet so they always get knocked up */
		VectorCopy(point, hit_point);
		hit_point[2] = ent->absmin[2];
		T_Damage(ent, inflictor, attacker, dir, hit_point, dir, (int)Q_max(1.0f, damage * amount),
			(int)(kick * amount), DAMAGE_RADIUS, MOD_UNKNOWN);

		if (ent->client && (ent->velocity[2] < 270.0f))
		{
			ent->velocity[2] = 270.0f;
		}
	}
}

static void
GunnerCmdrCounter(edict_t *self)
{
	vec3_t forward, right, start;
	const vec3_t offset = {20, 0, 14};
	trace_t tr;

	AngleVectors(self->s.angles, forward, right, NULL);
	M_ProjectFlashSource(self, offset, forward, right, start);
	tr = gi.trace(self->s.origin, NULL, NULL, start, self, MASK_SOLID);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BERSERK_SLAM);
	gi.WritePosition(tr.endpos);
	gi.WriteDir(forward);
	gi.multicast(tr.endpos, MULTICAST_PHS);

	guncmdr_slam_radius_damage(tr.endpos, self, self, 15, 250, self, 200);
}

static mframe_t guncmdr_frames_duck_attack[] = {
	{ai_move, 3.6f, NULL},
	{ai_move, 5.6f, monster_duck_down},
	{ai_move, 8.4f, NULL},
	{ai_move, 2.0f, monster_duck_hold},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 9.5f, GunnerCmdrCounter},
	{ai_charge, -1.5f, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, monster_duck_up},
	{ai_charge, 0, NULL},
	{ai_charge, 11.f, NULL},
	{ai_charge, 2.0f, NULL},
	{ai_charge, 5.6f, NULL}};

mmove_t guncmdr_move_duck_attack = {
	FRAME_c_attack901,
	FRAME_c_attack919,
	guncmdr_frames_duck_attack,
	guncmdr_run};
void
guncmdr_duck(edict_t *self, float eta /* unused */)
{
	if (!self)
	{
		return;
	}

	if ((self->monsterinfo.currentmove == &guncmdr_move_jump2) ||
		(self->monsterinfo.currentmove == &guncmdr_move_jump))
	{
		self->monsterinfo.aiflags &= ~AI_DUCKED;
		return;
	}

	if ((self->monsterinfo.currentmove == &guncmdr_move_fire_chain_dodge_left) ||
		(self->monsterinfo.currentmove == &guncmdr_move_fire_chain_dodge_right) ||
		(self->monsterinfo.currentmove == &guncmdr_move_attack_grenade_back_dodge_left) ||
		(self->monsterinfo.currentmove == &guncmdr_move_attack_grenade_back_dodge_right) ||
		(self->monsterinfo.currentmove == &guncmdr_move_attack_mortar_dodge))
	{
		/* dodging already: don't duck */
		self->monsterinfo.unduck(self);
		return;
	}

	self->monsterinfo.currentmove = &guncmdr_move_duck_attack;
}

void
guncmdr_sidestep(edict_t *self)
{
	if (!self)
	{
		return;
	}

	/* a dodge of its own while the chaingun fires */
	if ((self->monsterinfo.currentmove == &guncmdr_move_fire_chain) ||
		(self->monsterinfo.currentmove == &guncmdr_move_fire_chain_run))
	{
		self->monsterinfo.currentmove = !self->monsterinfo.lefty ? &guncmdr_move_fire_chain_dodge_right : &guncmdr_move_fire_chain_dodge_left;
		return;
	}

	/* grenades: note the frame, dodge, carry on from there */
	if (self->monsterinfo.currentmove == &guncmdr_move_attack_grenade_back)
	{
		self->count = self->s.frame;
		self->monsterinfo.currentmove = !self->monsterinfo.lefty ? &guncmdr_move_attack_grenade_back_dodge_right : &guncmdr_move_attack_grenade_back_dodge_left;
		return;
	}

	/* mortar: a crouched step */
	if (self->monsterinfo.currentmove == &guncmdr_move_attack_mortar)
	{
		self->count = self->s.frame;
		self->monsterinfo.currentmove = &guncmdr_move_attack_mortar_dodge;
		return;
	}

	if (self->monsterinfo.currentmove == &guncmdr_move_run)
	{
		return; /* the regular sidestep while running */
	}

	self->monsterinfo.aiflags &= ~AI_DODGING;
}

qboolean
guncmdr_blocked(edict_t *self, float dist)
{
	if (!self)
	{
		return false;
	}

	if (blocked_checkplat(self, dist))
	{
		return true;
	}

	if (!(self->spawnflags & SPAWNFLAG_GUNCMDR_NOJUMPING) && blocked_checkjump(self, dist, 192, 40))
	{
		guncmdr_jump(self);
		return true;
	}

	return false;
}

/* model has the commander frames (re-release data) */
qboolean
guncmdr_model_ok(edict_t *self)
{
	return M_ModelNumFrames(self) > FRAME_c_duckdeath29;
}

void
SP_monster_guncmdr_rerelease(edict_t *self)
{
	int i;

	sound_step = 0;
	sound_step2 = 0;
	sound_death = gi.soundindex("guncmdr/gcdrdeath1.wav");
	sound_pain = gi.soundindex("guncmdr/gcdrpain2.wav");
	sound_pain2 = gi.soundindex("guncmdr/gcdrpain1.wav");
	sound_idle = gi.soundindex("guncmdr/gcdridle1.wav");
	sound_open = gi.soundindex("guncmdr/gcdratck1.wav");
	sound_search = gi.soundindex("guncmdr/gcdrsrch1.wav");
	sound_sight = gi.soundindex("guncmdr/sight1.wav");
	gi.soundindex("guncmdr/gcdratck2.wav");
	gi.soundindex("guncmdr/gcdratck3.wav");

	gi.modelindex("models/monsters/gunner/gibs/chest.md2");
	gi.modelindex("models/monsters/gunner/gibs/foot.md2");
	gi.modelindex("models/monsters/gunner/gibs/garm.md2");
	gi.modelindex("models/monsters/gunner/gibs/gun.md2");
	gi.modelindex("models/monsters/gunner/gibs/head.md2");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/gunner/tris.md2");

	/* a quarter bigger than the gunner, unless the map scales it */
	for (i = 0; i < 3; i++)
	{
		if (!self->rrs.scale[i])
		{
			self->rrs.scale[i] = 1.25f;
		}
	}

	VectorSet(self->mins, -16, -16, -24);
	VectorSet(self->maxs, 16, 16, 36);

	self->health = 325 * st.health_multiplier;
	self->gib_health = -175;
	self->mass = 255;

	self->pain = guncmdr_pain;
	self->die = guncmdr_die;

	self->monsterinfo.stand = guncmdr_stand;
	self->monsterinfo.walk = guncmdr_walk;
	self->monsterinfo.run = guncmdr_run;
	self->monsterinfo.dodge = M_MonsterDodge;
	self->monsterinfo.duck = guncmdr_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = guncmdr_sidestep;
	self->monsterinfo.blocked = guncmdr_blocked;
	self->monsterinfo.attack = guncmdr_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = guncmdr_sight;
	self->monsterinfo.search = guncmdr_search;

	self->monsterinfo.power_armor_type = POWER_ARMOR_SHIELD;
	self->monsterinfo.power_armor_power = 200;

	gi.linkentity(self);

	self->monsterinfo.currentmove = &guncmdr_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start(self);

	self->s.skinnum = 2; /* the commander skin (monster_start clears it) */
}
