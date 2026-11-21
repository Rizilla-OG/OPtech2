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
 * optech2: additions this game library offers the engine beyond the game
 * and client game APIs (src/common/header/optech2_ext.h): its own player
 * movement (src/common/shared/pmove.c, compiled into this library), so the
 * client predicts with the same code the server moves players with, and its
 * client effects (src/game/cgame_fx/).
 *
 * =======================================================================
 */

#include "header/local.h"
#include "../common/header/optech2_ext.h"

extern const optech2_fx_ext_t fx_extension; /* cgame_fx/fx_main.c */

static const optech2_pmove_ext_t pmove_extension = {
	OPTECH2_PMOVE_EXT_VERSION,
	sizeof(pmove_t),
	PmoveEx
};

/*
 * OPtech2 additions the game library offers the engine, by name; NULL for
 * names it doesn't know. Exported next to GetGameAPI and GetCGameAPI.
 */
Q2_DLL_EXPORTED void *
OPtech2_GetExtension(const char *name)
{
	if (name && !strcmp(name, OPTECH2_EXT_PMOVE))
	{
		return (void *)&pmove_extension;
	}

	if (name && !strcmp(name, OPTECH2_EXT_FX))
	{
		return (void *)&fx_extension;
	}

	return NULL;
}
