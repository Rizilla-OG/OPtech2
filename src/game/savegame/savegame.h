/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2011 Knightmare
 * Copyright (C) 2011 Yamagi Burmeister
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
 */
#ifndef SAVEGAME_LOCAL_H
#define SAVEGAME_LOCAL_H

typedef struct
{
	char *funcStr;
	byte *funcPtr;
} fnlist_entry_t;

/*
 * Connects a human readable
 * function signature with
 * the corresponding pointer
 */
typedef struct
{
	const char *name;
	const fnlist_entry_t *start;
	const fnlist_entry_t *end;
} functionList_t;

typedef struct
{
	int ofs;
	const functionList_t *fnlist;
} fplist_entry_t;

/*
 * Maps field offsets to function lists
 * Used for getting the correct function list
 * for the given function pointer field
 */
typedef struct
{
	const fplist_entry_t *start;
	const fplist_entry_t *end;
} fptrList_t;

/*
 * Static context for a struct type
 * Used for de/serializing structs
 */
typedef struct
{
	const field_t *fields_start;
	const field_t *fields_end;

	const fptrList_t *fplist;

	size_t size;
} structdef_t;

/*
 * Connects a human readable
 * mmove_t string with the
 * corresponding pointer
 * */
typedef struct
{
	char	*mmoveStr;
	mmove_t *mmovePtr;
} mmoveList_t;

typedef struct
{
	char ver[32];
	char game[32];
	char os[32];
	char arch[32];
} savegameHeader_t;

/* optech2: tagged savegame format, see tables/savefields.h */
typedef enum
{
	SF_RAW, /* plain data, saved as bytes */
	SF_STRING, /* char pointer, saved as text */
	SF_EDICT, /* edict_t pointer, saved as entity number */
	SF_CLIENT, /* gclient_t pointer, saved as client number */
	SF_ITEM, /* gitem_t pointer, saved as item index */
	SF_FUNCTION, /* function pointer, saved as the function's name */
	SF_MMOVE /* mmove_t pointer, saved as the move's name */
} savefieldkind_t;

typedef struct
{
	const char *name; /* member path, e.g. "monsterinfo.stand" */
	size_t ofs;
	size_t size;
	savefieldkind_t kind;
} savefield_t;

#endif /* SAVEGAME_LOCAL_H */
