/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (c) ZeniMax Media Inc.
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
 * optech2: the server game module API of the 2023 Quake II re-release
 * (game API version 2023), in C. A re-release game library exports
 * GetGameAPI(); the engine hands it rr_game_import_t and calls its
 * rr_game_export_t.
 *
 * Every struct here must keep the re-release memory layout byte for
 * byte (game.h of the re-release source): game libraries are built
 * against those C++ definitions. C++ references are passed as pointers,
 * which is the same in memory; enums keep their underlying type.
 * tests/rerelease/layout_check.py compares every size and offset.
 *
 * =======================================================================
 */

#ifndef SV_GAME_RERELEASE_H
#define SV_GAME_RERELEASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../common/header/shared.h" /* cvar_t */
#include "../../common/header/rerelease.h" /* movement types */

#define RR_GAME_API_VERSION 2023

/* sizes the re-release game libraries are built with */
#define RR_MAX_EDICTS 8192
#define RR_MAX_MODELS 8192
#define RR_MAX_SOUNDS 2048
#define RR_MAX_IMAGES 512
#define RR_MAX_ITEMS 256
#define RR_MAX_STATS 64
#define RR_MAX_NETNAME 32
#define RR_MAX_ARMOR_TYPES 3
#define RR_MAX_SPLIT_PLAYERS 8

typedef struct rr_gclient_s rr_gclient_t;

/* svflags_t */
#define RR_SVF_NOCLIENT (1u << 0)
#define RR_SVF_DEADMONSTER (1u << 1)
#define RR_SVF_MONSTER (1u << 2)
#define RR_SVF_PLAYER (1u << 3)
#define RR_SVF_BOT (1u << 4)
#define RR_SVF_NOBOTS (1u << 5)
#define RR_SVF_RESPAWNING (1u << 6)
#define RR_SVF_PROJECTILE (1u << 7)
#define RR_SVF_INSTANCED (1u << 8)
#define RR_SVF_DOOR (1u << 9)
#define RR_SVF_NOCULL (1u << 10)
#define RR_SVF_HULL (1u << 11)

/* effects_t bits above 32 that the engine knows (as rr_effects) */
#define RR_EF_HOLOGRAM (1ull << 33)
#define RR_EF_FLASHLIGHT (1ull << 34)
#define RR_EF_TELEPORTER2 (1ull << 36)

/* renderfx_t bits that differ from the classic ones */
#define RR_RF_NO_ORIGIN_LERP (1u << 6)
#define RR_RF_CASTSHADOW (1u << 14)
#define RR_RF_SHELL_LITE_GREEN (1u << 19)

/* print_type_t */
#define RR_PRINT_LOW 0
#define RR_PRINT_MEDIUM 1
#define RR_PRINT_HIGH 2
#define RR_PRINT_CHAT 3
#define RR_PRINT_TYPEWRITER 4
#define RR_PRINT_CENTER 5
#define RR_PRINT_TTS 6
#define RR_PRINT_BROADCAST (1 << 3)
#define RR_PRINT_NO_NOTIFY (1 << 4)

/* server_flags_t */
#define RR_SERVER_FLAG_SLOW_TIME (1 << 0)
#define RR_SERVER_FLAG_INTERMISSION (1 << 1)
#define RR_SERVER_FLAG_LOADING (1 << 2)

/* BoxEdictsResult_t (enum class, int sized) */
#define RR_BOXEDICTS_KEEP 0
#define RR_BOXEDICTS_SKIP 1
#define RR_BOXEDICTS_END 64

typedef struct
{
	uint8_t r, g, b, a;
} rr_rgba_t;

typedef struct
{
	uint32_t number;

	float origin[3];
	float angles[3];
	float old_origin[3];
	int32_t modelindex;
	int32_t modelindex2, modelindex3, modelindex4;
	int32_t frame;
	int32_t skinnum;
	uint64_t effects;
	uint32_t renderfx;
	uint32_t solid;
	int32_t sound;
	uint8_t event;
	float alpha;
	float scale;
	uint8_t instance_bits;
	float loop_volume;
	float loop_attenuation;
	int32_t owner;
	int32_t old_frame;
} rr_entity_state_t;

