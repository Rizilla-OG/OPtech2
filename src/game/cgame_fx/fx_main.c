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
 * optech2: entry points of the game library's client effects (the
 * effects extension, src/common/header/optech2_ext.h).
 *
 * =======================================================================
 */

#include "fx_local.h"

const optech2_fx_import_t *fxi;
fx_client_state_t fx_cl;

/* fx_tempentities.c */
qboolean CL_ParseTEnt(int type);
void CL_RegisterTEntModels(void);
void CL_RegisterTEntSounds(void);
void CL_AddTEnts(void);
void CL_ClearTEnts(void);
void CL_ClearTEntModels(void);

void
FX_RefreshState(void)
{
	optech2_fx_state_t state;

	fxi->GetState(&state);

	fx_cl.time = state.time;
	fx_cl.frame.servertime = state.servertime;
	fx_cl.lerpfrac = state.lerpfrac;
	fx_cl.playernum = state.playernum;
	VectorCopy(state.vieworg, fx_cl.refdef.vieworg);
	VectorCopy(state.v_forward, fx_cl.v_forward);
	VectorCopy(state.v_right, fx_cl.v_right);
	VectorCopy(state.v_up, fx_cl.v_up);
	VectorCopy(state.gunorigin, fx_cl.gunorigin);
}

centity_t *
CL_AllocEntity(int entnum)
{
	static centity_t entity;

	if (!fxi->EntityState(entnum, entity.current.origin, entity.current.angles))
	{
		return NULL;
	}

	return &entity;
}

static void
FX_Init(const optech2_fx_import_t *import)
{
	fxi = import;
}

static qboolean
FX_ParseTempEntity(int type)
{
	FX_RefreshState();
	CL_NewFrameTEntSounds();
	return CL_ParseTEnt(type);
}

static qboolean
FX_ParseMuzzleFlash(int kind)
{
	FX_RefreshState();

	switch (kind)
	{
		case 1:
			CL_AddMuzzleFlash();
			return true;
		case 2:
			CL_AddMuzzleFlash2();
			return true;
		case 3:
			CL_AddMuzzleFlash3();
			return true;
	}

	return false;
}

static qboolean
FX_EntityEvent(int number, int event, const vec3_t origin)
{
	FX_RefreshState();
	return CL_EntityEvent(number, event, origin);
}

static void
FX_RegisterMedia(void)
{
	CL_RegisterTEntSounds();
	CL_RegisterTEntModels();
}

static void
FX_AddFrameEffects(void)
{
	FX_RefreshState();
	CL_AddTEnts();
}

static void
FX_ClearEffects(void)
{
	CL_ClearTEnts();
	memset(&fx_cl, 0, sizeof(fx_cl)); /* optech2: no client state from the last level */
}

static void
FX_ClearModels(void)
{
	CL_ClearTEntModels();
}

const optech2_fx_ext_t fx_extension = {
	OPTECH2_FX_EXT_VERSION,
	FX_Init,
	FX_ParseTempEntity,
	FX_ParseMuzzleFlash,
	FX_EntityTrails,
	FX_EntityEvent,
	FX_RegisterMedia,
	FX_AddFrameEffects,
	FX_ClearEffects,
	FX_ClearModels,
	FX_AddEntityModels,
	FX_AddViewWeapon,
	FX_AddShadowLight};
