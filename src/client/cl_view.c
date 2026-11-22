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
 *  =======================================================================
 *
 * This file implements the camera, e.g the player's view
 *
 * =======================================================================
 */

#include "header/client.h"
#include "../common/header/nav.h" /* optech2: cl_shownav */
#include "../common/header/debugdraw.h" /* optech2: game debug drawing */
#include "input/header/input.h"

/* development tools for weapons */
int gun_frame;
struct model_s *gun_model;

cvar_t *crosshair;
cvar_t *crosshair_3d;
cvar_t *crosshair_3d_glow;

cvar_t *crosshair_scale;
cvar_t *crosshair_color_r;
cvar_t *crosshair_color_g;
cvar_t *crosshair_color_b;
cvar_t *cl_testparticles;
cvar_t *cl_shownav; /* optech2: draw the navigation mesh (src/common/nav) */
cvar_t *cl_testentities;
cvar_t *cl_testlights;
cvar_t *cl_testblend;
cvar_t *crosshair_3d_glow_r;
cvar_t *crosshair_3d_glow_g;
cvar_t *crosshair_3d_glow_b;

cvar_t *cl_stats;

static int r_numdlights;
static dlight_t r_dlights[MAX_DLIGHTS];

static int r_numentities;
static entity_t r_entities[MAX_ENTITIES];

static int r_numparticles;
static particle_t r_particles[MAX_PARTICLES];

static lightstyle_t r_lightstyles[MAX_LIGHTSTYLES];

char cl_weaponmodels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
int num_cl_weaponmodels;

/*
 * Specifies the model that will be used as the world
 */
static void
V_ClearScene(void)
{
	r_numdlights = 0;
	r_numentities = 0;
	r_numparticles = 0;
}

void
V_AddEntity(const entity_t *ent)
{
	if (r_numentities >= MAX_ENTITIES)
	{
		Com_DPrintf("%s: game sends more than expected %d entities\n",
			__func__, MAX_ENTITIES);
		return;
	}

	r_entities[r_numentities++] = *ent;
}

/* optech2: the navigation mesh's edges near the view as dots, every 12
   units, 2 units above the floor. Only where the server runs in this
   process (single player, listen server): the mesh is the server's. */
static void
V_ShowNavEdge(const float *a, const float *b, void *user)
{
	vec3_t point;
	float length, step;
	int *budget = user;

	length = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));

	for (step = 0; step <= length && *budget > 0; step += 12, (*budget)--)
	{
		float fraction = (length > 0) ? step / length : 0;

		point[0] = a[0] + (b[0] - a[0]) * fraction;
		point[1] = a[1] + (b[1] - a[1]) * fraction;
		point[2] = a[2] + (b[2] - a[2]) * fraction + 2;
		V_AddParticle(point, 0xff30ff30, 1.0f);
	}
}

/* a path asked to be drawn (a request's draw_time, nav_path): magenta (it
   stands out from both the floor and the green mesh), every 4 units, in
   two rows */
static void
V_ShowNavPathSegment(const float *a, const float *b, void *user)
{
	vec3_t point;
	float length, step;
	int *budget = user;

	length = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));

	for (step = 0; step <= length && *budget > 1; step += 4, (*budget) -= 2)
	{
		float fraction = (length > 0) ? step / length : 0;

		point[0] = a[0] + (b[0] - a[0]) * fraction;
		point[1] = a[1] + (b[1] - a[1]) * fraction;
		point[2] = a[2] + (b[2] - a[2]) * fraction + 6;
		V_AddParticle(point, 0xffff30ff, 1.0f);
		point[2] += 4;
		V_AddParticle(point, 0xffff30ff, 1.0f);
	}
}

/* optech2: a line the game's debug drawing asked for, dotted in its
   color; long lines get sparser dots so a few don't take every particle */
static void
V_ShowDebugLine(const float *a, const float *b, unsigned int color, qboolean depth_test, void *user)
{
	vec3_t point;
	float length, step, spacing;
	int *budget = user;

	if (!depth_test)
	{
		return; /* drawn on top of everything by V_DrawDebugOverlay */
	}

	length = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
	spacing = (length > 256) ? length / 64 : 4;

	for (step = 0; step <= length && *budget > 0; step += spacing, (*budget)--)
	{
		float fraction = (length > 0) ? step / length : 0;

		point[0] = a[0] + (b[0] - a[0]) * fraction;
		point[1] = a[1] + (b[1] - a[1]) * fraction;
		point[2] = a[2] + (b[2] - a[2]) * fraction;
		V_AddParticle(point, color | 0xff000000, ((color >> 24) & 0xff) / 255.0f);
	}
}

