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
 * optech2: how an entity from the server is drawn, moved here from the
 * engine's CL_AddPacketEntities (cl_entities.c): its animation frame,
 * powerup shells, translucency, rotation, bobbing, linked models (vwep,
 * modelindex2-4), the power screen, flares, beams and lights of its own.
 * The engine still networks and interpolates it (origin, angles, frames);
 * the effects extension hands it here with AddEntityModels.
 *
 * =======================================================================
 */

#include "fx_local.h"

#define DLIGHT_CUTOFF 64 /* as the renderers (vid/header/ref.h) */

/* the engine's BigLong: the int's bytes read as big-endian (colours in
   skinnum). The shared BigLong calls a pointer only Swap_Init sets, and
   the game library never runs it */
static int
FX_BigLong(int value)
{
	unsigned char bytes[4];

	memcpy(bytes, &value, sizeof(bytes));
	return (int)(((unsigned)bytes[0] << 24) | ((unsigned)bytes[1] << 16) |
				 ((unsigned)bytes[2] << 8) | (unsigned)bytes[3]);
}

/* as the engine's Mod_RadiusFromBounds */
static float
FX_RadiusFromBounds(const vec3_t mins, const vec3_t maxs)
{
	vec3_t corner;
	int i;

	for (i = 0; i < 3; i++)
	{
		corner[i] = (fabsf(mins[i]) > fabsf(maxs[i])) ? fabsf(mins[i]) : fabsf(maxs[i]);
	}

	return VectorLength(corner);
}

/* an entity that is ours to look through: lights only */
static void
FX_ViewerEntityLights(const entity_t *ent, unsigned int effects)
{
	if (effects & EF_FLAG1)
	{
		V_AddLight(ent->origin, 225, 1.0f, 0.1f, 0.1f);
	}
	else if (effects & EF_FLAG2)
	{
		V_AddLight(ent->origin, 225, 0.1f, 0.1f, 1.0f);
	}
	else if (effects & EF_TAGTRAIL)
	{
		V_AddLight(ent->origin, 225, 1.0f, 1.0f, 0.0f);
	}
	else if (effects & EF_TRACKERTRAIL)
	{
		V_AddLight(ent->origin, 225, -1.0f, -1.0f, -1.0f);
	}
}

/* the re-release's flashlight: a row of lights to where the beam hits */
static void
FX_Flashlight(const entity_t *ent)
{
	vec3_t forward, end, diff, pos;
	vec3_t mins = {-1, -1, -1}, maxs = {1, 1, 1};
	trace_t trace;
	int len, i;
	float step;

	AngleVectors(ent->angles, forward, NULL, NULL);
	VectorMA(ent->origin, 256, forward, end);
	trace = fxi->Trace(ent->origin, mins, maxs, end, MASK_SHOT);

	/* step back a little, to light the nearest surfaces */
	VectorSubtract(trace.endpos, ent->origin, diff);
	len = VectorNormalize(diff);
	step = (float)len / 16.0f;

	for (i = 0; i < 3; i++)
	{
		diff[i] *= step;
	}

	/* at most 16 lights in a row */
	if (len > 16)
	{
		len = 16;
	}

	VectorCopy(trace.endpos, pos);

	for (i = 0; i < len; i++)
	{
		V_AddLight(pos, 128 * (len - i) / len + 64, 1, 1, 1);
		VectorSubtract(pos, diff, pos);
	}
}

