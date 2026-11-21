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
 * optech2: additions a game library offers the engine beyond the game and
 * client game APIs. The library exports
 *
 *   void *OPtech2_GetExtension(const char *name);
 *
 * which returns the extension's struct for a name it knows, or NULL. Each
 * struct starts with its version; the engine checks it before use.
 *
 * =======================================================================
 */

#ifndef COMMON_OPTECH2_EXT_H
#define COMMON_OPTECH2_EXT_H

#include "shared.h"

typedef void *(*OPtech2_GetExtension_t)(const char *name);

/* the game's player movement, used for client prediction so it matches
   the server's movement in that game */
#define OPTECH2_EXT_PMOVE "optech2_pmove"
#define OPTECH2_PMOVE_EXT_VERSION 1

typedef struct
{
	int version; /* OPTECH2_PMOVE_EXT_VERSION */
	int pmove_size; /* sizeof(pmove_t) the game was built with */
	void (*PmoveEx)(pmove_t *pmove, int *origin);
} optech2_pmove_ext_t;

/*
 * Client effects: the game library draws effects the server sends as
 * temporary entities (svc_temp_entity). The engine offers it the calls in
 * optech2_fx_import_t; the library returns optech2_fx_ext_t. A temp entity
 * goes to the library first, so it can add its own types and replace stock
 * ones; types it doesn't handle get the engine's built-in effects.
 */
#define OPTECH2_EXT_FX "optech2_fx"
#define OPTECH2_FX_EXT_VERSION 5

struct sfx_s;
struct model_s;
struct image_s;

/* a particle: same layout as the engine's cparticle_t. The engine moves it
   (vel, accel) and fades it (alphavel per second) until alpha reaches 0. */
typedef struct optech2_particle_s
{
	struct optech2_particle_s *next; /* engine's list, don't touch */
	float time; /* when it was made (Time()) */
	vec3_t org;
	vec3_t vel;
	vec3_t accel;
	unsigned color; /* 0xAABBGGRR */
	float alpha;
	float alphavel;
} optech2_particle_t;

/* a dynamic light: same layout as the engine's cdlight_t */
typedef struct
{
	int key; /* so entities can reuse the same light */
	vec3_t color;
	vec3_t origin;
	float radius;
	float die; /* Time() in msec when it goes out */
	float decay; /* radius dropped per second */
	float minlight;
} optech2_dlight_t;

/* a model to draw this frame: same layout as the engine's entity_t */
typedef struct
{
	struct model_s *model;
	float angles[3];
	float origin[3]; /* also RF_BEAM's "from" */
	int frame; /* also RF_BEAM's diameter */
	float oldorigin[3]; /* also RF_BEAM's "to" */
	int oldframe;
	float backlerp; /* 0.0 = current, 1.0 = old */
	int skinnum; /* also RF_BEAM's palette index */
	vec3_t scale;
	unsigned rr_mesh;
	int lightstyle;
	float alpha; /* with RF_TRANSLUCENT */
	unsigned color;
	struct image_s *skin; /* NULL: the model's own */
	int flags; /* RF_* */
} optech2_entity_t;

/* an entity with effect flags, for its trail and flying light */
typedef struct
{
	int number;
	unsigned int effects; /* EF_* */
	int renderfx; /* RF_* */
	int frame;
	vec3_t oldorigin; /* where it was drawn the frame before */
	vec3_t origin; /* in/out: where it is drawn now */
	vec3_t angles;
	int trailcount; /* in/out: diminishing trails (gibs, grenades) */
	int fly_stoptime; /* in/out: EF_FLIES */
} optech2_fx_entity_t;

/* the client state effects need, for the frame being drawn */
typedef struct
{
	int time; /* client time, msec */
	int servertime; /* time of the current server frame, msec */
	float lerpfrac; /* between the previous and the current server frame */
	int playernum; /* our player's entity number - 1 */
	vec3_t vieworg, v_forward, v_right, v_up;
	vec3_t gunorigin; /* view origin plus the gun offset, for beams from our weapon */
} optech2_fx_state_t;

typedef struct
{
	int version; /* OPTECH2_FX_EXT_VERSION */

	void (*Printf)(const char *fmt, ...);
	void (*Error)(const char *fmt, ...); /* drops to the console */

	/* the rest of the current temp entity message, after its type byte */
	int (*ReadByte)(void);
	int (*ReadShort)(void);
	int (*ReadLong)(void);
	float (*ReadFloat)(void);
	void (*ReadPos)(vec3_t pos); /* a position as the server wrote it (gi.WritePosition) */
	void (*ReadDir)(vec3_t dir); /* a direction (gi.WriteDir) */

	/* client time in msec, and a random number 0..0x7fff */
	int (*Time)(void);
	int (*Rand)(void);

	/* effects; NULL when the particle list is full */
	optech2_particle_t *(*AllocParticle)(void);
	optech2_dlight_t *(*AllocDlight)(int key);

	struct sfx_s *(*RegisterSound)(const char *name);
	void (*StartSound)(const vec3_t origin, int entnum, int channel, struct sfx_s *sfx,
		float volume, float attenuation, float timeofs);

	/* the world only (no entities) */
	trace_t (*Trace)(const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, int contentmask);

	void (*GetState)(optech2_fx_state_t *state);
	/* an entity's current position and angles; false: not known */
	qboolean (*EntityState)(int entnum, vec3_t origin, vec3_t angles);
	float (*CvarValue)(const char *name);
	unsigned (*PaletteColor)(byte index); /* the game palette as 0xAABBGGRR */

	struct model_s *(*RegisterModel)(const char *name);
	void (*RegisterPic)(const char *name); /* precache only */
	void (*AddEntity)(const optech2_entity_t *ent); /* this frame only */
	void (*AddLight)(const vec3_t org, float intensity, float r, float g, float b); /* this frame only */
} optech2_fx_import_t;

typedef struct
{
	int version; /* OPTECH2_FX_EXT_VERSION */

	/* once, after the library is opened */
	void (*Init)(const optech2_fx_import_t *import);

	/* a temp entity of this type arrived; read its data and return true,
	   or read nothing and return false to leave it to the engine */
	qboolean (*ParseTempEntity)(int type);

	/* a muzzle flash message arrived: kind 1 = svc_muzzleflash (players),
	   2 = svc_muzzleflash2, 3 = svc_muzzleflash3 (monsters). Read it and
	   return true, or read nothing and return false to leave it to the
	   engine */
	qboolean (*ParseMuzzleFlash)(int kind);

	/* an entity with effect flags is drawn this frame: add its trail and
	   flying light and return true, or return false to leave them to the
	   engine */
	qboolean (*EntityTrails)(optech2_fx_entity_t *ent);

	/* an entity's event (EV_FOOTSTEP, EV_ITEM_RESPAWN...) arrived: play it
	   and return true, or return false to leave it to the engine */
	qboolean (*EntityEvent)(int number, int event, const vec3_t origin);

	/* register models and sounds; at each level load */
	void (*RegisterMedia)(void);

	/* add this frame's effects (explosions, beams, lasers...) */
	void (*AddFrameEffects)(void);

	/* forget all effects (level change, disconnect) */
	void (*ClearEffects)(void);
} optech2_fx_ext_t;

#endif