/* optech2: where a world point shows on screen, for the debug overlay;
   false behind the view */
static qboolean
V_ProjectDebugPoint(const float *origin, float *x, float *y)
{
	vec3_t forward, right, up, delta;
	float depth;

	AngleVectors(cl.refdef.viewangles, forward, right, up);
	VectorSubtract(origin, cl.refdef.vieworg, delta);
	depth = DotProduct(delta, forward);

	if (depth < 1)
	{
		return false;
	}

	*x = cl.refdef.x + cl.refdef.width * 0.5f * (1 + DotProduct(delta, right) / depth / tanf(cl.refdef.fov_x * (float)M_PI / 360.0f));
	*y = cl.refdef.y + cl.refdef.height * 0.5f * (1 - DotProduct(delta, up) / depth / tanf(cl.refdef.fov_y * (float)M_PI / 360.0f));
	return true;
}

/* optech2: a debug line drawn without the depth test (seen through
   walls): projected and dotted on screen, cut where it passes behind
   the view */
static void
V_DrawDebugOverlayLine(const float *a, const float *b, unsigned int color, qboolean depth_test,
	void *user)
{
	vec3_t forward, right, up, start, end;
	float depth_a, depth_b, x0, y0, x1, y1, length, step, rgba[4];
	float size = SCR_GetConsoleScale() * 2;
	int *budget = user;

	if (depth_test)
	{
		return; /* a particle line, V_ShowDebugLine */
	}

	AngleVectors(cl.refdef.viewangles, forward, right, up);
	depth_a = DotProduct(a, forward) - DotProduct(cl.refdef.vieworg, forward);
	depth_b = DotProduct(b, forward) - DotProduct(cl.refdef.vieworg, forward);

	if (depth_a < 1 && depth_b < 1)
	{
		return;
	}

	VectorCopy(a, start);
	VectorCopy(b, end);

	/* cut the part behind the view off */
	if (depth_a < 1)
	{
		VectorLerp(a, b, (1 - depth_a) / (depth_b - depth_a), start);
	}
	else if (depth_b < 1)
	{
		VectorLerp(b, a, (1 - depth_b) / (depth_a - depth_b), end);
	}

	if (!V_ProjectDebugPoint(start, &x0, &y0) || !V_ProjectDebugPoint(end, &x1, &y1))
	{
		return;
	}

	rgba[0] = ((color >> 16) & 0xff) / 255.0f;
	rgba[1] = ((color >> 8) & 0xff) / 255.0f;
	rgba[2] = (color & 0xff) / 255.0f;
	rgba[3] = ((color >> 24) & 0xff) / 255.0f;
	length = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));

	for (step = 0; step <= length && *budget > 0; step += size * 2, (*budget)--)
	{
		float fraction = (length > 0) ? step / length : 0;
		float x = x0 + (x1 - x0) * fraction, y = y0 + (y1 - y0) * fraction;

		if (x >= cl.refdef.x && y >= cl.refdef.y && x < cl.refdef.x + cl.refdef.width &&
			y < cl.refdef.y + cl.refdef.height)
		{
			Draw_FillColor((int)x, (int)y, (int)size, (int)size, rgba);
		}
	}
}

/* optech2: a text the game's debug drawing placed in the world, drawn
   centered where it shows on screen (hidden behind walls when it asks
   for depth testing) */
static void
V_DrawDebugText(const float *origin, const char *message, unsigned int color, qboolean depth_test,
	void *user)
{
	vec3_t rgb, delta;
	float x, y, scale = SCR_GetConsoleScale();
	int width;

	(void)user;
	VectorSubtract(origin, cl.refdef.vieworg, delta);

	if (VectorLength(delta) > 2048 || !V_ProjectDebugPoint(origin, &x, &y))
	{
		return;
	}

	if (depth_test && (CM_BoxTrace(cl.refdef.vieworg, (float *)origin, vec3_origin, vec3_origin, 0,
						   MASK_SOLID)
							  .fraction < 1.0f))
	{
		return;
	}

	rgb[0] = ((color >> 16) & 0xff) / 255.0f;
	rgb[1] = ((color >> 8) & 0xff) / 255.0f;
	rgb[2] = (color & 0xff) / 255.0f;
	width = (int)(strlen(message) * 8 * scale);
	Draw_StringScaledCol((int)x - width / 2, (int)(y - 4 * scale), scale, rgb, message);
}

/* optech2: the game's debug drawing that goes on top of the view: lines
   seen through walls and world texts */