/* the combinations of powerup shells that look bad (Ground Zero's rules) */
static int
FX_ShellColors(int renderfx)
{
	if (strcmp(fxi->CvarString("game"), "rogue") != 0)
	{
		return renderfx;
	}

	/* ditch the half damage shell if any of red, blue or double is on */
	if ((renderfx & RF_SHELL_HALF_DAM) && (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE)))
	{
		renderfx &= ~RF_SHELL_HALF_DAM;
	}

	if (renderfx & RF_SHELL_DOUBLE)
	{
		/* lose the yellow shell if we have a red, blue or green one */
		if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_GREEN))
		{
			renderfx &= ~RF_SHELL_DOUBLE;
		}

		if (renderfx & RF_SHELL_RED)
		{
			/* red turns purple: add blue */
			renderfx |= RF_SHELL_BLUE;
		}
		else if (renderfx & RF_SHELL_BLUE)
		{
			/* blue turns cyan (flashing green); green if it's on already */
			if (renderfx & RF_SHELL_GREEN)
			{
				renderfx &= ~RF_SHELL_BLUE;
			}
			else
			{
				renderfx |= RF_SHELL_GREEN;
			}
		}
	}

	return renderfx;
}

/* a flare (re-release misc_flare): fades with distance, drawn additive */
static void
FX_AddFlare(const optech2_fx_entity_model_t *info, entity_t *ent)
{
	float fade_start = info->modelindex2;
	float fade_end = info->modelindex3;
	float distance;
	vec3_t to_view;

	VectorSubtract(cl.refdef.vieworg, ent->origin, to_view);
	distance = VectorLength(to_view);

	if (distance < fade_start)
	{
		return;
	}

	if (distance > fade_end)
	{
		ent->alpha = 1;
	}
	else
	{
		ent->alpha = (distance - fade_start) / (fade_end - fade_start);
	}

	ent->model = NULL;

	if ((info->renderfx & RF_CUSTOMSKIN) && ((unsigned)info->modelindex < MAX_MODELS))
	{
		ent->model = fxi->ModelForIndex(info->modelindex);
	}

	if (!ent->model)
	{
		ent->model = R_RegisterModel("misc/flare.tga");
	}

	ent->flags = info->renderfx | RF_TRANSLUCENT;
	ent->color = FX_BigLong(info->skinnum) & 0xFFFFFFFF;
	VectorCopy(info->scale, ent->scale);
	V_AddEntity(ent);
}

/* a light with no model (RF_CUSTOM_LIGHT): colour in skinnum, size in frame */
static void
FX_AddCustomLight(const optech2_fx_entity_model_t *info, const entity_t *ent)
{
	int color = info->skinnum ? FX_BigLong(info->skinnum) : -1;

	/* unsigned bytes, or channels over 127 went negative */
	V_AddLight(ent->origin, DLIGHT_CUTOFF + info->frame,
		((unsigned char *)&color)[0] / 255.0f,
		((unsigned char *)&color)[1] / 255.0f,
		((unsigned char *)&color)[2] / 255.0f);
}

