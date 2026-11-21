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

static void
FX_Init(const optech2_fx_import_t *import)
{
	fxi = import;
}

static qboolean
FX_ParseTempEntity(int type)
{
	FX_RefreshState();
	return CL_ParseTEnt(type);
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
}

const optech2_fx_ext_t fx_extension = {
	OPTECH2_FX_EXT_VERSION,
	FX_Init,
	FX_ParseTempEntity,
	FX_RegisterMedia,
	FX_AddFrameEffects,
	FX_ClearEffects};