typedef struct
{
	rr_pmove_state_t pmove;

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

	int16_t stats[RR_MAX_STATS];

	uint8_t team_id;
} rr_player_state_t;

/* the part of gclient_t the engine sees */
struct rr_gclient_s
{
	rr_player_state_t ps;
	int32_t ping;
};

typedef struct
{
	int32_t item_id;
	int32_t max_count;
} rr_armor_info_t;

/* read only info about an entity, for the engine's bots and tools */
typedef struct
{
	bool init;
	uint64_t ent_flags;
	uint8_t buttons;
	uint32_t spawnflags;
	int32_t item_id;
	int32_t armor_type;
	int32_t armor_value;
	int32_t health;
	int32_t max_health;
	int32_t starting_health;
	int32_t weapon;
	int32_t team;
	int32_t lobby_usernum;
	int32_t respawntime;
	int32_t viewheight;
	int32_t last_attackertime;
	uint8_t waterlevel;
	float viewangles[3];
	float viewforward[3];
	float velocity[3];
	float start_origin[3];
	float end_origin[3];
	rr_edict_t *enemy;
	rr_edict_t *ground_entity;
	const char *classname;
	const char *targetname;
	char netname[RR_MAX_NETNAME];
	int32_t inventory[RR_MAX_ITEMS];
	rr_armor_info_t armor_info[RR_MAX_ARMOR_TYPES];
} rr_sv_entity_t;

/* the part of edict_t the engine sees; edicts are ge->edict_size apart */
struct rr_edict_s
{
	rr_entity_state_t s;
	rr_gclient_t *client;

	rr_sv_entity_t sv;

	bool inuse;

	bool linked;
	int32_t linkcount;
	int32_t areanum, areanum2;

	uint32_t svflags;
	float mins[3], maxs[3];
	float absmin[3], absmax[3], size[3];
	uint8_t solid;
	uint32_t clipmask;
	rr_edict_t *owner;
};

/* PathRequest */
typedef struct
{
	float start[3];
	float goal[3];
	uint32_t pathFlags;
	float moveDist;

	struct
	{
		float drawTime;
	} debugging;

	struct
	{
		bool ignoreNodeFlags;
		float minHeight;
		float maxHeight;
		float radius;
	} nodeSearch;

	struct
	{
		float dropHeight;
		float jumpHeight;
	} traversals;

	struct
	{
		float (*array)[3];
		int64_t count;
	} pathPoints;
} rr_path_request_t;

/* PathReturnCode (enum class, int sized) */
enum
{
	RR_PATH_REACHED_GOAL,
	RR_PATH_REACHED_PATH_END,
	RR_PATH_TRAVERSAL_PENDING,
	RR_PATH_RAW_PATH_FOUND,
	RR_PATH_IN_PROGRESS,
	RR_PATH_START_PATH_ERRORS,
	RR_PATH_INVALID_START,
	RR_PATH_INVALID_GOAL,
	RR_PATH_NO_NAV_AVAILABLE,
	RR_PATH_NO_START_NODE,
	RR_PATH_NO_GOAL_NODE,
	RR_PATH_NO_PATH_FOUND,
	RR_PATH_MISSING_WALK_OR_SWIM_FLAG
};

/* PathInfo */
typedef struct
{
	int32_t numPathPoints;
	float pathDistSqr;
	float firstMovePoint[3];
	float secondMovePoint[3];
	int32_t pathLinkType;
	int32_t returnCode;
} rr_path_info_t;

typedef struct
{
	int32_t lighttype; /* shadow_light_type_t: point, cone */
	float radius;
	int32_t resolution;
	float intensity;
	float fade_start;
	float fade_end;
	int32_t lightstyle;
	float coneangle;
	float conedirection[3];
} rr_shadow_light_data_t;

typedef int32_t (*rr_box_edicts_filter_t)(rr_edict_t *ent, void *data);

