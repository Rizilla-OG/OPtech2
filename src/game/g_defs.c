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
 * =======================================================================
 *
 * optech2: data-driven definitions (items for now).
 *
 * Each folder in the search path stack can have defs/<folder>.items,
 * read in stack order (baseq2, the fs_mods folders, the game folder), so
 * a later folder's definitions win. A file is a list of blocks, written
 * like map entities:
 *
 *   // a stronger shells box
 *   {
 *   "classname" "ammo_shells"
 *   "quantity" "20"
 *   }
 *
 *   // a new item: starts as a copy of "base", then the fields below
 *   {
 *   "classname" "weapon_boomstick"
 *   "base" "weapon_shotgun"
 *   "pickup_name" "Boomstick"
 *   "ammo" "Bullets"
 *   }
 *
 * A block for an existing classname changes those fields of it; a new
 * classname needs "base", an existing item it copies (behaviour
 * included: pickup, use, firing). New items spawn in maps by their
 * classname. Fields: pickup_name, pickup_sound, world_model, view_model,
 * icon, count_width, quantity, ammo, gives (ammo pickups add to this item,
 * by pickup name), flags (names, see item_flag_names), rotate (0/1),
 * precaches; for weapons: damage, kick, pellets, speed, spread_h,
 * spread_v, radius_damage, damage_radius (weapon_field_names; which ones a
 * weapon uses depends on how it fires).
 *
 * Items are read once, when the game library starts, so item numbers stay
 * the same for a whole game. Saves store items by number: change the item
 * files and older saves may give the wrong items.
 */

#include <ctype.h>
#include <stddef.h>
#include "header/local.h"

#define DEFS_MAX_FOLDERS 20
#define DEFS_MAX_STRINGS 4096

/* strings the definitions point to, freed when items are read again */
static char *defs_strings[DEFS_MAX_STRINGS];
static int defs_string_count;

static const struct
{
	const char *name;
	int flag;
} item_flag_names[] = {
	{"weapon", IT_WEAPON},
	{"ammo", IT_AMMO},
	{"armor", IT_ARMOR},
	{"stay_coop", IT_STAY_COOP},
	{"key", IT_KEY},
	{"powerup", IT_POWERUP},
	{"melee", IT_MELEE},
	{"not_giveable", IT_NOT_GIVEABLE},
	{"instant_use", IT_INSTANT_USE},
	{"tech", IT_TECH},
	{"health", IT_HEALTH},
	{"flag", IT_FLAG},
};

/*
 * The folders of the search path stack, lowest first: baseq2, the fs_mods
 * folders in order, then the game folder; each name once.
 */
int
G_ModFolders(char folders[][MAX_QPATH], int max_folders)
{
	char list[MAX_OSPATH];
	char *cursor;
	const char *names[DEFS_MAX_FOLDERS + 2];
	int name_count = 0, count = 0, i, j;

	names[name_count++] = "baseq2";
	Q_strlcpy(list, gi.cvar("fs_mods", "", 0)->string, sizeof(list));
	cursor = list;

	while (*cursor && name_count < DEFS_MAX_FOLDERS)
	{
		cursor += strspn(cursor, " ,;");

		if (!*cursor)
		{
			break;
		}

		names[name_count++] = cursor;
		cursor += strcspn(cursor, " ,;");

		if (*cursor)
		{
			*cursor++ = '\0';
		}
	}

	names[name_count++] = gi.cvar("game", "", 0)->string;

	for (i = 0; i < name_count && count < max_folders; i++)
	{
		qboolean repeated = false;

		if (!names[i][0] || strstr(names[i], "..") || strchr(names[i], '/') || strchr(names[i], '\\'))
		{
			continue;
		}

		for (j = 0; j < count; j++)
		{
			if (!Q_stricmp(folders[j], names[i]))
			{
				repeated = true;
			}
		}

		if (!repeated)
		{
			Q_strlcpy(folders[count++], names[i], MAX_QPATH);
		}
	}

	return count;
}