void
V_DrawDebugOverlay(void)
{
	int budget = 20000;

	DebugDraw_ForEachLine(V_DrawDebugOverlayLine, &budget);
	DebugDraw_ForEachText(V_DrawDebugText, NULL);
}

static void
V_ShowNav(void)
{
	int budget = MAX_PARTICLES - r_numparticles - 256; /* leave room for the game's own */

	Nav_ForEachDebugSegment(V_ShowNavPathSegment, &budget);
	DebugDraw_ForEachLine(V_ShowDebugLine, &budget);

	if (cl_shownav->value && budget > 0)
	{
		/* cl_shownav 1, 2, 3: the small, medium, large monsters' mesh */
		Nav_ForEachEdge((int)cl_shownav->value - 1, cl.refdef.vieworg, 1024, V_ShowNavEdge, &budget);
	}
}

void
V_AddParticle(vec3_t org, unsigned int color, float alpha)
{
	particle_t *p;

	if (r_numparticles >= MAX_PARTICLES)
	{
		Com_DPrintf("%s: game sends more than expected %d particles\n",
			__func__, MAX_PARTICLES);
		return;
	}

	p = &r_particles[r_numparticles++];
	VectorCopy(org, p->origin);
	p->color = color;
	p->alpha = alpha;
}

void
V_AddLight(vec3_t org, float intensity, float r, float g, float b)
{
	dlight_t *dl;

	if (r_numdlights >= MAX_DLIGHTS)
	{
		Com_DPrintf("%s: game sends more than expected %d dlights\n",
			__func__, MAX_DLIGHTS);
		return;
	}

	dl = &r_dlights[r_numdlights++];
	VectorCopy(org, dl->origin);
	dl->intensity = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
}

