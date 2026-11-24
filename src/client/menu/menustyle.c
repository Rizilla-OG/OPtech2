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
 * optech2: the re-release menu look, for every menu (data pages and the
 * stock C menus alike), drawn from the menu's widgets: a dark panel under
 * the banner's place, rows 14 units high with a bar on the selected one,
 * labels left and values right, sliders as bars, fields as boxes, section
 * labels in blue and the status line under the rows, in the TrueType
 * font. A page too tall for the room is drawn smaller; its top stays put.
 *
 * The mouse works it: a pointer follows the mouse while a menu is open
 * (hidden again by a key or the gamepad), hovering selects, a click picks
 * (a choice steps to its next option, wrapping; a click on a slider's bar
 * sets that value), the right button goes back, the wheel moves.
 *
 * On when ui_menustyle is 1, or -1 (the default) on the re-release's data.
 */

#include "../header/client.h"
#include "header/qmenu.h"

#define STYLE_ROW_UNITS 14 /* optech2: a row's height in menu units (classic: 10) */
#define STYLE_PADDING_UNITS 10
#define STYLE_WIDTH_UNITS 380
#define STYLE_SLIDER_UNITS 110

static const float style_panel[4] = {0.0f, 0.0f, 0.0f, 0.70f};
static const float style_edge[4] = {0.95f, 0.60f, 0.15f, 0.60f};
static const float style_bar[4] = {0.95f, 0.55f, 0.10f, 0.28f};
static const float style_accent[4] = {1.00f, 0.65f, 0.15f, 0.95f};
static const float style_slider_back[4] = {1.0f, 1.0f, 1.0f, 0.15f};
static const float style_slider_fill[4] = {1.00f, 0.65f, 0.15f, 0.90f};
static const float style_field_back[4] = {1.0f, 1.0f, 1.0f, 0.10f};
static const float style_label[3] = {0.82f, 0.82f, 0.82f};
static const float style_selected[3] = {1.0f, 1.0f, 1.0f};
static const float style_value[3] = {1.00f, 0.80f, 0.45f};
static const float style_header[3] = {0.55f, 0.72f, 0.95f};
static const float style_dim[3] = {0.45f, 0.45f, 0.45f};
static const float style_status[3] = {0.70f, 0.70f, 0.70f};

/* optech2: the menu pointer, moved by the mouse while a menu is open */
static float style_pointer_x, style_pointer_y;
static qboolean style_pointer_shown; /* optech2: moved since the last keyboard or gamepad key */
static qboolean style_pointer_moved; /* optech2: hover picks the row under it at the next draw */

/* optech2: the row an owner-drawn item is in, for Menu_StyledValue */
static int style_value_right, style_value_y;
static float style_value_scale;

qboolean
Menu_Styled(void)
{
	return DataMenu_Styled();
}

/* optech2: text in a colour at pixel x, y */
static void
Style_Text(int x, int y, float scale, const float *color, const char *text)
{
	vec3_t rgb = {color[0], color[1], color[2]};

	Draw_StringScaledCol(x, y, scale, rgb, text);
}

static int
Style_TextWidth(float scale, const char *text)
{
	return (int)(strlen(text) * Draw_CharAdvance() * scale);
}

/* optech2: an owner-drawn item's value, right-aligned in its row (key bindings) */
void
Menu_StyledValue(const char *text)
{
	Style_Text(style_value_right - Style_TextWidth(style_value_scale, text), style_value_y,
		style_value_scale, style_value, text);
}

/* optech2: can the cursor stop on it */
static qboolean
Style_Selectable(const menucommon_s *item)
{
	return (item->type != MTYPE_SEPARATOR) && !(item->flags & (QMF_INACTIVE | QMF_HIDDEN));
}

/* optech2: a label as shown: its localization, leading spaces dropped */
static const char *
Style_Label(const menucommon_s *item)
{
	const char *label = SV_LocalizationUIMessage(item->alttext, item->name ? item->name : "");

	while (*label == ' ')
	{
		label++;
	}

	return label;
}