/* functions the engine provides to the game library */
typedef struct
{
	uint32_t tick_rate;
	float frame_time_s;
	uint32_t frame_time_ms;

	void (*Broadcast_Print)(int32_t printlevel, const char *message);
	void (*Com_Print)(const char *msg);
	void (*Client_Print)(rr_edict_t *ent, int32_t printlevel, const char *message);
	void (*Center_Print)(rr_edict_t *ent, const char *message);

	void (*sound)(rr_edict_t *ent, uint8_t channel, int soundindex, float volume,
		float attenuation, float timeofs);
	void (*positioned_sound)(const float *origin, rr_edict_t *ent, uint8_t channel,
		int soundindex, float volume, float attenuation, float timeofs);
	void (*local_sound)(rr_edict_t *target, const float *origin, rr_edict_t *ent,
		uint8_t channel, int soundindex, float volume, float attenuation,
		float timeofs, uint32_t dupe_key);

	void (*configstring)(int num, const char *string);
	const char *(*get_configstring)(int num);

	void (*Com_Error)(const char *message);

	int (*modelindex)(const char *name);
	int (*soundindex)(const char *name);
	int (*imageindex)(const char *name);

	void (*setmodel)(rr_edict_t *ent, const char *name);

	rr_trace_t (*trace)(const float *start, const float *mins, const float *maxs,
		const float *end, const rr_edict_t *passent, uint32_t contentmask);
	rr_trace_t (*clip)(rr_edict_t *entity, const float *start, const float *mins,
		const float *maxs, const float *end, uint32_t contentmask);
	uint32_t (*pointcontents)(const float *point);
	bool (*inPVS)(const float *p1, const float *p2, bool portals);
	bool (*inPHS)(const float *p1, const float *p2, bool portals);
	void (*SetAreaPortalState)(int portalnum, bool open);
	bool (*AreasConnected)(int area1, int area2);

	void (*linkentity)(rr_edict_t *ent);
	void (*unlinkentity)(rr_edict_t *ent);

	size_t (*BoxEdicts)(const float *mins, const float *maxs, rr_edict_t **list,
		size_t maxcount, int32_t areatype, rr_box_edicts_filter_t filter,
		void *filter_data);

	void (*multicast)(const float *origin, int32_t to, bool reliable);
	void (*unicast)(rr_edict_t *ent, bool reliable, uint32_t dupe_key);

	void (*WriteChar)(int c);
	void (*WriteByte)(int c);
	void (*WriteShort)(int c);
	void (*WriteLong)(int c);
	void (*WriteFloat)(float f);
	void (*WriteString)(const char *s);
	void (*WritePosition)(const float *pos);
	void (*WriteDir)(const float *pos);
	void (*WriteAngle)(float f);
	void (*WriteEntity)(const rr_edict_t *e);

	void *(*TagMalloc)(size_t size, int tag);
	void (*TagFree)(void *block);
	void (*FreeTags)(int tag);

	cvar_t *(*cvar)(const char *var_name, const char *value, uint32_t flags);
	cvar_t *(*cvar_set)(const char *var_name, const char *value);
	cvar_t *(*cvar_forceset)(const char *var_name, const char *value);

	int (*argc)(void);
	const char *(*argv)(int n);
	const char *(*args)(void);

	void (*AddCommandString)(const char *text);

	void (*DebugGraph)(float value, int color);

	void *(*GetExtension)(const char *name);

	/* bots */
	void (*Bot_RegisterEdict)(const rr_edict_t *edict);
	void (*Bot_UnRegisterEdict)(const rr_edict_t *edict);
	int32_t (*Bot_MoveToPoint)(const rr_edict_t *bot, const float *point,
		const float moveTolerance);
	int32_t (*Bot_FollowActor)(const rr_edict_t *bot, const rr_edict_t *actor);

	bool (*GetPathToGoal)(const rr_path_request_t *request, rr_path_info_t *info);

	void (*Loc_Print)(rr_edict_t *ent, int32_t level, const char *base,
		const char **args, size_t num_args);

	/* debug drawing */
	void (*Draw_Line)(const float *start, const float *end, const rr_rgba_t *color,
		const float lifeTime, const bool depthTest);
	void (*Draw_Point)(const float *point, const float size, const rr_rgba_t *color,
		const float lifeTime, const bool depthTest);
	void (*Draw_Circle)(const float *origin, const float radius,
		const rr_rgba_t *color, const float lifeTime, const bool depthTest);
	void (*Draw_Bounds)(const float *mins, const float *maxs, const rr_rgba_t *color,
		const float lifeTime, const bool depthTest);
	void (*Draw_Sphere)(const float *origin, const float radius,
		const rr_rgba_t *color, const float lifeTime, const bool depthTest);
	void (*Draw_OrientedWorldText)(const float *origin, const char *text,
		const rr_rgba_t *color, const float size, const float lifeTime,
		const bool depthTest);
	void (*Draw_StaticWorldText)(const float *origin, const float *angles,
		const char *text, const rr_rgba_t *color, const float size,
		const float lifeTime, const bool depthTest);
	void (*Draw_Cylinder)(const float *origin, const float halfHeight,
		const float radius, const rr_rgba_t *color, const float lifeTime,
		const bool depthTest);
	void (*Draw_Ray)(const float *origin, const float *direction, const float length,
		const float size, const rr_rgba_t *color, const float lifeTime,
		const bool depthTest);
	void (*Draw_Arrow)(const float *start, const float *end, const float size,
		const rr_rgba_t *lineColor, const rr_rgba_t *arrowColor,
		const float lifeTime, const bool depthTest);

	void (*ReportMatchDetails_Multicast)(bool is_end);

	uint32_t (*ServerFrame)(void);

	void (*SendToClipBoard)(const char *text);

	size_t (*Info_ValueForKey)(const char *s, const char *key, char *buffer,
		size_t buffer_len);
	bool (*Info_RemoveKey)(char *s, const char *key);
	bool (*Info_SetValueForKey)(char *s, const char *key, const char *value);
} rr_game_import_t;