static float
smoothstep(float edge0, float edge1, float x)
{
	/* https://registry.khronos.org/OpenGL-Refpages/gl4/html/smoothstep.xhtml */
	float t;

	t = Q_clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/* calculate the fade distance from screen to light */
static float
fade_distance_to_light(float fade_start, float fade_end, const vec3_t light_origin,
	const vec3_t org)
{
	float dist_to_light, frac_to_end, min_frag_dist;
	vec3_t diff;

	if (fade_start <= 1.0f && fade_end <= 1.0f)
	{
		return 1.0f;
	}
	else if (fade_start > fade_end)
	{
		return 1.0f;
	}

	VectorSubtract(org, light_origin, diff);
	dist_to_light = VectorLengthSquared(diff);
	frac_to_end = Q_clamp(dist_to_light / fade_end, 0.0f, 1.0f);
	min_frag_dist = fade_start / fade_end;

	if (min_frag_dist > 1.0f)
	{
		return 1.0f;
	}
	else if (min_frag_dist <= 0)
	{
		return frac_to_end;
	}

	return 1.0f - smoothstep(min_frag_dist, 1.0f, frac_to_end);
}

void
V_AddLightShadow(cl_shadow_light_t *light)
{
	YQ2_ALIGNAS_TYPE(unsigned) byte color[4];
	dlight_t *dl;
	float fade;

	if (r_numdlights >= MAX_DLIGHTS)
	{
		Com_DPrintf("%s: game sends more than expected %d dlights\n",
			__func__, MAX_DLIGHTS);
		return;
	}

	fade = fade_distance_to_light(light->fade_start, light->fade_end,
		light->origin, cl.refdef.vieworg);

	if (fade <= 0.0f)
	{
		return;
	}

	dl = &r_dlights[r_numdlights++];
	VectorCopy(light->origin, dl->origin);

	dl->intensity = light->intensity * fade;
	if (light->lightstyle != -1)
	{
		dl->intensity *= r_lightstyles[light->lightstyle].white;
	}

	*(int *) color = light->color;
	dl->color[0] = color[0] / 255.f;
	dl->color[1] = color[1] / 255.f;
	dl->color[2] = color[2] / 255.f;

	/* TODO: implement:
	 *   radius,
	 *   coneangle,
	 *   conedirection,
	 *   fade_start,
	 *   fade_end */
}

void
V_AddLightStyle(int style, float r, float g, float b)
{
	lightstyle_t *ls;

	if ((style < 0) || (style >= MAX_LIGHTSTYLES))
	{
		Com_Error(ERR_DROP, "Bad light style %i", style);
		return;
	}

	ls = &r_lightstyles[style];

	ls->white = r + g + b;
	ls->rgb[0] = r;
	ls->rgb[1] = g;
	ls->rgb[2] = b;
}

/*
 *If cl_testparticles is set, create 4096 particles in the view
 */
static void
V_TestParticles(void)
{
	int i;

	r_numparticles = MAX_PARTICLES;

	for (i = 0; i < r_numparticles; i++)
	{
		float d, r, u;
		particle_t *p;
		int j;

		d = i * 0.25f;
		r = 4 * ((i & 7) - 3.5f);
		u = 4 * (((i >> 3) & 7) - 3.5f);
		p = &r_particles[i];

		for (j = 0; j < 3; j++)
		{
			p->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * d +
						   cl.v_right[j] * r + cl.v_up[j] * u;
		}

		/* grey */
		p->color = 0xFF7B7B7B;
		p->alpha = cl_testparticles->value;
	}
}

/*
 * If cl_testentities is set, create 32 player models
 */
static void
V_TestEntities(void)
{
	int i;

	r_numentities = 32;
	memset(r_entities, 0, sizeof(r_entities));

	for (i = 0; i < r_numentities; i++)
	{
		entity_t *ent;
		float f, r;
		int j;

		ent = &r_entities[i];

		r = 64.0f * ((float)(i % 4) - 1.5f);
		f = (float)(64 * (i / 4) + 128);

		for (j = 0; j < 3; j++)
		{
			ent->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * f +
							 cl.v_right[j] * r;
		}

		ent->model = cl.baseclientinfo.model;
		ent->skin = cl.baseclientinfo.skin;
	}
}

/*
 * If cl_testlights is set, create test lights in front of the view.
 * optech2: cl_testlights 1 = MAX_DLIGHTS lights; a bigger value = that many lights.
 */
static void
V_TestLights(void)
{
	int i;
	int testLightCount = (int)cl_testlights->value;

	r_numdlights = (testLightCount > 1 && testLightCount < MAX_DLIGHTS) ? testLightCount : MAX_DLIGHTS;
	memset(r_dlights, 0, sizeof(r_dlights));

	for (i = 0; i < r_numdlights; i++)
	{
		dlight_t *dl;
		float f, r;
		int j;

		dl = &r_dlights[i];

		r = 64 * ((i % 4) - 1.5f);
		f = 64 * (i / 4.0f) + 128;

		for (j = 0; j < 3; j++)
		{
			dl->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * f +
							cl.v_right[j] * r;
		}

		dl->color[0] = (float)(((i % 6) + 1) & 1);
		dl->color[1] = (float)((((i % 6) + 1) & 2) >> 1);
		dl->color[2] = (float)((((i % 6) + 1) & 4) >> 2);
		dl->intensity = 200;
	}
}

static void
V_Listlights_f(void)
{
	int i;

	Com_Printf("LigthStyle:\n");
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		lightstyle_t *ls;

		ls = &r_lightstyles[i];

		if (ls->white == 0)
		{
			continue;
		}

		Com_Printf("%d: rgb: (%.2f, %.2f, %.2f)\n",
			i, ls->rgb[0], ls->rgb[1], ls->rgb[2]);
	}

	Com_Printf("DLigths:\n");
	for (i = 0; i < r_numdlights; i++)
	{
		dlight_t *dl;

		dl = &r_dlights[i];

		Com_Printf("%d: intensity: %.2f rgb: (%.2f, %.2f, %.2f) origin: (%.2f, %.2f, %.2f)\n",
			i, dl->intensity, dl->color[0], dl->color[1], dl->color[2],
			dl->origin[0], dl->origin[1], dl->origin[2]
		);
	}

	Com_Printf("DLigths: %d\n", r_numdlights);
}

/* time relative to */
static int sec_start = 0;

static void
CL_PrintInSameLine(const char *message)
{
	char emptyline[80]; /* clear full 25x80 line*/
	float scale;
	int cols, linesize;

	if (developer->value)
	{
		Com_Printf("%s: %.2fs:%s\n",
			__func__, (Sys_Milliseconds() - sec_start) / 1000.0, message);
		return;
	}

	scale = SCR_GetConsoleScale();
	if (scale < 1)
	{
		scale = 1;
	}

	cols = viddef.width / (8 * scale) - 1;

	if (cols > (sizeof(emptyline) - 1))
	{
		cols = sizeof(emptyline) - 1;
	}

	/* go to line start */
	linesize = snprintf(emptyline, cols - 2, "\r%s", message);
	/* fill all with spaces */
	memset(emptyline + linesize, ' ', cols - linesize);
	/* go to begin after print */
	emptyline[cols - 1] = '\r';
	emptyline[cols] = 0;

	Com_Printf("%s", emptyline);
}

