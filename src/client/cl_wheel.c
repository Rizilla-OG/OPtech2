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
 * optech2: the re-release's weapon and powerup wheels. Hold +wheel
 * (weapons) or +wheel2 (powerups): the items the player has sit around a
 * circle in the game's order, the mouse points at one, and letting go
 * switches to it (use_index_only <item>). The game library says what the
 * wheels hold (CS_WHEEL_WEAPONS / _AMMO / _POWERUPS configstrings) and,
 * through its client game, what the player has.
 *
 * =======================================================================
 */

#include "header/client.h"

#define WHEEL_WEAPONS 0
#define WHEEL_POWERUPS 1

typedef struct
{
	int item; /* CS_ITEMS index */
	int image; /* CS_IMAGES index */
	int ammo; /* CS_WHEEL_AMMO index, -1 for none */
	int min_ammo; /* ammo needed to use it */
	int sort;
	int count; /* ammo (weapons) or how many (powerups); -1 unknown */
	qboolean usable;
	qboolean active; /* the weapon in hand */
} wheel_slot_t;

static int wheel_open = -1; /* WHEEL_WEAPONS, WHEEL_POWERUPS, -1 closed */
static float wheel_cursor[2]; /* mouse offset from the center, pixels */
static wheel_slot_t wheel_slots[MAX_WHEEL_ITEMS];
static int wheel_slot_count;
static int wheel_selected = -1;

static cvar_t *cl_wheel_deadzone; /* fraction of the radius before a pick */

/* the n-th |-separated number of a configstring, or fallback */
static int
Wheel_Field(const char *text, int n, int fallback)
{
	while (n > 0 && *text)
	{
		if (*text++ == '|')
		{
			n--;
		}
	}

	return (n == 0 && *text) ? atoi(text) : fallback;
}

/* optech2: the ammo count under which the HUD warns for a weapon wheel
   slot (CS_WHEEL_WEAPONS field 6); 0 when the game gives none */
int
CL_Wheel_WarnAmmo(int weapon_slot)
{
	if (weapon_slot < 0 || weapon_slot >= MAX_WHEEL_ITEMS)
	{
		return 0;
	}

	return Wheel_Field(cl.configstrings[CS_WHEEL_WEAPONS + weapon_slot], 6, 0);
}

static int
Wheel_SortSlots(const void *a, const void *b)
{
	const wheel_slot_t *first = a, *second = b;

	if (first->sort != second->sort)
	{
		return first->sort - second->sort;
	}

	return first->item - second->item;
}

/* what the open wheel holds, in the game's order */
static void
Wheel_Gather(void)
{
	uint32_t owned = 0;
	int active = -1, i;

	wheel_slot_count = 0;

	if (wheel_open == WHEEL_WEAPONS)
	{
		if (!CL_CGame_WheelWeapons(&owned, &active))
		{
			return;
		}

		for (i = 0; i < MAX_WHEEL_ITEMS; i++)
		{
			const char *text = cl.configstrings[CS_WHEEL_WEAPONS + i];
			wheel_slot_t *slot;

			if (!text[0] || !(owned & (1u << i)))
			{
				continue;
			}

			slot = &wheel_slots[wheel_slot_count++];
			slot->item = Wheel_Field(text, 0, 0);
			slot->image = Wheel_Field(text, 1, 0);
			slot->ammo = Wheel_Field(text, 2, -1);
			slot->min_ammo = Wheel_Field(text, 3, 1);
			slot->sort = Wheel_Field(text, 5, i);
			slot->count = (slot->ammo >= 0) ? CL_CGame_WheelAmmo(slot->ammo) : -1;
			slot->usable = (slot->ammo < 0) || (slot->count >= slot->min_ammo);
			slot->active = (i == active);
		}
	}
	else
	{
		for (i = 0; i < MAX_WHEEL_ITEMS; i++)
		{
			const char *text = cl.configstrings[CS_WHEEL_POWERUPS + i];
			wheel_slot_t *slot;
			int count;

			if (!text[0])
			{
				continue;
			}

			count = CL_CGame_WheelPowerup(i);

			if (count <= 0)
			{
				continue;
			}

			slot = &wheel_slots[wheel_slot_count++];
			slot->item = Wheel_Field(text, 0, 0);
			slot->image = Wheel_Field(text, 1, 0);
			slot->ammo = -1;
			slot->min_ammo = 0;
			slot->sort = Wheel_Field(text, 3, i);
			/* on/off powerups show no count */
			slot->count = Wheel_Field(text, 2, 0) ? -1 : count;
			slot->usable = true;
			slot->active = false;
		}
	}

	qsort(wheel_slots, wheel_slot_count, sizeof(wheel_slots[0]), Wheel_SortSlots);
}