/* "folder" lower case into "prefix<folder>suffix" */
void
G_ModFolderFile(char *out, size_t size, const char *prefix, const char *folder, const char *suffix)
{
	size_t i;

	Com_sprintf(out, size, "%s%s%s", prefix, folder, suffix);

	for (i = 0; out[i]; i++)
	{
		out[i] = tolower((unsigned char)out[i]);
	}
}

static const char *
Defs_String(const char *text)
{
	char *copy;

	if (defs_string_count == DEFS_MAX_STRINGS)
	{
		gi.dprintf("defs: too many strings, ignoring \"%s\"\n", text);
		return NULL;
	}

	copy = strdup(text);
	defs_strings[defs_string_count++] = copy;
	return copy;
}

static void
Defs_FreeStrings(void)
{
	int i;

	for (i = 0; i < defs_string_count; i++)
	{
		free(defs_strings[i]);
	}

	defs_string_count = 0;
}

static gitem_t *
Defs_FindItem(const char *classname, int item_count)
{
	int i;

	for (i = 1; i < item_count; i++)
	{
		if (itemlist[i].classname && !Q_stricmp(itemlist[i].classname, classname))
		{
			return &itemlist[i];
		}
	}

	return NULL;
}

static qboolean
Defs_ParseInteger(const char *text, int *value)
{
	char *end;
	long number = strtol(text, &end, 10);

	if (end == text || *end)
	{
		return false;
	}

	*value = (int)number;
	return true;
}

static int
Defs_ParseFlags(const char *where, const char *text)
{
	char list[256];
	char *cursor = list, *name;
	int flags = 0;
	size_t i;

	Q_strlcpy(list, text, sizeof(list));

	while (*cursor)
	{
		cursor += strspn(cursor, " ,|");

		if (!*cursor)
		{
			break;
		}

		name = cursor;
		cursor += strcspn(cursor, " ,|");

		if (*cursor)
		{
			*cursor++ = '\0';
		}

		for (i = 0; i < ARRLEN(item_flag_names); i++)
		{
			if (!Q_stricmp(item_flag_names[i].name, name))
			{
				flags |= item_flag_names[i].flag;
				break;
			}
		}

		if (i == ARRLEN(item_flag_names))
		{
			gi.dprintf("%s: unknown flag \"%s\"\n", where, name);
		}
	}

	return flags;
}

static const struct
{
	const char *name;
	int bit;
	size_t offset;
} weapon_field_names[] = {
	{"damage", WDEF_DAMAGE, offsetof(weapon_def_t, damage)},
	{"kick", WDEF_KICK, offsetof(weapon_def_t, kick)},
	{"pellets", WDEF_PELLETS, offsetof(weapon_def_t, pellets)},
	{"speed", WDEF_SPEED, offsetof(weapon_def_t, speed)},
	{"spread_h", WDEF_SPREAD_H, offsetof(weapon_def_t, spread_h)},
	{"spread_v", WDEF_SPREAD_V, offsetof(weapon_def_t, spread_v)},
	{"radius_damage", WDEF_RADIUS_DAMAGE, offsetof(weapon_def_t, radius_damage)},
	{"damage_radius", WDEF_DAMAGE_RADIUS, offsetof(weapon_def_t, damage_radius)},
};

/* a weapon number (damage, speed, ...); false if the key isn't one */
static qboolean
Defs_SetWeaponField(gitem_t *item, const char *where, const char *key, const char *value)
{
	size_t i;
	int number;

	for (i = 0; i < ARRLEN(weapon_field_names); i++)
	{
		if (!Q_stricmp(key, weapon_field_names[i].name))
		{
			break;
		}
	}

	if (i == ARRLEN(weapon_field_names))
	{
		return false;
	}

	if (!Defs_ParseInteger(value, &number) || number < 0 || number > 100000)
	{
		gi.dprintf("%s: \"%s\" needs a whole number from 0 to 100000, not \"%s\"\n", where, key, value);
		return true;
	}

	if (!(item->flags & IT_WEAPON))
	{
		gi.dprintf("%s: \"%s\" is for weapons; this item isn't one\n", where, key);
		return true;
	}

	*(int *)((byte *)&item->weapon + weapon_field_names[i].offset) = number;
	item->weapon.set |= weapon_field_names[i].bit;
	return true;
}