void
CL_SetSky(void)
{
	float rotate = 0;
	int count, autorotate = 1;
	vec3_t axis = {0};

	count = sscanf(cl.configstrings[CS_SKYROTATE], "%f %d", &rotate, &autorotate);
	if (count <= 0)
	{
		Com_DPrintf("%s: Unexpected rotate %s\n", __func__, cl.configstrings[CS_SKYROTATE]);
	}

	count = sscanf(cl.configstrings[CS_SKYAXIS], "%f %f %f", &axis[0], &axis[1], &axis[2]);
	if (count != 3)
	{
		Com_DPrintf("%s: Unexpected axis %s\n", __func__, cl.configstrings[CS_SKYAXIS]);
		VectorClear(axis);
	}

	R_SetSky(cl.configstrings[CS_SKY], rotate, autorotate, axis);
}

/*
 * Call before entering a new level, or after changing dlls
 */
void
CL_PrepRefresh(void)
{
	char mapname[MAX_QPATH];
	size_t mapnamelen;
	int i;

	if (strncmp(cl.configstrings[CS_MODELS + 1], "maps/", 5) != 0)
	{
		return;
	}

	sec_start = Sys_Milliseconds();
	SCR_AddDirtyPoint(0, 0);
	SCR_AddDirtyPoint(viddef.width - 1, viddef.height - 1);

	/* let the refresher load the map */
	Q_strlcpy(mapname, cl.configstrings[CS_MODELS + 1] + 5, sizeof(mapname)); /* skip "maps/" */
	mapnamelen = strlen(mapname);

	/* register models, pics, and skins */
	Com_Printf("Map: %s", mapname);
	if (mapnamelen < 4)
	{
		Com_Printf(" is invalid\n");
		return;
	}

	mapname[mapnamelen - 4] = 0; /* cut off ".bsp" */

	SCR_UpdateScreen();
	CL_PrintInSameLine("\nMap is loading...");
	R_BeginRegistration(mapname);

	/* precache status bar pics */
	CL_PrintInSameLine("Pics");
	SCR_UpdateScreen();
	SCR_TouchPics();
	CL_PrintInSameLine("Temporary models");

	CL_RegisterTEntModels();

	num_cl_weaponmodels = 1;
	strcpy(cl_weaponmodels[0], "weapon.md2");

	CL_PrintInSameLine("Models");
	SCR_UpdateScreen();

	for (i = 1; i < MAX_MODELS && cl.configstrings[CS_MODELS + i][0]; i++)
	{
		const char *name;

		name = cl.configstrings[CS_MODELS + i];

		if (developer->value && name[0] != '*')
		{
			CL_PrintInSameLine(name);
			SCR_UpdateScreen();
			IN_Update();
		}

		if (*name == '#')
		{
			/* special player weapon model */
			if (num_cl_weaponmodels < MAX_CLIENTWEAPONMODELS)
			{
				Q_strlcpy(cl_weaponmodels[num_cl_weaponmodels],
						cl.configstrings[CS_MODELS + i] + 1,
						sizeof(cl_weaponmodels[num_cl_weaponmodels]));

				num_cl_weaponmodels++;
			}
		}
		else
		{
			cl.model_draw[i] = R_RegisterModel(cl.configstrings[CS_MODELS + i]);

			cl.model_clip[i] = (*name == '*') ?
				CM_InlineModel(name) :
				NULL;
		}
	}

	CL_PrintInSameLine("Images");
	SCR_UpdateScreen();
	IN_Update();

	for (i = 1; i < MAX_IMAGES && cl.configstrings[CS_IMAGES + i][0]; i++)
	{
		cl.image_precache[i] = Draw_FindPic(cl.configstrings[CS_IMAGES + i]);
	}

	CL_PrintInSameLine("Clients");
	SCR_UpdateScreen();

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (cl.configstrings[CS_PLAYERSKINS + i][0])
		{
			if (developer->value)
			{
				Com_Printf("Client %i\r", i);
				SCR_UpdateScreen();
				IN_Update();
			}

			CL_ParseClientinfo(i);
		}
	}

	/* load shadow lights configstrings */
	for (i = 0; i < MAX_SHADOW_LIGHTS; i++)
	{
		int cs_idx;

		cs_idx = CS_SHADOWLIGHTS + i;
		if (cl.configstrings[cs_idx][0])
		{
			CL_LoadShadowLight(i, cl.configstrings[cs_idx]);
		}
	}

	CL_LoadClientinfo(&cl.baseclientinfo, "unnamed\\male/grunt");

	/* set sky textures and speed */
	CL_PrintInSameLine("Sky");
	SCR_UpdateScreen();
	CL_SetSky();
	CL_PrintInSameLine("Cleanup.....");

	/* the renderer can now free unneeded stuff */
	R_EndRegistration();
	CL_PrintInSameLine("Map loaded.");

	/* clear any lines of console text */
	Con_ClearNotify();

	/* optech2: client game module of the game library, if it has one */
	CL_CGame_Load();
	CL_CGame_ClearNotify(); /* optech2: no kill feed from the last level */
	CL_CGame_NewLevel(); /* optech2: per-level HUD string slots */
	CL_CGame_RegisterEffects(); /* optech2: models are freed between levels */

	SCR_UpdateScreen();
	cl.refresh_prepped = true;
	cl.force_refdef = true; /* make sure we have a valid refdef */

	/* start the cd track */
	OGG_PlayTrack(cl.configstrings[CS_CDTRACK], true, true);
}

