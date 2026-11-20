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
 * optech2: the game's own player movement. The shared movement code
 * (src/common/shared/pmove.c) is compiled in here, with its entry points
 * static, so a game or mod that changes it changes both the server's
 * movement and the client's prediction: the engine asks this library for
 * its movement through OPtech2_GetExtension(OPTECH2_EXT_PMOVE).
 *
 * =======================================================================
 */

#include "header/local.h"
#include "../common/header/optech2_ext.h"

/* the shared movement code, its two entry points kept to this file */
#define PMOVE_API static
#include "../common/shared/pmove.c"

void
G_PmoveEx(pmove_t *pmove, int *origin)
{
	PmoveEx(pmove, origin);
}

void
G_Pmove(pmove_t *pmove)
{
	Pmove(pmove);
}

static const optech2_pmove_ext_t pmove_extension = {
	OPTECH2_PMOVE_EXT_VERSION,
	sizeof(pmove_t),
	G_PmoveEx};

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

	return NULL;
}