/* optech2: a choice's shown value (only its first line) */
static void
Style_ChoiceValue(const menulist_s *list, char *out, size_t size)
{
	int count = 0;
	char *newline;

	if (list->itemnames)
	{
		while (list->itemnames[count])
		{
			count++;
		}
	}

	if (!count || (list->curvalue < 0) || (list->curvalue >= count))
	{
		Q_strlcpy(out, "", size);
		return;
	}

	Q_strlcpy(out, list->itemnames[list->curvalue], size);
	newline = strchr(out, '\n');

	if (newline)
	{
		*newline = '\0';
	}
}

/* optech2: the slider bar's left edge and width in a row ending at right */
static void
Style_SliderBar(float scale, int right, int *bar_x, int *bar_width)
{
	*bar_width = (int)(STYLE_SLIDER_UNITS * scale);
	*bar_x = right - (int)(5 * Draw_CharAdvance() * scale) - (int)(6 * scale) - *bar_width;
}

/* optech2: is the item a value with no label of its own (a heading above names it) */
static qboolean
Style_Unnamed(const menucommon_s *item)
{
	return ((item->type == MTYPE_SPINCONTROL) || (item->type == MTYPE_LIST) || (item->type == MTYPE_SLIDER) ||
			   (item->type == MTYPE_FIELD)) &&
		   (!item->name || !item->name[0]);
}