/* one field of an item; false if the key isn't an item field */
static qboolean
Defs_SetItemField(gitem_t *item, const char *where, const char *key, const char *value)
{
	int number;

	if (!Q_stricmp(key, "pickup_name"))
	{
		item->pickup_name = Defs_String(value);
	}
	else if (!Q_stricmp(key, "pickup_sound"))
	{
		item->pickup_sound = value[0] ? Defs_String(value) : NULL;
	}
	else if (!Q_stricmp(key, "world_model"))
	{
		item->world_model = Defs_String(value);
	}
	else if (!Q_stricmp(key, "view_model"))
	{
		item->view_model = value[0] ? Defs_String(value) : NULL;
	}
	else if (!Q_stricmp(key, "icon"))
	{
		item->icon = Defs_String(value);
	}
	else if (!Q_stricmp(key, "ammo"))
	{
		item->ammo = value[0] ? Defs_String(value) : NULL;
	}
	else if (Defs_SetWeaponField(item, where, key, value))
	{
		/* a weapon number */
	}
	else if (!Q_stricmp(key, "gives"))
	{
		item->gives = value[0] ? Defs_String(value) : NULL;
	}
	else if (!Q_stricmp(key, "precaches"))
	{
		item->precaches = Defs_String(value);
	}
	else if (!Q_stricmp(key, "flags"))
	{
		item->flags = Defs_ParseFlags(where, value);
	}
	else if (!Q_stricmp(key, "quantity") || !Q_stricmp(key, "count_width") || !Q_stricmp(key, "rotate"))
	{
		if (!Defs_ParseInteger(value, &number))
		{
			gi.dprintf("%s: \"%s\" needs a whole number, not \"%s\"\n", where, key, value);
		}
		else if (!Q_stricmp(key, "quantity"))
		{
			item->quantity = number;
		}
		else if (!Q_stricmp(key, "count_width"))
		{
			item->count_width = number;
		}
		else if (number)
		{
			item->world_model_flags |= EF_ROTATE;
		}
		else
		{
			item->world_model_flags &= ~EF_ROTATE;
		}
	}
	else
	{
		return false;
	}

	return true;
}

#define DEFS_MAX_KEYS 32