float
CalcFov(float fov_x, float width, float height)
{
	float a;
	float x;

	if ((fov_x < 1) || (fov_x > 179))
	{
		Com_Error(ERR_DROP, "Bad fov: %f", fov_x);
		return 90;
	}

	x = width / (float)tan(fov_x / 360 * M_PI);

	a = (float)atan(height / x);

	a = a * 360 / M_PI;

	return a;
}

/* gun frame debugging functions */
static void
V_Gun_Next_f(void)
{
	gun_frame++;
	Com_Printf("frame %i\n", gun_frame);
}

static void
V_Gun_Prev_f(void)
{
	gun_frame--;

	if (gun_frame < 0)
	{
		gun_frame = 0;
	}

	Com_Printf("frame %i\n", gun_frame);
}

static void
V_Gun_Model_f(void)
{
	char name[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		gun_model = NULL;
		return;
	}

	/* optech2: a path with an extension is used as given (any model
	   format), a bare name means models/<name>/tris.md2 as before */
	if (strchr(COM_SkipPath(Cmd_Argv(1)), '.'))
	{
		Q_strlcpy(name, Cmd_Argv(1), sizeof(name));
	}
	else
	{
		Com_sprintf(name, sizeof(name), "models/%s/tris.md2", Cmd_Argv(1));
	}

	gun_model = R_RegisterModel(name);
}

static int
entitycmpfnc(const entity_t *a, const entity_t *b)
{
	/* all other models are sorted by model then skin */
	if (a->model == b->model)
	{
		return (a->skin == b->skin) ? 0 :
			(a->skin > b->skin) ? 1 : -1;
	}
	else
	{
		return (a->model > b->model) ? 1 : -1;
	}
}

static void
V_Render3dCrosshair(void)
{
	crosshair_3d = Cvar_Get("crosshair_3d", "0", CVAR_ARCHIVE);
	crosshair_3d_glow = Cvar_Get("crosshair_3d_glow", "0", CVAR_ARCHIVE);

	if(crosshair_3d->value || crosshair_3d_glow->value)
	{
		trace_t crosshair_trace;
		vec3_t end;

		VectorMA(cl.refdef.vieworg, 8192, cl.v_forward, end);
		crosshair_trace = CL_PMTrace(cl.refdef.vieworg, vec3_origin, vec3_origin, end);

		if(crosshair_3d_glow->value){
			crosshair_3d_glow_r = Cvar_Get("crosshair_3d_glow_r", "5", CVAR_ARCHIVE);
			crosshair_3d_glow_g = Cvar_Get("crosshair_3d_glow_g", "1", CVAR_ARCHIVE);
			crosshair_3d_glow_b = Cvar_Get("crosshair_3d_glow_b", "4", CVAR_ARCHIVE);

			V_AddLight(
				crosshair_trace.endpos,
				crosshair_3d_glow->value,
				crosshair_3d_glow_r->value,
				crosshair_3d_glow_g->value,
				crosshair_3d_glow_b->value
			);
		}

		if(crosshair_3d->value){
			entity_t crosshair_ent = {0};

			crosshair_ent.origin[0] = crosshair_trace.endpos[0];
			crosshair_ent.origin[1] = crosshair_trace.endpos[1];
			crosshair_ent.origin[2] = crosshair_trace.endpos[2];

			crosshair_ent.model = R_RegisterModel("models/crosshair/tris.md2");
			//crosshair_ent.skin = R_RegisterSkin("models/crosshair/skin.pcx");

			AngleVectors2(crosshair_trace.plane.normal, crosshair_ent.angles);
			crosshair_ent.flags = RF_DEPTHHACK | RF_FULLBRIGHT | RF_NOSHADOW;

			V_AddEntity(&crosshair_ent);
		}
	}
}