/* functions the game library provides to the engine */
typedef struct
{
	int apiversion;

	void (*PreInit)(void);
	void (*Init)(void);
	void (*Shutdown)(void);

	void (*SpawnEntities)(const char *mapname, const char *entstring,
		const char *spawnpoint);

	char *(*WriteGameJson)(bool autosave, size_t *out_size);
	void (*ReadGameJson)(const char *json);

	char *(*WriteLevelJson)(bool transition, size_t *out_size);
	void (*ReadLevelJson)(const char *json);

	bool (*CanSave)(void);

	rr_edict_t *(*ClientChooseSlot)(const char *userinfo, const char *social_id,
		bool isBot, rr_edict_t **ignore, size_t num_ignore, bool cinematic);
	bool (*ClientConnect)(rr_edict_t *ent, char *userinfo, const char *social_id,
		bool isBot);
	void (*ClientBegin)(rr_edict_t *ent);
	void (*ClientUserinfoChanged)(rr_edict_t *ent, const char *userinfo);
	void (*ClientDisconnect)(rr_edict_t *ent);
	void (*ClientCommand)(rr_edict_t *ent);
	void (*ClientThink)(rr_edict_t *ent, rr_usercmd_t *cmd);

	void (*RunFrame)(bool main_loop);
	void (*PrepFrame)(void);

	void (*ServerCommand)(void);

	rr_edict_t *edicts;
	size_t edict_size;
	uint32_t num_edicts;
	uint32_t max_edicts;

	int32_t server_flags;

	void (*Pmove)(rr_pmove_t *pmove);

	void *(*GetExtension)(const char *name);

	void (*Bot_SetWeapon)(rr_edict_t *botEdict, const int weaponIndex,
		const bool instantSwitch);
	void (*Bot_TriggerEdict)(rr_edict_t *botEdict, rr_edict_t *edict);
	void (*Bot_UseItem)(rr_edict_t *botEdict, const int32_t itemID);
	int32_t (*Bot_GetItemID)(const char *classname);
	void (*Edict_ForceLookAtPoint)(rr_edict_t *edict, const float *point);
	bool (*Bot_PickedUpItem)(rr_edict_t *botEdict, rr_edict_t *itemEdict);

	bool (*Entity_IsVisibleToPlayer)(rr_edict_t *ent, rr_edict_t *player);

	const rr_shadow_light_data_t *(*GetShadowLightData)(int32_t entity_number);
} rr_game_export_t;

typedef rr_game_export_t *(*rr_GetGameAPI_t)(rr_game_import_t *import);

#endif /* SV_GAME_RERELEASE_H */
