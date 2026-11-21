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
#define OPTECH2_FX_EXT_VERSION 1

struct sfx_s;

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

typedef struct
{
	int version; /* OPTECH2_FX_EXT_VERSION */

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
} optech2_fx_import_t;

typedef struct
{
	int version; /* OPTECH2_FX_EXT_VERSION */

	/* once, after the library is opened */
	void (*Init)(const optech2_fx_import_t *import);

	/* a temp entity of this type arrived; read its data and return true,
	   or read nothing and return false to leave it to the engine */
	qboolean (*ParseTempEntity)(int type);
} optech2_fx_ext_t;

#endif
