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

#endif