/* the slot the cursor points at, or -1 inside the dead zone */
static int
Wheel_PointedSlot(float radius)
{
	float length = sqrtf(wheel_cursor[0] * wheel_cursor[0] + wheel_cursor[1] * wheel_cursor[1]);
	float angle, step;
	int slot;

	if (!wheel_slot_count || (length < radius * cl_wheel_deadzone->value))
	{
		return -1;
	}

	/* clockwise from the top, as the slots are laid out */
	angle = atan2f(wheel_cursor[0], -wheel_cursor[1]);

	if (angle < 0)
	{
		angle += 2 * (float)M_PI;
	}

	step = 2 * (float)M_PI / wheel_slot_count;
	slot = (int)((angle + step / 2) / step) % wheel_slot_count;
	return slot;
}

static float
Wheel_Radius(void)
{
	return (viddef.width < viddef.height ? viddef.width : viddef.height) * 0.3f;
}

static void
Wheel_Open(int which)
{
	if (!CL_CGame_Active() || (cls.state != ca_active) || (cls.key_dest != key_game))
	{
		return;
	}

	wheel_open = which;
	wheel_cursor[0] = wheel_cursor[1] = 0;
	wheel_selected = -1;
}

static void
Wheel_Close(int which)
{
	if (wheel_open != which)
	{
		return;
	}

	Wheel_Gather();

	if ((wheel_selected >= 0) && (wheel_selected < wheel_slot_count))
	{
		/* the server's game picks it; use_index_only skips weapon chains */
		Cbuf_AddText(va("cmd use_index_only %d\n", wheel_slots[wheel_selected].item));
	}

	wheel_open = -1;
	wheel_selected = -1;
}

static void
Wheel_Down_f(void)
{
	Wheel_Open(WHEEL_WEAPONS);
}
static void
Wheel_Up_f(void)
{
	Wheel_Close(WHEEL_WEAPONS);
}
static void
Wheel2_Down_f(void)
{
	Wheel_Open(WHEEL_POWERUPS);
}
static void
Wheel2_Up_f(void)
{
	Wheel_Close(WHEEL_POWERUPS);
}

/* what the wheels hold, for checks and the curious */
static void
Wheel_List_f(void)
{
	int which;

	for (which = WHEEL_WEAPONS; which <= WHEEL_POWERUPS; which++)
	{
		int i;

		wheel_open = which;
		Wheel_Gather();

		Com_Printf("wheel %s: %d\n", which == WHEEL_WEAPONS ? "weapons" : "powerups", wheel_slot_count);

		for (i = 0; i < wheel_slot_count; i++)
		{
			const wheel_slot_t *slot = &wheel_slots[i];

			Com_Printf("  %s (item %d, %s) count %d%s%s\n",
				SV_LocalizationMessage(cl.configstrings[CS_ITEMS + slot->item], NULL),
				slot->item, cl.configstrings[CS_IMAGES + slot->image], slot->count,
				slot->usable ? "" : " not usable", slot->active ? " in hand" : "");
		}
	}

	wheel_open = -1;
}

qboolean CL_Wheel_Mouse(float dx, float dy);

/* move the open wheel's cursor as the mouse would (checks, binds) */
static void
Wheel_Aim_f(void)
{
	if (Cmd_Argc() != 3)
	{
		Com_Printf("usage: wheel_aim <dx> <dy>\n");
		return;
	}

	CL_Wheel_Mouse((float)atof(Cmd_Argv(1)), (float)atof(Cmd_Argv(2)));
}

/* the wheel takes the mouse while it is open */
qboolean
CL_Wheel_Mouse(float dx, float dy)
{
	float radius, length;

	if (wheel_open < 0)
	{
		return false;
	}

	if ((cls.key_dest != key_game) || (cls.state != ca_active))
	{
		wheel_open = -1; /* a menu or the console came up */
		return false;
	}

	radius = Wheel_Radius();
	wheel_cursor[0] += dx;
	wheel_cursor[1] += dy;

	/* keep the cursor on the ring, so a turn back is quick */
	length = sqrtf(wheel_cursor[0] * wheel_cursor[0] + wheel_cursor[1] * wheel_cursor[1]);

	if (length > radius)
	{
		wheel_cursor[0] *= radius / length;
		wheel_cursor[1] *= radius / length;
	}

	return true;
}

