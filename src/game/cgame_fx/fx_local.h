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
 * optech2: the game library's client effects (src/game/cgame_fx/). The
 * code here started as copies of the engine's src/client/cl_tempentities.c,
 * cl_effects.c and cl_particles.c; this header maps the engine calls that
 * code makes onto the effects extension (optech2_fx_import_t), so the
 * copies stay close to the originals. A mod changes effects here.
 *
 * =======================================================================
 */

#ifndef GAME_CGAME_FX_LOCAL_H
#define GAME_CGAME_FX_LOCAL_H

#include "../../common/header/shared.h"
#include "../../common/header/optech2_ext.h"

/* the engine's calls, set by FX_Init */
extern const optech2_fx_import_t *fxi;

#include <limits.h>

#define MAX_CL_ENTNUM SHRT_MAX /* as the engine */
#define NUMVERTEXNORMALS 162 /* as the engine (common.h) */
#define MAX_SUSTAINS 128
#define PARTICLE_GRAVITY 40
#define INSTANT_PARTICLE -10000.0

typedef struct cl_sustain
{
	int id;
	int type;
	int endtime;
	int nextthink;
	int thinkinterval;
	vec3_t org;
	vec3_t dir;
	unsigned int basecolor;
	unsigned int finalcolor;
	int count;
	int magnitude;
	void (*think)(struct cl_sustain *self);
} cl_sustain_t;

/* the engine's client entity, as far as effects use it: its current
   position and angles (CL_AllocEntity asks the engine) */
typedef struct
{
	struct
	{
		vec3_t origin;
		vec3_t angles;
	} current;
	vec3_t lerp_origin; /* trails: where it was drawn the frame before */
	int trailcount; /* trails: diminishing trails */
	int fly_stoptime; /* trails: EF_FLIES */
} centity_t;

centity_t *CL_AllocEntity(int entnum); /* NULL when the engine doesn't know it */

/* the engine's types, by their extension names */
typedef optech2_particle_t cparticle_t;
typedef optech2_dlight_t cdlight_t;
typedef optech2_entity_t entity_t;

/* what the copied code reads from the engine's client state, refreshed
   by FX_RefreshState before each call into this code */
typedef struct
{
	int time; /* cl.time */
	float lerpfrac;
	int playernum;
	vec3_t v_forward, v_right, v_up;
	struct
	{
		vec3_t vieworg;
	} refdef;
	struct
	{
		int servertime;
	} frame;
	vec3_t gunorigin;
} fx_client_state_t;

extern fx_client_state_t fx_cl;
#define cl fx_cl

void FX_RefreshState(void);
void CL_NewFrameTEntSounds(void); /* optech2 */

/* messages: the engine's current temp entity message */
#define MSG_ReadByte(msg) fxi->ReadByte()
#define MSG_ReadShort(msg) fxi->ReadShort()
#define MSG_ReadLong(msg) fxi->ReadLong()
#define MSG_ReadPos(msg, pos, protocol) fxi->ReadPos(pos)
#define MSG_ReadDir(msg, dir) fxi->ReadDir(dir)

/* sounds, models, drawing */
#define S_RegisterSound(name) fxi->RegisterSound(name)
#define S_StartSound(origin, entnum, channel, sfx, volume, attenuation, timeofs) \
	fxi->StartSound(origin, entnum, channel, sfx, volume, attenuation, timeofs)
#define R_RegisterModel(name) fxi->RegisterModel(name)
#define V_AddEntity(ent) fxi->AddEntity(ent)
#define V_AddLight(org, intensity, r, g, b) fxi->AddLight(org, intensity, r, g, b)
#define CL_AllocDlight(key) fxi->AllocDlight(key)

#define Com_Printf fxi->Printf
#define Com_Error(code, ...) fxi->Error(__VA_ARGS__)
#define Com_DPrintf(...)                 \
	do                                   \
	{                                    \
		if (fxi->CvarValue("developer")) \
			fxi->Printf(__VA_ARGS__);    \
	} while (0)
#define Draw_FindPic(name) fxi->RegisterPic(name)

/* fx_tempentities.c */
void CL_SmokeAndFlash(vec3_t origin);

/* fx_muzzleflash.c */
void CL_AddMuzzleFlash(void);
void CL_AddMuzzleFlash2(void);
void CL_AddMuzzleFlash3(void);

/* fx_events.c */
qboolean CL_EntityEvent(int number, int event, const vec3_t origin);

/* fx_tempentities.c (more) */
struct sfx_s *CL_RandomFootstepSfx(void);

/* fx_trails.c */
qboolean FX_EntityTrails(optech2_fx_entity_t *info);

/* fx_effects.c */
cparticle_t *CL_AllocParticle(void);
unsigned int CL_CombineColors(unsigned int basecolor, unsigned int finalcolor, float scale);
void CL_ExplosionParticles(const vec3_t org);
void CL_BigTeleportParticles(const vec3_t org);
void CL_BlasterParticles(const vec3_t org, const vec3_t dir);
void CL_RailTrail(const vec3_t start, const vec3_t end);
void CL_BubbleTrail(const vec3_t start, const vec3_t end);
void CL_BFGExplosionParticles(const vec3_t org);
void CL_TeleportParticles(const vec3_t org);
void CL_FlameEffects(const vec3_t origin);
void CL_Flashlight(int ent, const vec3_t pos);
void CL_ColorFlash(vec3_t pos, int ent, float intensity, float r, float g, float b);
void CL_DebugTrail(const vec3_t start, const vec3_t end);
void CL_ForceWall(const vec3_t start, const vec3_t end, int color);
void CL_BubbleTrail2(vec3_t start, vec3_t end, int dist);
void CL_Heatbeam(vec3_t start, vec3_t forward);
void CL_ParticleSteamEffect(vec3_t org, vec3_t dir, unsigned int basecolor, unsigned int finalcolor,
	int count, int magnitude);
void CL_ParticleSteamEffect2(cl_sustain_t *self);
void CL_MonsterPlasma_Shell(vec3_t origin);
void CL_Widowbeamout(cl_sustain_t *self);
void CL_Nukeblast(cl_sustain_t *self);
void CL_WidowSplash(const vec3_t org);
void CL_PowerSplash(void);
void CL_ColorExplosionParticles(const vec3_t org, unsigned int basecolor, unsigned int finalcolor);
void CL_ParticleSmokeEffect(const vec3_t org, vec3_t dir, unsigned int basecolor, unsigned int finalcolor,
	int count, int magnitude);
void CL_BlasterParticles2(const vec3_t org, const vec3_t dir, unsigned int basecolor, unsigned int finalcolor);
void CL_ParticleEffect(vec3_t org, vec3_t dir, unsigned int basecolor, unsigned int finalcolor, int count);
void CL_ParticleEffect2(vec3_t org, vec3_t dir, unsigned int basecolor, unsigned int finalcolor, int count);
void CL_ParticleEffect3(vec3_t org, vec3_t dir, unsigned int color, int count);

#define VID_PaletteColor(index) fxi->PaletteColor(index)

#endif