qboolean
FX_AddEntityModels(optech2_fx_entity_model_t *info)
{
	unsigned int effects = info->effects;
	int renderfx = info->renderfx;
	float autorotate, autobob;
	int autoanim;
	entity_t ent = {0};

	FX_RefreshState();

	autorotate = anglemod(cl.time * 0.1f); /* bonus items rotate at a fixed rate */
	autoanim = 2 * cl.time / 1000; /* brush models can auto animate their frames */
	autobob = 5 * sinf(cl.time / 400.0f);

	ent.rr_mesh = info->rr_mesh;
	ent.oldframe = info->oldframe;
	ent.backlerp = info->backlerp;
	VectorCopy(info->origin, ent.origin);
	VectorCopy(info->oldorigin, ent.oldorigin);
	info->trails = false;

	/* frame */
	if (effects & EF_ANIM01)
	{
		ent.frame = autoanim & 1;
	}
	else if (effects & EF_ANIM23)
	{
		ent.frame = 2 + (autoanim & 1);
	}
	else if (effects & EF_ANIM_ALL)
	{
		ent.frame = autoanim;
	}
	else if (effects & EF_ANIM_ALLFAST)
	{
		ent.frame = cl.time / 100;
	}
	else
	{
		ent.frame = info->frame;
	}

	/* powerups show as shells */
	if (effects & EF_PENT)
	{
		effects &= ~EF_PENT;
		effects |= EF_COLOR_SHELL;
		renderfx |= RF_SHELL_RED;
	}

	if (effects & EF_QUAD)
	{
		effects &= ~EF_QUAD;
		effects |= EF_COLOR_SHELL;
		renderfx |= RF_SHELL_BLUE;
	}

	if (effects & EF_DOUBLE)
	{
		effects &= ~EF_DOUBLE;
		effects |= EF_COLOR_SHELL;
		renderfx |= RF_SHELL_DOUBLE;
	}

	if (effects & EF_HALF_DAMAGE)
	{
		effects &= ~EF_HALF_DAMAGE;
		effects |= EF_COLOR_SHELL;
		renderfx |= RF_SHELL_HALF_DAM;
	}

	/* a beam has no model to put a shell around (q2arpg's elemental
	   tracers come with one; drawn, it filled the view) */
	if (renderfx & RF_BEAM)
	{
		effects &= ~EF_COLOR_SHELL;
		renderfx &= ~(RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE |
					  RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM);
	}

	if (effects & EF_BOB)
	{
		ent.origin[2] += autobob;
		ent.oldorigin[2] += autobob;
	}

	VectorCopy(ent.origin, info->origin);

	if (renderfx & RF_FLARE)
	{
		FX_AddFlare(info, &ent);
		return true;
	}

	if (renderfx & RF_CUSTOM_LIGHT)
	{
		FX_AddCustomLight(info, &ent);
		return true;
	}

	if (renderfx & RF_BEAM)
	{
		/* a beam starting at our own eye (a re-release game's hitscan
		   tracer starts where the shot does, just under the view) comes
		   out of the view weapon's muzzle instead */
		vec3_t from_eye;

		VectorSubtract(ent.origin, cl.refdef.vieworg, from_eye);

		if (VectorLength(from_eye) < 16.0f)
		{
			float hand = fxi->CvarValue("hand");
			float side = (hand == 1) ? -7.0f : (hand == 2) ? 0.0f
														   : 7.0f;

			VectorMA(cl.refdef.vieworg, 16.0f, cl.v_forward, ent.origin);
			VectorMA(ent.origin, side, cl.v_right, ent.origin);
			VectorMA(ent.origin, -8.0f, cl.v_up, ent.origin);
		}

		ent.alpha = Q_clamp(fxi->CvarValue("cl_laseralpha"), 0.0f, 1.0f);

		/* the four beam colors are encoded in 32 bits of skinnum (hack) */
		ent.skinnum = (info->skinnum >> ((randk() % 4) * 8)) & 0xff;
		ent.model = NULL;
	}
	else
	{
		if (info->modelindex == CUSTOM_PLAYER_MODEL)
		{
			/* the player's own model and skin */
			ent.skinnum = 0;
			fxi->PlayerModel(info->skinnum, (renderfx & RF_USE_DISGUISE) != 0, &ent.model, &ent.skin);
		}
		else
		{
			ent.skinnum = info->skinnum;
			ent.skin = NULL;
			ent.model = fxi->ModelForIndex(info->modelindex);
		}

		VectorCopy(info->scale, ent.scale);
	}

	/* only used for the black hole model right now */
	if ((renderfx & RF_TRANSLUCENT) && !(renderfx & RF_BEAM))
	{
		ent.alpha = 0.70f;
	}

	/* render effects (fullbright, translucent...) go on the shell, if any */
	ent.flags = (effects & EF_COLOR_SHELL) ? 0 : renderfx;

	/* angles */
	if (effects & EF_ROTATE)
	{
		/* some bonus items auto-rotate */
		ent.angles[0] = 0;
		ent.angles[1] = autorotate;
		ent.angles[2] = 0;
	}
	else if (effects & EF_SPINNINGLIGHTS)
	{
		vec3_t forward, start;

		ent.angles[0] = 0;
		ent.angles[1] = anglemod((float)(cl.time / 2)) + info->state_angles[1];
		ent.angles[2] = 180;

		AngleVectors(ent.angles, forward, NULL, NULL);
		VectorMA(ent.origin, 64, forward, start);
		V_AddLight(start, 100, 1, 0, 0);
	}
	else
	{
		VectorCopy(info->angles, ent.angles);
	}

	VectorCopy(ent.angles, info->angles);

	if (info->rr_effects & EF_FLASHLIGHT)
	{
		FX_Flashlight(&ent);
	}

	/* our own player: lights only, the view is inside it */
	if (info->number == cl.playernum + 1)
	{
		FX_ViewerEntityLights(&ent, effects);
		return true;
	}

	/* invisible */
	if (!info->modelindex)
	{
		return true;
	}

	if (effects & EF_BFG)
	{
		ent.flags |= RF_TRANSLUCENT;
		ent.alpha = 0.30f;
	}

	/* its own alpha (re-release s.alpha: fades, teleports out); 0 = not set */
	if ((info->alpha > 0) && (info->alpha < 1))
	{
		ent.flags |= RF_TRANSLUCENT;
		ent.alpha = info->alpha;
	}

	if (info->rr_effects & EF_HOLOGRAM)
	{
		ent.flags |= RF_TRANSLUCENT;
		ent.alpha = info->alpha;
	}

	if (effects & EF_PLASMA)
	{
		ent.flags |= RF_TRANSLUCENT;
		ent.alpha = 0.6f;
	}

	if (effects & EF_SPHERETRANS)
	{
		ent.flags |= RF_TRANSLUCENT;
		ent.alpha = (effects & EF_TRACKERTRAIL) ? 0.6f : 0.3f;
	}

	V_AddEntity(&ent);

	/* a colour shell is a second copy of the model */
	if (effects & EF_COLOR_SHELL)
	{
		ent.flags = FX_ShellColors(renderfx) | RF_TRANSLUCENT;
		ent.alpha = 0.30f;
		V_AddEntity(&ent);
	}

	ent.skin = NULL; /* never a custom skin on the linked models */
	ent.skinnum = 0;
	ent.flags = 0;
	ent.alpha = 0;

	/* linked models */
	if (info->modelindex2)
	{
		if (info->modelindex2 == CUSTOM_PLAYER_MODEL)
		{
			ent.model = fxi->PlayerWeaponModel(info->skinnum);
		}
		else
		{
			ent.model = fxi->ModelForIndex(info->modelindex2);
		}

		/* the defender sphere's shell is see-through */
		if (!Q_strcasecmp(fxi->ModelName(info->modelindex2), "models/items/shell/tris.md2"))
		{
			ent.alpha = 0.32f;
			ent.flags = RF_TRANSLUCENT;
		}

		V_AddEntity(&ent);

		ent.flags = 0;
		ent.alpha = 0;
	}

	if (info->modelindex3)
	{
		ent.model = fxi->ModelForIndex(info->modelindex3);
		V_AddEntity(&ent);
	}

	if (info->modelindex4)
	{
		ent.model = fxi->ModelForIndex(info->modelindex4);
		V_AddEntity(&ent);
	}

	if (effects & EF_POWERSCREEN)
	{
		ent.model = CL_PowerScreenModel();
		ent.oldframe = 0;
		ent.frame = 0;
		ent.flags |= (RF_TRANSLUCENT | RF_SHELL_GREEN);
		ent.alpha = 0.30f;
		V_AddEntity(&ent);
	}

	if (info->rr_effects & EF_HOLOGRAM)
	{
		float radius = 10.0f;
		vec3_t mins, maxs;

		if (fxi->ModelBounds(fxi->ModelName(info->modelindex), mins, maxs))
		{
			radius = FX_RadiusFromBounds(mins, maxs);
		}

		radius *= (info->scale[0] + info->scale[1] + info->scale[2]) / 3.0f;
		CL_HologramParticles(ent.origin, radius);
	}

	info->trails = true;
	return true;
}
