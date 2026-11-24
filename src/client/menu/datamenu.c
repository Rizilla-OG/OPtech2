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
 * optech2: data menus. A menu page is a text file, menus/<name>.menu, in
 * the game folder or a mod folder (the top folder's copy wins), written
 * in the same "key" "value" blocks as the definition files:
 *
 *   {                                  the first block is the page
 *   "banner" "m_banner_options"        a pic above the items, or
 *   "title" "Options"                  a line of text instead
 *   }
 *   {
 *   "type" "slider"                    slider, choice, toggle, action,
 *   "label" "effects volume"           field, label or space
 *   "cvar" "s_volume"
 *   "min" "0" "max" "1" "step" "0.1"
 *   }
 *
 * Page keys: banner or title, "x" (pixels from the screen's middle, menu
 * scale), "y" (the same for the top; centered without it), "if" (when it
 * doesn't hold the page isn't there and the stock menu opens instead),
 * "status" (a status line; $name gives a cvar's value: "dmflags = $dmflags").
 *
 * Items: slider (cvar, min, max, step, format), choice (cvar, options
 * "name=value;name=value", command run after a change; several cvars,
 * space separated, take space separated values: "1 0.5 0"), toggle (a
 * choice of no=0;yes=1), invert (yes makes the cvar negative, keeping its
 * size: m_pitch), action (command, run on enter; "align" "right" lines it
 * up with the other labels), choice with "mask" (only those bits of the
 * cvar change: "mask" "64" "options" "no=0;yes=64" for one dmflags bit),
 * field (cvar, length, committed when the page
 * closes), label (text only), space (a blank line). Any item takes "gap"
 * (extra pixels above it), "x" (pixels right of the item column), "status"
 * (status bar text), "alttext" (a
 * localization key for the label) and "if" (left out when it doesn't
 * hold).
 *
 * "if": names separated by spaces, all must hold, "!name" for not: gamepad,
 * tall (room for the long options page), episodes, levelselect, mods,
 * curl (built with HTTP downloads), ingame, game=<name> (the game cvar;
 * game=baseq2 also when it's empty), or else a cvar (true when not 0).
 *
 * The stock options, multiplayer and game menus ship as pages in
 * baseq2/menus/ (stuff/menus/ in the source).
 *
 * "menu <name>" opens a page. A page named like a stock menu replaces it
 * (menus/options.menu for menu_options, even from inside other menus);
 * "menu_builtin <name>" opens the stock one anyway.
 */

#include <ctype.h>
#include <stddef.h>

#include "../header/client.h"
#include "header/qmenu.h"

/* optech2: from menu.c */
const char *Default_MenuKey(menuframework_s *m, int key);
void M_Banner(const char *name);

#define DATAMENU_MAX_PAGES 16
#define DATAMENU_MAX_ITEMS 64 /* optech2: what a menuframework_s holds */
#define DATAMENU_MAX_OPTIONS 32
#define DATAMENU_LINE_HEIGHT 10

enum
{
	DATAMENU_SLIDER,
	DATAMENU_CHOICE,
	DATAMENU_ACTION,
	DATAMENU_FIELD,
	DATAMENU_LABEL,
	DATAMENU_SPACE
};

/* optech2: one item; the widget comes first, so the widget pointer qmenu
   hands to callbacks is also the item's */
typedef struct
{
	union
	{
		menucommon_s generic; /* optech2: an action or separator is only this */
		menuslider_s slider;
		menulist_s list;
		menufield_s field;
	} widget;

	int kind;
	char label[64];
	char alttext[64];
	char status[128];
	char cvar[128]; /* optech2: a choice may set several, space separated */
	char command[256];
	qboolean invert; /* optech2: "invert": yes makes the cvar negative, keeping its size */
	int mask; /* optech2: "mask": the choice sets only these bits of the cvar (dmflags) */
	char format[16];
	int option_count;
	char option_names[DATAMENU_MAX_OPTIONS][32];
	char option_values[DATAMENU_MAX_OPTIONS][32];
	const char *option_name_list[DATAMENU_MAX_OPTIONS + 1]; /* optech2: NULL ended, for qmenu */
} datamenu_item_t;

typedef struct
{
	char name[MAX_QPATH];
	char banner[MAX_QPATH];
	char title[64];
	int x_offset; /* optech2: "x": pixels (menu scale) from the screen's middle */
	qboolean fixed_y; /* optech2: "y" given: pixels from the screen's middle; else centered */
	int y_offset;
	char status[128]; /* optech2: "status": the page's status line; $cvar expands */
	char status_text[256];
	menuframework_s frame;
	int item_count;
	datamenu_item_t items[DATAMENU_MAX_ITEMS];
} datamenu_page_t;

static datamenu_page_t datamenu_pages[DATAMENU_MAX_PAGES];
static qboolean datamenu_builtin_only; /* optech2: menu_builtin is opening a stock menu */

/* optech2: a key/value pair of the block being read */
typedef struct
{
	char key[64];
	char value[256];
} datamenu_pair_t;

#define DATAMENU_MAX_PAIRS 32

static const char *
DataMenu_Value(const datamenu_pair_t *pairs, int pair_count, const char *key, const char *fallback)
{
	int i;

	for (i = 0; i < pair_count; i++)
	{
		if (!Q_stricmp(pairs[i].key, key))
		{
			return pairs[i].value;
		}
	}

	return fallback;
}

/* optech2: reads one { "key" "value" ... } block; false at the end of the file or on a syntax error */
static qboolean
DataMenu_ReadBlock(char **cursor, const char *file_name, datamenu_pair_t *pairs, int *pair_count)
{
	const char *token = COM_Parse(cursor);

	*pair_count = 0;

	if (!*cursor || !token[0])
	{
		return false;
	}

	if (strcmp(token, "{"))
	{
		Com_Printf("%s: expected {, found \"%s\"\n", file_name, token);
		return false;
	}

	while (1)
	{
		token = COM_Parse(cursor);

		if (!*cursor)
		{
			Com_Printf("%s: missing }\n", file_name);
			return false;
		}

		if (!strcmp(token, "}"))
		{
			return true;
		}

		if (*pair_count < DATAMENU_MAX_PAIRS)
		{
			Q_strlcpy(pairs[*pair_count].key, token, sizeof(pairs[*pair_count].key));
		}

		token = COM_Parse(cursor);

		if (!*cursor)
		{
			Com_Printf("%s: missing value\n", file_name);
			return false;
		}

		if (*pair_count < DATAMENU_MAX_PAIRS)
		{
			Q_strlcpy(pairs[*pair_count].value, token, sizeof(pairs[*pair_count].value));
			(*pair_count)++;
		}
	}
}

/* optech2: "name=value;name=value" (a name alone is its own value) */
static void
DataMenu_ParseOptions(datamenu_item_t *item, const char *text)
{
	char buffer[1024];
	char *entry, *next;

	Q_strlcpy(buffer, text, sizeof(buffer));
	item->option_count = 0;

	for (entry = buffer; entry && *entry && (item->option_count < DATAMENU_MAX_OPTIONS); entry = next)
	{
		char *equals;

		next = strchr(entry, ';');

		if (next)
		{
			*next++ = '\0';
		}

		equals = strchr(entry, '=');

		if (equals)
		{
			*equals = '\0';
			Q_strlcpy(item->option_values[item->option_count], equals + 1, sizeof(item->option_values[0]));
		}
		else
		{
			Q_strlcpy(item->option_values[item->option_count], entry, sizeof(item->option_values[0]));
		}

		Q_strlcpy(item->option_names[item->option_count], entry, sizeof(item->option_names[0]));
		item->option_name_list[item->option_count] = item->option_names[item->option_count];
		item->option_count++;
	}

	item->option_name_list[item->option_count] = NULL;
}

/* optech2: splits "a b c" into up to max words; returns how many */
static int
DataMenu_Words(const char *text, char words[][64], int max)
{
	int count = 0;

	while (*text && (count < max))
	{
		int length = 0;

		while (*text == ' ')
		{
			text++;
		}

		if (!*text)
		{
			break;
		}

		while (*text && (*text != ' '))
		{
			if (length < 63)
			{
				words[count][length++] = *text;
			}

			text++;
		}

		words[count][length] = '\0';
		count++;
	}

	return count;
}

#define DATAMENU_MAX_CHOICE_CVARS 4

/* optech2: the option matching the cvars now: exactly (numbers as numbers),
   else the nearest when all are numbers (crosshair colours), else 0 */
static int
DataMenu_CurrentOption(const datamenu_item_t *item)
{
	char cvars[DATAMENU_MAX_CHOICE_CVARS][64];
	int cvar_count, i, nearest = 0;
	float nearest_distance = -1;

	if (item->invert)
	{
		return (Cvar_VariableValue(item->cvar) < 0) ? 1 : 0;
	}

	if (item->mask)
	{
		int bits = (int)Cvar_VariableValue(item->cvar) & item->mask;

		for (i = 0; i < item->option_count; i++)
		{
			if ((atoi(item->option_values[i]) & item->mask) == bits)
			{
				return i;
			}
		}

		return 0;
	}

	cvar_count = DataMenu_Words(item->cvar, cvars, DATAMENU_MAX_CHOICE_CVARS);

	for (i = 0; i < item->option_count; i++)
	{
		char values[DATAMENU_MAX_CHOICE_CVARS][64];
		int value_count = DataMenu_Words(item->option_values[i], values, DATAMENU_MAX_CHOICE_CVARS);
		qboolean exact = (value_count == cvar_count);
		int j;

		for (j = 0; exact && (j < cvar_count); j++)
		{
			const char *current = Cvar_VariableString(cvars[j]);

			if (strcmp(values[j], current) &&
				!(Q_strisnum(values[j]) && Q_strisnum(current) && (atof(values[j]) == atof(current))))
			{
				exact = false;
			}
		}

		if (exact)
		{
			return i;
		}

		/* optech2: not exact: how near, when all are numbers */
		if (value_count == cvar_count)
		{
			qboolean numeric = true;
			float distance = 0;

			for (j = 0; j < cvar_count; j++)
			{
				const char *current = Cvar_VariableString(cvars[j]);

				if (!Q_strisnum(values[j]) || !Q_strisnum(current))
				{
					numeric = false;
					break;
				}

				distance += (float)((atof(values[j]) - atof(current)) * (atof(values[j]) - atof(current)));
			}

			if (numeric && ((nearest_distance < 0) || (distance < nearest_distance)))
			{
				nearest_distance = distance;
				nearest = i;
			}
		}
	}

	return nearest;
}

static void
DataMenu_ChoiceChanged(void *self)
{
	datamenu_item_t *item = (datamenu_item_t *)self;
	int option = item->widget.list.curvalue;

	if ((option < 0) || (option >= item->option_count))
	{
		return;
	}

	if (item->invert)
	{
		float size = fabsf(Cvar_VariableValue(item->cvar));

		Cvar_SetValue(item->cvar, option ? -size : size);
	}
	else if (item->mask)
	{
		int bits = (int)Cvar_VariableValue(item->cvar) & ~item->mask;

		Cvar_SetValue(item->cvar, (float)(bits | (atoi(item->option_values[option]) & item->mask)));
	}
	else if (item->cvar[0])
	{
		char cvars[DATAMENU_MAX_CHOICE_CVARS][64];
		char values[DATAMENU_MAX_CHOICE_CVARS][64];
		int cvar_count = DataMenu_Words(item->cvar, cvars, DATAMENU_MAX_CHOICE_CVARS);
		int value_count = DataMenu_Words(item->option_values[option], values, DATAMENU_MAX_CHOICE_CVARS);

		if (cvar_count == 1)
		{
			Cvar_Set(cvars[0], item->option_values[option]); /* optech2: one cvar takes the value whole */
		}
		else
		{
			for (int j = 0; (j < cvar_count) && (j < value_count); j++)
			{
				Cvar_Set(cvars[j], values[j]);
			}
		}
	}

	if (item->command[0])
	{
		Cbuf_AddText(item->command);
		Cbuf_AddText("\n");
	}
}

/* optech2: "if": space separated names, all must hold; "!name" for not.
   A name is one of M_MenuCondition's (gamepad, tall, episodes,
   levelselect, ingame) or else a cvar, true when not 0 */
static qboolean
DataMenu_Condition(const char *expression)
{
	char words[8][64];
	int count = DataMenu_Words(expression, words, 8);
	int i;

	for (i = 0; i < count; i++)
	{
		const char *name = words[i];
		qboolean negate = (name[0] == '!');
		qboolean known, holds;

		if (negate)
		{
			name++;
		}

		holds = M_MenuCondition(name, &known);

		if (!known)
		{
			holds = (Cvar_VariableValue(name) != 0);
		}

		if (holds == negate)
		{
			return false;
		}
	}

	return true;
}

/* optech2: the page's status line with each $cvar replaced by its value */
static void
DataMenu_ExpandStatus(datamenu_page_t *page)
{
	const char *in = page->status;
	size_t out = 0;

	while (*in && (out < sizeof(page->status_text) - 1))
	{
		if ((*in == '$') && (isalnum((unsigned char)in[1]) || (in[1] == '_')))
		{
			char name[64];
			size_t length = 0;

			in++;

			while ((isalnum((unsigned char)*in) || (*in == '_')) && (length < sizeof(name) - 1))
			{
				name[length++] = *in++;
			}

			name[length] = '\0';
			out += Q_strlcpy(page->status_text + out, Cvar_VariableString(name), sizeof(page->status_text) - out);

			if (out > sizeof(page->status_text) - 1)
			{
				out = sizeof(page->status_text) - 1;
			}
		}
		else
		{
			page->status_text[out++] = *in++;
		}
	}

	page->status_text[out] = '\0';
}

static void
DataMenu_ActionSelected(void *self)
{
	const datamenu_item_t *item = (const datamenu_item_t *)self;

	if (item->command[0])
	{
		Cbuf_AddText(item->command);
		Cbuf_AddText("\n");
	}
}

/* optech2: fields are committed to their cvars when the page closes */
static void
DataMenu_Close(menuframework_s *frame)
{
	int i;

	for (i = 0; i < DATAMENU_MAX_PAGES; i++)
	{
		datamenu_page_t *page = &datamenu_pages[i];
		int j;

		if (&page->frame != frame)
		{
			continue;
		}

		for (j = 0; j < page->item_count; j++)
		{
			const datamenu_item_t *item = &page->items[j];

			if ((item->kind == DATAMENU_FIELD) && item->cvar[0])
			{
				Cvar_Set(item->cvar, item->widget.field.buffer);
			}
		}
	}
}

static void
DataMenu_Draw(menuframework_s *frame)
{
	const datamenu_page_t *page = (const datamenu_page_t *)((const byte *)frame - offsetof(datamenu_page_t, frame));

	if (page->banner[0])
	{
		M_Banner(page->banner);
	}
	else if (page->title[0])
	{
		float scale = SCR_GetMenuScale();
		int width = (int)strlen(page->title) * 8 * scale;

		Menu_DrawString((viddef.width - width) / 2, frame->y - 30, page->title);
	}

	/* optech2: choices follow their cvars (a command like "reset defaults" may change them) */
	for (int i = 0; i < page->item_count; i++)
	{
		datamenu_item_t *item = (datamenu_item_t *)&page->items[i];

		if ((item->kind == DATAMENU_CHOICE) && item->cvar[0])
		{
			item->widget.list.curvalue = DataMenu_CurrentOption(item);
		}
	}

	if (page->status[0])
	{
		DataMenu_ExpandStatus((datamenu_page_t *)page);
		frame->statusbar = page->status_text;
	}

	Menu_AdjustCursor(frame, 1);
	Menu_Draw(frame);
}

/* optech2: fills an item from its block; false for an unknown type */
static qboolean
DataMenu_BuildItem(datamenu_item_t *item, const datamenu_pair_t *pairs, int pair_count, int y, const char *file_name)
{
	const char *type = DataMenu_Value(pairs, pair_count, "type", "");
	menucommon_s *generic = &item->widget.generic;

	memset(item, 0, sizeof(*item));
	Q_strlcpy(item->label, DataMenu_Value(pairs, pair_count, "label", ""), sizeof(item->label));
	Q_strlcpy(item->alttext, DataMenu_Value(pairs, pair_count, "alttext", ""), sizeof(item->alttext));
	Q_strlcpy(item->status, DataMenu_Value(pairs, pair_count, "status", ""), sizeof(item->status));
	Q_strlcpy(item->cvar, DataMenu_Value(pairs, pair_count, "cvar", ""), sizeof(item->cvar));
	Q_strlcpy(item->command, DataMenu_Value(pairs, pair_count, "command", ""), sizeof(item->command));

	generic->name = item->label;
	generic->alttext = item->alttext[0] ? item->alttext : NULL;
	/* optech2: "x": pixels (menu scale) right of the item column */
	generic->x = (int)(atoi(DataMenu_Value(pairs, pair_count, "x", "0")) * SCR_GetMenuScale());
	generic->y = y;
	generic->statusbar = item->status[0] ? item->status : NULL;

	if (!Q_stricmp(type, "slider"))
	{
		item->kind = DATAMENU_SLIDER;
		generic->type = MTYPE_SLIDER;
		item->widget.slider.cvar = item->cvar;
		item->widget.slider.minvalue = (float)atof(DataMenu_Value(pairs, pair_count, "min", "0"));
		item->widget.slider.maxvalue = (float)atof(DataMenu_Value(pairs, pair_count, "max", "1"));
		item->widget.slider.slidestep = (float)atof(DataMenu_Value(pairs, pair_count, "step", "0.1"));
		Q_strlcpy(item->format, DataMenu_Value(pairs, pair_count, "format", "%.1f"), sizeof(item->format));
		item->widget.slider.printformat = item->format;
	}
	else if (!Q_stricmp(type, "choice") || !Q_stricmp(type, "toggle") || !Q_stricmp(type, "invert"))
	{
		item->kind = DATAMENU_CHOICE;
		item->invert = !Q_stricmp(type, "invert");
		item->mask = atoi(DataMenu_Value(pairs, pair_count, "mask", "0"));
		generic->type = MTYPE_SPINCONTROL;
		generic->callback = DataMenu_ChoiceChanged;
		DataMenu_ParseOptions(item, DataMenu_Value(pairs, pair_count, "options",
										Q_stricmp(type, "choice") ? "no=0;yes=1" : ""));
		item->widget.list.itemnames = item->option_name_list;
		item->widget.list.curvalue = item->cvar[0] ? DataMenu_CurrentOption(item) : 0;
	}
	else if (!Q_stricmp(type, "action"))
	{
		item->kind = DATAMENU_ACTION;
		generic->type = MTYPE_ACTION;
		/* optech2: "align" "right" ends the label at the item column like other labels */
		generic->flags = Q_stricmp(DataMenu_Value(pairs, pair_count, "align", "left"), "right") ? QMF_LEFT_JUSTIFY : 0;
		generic->callback = DataMenu_ActionSelected;
	}
	else if (!Q_stricmp(type, "field"))
	{
		int length = atoi(DataMenu_Value(pairs, pair_count, "length", "20"));

		item->kind = DATAMENU_FIELD;
		generic->type = MTYPE_FIELD;
		Field_InitState(&item->widget.field, item->cvar[0] ? Cvar_VariableString(item->cvar) : "",
			length, length);
	}
	else if (!Q_stricmp(type, "label") || !Q_stricmp(type, "space"))
	{
		item->kind = Q_stricmp(type, "label") ? DATAMENU_SPACE : DATAMENU_LABEL;
		generic->type = MTYPE_SEPARATOR;

		if (item->kind == DATAMENU_SPACE)
		{
			generic->name = NULL;
		}
	}
	else
	{
		Com_Printf("%s: unknown item type \"%s\"\n", file_name, type);
		return false;
	}

	return true;
}

/* optech2: a page slot: the one this name had, or a free one, or the first */
static datamenu_page_t *
DataMenu_PageSlot(const char *name)
{
	int i;

	for (i = 0; i < DATAMENU_MAX_PAGES; i++)
	{
		if (!Q_stricmp(datamenu_pages[i].name, name))
		{
			return &datamenu_pages[i];
		}
	}

	for (i = 0; i < DATAMENU_MAX_PAGES; i++)
	{
		if (!datamenu_pages[i].name[0])
		{
			return &datamenu_pages[i];
		}
	}

	Com_Printf("Too many data menu pages, reusing the first\n");
	return &datamenu_pages[0];
}

/* optech2: loads menus/<name>.menu into a page; NULL if it doesn't exist or is empty */
static datamenu_page_t *
DataMenu_Load(const char *name)
{
	char file_name[MAX_QPATH];
	datamenu_pair_t pairs[DATAMENU_MAX_PAIRS];
	datamenu_page_t *page;
	char *cursor;
	char *text;
	int pair_count, length, y = 0;

	if (!name[0] || strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
	{
		return NULL;
	}

	Com_sprintf(file_name, sizeof(file_name), "menus/%s.menu", name);
	length = FS_LoadFile(file_name, (void **)&text);

	if (length <= 0 || !text)
	{
		return NULL;
	}

	page = DataMenu_PageSlot(name);
	memset(page, 0, sizeof(*page));
	Q_strlcpy(page->name, name, sizeof(page->name));
	cursor = text;

	/* optech2: the page block */
	if (DataMenu_ReadBlock(&cursor, file_name, pairs, &pair_count))
	{
		Q_strlcpy(page->banner, DataMenu_Value(pairs, pair_count, "banner", ""), sizeof(page->banner));
		Q_strlcpy(page->title, DataMenu_Value(pairs, pair_count, "title", ""), sizeof(page->title));
		page->x_offset = atoi(DataMenu_Value(pairs, pair_count, "x", "0"));
		Q_strlcpy(page->status, DataMenu_Value(pairs, pair_count, "status", ""), sizeof(page->status));
		page->fixed_y = (DataMenu_Value(pairs, pair_count, "y", NULL) != NULL);
		page->y_offset = atoi(DataMenu_Value(pairs, pair_count, "y", "0"));

		/* optech2: a page whose "if" doesn't hold isn't there: the stock menu opens instead */
		if (!DataMenu_Condition(DataMenu_Value(pairs, pair_count, "if", "")))
		{
			FS_FreeFile(text);
			page->name[0] = '\0';
			return NULL;
		}
	}

	/* optech2: the items */
	while (cursor && DataMenu_ReadBlock(&cursor, file_name, pairs, &pair_count))
	{
		datamenu_item_t *item;

		if (page->item_count >= DATAMENU_MAX_ITEMS)
		{
			Com_Printf("%s: more than %d items, the rest are left out\n", file_name, DATAMENU_MAX_ITEMS);
			break;
		}

		/* optech2: an item whose "if" doesn't hold is left out, with its line */
		if (!DataMenu_Condition(DataMenu_Value(pairs, pair_count, "if", "")))
		{
			continue;
		}

		y += atoi(DataMenu_Value(pairs, pair_count, "gap", "0"));
		item = &page->items[page->item_count];

		if (DataMenu_BuildItem(item, pairs, pair_count, y, file_name))
		{
			page->item_count++;
			y += DATAMENU_LINE_HEIGHT;
		}
	}

	FS_FreeFile(text);

	if (!page->item_count)
	{
		Com_Printf("%s: no items\n", file_name);
		page->name[0] = '\0';
		return NULL;
	}

	return page;
}

/* optech2: opens the data page of this name; false if there is none */
qboolean
DataMenu_Open(const char *name)
{
	datamenu_page_t *page = DataMenu_Load(name);
	int i;

	if (!page)
	{
		return false;
	}

	page->frame.x = viddef.width / 2 + (int)(page->x_offset * SCR_GetMenuScale());
	page->frame.nitems = 0;
	page->frame.cursor = 0;
	page->frame.draw = DataMenu_Draw;
	page->frame.key = Default_MenuKey;
	page->frame.close = DataMenu_Close;

	for (i = 0; i < page->item_count; i++)
	{
		Menu_AddItem(&page->frame, &page->items[i].widget);
	}

	if (page->fixed_y)
	{
		page->frame.y = viddef.height / (2 * SCR_GetMenuScale()) + page->y_offset; /* optech2: as the stock menus place it */
	}
	else
	{
		Menu_Center(&page->frame);
	}
	Menu_AdjustCursor(&page->frame, 1);
	M_PushMenu(&page->frame);
	return true;
}

/* optech2: called first by each stock menu: true when a data page replaced it */
qboolean
DataMenu_Override(const char *name)
{
	if (datamenu_builtin_only)
	{
		return false;
	}

	return DataMenu_Open(name);
}

static void
DataMenu_Menu_f(void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf("usage: menu <name>   (opens menus/<name>.menu)\n");
		return;
	}

	if (!DataMenu_Open(Cmd_Argv(1)))
	{
		Com_Printf("No menu page menus/%s.menu (or its \"if\" doesn't hold)\n", Cmd_Argv(1));
	}
}

static void
DataMenu_Builtin_f(void)
{
	char command[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf("usage: menu_builtin <name>   (the stock menu, even when a data page replaces it)\n");
		return;
	}

	Com_sprintf(command, sizeof(command), "menu_%s", Cmd_Argv(1));

	if (!Cmd_Exists(command))
	{
		Com_Printf("No stock menu %s\n", Cmd_Argv(1));
		return;
	}

	/* optech2: run it now, so the flag covers only this menu */
	datamenu_builtin_only = true;
	Cmd_ExecuteString(command);
	datamenu_builtin_only = false;
}

void
DataMenu_Init(void)
{
	Cmd_AddCommand("menu", DataMenu_Menu_f);
	Cmd_AddCommand("menu_builtin", DataMenu_Builtin_f);
}