/* reads one { ... } block; false at the end of the file or on an error */
static qboolean
Defs_ParseBlock(char **data, const char *file, int *item_count)
{
	char keys[DEFS_MAX_KEYS][64];
	char values[DEFS_MAX_KEYS][MAX_TOKEN_CHARS];
	const char *token;
	const char *classname = NULL, *base = NULL;
	char where[MAX_QPATH + 80];
	gitem_t *item;
	int key_count = 0, i;

	token = COM_Parse(data);

	if (!*data)
	{
		return false;
	}

	if (strcmp(token, "{"))
	{
		gi.dprintf("%s: expected \"{\", found \"%s\"; skipping the rest of the file\n", file, token);
		return false;
	}

	while (1)
	{
		token = COM_Parse(data);

		if (!*data)
		{
			gi.dprintf("%s: a block is missing its \"}\"\n", file);
			return false;
		}

		if (!strcmp(token, "}"))
		{
			break;
		}

		if (key_count == DEFS_MAX_KEYS)
		{
			gi.dprintf("%s: more than %d fields in a block\n", file, DEFS_MAX_KEYS);
			return false;
		}

		Q_strlcpy(keys[key_count], token, sizeof(keys[0]));
		token = COM_Parse(data);

		if (!*data || !strcmp(token, "}"))
		{
			gi.dprintf("%s: \"%s\" has no value\n", file, keys[key_count]);
			return false;
		}

		Q_strlcpy(values[key_count], token, sizeof(values[0]));

		if (!Q_stricmp(keys[key_count], "classname"))
		{
			classname = values[key_count];
		}
		else if (!Q_stricmp(keys[key_count], "base"))
		{
			base = values[key_count];
		}

		key_count++;
	}

	if (!classname)
	{
		gi.dprintf("%s: a block has no \"classname\"; skipped\n", file);
		return true;
	}

	Com_sprintf(where, sizeof(where), "%s, %s", file, classname);
	item = Defs_FindItem(classname, *item_count);

	if (!item)
	{
		const gitem_t *base_item = base ? Defs_FindItem(base, *item_count) : NULL;

		if (!base_item)
		{
			gi.dprintf("%s: a new item needs \"base\", an existing item to copy%s; skipped\n", where,
				base ? " (that one doesn't exist)" : "");
			return true;
		}

		if (*item_count >= MAX_ITEMS - 1)
		{
			gi.dprintf("%s: more than %d items; skipped\n", where, MAX_ITEMS - 1);
			return true;
		}

		item = &itemlist[(*item_count)++];
		*item = *base_item;
		item->classname = Defs_String(classname);
		item->pickup_name = item->classname; /* until it gets its own */
	}
	else if (base)
	{
		gi.dprintf("%s: \"base\" only applies to new items; ignored\n", where);
	}

	for (i = 0; i < key_count; i++)
	{
		if (!Q_stricmp(keys[i], "classname") || !Q_stricmp(keys[i], "base"))
		{
			continue;
		}

		if (!Defs_SetItemField(item, where, keys[i], values[i]))
		{
			gi.dprintf("%s: unknown field \"%s\"\n", where, keys[i]);
		}
	}

	return true;
}

/*
 * Reads every folder's defs/<folder>.items into itemlist. item_count is
 * the number of items so far (index 0 is the empty one); returns the new
 * count.
 */
int
Defs_LoadItems(int item_count)
{
	char folders[DEFS_MAX_FOLDERS][MAX_QPATH];
	char name[MAX_QPATH + 16];
	int folder_count, i, before;

	Defs_FreeStrings();
	folder_count = G_ModFolders(folders, DEFS_MAX_FOLDERS);

	for (i = 0; i < folder_count; i++)
	{
		char *buffer, *text, *data;
		int length;

		G_ModFolderFile(name, sizeof(name), "defs/", folders[i], ".items");
		length = gi.LoadFile(name, (void **)&buffer);

		if (length <= 0 || !buffer)
		{
			continue;
		}

		/* the file isn't zero terminated */
		text = malloc(length + 1);

		if (!text)
		{
			gi.FreeFile(buffer);
			continue;
		}

		memcpy(text, buffer, length);
		text[length] = '\0';
		gi.FreeFile(buffer);

		before = item_count;
		data = text;

		while (Defs_ParseBlock(&data, name, &item_count))
		{
		}

		gi.dprintf("Item definitions: %s (%d new)\n", name, item_count - before);
		free(text);
	}

	/* "gives" names an item by its pickup name: check now all are read */
	for (i = 1; i < item_count; i++)
	{
		int j;

		if (!itemlist[i].gives)
		{
			continue;
		}

		for (j = 1; j < item_count; j++)
		{
			if (itemlist[j].pickup_name && !Q_stricmp(itemlist[j].pickup_name, itemlist[i].gives))
			{
				break;
			}
		}

		if (j == item_count)
		{
			gi.dprintf("defs: %s: \"gives\" \"%s\" isn't an item's pickup_name\n", itemlist[i].classname,
				itemlist[i].gives);
		}
	}

	return item_count;
}