void
V_RenderView(float stereo_separation)
{
	if (cls.state != ca_active)
	{
		R_EndWorldRenderpass();
		return;
	}

	if (!cl.refresh_prepped)
	{
		R_EndWorldRenderpass();
		return;			// still loading
	}

	if (cl_timedemo->value)
	{
		if (!cl.timedemo_start)
		{
			cl.timedemo_start = Sys_Milliseconds();
		}

		cl.timedemo_frames++;
		CL_TimedemoRecordFrame(); // optech2: frame-time stats for benchmarks
	}

	/* an invalid frame will just use the exact previous refdef
	   we can't use the old frame if the video mode has changed, though... */
	if (cl.frame.valid && (cl.force_refdef || !cl_paused->value))
	{
		cl.force_refdef = false;

		V_ClearScene();

		/* build a refresh entity list and calc cl.sim*
		   this also calls CL_CalcViewValues which loads
		   v_forward, etc. */
		CL_AddEntities();

		// before changing viewport we should trace the crosshair position
		V_Render3dCrosshair();

		if (cl_testparticles->value)
		{
			V_TestParticles();
		}

		V_ShowNav(); /* optech2: paths asked to be drawn; the mesh with cl_shownav */

		if (cl_testentities->value)
		{
			V_TestEntities();
		}

		if (cl_testlights->value)
		{
			V_TestLights();
		}

		if (cl_testblend->value)
		{
			cl.refdef.blend[0] = 1;
			cl.refdef.blend[1] = 0.5;
			cl.refdef.blend[2] = 0.25;
			cl.refdef.blend[3] = 0.5;
		}

		/* offset vieworg appropriately if
		   we're doing stereo separation */

		if (stereo_separation != 0)
		{
			vec3_t tmp;

			VectorScale(cl.v_right, stereo_separation, tmp);
			VectorAdd(cl.refdef.vieworg, tmp, cl.refdef.vieworg);
		}

		/* never let it sit exactly on a node line, because a water plane can
		   dissapear when viewed with the eye exactly on it. the server protocol
		   only specifies to 1/8 pixel, so add 1/16 in each axis */
		cl.refdef.vieworg[0] += 1.0 / 16;
		cl.refdef.vieworg[1] += 1.0 / 16;
		cl.refdef.vieworg[2] += 1.0 / 16;

		cl.refdef.time = cl.time * 0.001f;

		cl.refdef.areabits = cl.frame.areabits;

		if (!cl_add_entities->value)
		{
			r_numentities = 0;
		}

		if (!cl_add_particles->value)
		{
			r_numparticles = 0;
		}

		if (!cl_add_lights->value)
		{
			r_numdlights = 0;
		}

		if (!cl_add_blend->value)
		{
			VectorClear(cl.refdef.blend);
		}

		cl.refdef.num_entities = r_numentities;
		cl.refdef.entities = r_entities;
		cl.refdef.num_particles = r_numparticles;
		cl.refdef.particles = r_particles;
		cl.refdef.num_dlights = r_numdlights;
		cl.refdef.dlights = r_dlights;
		cl.refdef.lightstyles = r_lightstyles;

		cl.refdef.rdflags = cl.frame.playerstate.rdflags;

		/* sort entities for better cache locality */
		qsort(cl.refdef.entities, cl.refdef.num_entities,
				sizeof(cl.refdef.entities[0]), (int (*)(const void *, const void *))
				entitycmpfnc);
	} else if (cl.frame.valid && cl_paused->value && gl1_stereo->value) {
		// We need to adjust the refdef in stereo mode when paused.
		vec3_t tmp;
		CL_CalcViewValues();
		VectorScale( cl.v_right, stereo_separation, tmp );
		VectorAdd( cl.refdef.vieworg, tmp, cl.refdef.vieworg );

		cl.refdef.vieworg[0] += 1.0/16;
		cl.refdef.vieworg[1] += 1.0/16;
		cl.refdef.vieworg[2] += 1.0/16;

		cl.refdef.time = cl.time*0.001;
	}

	cl.refdef.x = scr_vrect.x;
	cl.refdef.y = scr_vrect.y;
	cl.refdef.width = scr_vrect.width;
	cl.refdef.height = scr_vrect.height;
	cl.refdef.fov_y = CalcFov(cl.refdef.fov_x, (float)cl.refdef.width,
				(float)cl.refdef.height);

	R_RenderFrame(&cl.refdef);

	if (cl_stats->value)
	{
		Com_Printf("ent:%i  lt:%i  part:%i\n", r_numentities,
				r_numdlights, r_numparticles);
	}

	if (log_stats->value && (log_stats_file != 0))
	{
		fprintf(log_stats_file, "%i,%i,%i,", r_numentities,
				r_numdlights, r_numparticles);
	}

	SCR_AddDirtyPoint(scr_vrect.x, scr_vrect.y);
	SCR_AddDirtyPoint(scr_vrect.x + scr_vrect.width - 1,
			scr_vrect.y + scr_vrect.height - 1);

	SCR_DrawCrosshair();
}