qboolean
CL_Wheel_IsOpen(void)
{
	return wheel_open >= 0;
}

void
CL_Wheel_Draw(void)
{
	static const float backdrop[4] = {0.0f, 0.0f, 0.0f, 0.55f};
	static const float picked[4] = {0.85f, 0.55f, 0.15f, 0.75f};
	static const float in_hand[4] = {0.35f, 0.35f, 0.35f, 0.7f};
	static const vec3_t grey = {0.35f, 0.35f, 0.35f};
	float radius, hud_scale;
	int center_x, center_y, i, icon_size;

	if (wheel_open < 0)
	{
		return;
	}

	if ((cls.key_dest != key_game) || (cls.state != ca_active))
	{
		wheel_open = -1;
		return;
	}

	Wheel_Gather();

	radius = Wheel_Radius();
	hud_scale = SCR_GetHUDScale();
	icon_size = (int)(24 * hud_scale * 1.5f);

	/* many items: smaller icons, so neighbours don't overlap on the ring */
	if (wheel_slot_count > 1)
	{
		int fits = (int)(2 * (float)M_PI * radius / wheel_slot_count / 1.6f);

		if (icon_size > fits)
		{
			icon_size = fits;
		}
	}
	center_x = viddef.width / 2;
	center_y = viddef.height / 2;
	wheel_selected = Wheel_PointedSlot(radius);

	for (i = 0; i < wheel_slot_count; i++)
	{
		const wheel_slot_t *slot = &wheel_slots[i];
		float angle = 2 * (float)M_PI * i / wheel_slot_count;
		int x = center_x + (int)(sinf(angle) * radius);
		int y = center_y - (int)(cosf(angle) * radius);
		int box = icon_size + icon_size / 2;
		const char *pic = cl.configstrings[CS_IMAGES + slot->image];
		float pic_scale = icon_size / 24.0f;

		Draw_FillColor(x - box / 2, y - box / 2, box, box,
			(i == wheel_selected) ? picked : (slot->active ? in_hand : backdrop));

		if (slot->usable)
		{
			Draw_PicScaled(x - icon_size / 2, y - icon_size / 2, pic, pic_scale);
		}
		else
		{
			Draw_PicScaledCol(x - icon_size / 2, y - icon_size / 2, pic, pic_scale, grey, NULL);
		}
	}

	/* the pointed item's name in the middle */
	if (wheel_selected >= 0)
	{
		const wheel_slot_t *slot = &wheel_slots[wheel_selected];
		const char *name = SV_LocalizationMessage(cl.configstrings[CS_ITEMS + slot->item], NULL);
		float name_scale = hud_scale * 1.5f;
		int width = (int)(strlen(name) * Draw_CharAdvance() * name_scale);

		Draw_StringScaled(center_x - width / 2, center_y - (int)(12 * hud_scale), name_scale, false, name);

		/* its ammo (or how many), green when usable */
		if (slot->count >= 0)
		{
			const char *count = va("%d", slot->count);

			width = (int)(strlen(count) * Draw_CharAdvance() * hud_scale);
			Draw_StringScaled(center_x - width / 2, center_y + (int)(4 * hud_scale), hud_scale,
				!slot->usable, count);
		}
	}
	else if (!wheel_slot_count)
	{
		const char *empty = (wheel_open == WHEEL_WEAPONS) ? "no weapons" : "no powerups";
		int width = (int)(strlen(empty) * Draw_CharAdvance() * hud_scale);

		Draw_StringScaled(center_x - width / 2, center_y - (int)(4 * hud_scale), hud_scale, true, empty);
	}

	/* where the mouse points */
	Draw_FillColor(center_x + (int)wheel_cursor[0] - 2, center_y + (int)wheel_cursor[1] - 2, 5, 5, picked);
}

void
CL_Wheel_Init(void)
{
	cl_wheel_deadzone = Cvar_Get("cl_wheel_deadzone", "0.25", CVAR_ARCHIVE);

	Cmd_AddCommand("+wheel", Wheel_Down_f);
	Cmd_AddCommand("-wheel", Wheel_Up_f);
	Cmd_AddCommand("+wheel2", Wheel2_Down_f);
	Cmd_AddCommand("-wheel2", Wheel2_Up_f);
	Cmd_AddCommand("wheel_list", Wheel_List_f);
	Cmd_AddCommand("wheel_aim", Wheel_Aim_f);
}