void
Menu_DrawStyled(menuframework_s *menu)
{
	float menu_scale = SCR_GetMenuScale();
	/* optech2: each item's row in units from the panel's top, -1 = not drawn */
	float row_units[MAXMENUITEMS];
	/* optech2: the label shown: a heading directly above an unnamed value names it */
	const char *labels[MAXMENUITEMS];
	float units = 0, panel_units, scale;
	int previous_y = 0, i;
	qboolean any = false, half_gap = false;
	int panel_top, padding, row_height, text_offset, width, content_height, panel_height, x;
	const menucommon_s *selected_item;
	const char *status = NULL;

	/* optech2: rows one after another; a blank line or a wider classic gap
	   becomes half a row; bitmaps (pictures, player setup's skin icon) are
	   left out */
	for (i = 0; i < menu->nitems; i++)
	{
		row_units[i] = -1;
		labels[i] = NULL;
	}

	for (i = 0; i < menu->nitems; i++)
	{
		const menucommon_s *item = (const menucommon_s *)menu->items[i];

		if ((item->flags & QMF_HIDDEN) || (item->type == MTYPE_BITMAP))
		{
			continue;
		}

		if ((item->type == MTYPE_SEPARATOR) && (!item->name || !item->name[0]))
		{
			half_gap = any;
			continue;
		}

		if ((item->type == MTYPE_SEPARATOR) && (i + 1 < menu->nitems) &&
			Style_Unnamed((const menucommon_s *)menu->items[i + 1]) &&
			(((const menucommon_s *)menu->items[i + 1])->y - item->y <= 10))
		{
			labels[i + 1] = Style_Label(item); /* optech2: drawn on the value's row */
			continue;
		}

		if (any)
		{
			units += STYLE_ROW_UNITS;

			if (half_gap || (item->y - previous_y > 10))
			{
				units += STYLE_ROW_UNITS / 2;
			}
		}

		row_units[i] = units;
		previous_y = item->y;
		half_gap = false;
		any = true;
	}

	if (!any)
	{
		return;
	}

	/* optech2: the rows, plus padding and a status line */
	panel_units = units + STYLE_ROW_UNITS + 2 * STYLE_PADDING_UNITS + STYLE_ROW_UNITS;
	/* optech2: the top stays put, under the banner's place, whatever the rows */
	panel_top = viddef.height / 2 - (int)(76 * menu_scale);
	/* optech2: a page too tall for the room under it is drawn smaller */
	scale = Q_min(menu_scale, (viddef.height - panel_top + 2 * menu_scale) / panel_units);
	padding = (int)(STYLE_PADDING_UNITS * scale);
	row_height = (int)(STYLE_ROW_UNITS * scale);
	text_offset = (row_height - (int)(8 * scale)) / 2;
	width = (int)(STYLE_WIDTH_UNITS * scale);

	if (width > viddef.width - 2 * padding)
	{
		width = viddef.width - 2 * padding;
	}

	content_height = (int)((units + STYLE_ROW_UNITS) * scale);
	panel_height = content_height + 2 * padding + row_height;
	panel_top += padding - (int)(STYLE_PADDING_UNITS * menu_scale); /* optech2: the panel's edge stays put too */
	x = (viddef.width - width) / 2;

	if (menu->style_right)
	{
		/* optech2: room for a picture beside the panel (player setup's model) */
		int right = viddef.width / 2 + (int)(menu->style_right * scale);

		width = Q_min(width, right - padding);
		x = right - width;
	}

	Draw_FillColor(x, panel_top - padding, width, panel_height, style_panel);
	Draw_FillColor(x, panel_top - padding, width, (int)scale, style_edge);
	Draw_FillColor(x, panel_top - padding + panel_height - (int)scale, width, (int)scale, style_edge);

	menu->style_x = x;
	menu->style_width = width;
	menu->style_row_height = row_height;
	menu->style_scale = scale;

	for (i = 0; i < menu->nitems; i++)
	{
		menu->style_row_top[i] = (row_units[i] < 0) ? -1 : panel_top + (int)(row_units[i] * scale);
	}

	/* optech2: the mouse moved: the row under the pointer becomes the selected one */
	if (style_pointer_moved)
	{
		style_pointer_moved = false;

		for (i = 0; i < menu->nitems; i++)
		{
			if (Style_Selectable((const menucommon_s *)menu->items[i]) && (menu->style_row_top[i] >= 0) &&
				(style_pointer_x >= x) && (style_pointer_x < x + width) &&
				(style_pointer_y >= menu->style_row_top[i]) &&
				(style_pointer_y < menu->style_row_top[i] + row_height))
			{
				menu->cursor = i;
			}
		}
	}

	for (i = 0; i < menu->nitems; i++)
	{
		menucommon_s *item = (menucommon_s *)menu->items[i];
		int top = menu->style_row_top[i];
		int text_y = top + text_offset;
		int right = x + width - padding;
		qboolean selected = (i == menu->cursor);
		const float *text_color = selected ? style_selected : style_label;

		if (top < 0)
		{
			continue; /* optech2: hidden, a bitmap, a blank line, or a heading drawn on its value's row */
		}

		if (item->flags & (QMF_GRAYED | QMF_INACTIVE))
		{
			text_color = style_dim;
		}

		if (item->type == MTYPE_SEPARATOR)
		{
			if (item->name && item->name[0])
			{
				Style_Text(x + padding, text_y, scale, style_header, Style_Label(item));
			}

			continue;
		}

		if (selected)
		{
			Draw_FillColor(x + (int)scale, top, width - 2 * (int)scale, row_height, style_bar);
			Draw_FillColor(x + (int)scale, top, (int)(3 * scale), row_height, style_accent);
		}

		Style_Text(x + padding, text_y, scale, text_color, labels[i] ? labels[i] : Style_Label(item));

		switch (item->type)
		{
			case MTYPE_SPINCONTROL:
			case MTYPE_LIST:
			{
				char value[96], shown[104];

				Style_ChoiceValue((const menulist_s *)item, value, sizeof(value));
				Com_sprintf(shown, sizeof(shown), selected ? "< %s >" : "%s", value);
				Style_Text(right - Style_TextWidth(scale, shown), text_y, scale, style_value, shown);
				break;
			}

			case MTYPE_SLIDER:
			{
				const menuslider_s *slider = (const menuslider_s *)item;
				float value = slider->cvar ? Cvar_VariableValue(slider->cvar) : 0;
				float range = slider->maxvalue - slider->minvalue;
				float fraction;
				int bar_x, bar_width, bar_height = (int)(4 * scale);
				char text[32];

				if (slider->abs && (value < 0))
				{
					value = -value;
				}

				fraction = (range != 0) ? Q_clamp((value - slider->minvalue) / range, 0.0f, 1.0f) : 0;
				Style_SliderBar(scale, right, &bar_x, &bar_width);
				Draw_FillColor(bar_x, top + (row_height - bar_height) / 2, bar_width, bar_height, style_slider_back);
				Draw_FillColor(bar_x, top + (row_height - bar_height) / 2, (int)(bar_width * fraction), bar_height,
					style_slider_fill);
				Com_sprintf(text, sizeof(text), slider->printformat ? slider->printformat : "%.1f", value);
				Style_Text(right - Style_TextWidth(scale, text), text_y, scale, style_value, text);
				break;
			}

			case MTYPE_FIELD:
			{
				const menufield_s *field = (const menufield_s *)item;
				int field_width = (int)((field->visible_length + 1) * Draw_CharAdvance() * scale);
				char text[96];

				Draw_FillColor(right - field_width - (int)(2 * scale), top + (int)scale, field_width + (int)(4 * scale),
					row_height - 2 * (int)scale, style_field_back);
				Com_sprintf(text, sizeof(text), "%s%s", field->buffer,
					(selected && ((cls.realtime / 250) & 1)) ? "_" : "");
				Style_Text(right - field_width, text_y, scale, style_value, text);
				break;
			}

			case MTYPE_ACTION:
				if (item->ownerdraw)
				{
					/* optech2: its own value (a key binding) in the value column */
					style_value_right = right;
					style_value_y = text_y;
					style_value_scale = scale;
					item->ownerdraw(item);
				}
				else if (item->flags & QMF_SUBMENU)
				{
					Style_Text(right - Style_TextWidth(scale, ">"), text_y, scale, style_value, ">");
				}

				break;
		}
	}

	/* optech2: the status line: the selected item's, else the menu's */
	selected_item = Menu_ItemAtCursor(menu);

	if (selected_item && selected_item->statusbarfunc)
	{
		selected_item->statusbarfunc((void *)selected_item);
		status = menu->statusbar;
	}
	else if (selected_item && selected_item->statusbar)
	{
		status = selected_item->statusbar;
	}
	else
	{
		status = menu->statusbar;
	}

	if (status && status[0])
	{
		Style_Text(x + (width - Style_TextWidth(scale, status)) / 2,
			panel_top + content_height + padding / 2 + text_offset, scale, style_status, status);
	}

	/* optech2: the pointer, while the mouse is in use */
	if (style_pointer_shown)
	{
		static const float pointer_edge[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		static const float pointer_fill[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		int size = (int)(6 * menu_scale);

		Draw_FillColor((int)style_pointer_x - 1, (int)style_pointer_y - 1, size + 2, size + 2, pointer_edge);
		Draw_FillColor((int)style_pointer_x, (int)style_pointer_y, size, size, pointer_fill);
	}
}

/* optech2: the row under the menu pointer, -1 if none */
static int
Style_RowAtPointer(const menuframework_s *menu)
{
	int i;

	if ((style_pointer_x < menu->style_x) || (style_pointer_x >= menu->style_x + menu->style_width))
	{
		return -1;
	}

	for (i = 0; i < menu->nitems; i++)
	{
		if (Style_Selectable((const menucommon_s *)menu->items[i]) && (menu->style_row_top[i] >= 0) &&
			(style_pointer_y >= menu->style_row_top[i]) &&
			(style_pointer_y < menu->style_row_top[i] + menu->style_row_height))
		{
			return i;
		}
	}

	return -1;
}

/*
 * optech2: the mouse on a styled menu. Returns true when it took the key
 * (*sound_name set to what to play, or NULL); for K_MOUSE2 and the wheel it
 * hands back the key to use instead in *replacement.
 */
qboolean
Menu_StyledMouse(menuframework_s *menu, int key, int *replacement, const char **sound_name)
{
	*sound_name = NULL;
	*replacement = key;

	if (!Menu_Styled())
	{
		return false;
	}

	switch (key)
	{
		case K_MOUSE2:
			*replacement = K_ESCAPE;
			return false;

		case K_MWHEELUP:
			*replacement = K_UPARROW;
			return false;

		case K_MWHEELDOWN:
			*replacement = K_DOWNARROW;
			return false;

		case K_MOUSE3:
		case K_MOUSE4:
		case K_MOUSE5:
			return true;

		case K_MOUSE1:
		{
			int row = Style_RowAtPointer(menu);
			menucommon_s *item;

			if (row < 0)
			{
				return true;
			}

			menu->cursor = row;
			item = (menucommon_s *)menu->items[row];

			if ((item->type == MTYPE_SPINCONTROL) || (item->type == MTYPE_LIST))
			{
				menulist_s *list = (menulist_s *)item;

				if (!Menu_SlideItem(menu, 1) && list->itemnames && list->itemnames[0])
				{
					list->curvalue = 0; /* optech2: wrap to the first option */

					if (item->callback)
					{
						item->callback(item);
					}
				}

				*sound_name = menu_move_sound;
				return true;
			}

			if (item->type == MTYPE_SLIDER)
			{
				menuslider_s *slider = (menuslider_s *)item;
				int right = menu->style_x + menu->style_width - (int)(STYLE_PADDING_UNITS * menu->style_scale);
				int bar_x, bar_width;
				float step = slider->slidestep ? slider->slidestep : 0.1f;
				float fraction, value;

				Style_SliderBar(menu->style_scale, right, &bar_x, &bar_width);
				fraction = (style_pointer_x - bar_x) / (float)bar_width;

				if (!slider->cvar || (fraction < -0.05f) || (fraction > 1.05f))
				{
					return true; /* optech2: on the label: nothing */
				}

				value = slider->minvalue + Q_clamp(fraction, 0.0f, 1.0f) * (slider->maxvalue - slider->minvalue);
				value = slider->minvalue + step * (int)((value - slider->minvalue) / step + 0.5f);
				value = Q_clamp(value, slider->minvalue, slider->maxvalue);

				if (slider->abs && (Cvar_VariableValue(slider->cvar) < 0))
				{
					value = -value;
				}

				Cvar_SetValue(slider->cvar, value);

				if (item->callback)
				{
					item->callback(item);
				}

				*sound_name = menu_move_sound;
				return true;
			}

			if (item->type == MTYPE_FIELD)
			{
				return true; /* optech2: selected, ready to type */
			}

			*replacement = K_ENTER;
			return false;
		}

		default:
			style_pointer_shown = false; /* optech2: keyboard or gamepad: the pointer goes */
			return false;
	}
}

/* optech2: the mouse moved while a menu is open (input/sdl2.c, sdl3.c) */
void
Menu_MouseMove(float dx, float dy)
{
	if (!style_pointer_shown)
	{
		style_pointer_shown = true;

		if ((style_pointer_x <= 0) && (style_pointer_y <= 0))
		{
			style_pointer_x = viddef.width / 2.0f;
			style_pointer_y = viddef.height / 2.0f;
		}
	}

	style_pointer_x = Q_clamp(style_pointer_x + dx, 0.0f, (float)(viddef.width - 1));
	style_pointer_y = Q_clamp(style_pointer_y + dy, 0.0f, (float)(viddef.height - 1));
	style_pointer_moved = true;
}

/* optech2: puts the pointer at x, y (pixels), as if the mouse moved there */
void
Menu_SetPointer(float x, float y)
{
	style_pointer_shown = true;
	style_pointer_x = x;
	style_pointer_y = y;
	style_pointer_moved = true;
}
