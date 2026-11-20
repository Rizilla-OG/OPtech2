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
 * optech2: the client game module API of the 2023 Quake II re-release
 * (cgame API version 2022), in C. A game library exports GetCGameAPI();
 * the engine hands it cgame_import_t and calls its cgame_export_t.
 *
 * Every struct here must keep the re-release memory layout byte for
 * byte (game.h of the re-release source): modules are built against
 * those C++ definitions. C++ references are passed as pointers, which
 * is the same in memory. Checked against the re-release headers on
 * x86_64: player state 296 bytes, import 296, export 152, same offsets.
 *
 * =======================================================================
 */

#ifndef CL_CGAME_RERELEASE_H
#define CL_CGAME_RERELEASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../common/header/shared.h" /* cvar_t */

#define CGAME_API_VERSION 2022

/* sizes the re-release modules are built with */
#define CG_MAX_ITEMS 256
#define CG_MAX_STATS 64

typedef struct
{
	uint8_t r, g, b, a;
} cg_rgba_t;

typedef struct
{
	float x, y;
} cg_vec2_t;

typedef struct
{
	int32_t x, y, width, height;
} cg_vrect_t;

/* text_align_t (an enum class, int sized) */
enum
{
	CG_ALIGN_LEFT,
	CG_ALIGN_CENTER,
	CG_ALIGN_RIGHT
};

/* layout_flags_t (int16) */
enum
{
	CG_LAYOUTS_LAYOUT = 1 << 0, /* svc_layout is active */
	CG_LAYOUTS_INVENTORY = 1 << 1, /* inventory is active */
	CG_LAYOUTS_HIDE_HUD = 1 << 2, /* hide entire hud (cameras etc.) */
	CG_LAYOUTS_INTERMISSION = 1 << 3, /* intermission is being drawn */
	CG_LAYOUTS_HELP = 1 << 4, /* help is active */
	CG_LAYOUTS_HIDE_CROSSHAIR = 1 << 5
};

/* transient data from the server */
typedef struct
{
	char layout[1024];
	int16_t inventory[CG_MAX_ITEMS];
} cg_server_data_t;

/* the re-release pmove_state_t */
typedef struct
{
	int32_t pm_type;
	float origin[3];
	float velocity[3];
	uint16_t pm_flags;
	uint16_t pm_time;
	int16_t gravity;
	float delta_angles[3];
	int8_t viewheight;
} cg_pmove_state_t;

/* the re-release player_state_t */
typedef struct
{
	cg_pmove_state_t pmove;

	float viewangles[3];
	float viewoffset[3];
	float kick_angles[3];

	float gunangles[3];
	float gunoffset[3];
	int32_t gunindex;
	int32_t gunskin;
	int32_t gunframe;
	int32_t gunrate;

	float screen_blend[4];
	float damage_blend[4];

	float fov;

	uint8_t rdflags;

	int16_t stats[CG_MAX_STATS];

	uint8_t team_id;
} cg_player_state_t;

/* functions the engine provides to the client game module */
typedef struct
{
	uint32_t tick_rate;
	float frame_time_s;
	uint32_t frame_time_ms;

	void (*Com_Print)(const char *msg);

	const char *(*get_configstring)(int num);

	void (*Com_Error)(const char *message);

	void *(*TagMalloc)(size_t size, int tag);
	void (*TagFree)(void *block);
	void (*FreeTags)(int tag);

	cvar_t *(*cvar)(const char *var_name, const char *value, int flags);
	cvar_t *(*cvar_set)(const char *var_name, const char *value);
	cvar_t *(*cvar_forceset)(const char *var_name, const char *value);

	void (*AddCommandString)(const char *text);

	void *(*GetExtension)(const char *name);

	bool (*CL_FrameValid)(void);

	float (*CL_FrameTime)(void);

	uint64_t (*CL_ClientTime)(void);
	uint64_t (*CL_ClientRealTime)(void);
	int32_t (*CL_ServerFrame)(void);
	int32_t (*CL_ServerProtocol)(void);
	const char *(*CL_GetClientName)(int32_t index);
	const char *(*CL_GetClientPic)(int32_t index);
	const char *(*CL_GetClientDogtag)(int32_t index);
	const char *(*CL_GetKeyBinding)(const char *binding);
	bool (*Draw_RegisterPic)(const char *name);
	void (*Draw_GetPicSize)(int *w, int *h, const char *name);
	void (*SCR_DrawChar)(int x, int y, int scale, int num, bool shadow);
	void (*SCR_DrawPic)(int x, int y, int w, int h, const char *name);
	void (*SCR_DrawColorPic)(int x, int y, int w, int h, const char *name, const cg_rgba_t *color);

	void (*SCR_SetAltTypeface)(bool enabled);
	void (*SCR_DrawFontString)(const char *str, int x, int y, int scale, const cg_rgba_t *color, bool shadow, int align);
	cg_vec2_t (*SCR_MeasureFontString)(const char *str, int scale);
	float (*SCR_FontLineHeight)(int scale);

	bool (*CL_GetTextInput)(const char **msg, bool *is_team);

	int32_t (*CL_GetWarnAmmoCount)(int32_t weapon_id);

	const char *(*Localize)(const char *base, const char **args, size_t num_args);

	int32_t (*SCR_DrawBind)(int32_t isplit, const char *binding, const char *purpose, int x, int y, int scale);

	bool (*CL_InAutoDemoLoop)(void);
} cgame_import_t;

/* functions the client game module provides to the engine */
typedef struct
{
	int apiversion;

	void (*Init)(void);
	void (*Shutdown)(void);

	void (*DrawHUD)(int32_t isplit, const cg_server_data_t *data, cg_vrect_t hud_vrect, cg_vrect_t hud_safe,
		int32_t scale, int32_t playernum, const cg_player_state_t *ps);
	void (*TouchPics)(void);

	int16_t (*LayoutFlags)(const cg_player_state_t *ps);

	int32_t (*GetActiveWeaponWheelWeapon)(const cg_player_state_t *ps);
	uint32_t (*GetOwnedWeaponWheelWeapons)(const cg_player_state_t *ps);
	int16_t (*GetWeaponWheelAmmoCount)(const cg_player_state_t *ps, int32_t ammo_id);
	int16_t (*GetPowerupWheelCount)(const cg_player_state_t *ps, int32_t powerup_id);
	int16_t (*GetHitMarkerDamage)(const cg_player_state_t *ps);

	void (*Pmove)(void *pmove); /* re-release pmove_t; used from M2 step 3 */

	void (*ParseConfigString)(int32_t i, const char *s);
	void (*ParseCenterPrint)(const char *str, int isplit, bool instant);
	void (*ClearNotify)(int32_t isplit);
	void (*ClearCenterprint)(int32_t isplit);
	void (*NotifyMessage)(int32_t isplit, const char *msg, bool is_chat);

	void (*GetMonsterFlashOffset)(int32_t id, float *offset);

	void *(*GetExtension)(const char *name);
} cgame_export_t;

typedef cgame_export_t *(*GetCGameAPI_t)(cgame_import_t *import);

#endif
