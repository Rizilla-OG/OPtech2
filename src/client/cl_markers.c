/*
 * Copyright (C) 2026 OpenTech2 contributors
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
 * optech2: the re-release's markers. The compass's trail (svc_help_path):
 * an arrow (models/objects/pointer) at each point the game sends along the
 * way to the objective, pointing on, fading after a few seconds. Points of
 * interest (svc_poi: the objective, coop pings): an icon on screen where
 * the point is, held at the screen's edge while it is out of view.
 *
 * =======================================================================
 */

#include "header/client.h"

#define MAX_HELP_ARROWS 64
#define HELP_ARROW_LIFE_MS 4000
#define HELP_ARROW_FADE_MS 1000
#define MAX_POIS 64
#define POI_FLAG_HIDE_ON_AIM 1
#define HELP_ARROW_SCALE 6 /* the model is about 4 x 6 units */

typedef struct
{
	vec3_t origin;
	vec3_t angles;
	int start; /* cls.realtime when it showed up */
} help_arrow_t;

typedef struct
{
	int key; /* the game's id: replaces the marker with the same key */
	vec3_t origin;
	int image;
	int color; /* palette index */
	int flags;
	int expires; /* cls.realtime */
} poi_t;

static help_arrow_t help_arrows[MAX_HELP_ARROWS];
static int help_arrow_count;
static poi_t pois[MAX_POIS];

void
CL_Markers_Clear(void)
{
	help_arrow_count = 0;
	memset(pois, 0, sizeof(pois));
}

void
CL_Markers_HelpPath(qboolean first, const vec3_t pos, const vec3_t dir)
{
	help_arrow_t *arrow;

	/* a new trail replaces the old one */
	if (first)
	{
		help_arrow_count = 0;
	}

	if (help_arrow_count >= MAX_HELP_ARROWS)
	{
		memmove(help_arrows, help_arrows + 1, sizeof(help_arrows[0]) * (MAX_HELP_ARROWS - 1));
		help_arrow_count--;
	}

	arrow = &help_arrows[help_arrow_count++];
	VectorCopy(pos, arrow->origin);
	/* yaw and pitch along the trail (dir is normalized) */
	arrow->angles[YAW] = atan2f(dir[1], dir[0]) * 180.0f / (float)M_PI;
	arrow->angles[PITCH] = -atan2f(dir[2], sqrtf(dir[0] * dir[0] + dir[1] * dir[1])) * 180.0f / (float)M_PI;
	arrow->angles[ROLL] = 0;
	arrow->start = cls.realtime;
}

void
CL_Markers_POI(int key, int time_ms, const vec3_t pos, int image, int color, int flags)
{
	poi_t *slot = NULL;
	int i;

	for (i = 0; i < MAX_POIS; i++)
	{
		if (pois[i].expires > cls.realtime && pois[i].key == key)
		{
			slot = &pois[i];
			break;
		}
	}

	/* a free (or the soonest to go) slot */
	for (i = 0; !slot && i < MAX_POIS; i++)
	{
		if (pois[i].expires <= cls.realtime)
		{
			slot = &pois[i];
		}
	}

	if (!slot)
	{
		slot = &pois[0];
	}

	slot->key = key;
	VectorCopy(pos, slot->origin);
	slot->image = image;
	slot->color = color;
	slot->flags = flags;
	slot->expires = cls.realtime + ((time_ms > 0) ? time_ms : 0);
}

/* the compass trail's arrows, in the world */
void
CL_Markers_AddEntities(void)
{
	struct model_s *model;
	int i;

	if (!help_arrow_count)
	{
		return;
	}

	model = R_RegisterModel("models/objects/pointer/tris.md2");

	if (!model)
	{
		return;
	}

	for (i = 0; i < help_arrow_count; i++)
	{
		const help_arrow_t *arrow = &help_arrows[i];
		int age = cls.realtime - arrow->start;
		entity_t ent;

		if (age >= HELP_ARROW_LIFE_MS)
		{
			continue;
		}

		memset(&ent, 0, sizeof(ent));
		ent.model = model;
		VectorCopy(arrow->origin, ent.origin);
		ent.origin[2] += 2; /* just off the floor */
		VectorCopy(ent.origin, ent.oldorigin);
		VectorCopy(arrow->angles, ent.angles);
		VectorSet(ent.scale, HELP_ARROW_SCALE, HELP_ARROW_SCALE, HELP_ARROW_SCALE);
		ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
		ent.alpha = (age > HELP_ARROW_LIFE_MS - HELP_ARROW_FADE_MS) ? (float)(HELP_ARROW_LIFE_MS - age) / HELP_ARROW_FADE_MS : 1.0f;
		V_AddEntity(&ent);
	}
}

/* points of interest, on screen */
void
CL_Markers_Draw(void)
{
	float scale = SCR_GetHUDScale();
	int i;

	for (i = 0; i < MAX_POIS; i++)
	{
		const poi_t *poi = &pois[i];
		const char *name;
		unsigned rgb;
		vec3_t color;
		float x, y, center_x, center_y, half_w, half_h;
		int w = 0, h = 0, size_w, size_h;
		qboolean on_screen;

		if (poi->expires <= cls.realtime)
		{
			continue;
		}

		name = (poi->image > 0 && poi->image < MAX_IMAGES) ? cl.configstrings[CS_IMAGES + poi->image] : "";

		if (!name[0])
		{
			name = "marker";
		}

		Draw_GetPicSize(&w, &h, name);

		if (w <= 0 || h <= 0)
		{
			continue;
		}

		size_w = (int)(w * scale);
		size_h = (int)(h * scale);
		center_x = cl.refdef.x + cl.refdef.width * 0.5f;
		center_y = cl.refdef.y + cl.refdef.height * 0.5f;
		half_w = cl.refdef.width * 0.5f - size_w;
		half_h = cl.refdef.height * 0.5f - size_h;

		on_screen = V_ProjectToScreen(poi->origin, &x, &y) &&
					fabsf(x - center_x) <= half_w && fabsf(y - center_y) <= half_h;

		if (!on_screen)
		{
			/* hold it at the edge, in the point's direction */
			vec3_t forward, right, up, delta;
			float dx, dy, fit;

			AngleVectors(cl.refdef.viewangles, forward, right, up);
			VectorSubtract(poi->origin, cl.refdef.vieworg, delta);
			dx = DotProduct(delta, right);
			dy = -DotProduct(delta, up);

			if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f)
			{
				dy = 1;
			}

			fit = fminf(half_w / fmaxf(fabsf(dx), 0.001f), half_h / fmaxf(fabsf(dy), 0.001f));
			x = center_x + dx * fit;
			y = center_y + dy * fit;
		}
		else if ((poi->flags & POI_FLAG_HIDE_ON_AIM) &&
				 (fabsf(x - center_x) < size_w * 2) && (fabsf(y - center_y) < size_h * 2))
		{
			continue;
		}

		rgb = VID_PaletteColor((byte)poi->color);
		color[0] = (rgb & 0xff) / 255.0f;
		color[1] = ((rgb >> 8) & 0xff) / 255.0f;
		color[2] = ((rgb >> 16) & 0xff) / 255.0f;
		Draw_PicScaledCol((int)(x - size_w / 2), (int)(y - size_h / 2), name, scale, color, NULL);
	}
}