static void
V_Viewpos_f(void)
{
	Com_Printf("position: %i %i %i, angles: %i %i %i\n",
			(int)cl.refdef.vieworg[0], (int)cl.refdef.vieworg[1],
			(int)cl.refdef.vieworg[2],
			(int)cl.refdef.viewangles[PITCH], (int)cl.refdef.viewangles[YAW],
			(int)cl.refdef.viewangles[ROLL]);
}

void
V_Init(void)
{
	Cmd_AddCommand("gun_next", V_Gun_Next_f);
	Cmd_AddCommand("gun_prev", V_Gun_Prev_f);
	Cmd_AddCommand("gun_model", V_Gun_Model_f);

	Cmd_AddCommand("viewpos", V_Viewpos_f);
	Cmd_AddCommand("listlights", V_Listlights_f);

	crosshair = Cvar_Get("crosshair", "0", CVAR_ARCHIVE);
	crosshair_scale = Cvar_Get("crosshair_scale", "-1", CVAR_ARCHIVE);
	crosshair_color_r = Cvar_Get("crosshair_color_r", "1", CVAR_ARCHIVE);
	crosshair_color_g = Cvar_Get("crosshair_color_g", "1", CVAR_ARCHIVE);
	crosshair_color_b = Cvar_Get("crosshair_color_b", "1", CVAR_ARCHIVE);
	cl_testblend = Cvar_Get("cl_testblend", "0", 0);
	cl_testparticles = Cvar_Get("cl_testparticles", "0", 0);
	cl_shownav = Cvar_Get("cl_shownav", "0", 0);
	cl_testentities = Cvar_Get("cl_testentities", "0", 0);
	cl_testlights = Cvar_Get("cl_testlights", "0", 0);

	cl_stats = Cvar_Get("cl_stats", "0", 0);
}


/* optech2: frame-time stats for timedemo benchmarks. Durations are kept in
   microseconds; frames past the buffer size are ignored. */
#define TIMEDEMO_MAX_FRAMES 131072
static int timedemo_frame_us[TIMEDEMO_MAX_FRAMES];
static int timedemo_recorded_frames;

void
CL_TimedemoRecordFrame(void)
{
	long long now_us = Sys_Microseconds();

	if (cl.timedemo_frames <= 1)
	{
		/* first frame of a new timedemo: start fresh */
		timedemo_recorded_frames = 0;
	}
	else if (timedemo_recorded_frames < TIMEDEMO_MAX_FRAMES)
	{
		timedemo_frame_us[timedemo_recorded_frames++] = (int)(now_us - cl.timedemo_last_frame_us);
	}

	cl.timedemo_last_frame_us = now_us;
}

static int
CL_TimedemoCompareInts(const void *first, const void *second)
{
	return *(const int *)first - *(const int *)second;
}

void
CL_TimedemoPrintFrameStats(void)
{
	int frame_count = timedemo_recorded_frames;
	long long total_us = 0;
	int frame_index;

	if (frame_count < 2)
	{
		return;
	}

	for (frame_index = 0; frame_index < frame_count; frame_index++)
	{
		total_us += timedemo_frame_us[frame_index];
	}
	qsort(timedemo_frame_us, frame_count, sizeof(int), CL_TimedemoCompareInts);

	/* one line, easy for scripts to parse */
	Com_Printf("frametime_ms avg %.3f p50 %.3f p95 %.3f p99 %.3f max %.3f frames %d\n",
		total_us / 1000.0 / frame_count,
		timedemo_frame_us[frame_count / 2] / 1000.0,
		timedemo_frame_us[(frame_count * 95) / 100] / 1000.0,
		timedemo_frame_us[(frame_count * 99) / 100] / 1000.0,
		timedemo_frame_us[frame_count - 1] / 1000.0,
		frame_count);
	timedemo_recorded_frames = 0;
}
