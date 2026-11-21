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
 * optech2: gameplay scripts in sandboxed Lua 5.4.
 *
 * Each folder in the search path stack runs its own script,
 * scripts/<folder>.lua: baseq2 first, then the fs_mods folders in order,
 * then the game folder. So two mods' scripts load side by side, and a
 * script runs in the same order its folder's files override.
 *
 * Scripts get string, table, math, utf8 and coroutine, and the base
 * library without file access (no dofile, loadfile, require, or binary
 * chunks). No io, os, package or debug. Each call is cut off after
 * SCRIPT_MAX_INSTRUCTIONS, and all scripts share SCRIPT_MAX_MEMORY. An
 * error in a handler is printed and the game carries on.
 *
 * The API, in the global table "game":
 *   game.on(event, function)    run function on event (see below)
 *   game.time()                 level time in seconds
 *   game.map()                  map name
 *   game.cvar(name)             a cvar's value, as a string ("" if unset)
 *   game.bprint(text)           print to everyone
 *   game.cprint(player, text)   print to one player
 *   game.centerprint(player, text)
 *   game.name(player)           player name
 *   game.health(player)         health
 *   game.set_health(player, n)
 *   game.give(player, item, n)  add n (default 1) of an item by its
 *                               pickup name, e.g. "Shells"
 *   game.count(player, item)    how many of an item the player has
 *   game.weapon(player)         pickup name of the weapon in hand ("" if none)
 *   game.use(player, item)      use an item the player has, like the "use"
 *                               command: a weapon is switched to; true if
 *                               it was used
 *   game.position(player)       x, y, z of the player's feet
 *   game.yaw(player)            which way the player looks, in degrees
 *                               (0 = +x, 90 = +y)
 *   game.spawn(classname, x, y, z)
 *                               spawn an item or monster there; true if it
 *                               spawned
 * Players are numbers, 1 to maxclients.
 *
 * Events:
 *   level_start(map)            after the level's entities spawned
 *   player_spawn(player)        a player (re)spawned
 *   item_pickup(player, item)   before a pickup (item is its pickup name,
 *                               e.g. "Health"); return false to refuse it.
 *                               Asked on every touch, also for items
 *                               the player can't take (full ammo):
 *                               don't count pickups here.
 *   item_taken(player, item)    after the player took an item
 *   player_damage(player, damage, attacker)
 *                               before a player takes damage; attacker is
 *                               "player" or a classname. Return a number
 *                               to change the damage (0 for none)
 *   monster_damage(monster, damage, player)
 *                               before a monster takes damage; monster is
 *                               its classname, player the attacker's
 *                               number (0 if not a player). Return a
 *                               number to change the damage
 *   player_death(player, attacker)
 *   monster_death(monster, player, x, y, z)
 *                               where it died
 *
 * print() goes to the server console.
 */

#include <ctype.h>
#include <stdarg.h>
#include "../header/local.h"
#include "../lua/lua.h"
#include "../lua/lauxlib.h"
#include "../lua/lualib.h"

#define SCRIPT_MAX_MEMORY (64 * 1024 * 1024)
#define SCRIPT_MAX_INSTRUCTIONS 10000000
#define SCRIPT_HOOK_STEP 10000
#define SCRIPT_MAX_FOLDERS 20

static const char *script_events[] = {"level_start", "player_spawn", "item_pickup", "item_taken",
	"player_damage", "monster_damage", "player_death", "monster_death"};
#define SCRIPT_EVENT_COUNT (int)(sizeof(script_events) / sizeof(script_events[0]))

static lua_State *script_state;
static size_t script_memory_used;
static int script_instructions_left;

/* ---- sandbox ---- */

static void *
Script_Alloc(void *ud, void *ptr, size_t old_size, size_t new_size)
{
	(void)ud;

	if (!ptr)
	{
		old_size = 0; /* old_size is the object type when ptr is NULL */
	}

	if (new_size == 0)
	{
		free(ptr);
		script_memory_used -= old_size;
		return NULL;
	}

	if (new_size > old_size && script_memory_used + (new_size - old_size) > SCRIPT_MAX_MEMORY)
	{
		return NULL; /* Lua raises "not enough memory" */
	}

	ptr = realloc(ptr, new_size);

	if (ptr)
	{
		script_memory_used += new_size;
		script_memory_used -= old_size;
	}

	return ptr;
}

static void
Script_CountHook(lua_State *L, lua_Debug *ar)
{
	(void)ar;
	script_instructions_left -= SCRIPT_HOOK_STEP;

	if (script_instructions_left <= 0)
	{
		luaL_error(L, "script ran too long (endless loop?)");
	}
}

/* print(...) to the server console */
static int
Script_Print(lua_State *L)
{
	char line[1024];
	size_t used = 0;
	int count = lua_gettop(L), i;

	line[0] = '\0';

	for (i = 1; i <= count; i++)
	{
		const char *text = luaL_tolstring(L, i, NULL);

		Com_sprintf(line + used, sizeof(line) - used, "%s%s", i > 1 ? " " : "", text);
		used = strlen(line);
		lua_pop(L, 1);

		if (used >= sizeof(line) - 1)
		{
			break;
		}
	}

	gi.dprintf("%s\n", line);
	return 0;
}

/* load(text [, name]): text chunks only, run in the script environment */
static int
Script_Load(lua_State *L)
{
	size_t length;
	const char *text = luaL_checklstring(L, 1, &length);
	const char *name = luaL_optstring(L, 2, "=(load)");

	if (luaL_loadbufferx(L, text, length, name, "t") != LUA_OK)
	{
		luaL_pushfail(L);
		lua_insert(L, -2);
		return 2; /* nil, message */
	}

	return 1;
}

static void
Script_OpenLibraries(lua_State *L)
{
	static const luaL_Reg libraries[] = {
		{LUA_GNAME, luaopen_base},
		{LUA_COLIBNAME, luaopen_coroutine},
		{LUA_TABLIBNAME, luaopen_table},
		{LUA_STRLIBNAME, luaopen_string},
		{LUA_MATHLIBNAME, luaopen_math},
		{LUA_UTF8LIBNAME, luaopen_utf8},
		{NULL, NULL}};
	static const char *removed[] = {"dofile", "loadfile", "require", NULL};
	const luaL_Reg *library;
	int i;

	for (library = libraries; library->func; library++)
	{
		luaL_requiref(L, library->name, library->func, 1);
		lua_pop(L, 1);
	}

	for (i = 0; removed[i]; i++)
	{
		lua_pushnil(L);
		lua_setglobal(L, removed[i]);
	}

	lua_pushcfunction(L, Script_Print);
	lua_setglobal(L, "print");
	lua_pushcfunction(L, Script_Load);
	lua_setglobal(L, "load");
}

/* ---- the game API ---- */

static edict_t *
Script_CheckPlayer(lua_State *L, int arg)
{
	lua_Integer number = luaL_checkinteger(L, arg);
	edict_t *player;

	if (number < 1 || number > game.maxclients)
	{
		luaL_argerror(L, arg, "no such player");
	}

	player = g_edicts + number;

	if (!player->inuse || !player->client)
	{
		luaL_argerror(L, arg, "player not in the game");
	}

	return player;
}

static int
Script_On(lua_State *L)
{
	const char *event = luaL_checkstring(L, 1);
	int i;

	luaL_checktype(L, 2, LUA_TFUNCTION);

	for (i = 0; i < SCRIPT_EVENT_COUNT; i++)
	{
		if (!strcmp(event, script_events[i]))
		{
			break;
		}
	}

	if (i == SCRIPT_EVENT_COUNT)
	{
		return luaL_argerror(L, 1, "unknown event");
	}

	/* registry[event] is a list of handlers, run in the order added */
	lua_getfield(L, LUA_REGISTRYINDEX, event);
	lua_pushvalue(L, 2);
	lua_rawseti(L, -2, luaL_len(L, -2) + 1);
	return 0;
}

static int
Script_Time(lua_State *L)
{
	lua_pushnumber(L, level.time);
	return 1;
}

static int
Script_Map(lua_State *L)
{
	lua_pushstring(L, level.mapname);
	return 1;
}

static int
Script_Cvar(lua_State *L)
{
	cvar_t *var = gi.cvar(luaL_checkstring(L, 1), NULL, 0);

	lua_pushstring(L, var ? var->string : "");
	return 1;
}

static int
Script_BPrint(lua_State *L)
{
	gi.bprintf(PRINT_HIGH, "%s\n", luaL_checkstring(L, 1));
	return 0;
}

static int
Script_CPrint(lua_State *L)
{
	edict_t *player = Script_CheckPlayer(L, 1);

	gi.cprintf(player, PRINT_HIGH, "%s\n", luaL_checkstring(L, 2));
	return 0;
}

static int
Script_CenterPrint(lua_State *L)
{
	edict_t *player = Script_CheckPlayer(L, 1);

	gi.centerprintf(player, "%s", luaL_checkstring(L, 2));
	return 0;
}

static int
Script_Name(lua_State *L)
{
	lua_pushstring(L, Script_CheckPlayer(L, 1)->client->pers.netname);
	return 1;
}

static int
Script_Health(lua_State *L)
{
	lua_pushinteger(L, Script_CheckPlayer(L, 1)->health);
	return 1;
}

static int
Script_SetHealth(lua_State *L)
{
	edict_t *player = Script_CheckPlayer(L, 1);
	lua_Integer health = luaL_checkinteger(L, 2);

	if (player->health > 0) /* the living only: no raising the dead */
	{
		player->health = (int)Q_clamp(health, 1, 10000);
	}

	return 0;
}

static int
Script_Give(lua_State *L)
{
	edict_t *player = Script_CheckPlayer(L, 1);
	const gitem_t *item = FindItem(luaL_checkstring(L, 2));
	lua_Integer count = luaL_optinteger(L, 3, 1);
	int index;

	if (!item)
	{
		return luaL_argerror(L, 2, "no such item");
	}

	index = ITEM_INDEX(item);
	player->client->pers.inventory[index] = (int)Q_clamp(player->client->pers.inventory[index] + count, 0, 999);
	return 0;
}

static int
Script_Count(lua_State *L)
{
	edict_t *player = Script_CheckPlayer(L, 1);
	const gitem_t *item = FindItem(luaL_checkstring(L, 2));

	if (!item)
	{
		return luaL_argerror(L, 2, "no such item");
	}

	lua_pushinteger(L, player->client->pers.inventory[ITEM_INDEX(item)]);
	return 1;
}

static int
Script_Weapon(lua_State *L)
{
	const gitem_t *weapon = Script_CheckPlayer(L, 1)->client->pers.weapon;

	lua_pushstring(L, (weapon && weapon->pickup_name) ? weapon->pickup_name : "");
	return 1;
}

static int
Script_Use(lua_State *L)
{
	edict_t *player = Script_CheckPlayer(L, 1);
	const gitem_t *item = FindItem(luaL_checkstring(L, 2));

	if (!item)
	{
		return luaL_argerror(L, 2, "no such item");
	}

	if (!item->use || !player->client->pers.inventory[ITEM_INDEX(item)] || player->health < 1)
	{
		lua_pushboolean(L, false);
		return 1;
	}

	item->use(player, item);
	lua_pushboolean(L, true);
	return 1;
}

static int
Script_Position(lua_State *L)
{
	const edict_t *player = Script_CheckPlayer(L, 1);

	lua_pushnumber(L, player->s.origin[0]);
	lua_pushnumber(L, player->s.origin[1]);
	lua_pushnumber(L, player->s.origin[2] + player->mins[2]);
	return 3;
}

static int
Script_Yaw(lua_State *L)
{
	lua_pushnumber(L, Script_CheckPlayer(L, 1)->client->v_angle[YAW]);
	return 1;
}

static int
Script_Spawn(lua_State *L)
{
	const char *classname = luaL_checkstring(L, 1);
	edict_t *ent;

	if (strlen(classname) >= MAX_QPATH)
	{
		return luaL_argerror(L, 1, "too long");
	}

	ent = G_Spawn();
	ent->classname = G_CopyString(classname, TAG_LEVEL);
	ent->s.origin[0] = (float)luaL_checknumber(L, 2);
	ent->s.origin[1] = (float)luaL_checknumber(L, 3);
	ent->s.origin[2] = (float)luaL_checknumber(L, 4);
	ED_CallSpawn(ent);

	if (ent->inuse)
	{
		gi.linkentity(ent);
	}

	lua_pushboolean(L, ent->inuse);
	return 1;
}

static void
Script_OpenGameAPI(lua_State *L)
{
	static const luaL_Reg functions[] = {
		{"on", Script_On},
		{"time", Script_Time},
		{"map", Script_Map},
		{"cvar", Script_Cvar},
		{"bprint", Script_BPrint},
		{"cprint", Script_CPrint},
		{"centerprint", Script_CenterPrint},
		{"name", Script_Name},
		{"health", Script_Health},
		{"set_health", Script_SetHealth},
		{"give", Script_Give},
		{"count", Script_Count},
		{"weapon", Script_Weapon},
		{"use", Script_Use},
		{"spawn", Script_Spawn},
		{"position", Script_Position},
		{"yaw", Script_Yaw},
		{NULL, NULL}};
	int i;

	luaL_newlib(L, functions);
	lua_setglobal(L, "game");

	for (i = 0; i < SCRIPT_EVENT_COUNT; i++)
	{
		lua_newtable(L);
		lua_setfield(L, LUA_REGISTRYINDEX, script_events[i]);
	}
}

/* ---- running scripts ---- */

/* calls the function under its arguments on the stack; leaves its result */
static qboolean
Script_Call(int argument_count, const char *what)
{
	int result;

	script_instructions_left = SCRIPT_MAX_INSTRUCTIONS;
	lua_sethook(script_state, Script_CountHook, LUA_MASKCOUNT, SCRIPT_HOOK_STEP);
	result = lua_pcall(script_state, argument_count, 1, 0);
	lua_sethook(script_state, NULL, 0, 0);

	if (result != LUA_OK)
	{
		gi.dprintf("script error in %s: %s\n", what, lua_tostring(script_state, -1));
		return false;
	}

	return true;
}

static void
Script_RunFile(const char *folder)
{
	char name[MAX_QPATH + 16];
	char chunk_name[MAX_QPATH + 20];
	void *buffer;
	int length;

	G_ModFolderFile(name, sizeof(name), "scripts/", folder, ".lua");
	length = gi.LoadFile(name, &buffer);

	if (length <= 0 || !buffer)
	{
		return;
	}

	Com_sprintf(chunk_name, sizeof(chunk_name), "@%s", name);

	if (luaL_loadbufferx(script_state, buffer, length, chunk_name, "t") != LUA_OK)
	{
		gi.dprintf("script error in %s: %s\n", name, lua_tostring(script_state, -1));
		lua_pop(script_state, 1);
	}
	else if (Script_Call(0, name))
	{
		gi.dprintf("Script loaded: %s\n", name);
		lua_pop(script_state, 1);
	}
	else
	{
		lua_pop(script_state, 1);
	}

	gi.FreeFile(buffer);
}

void
Script_Init(void)
{
	char folders[SCRIPT_MAX_FOLDERS][MAX_QPATH];
	int count, i;

	Script_Shutdown();

	script_memory_used = 0;
	script_state = lua_newstate(Script_Alloc, NULL);

	if (!script_state)
	{
		gi.dprintf("scripts: couldn't start Lua\n");
		return;
	}

	Script_OpenLibraries(script_state);
	Script_OpenGameAPI(script_state);

	count = G_ModFolders(folders, SCRIPT_MAX_FOLDERS);

	for (i = 0; i < count; i++)
	{
		Script_RunFile(folders[i]);
	}
}

void
Script_Shutdown(void)
{
	if (script_state)
	{
		lua_close(script_state);
		script_state = NULL;
	}
}

/*
 * runs every handler of event with arguments described by types, one
 * character each: 'i' an integer, 's' a string, 'n' a number (double).
 * Returns false if any handler returned false; a handler's number result
 * goes to *number_result, if given (the last one wins).
 */
static qboolean
Script_FireV(const char *event, double *number_result, const char *types, ...)
{
	qboolean allowed = true;
	lua_Integer count, i;

	if (!script_state)
	{
		return true;
	}

	lua_getfield(script_state, LUA_REGISTRYINDEX, event);
	count = luaL_len(script_state, -1);

	for (i = 1; i <= count; i++)
	{
		const char *type;
		va_list arguments;

		lua_rawgeti(script_state, -1, i);
		va_start(arguments, types);

		for (type = types; *type; type++)
		{
			switch (*type)
			{
				case 'i':
					lua_pushinteger(script_state, va_arg(arguments, int));
					break;
				case 's':
					lua_pushstring(script_state, va_arg(arguments, const char *));
					break;
				default:
					lua_pushnumber(script_state, va_arg(arguments, double));
					break;
			}
		}

		va_end(arguments);

		if (Script_Call((int)strlen(types), event))
		{
			if (lua_isboolean(script_state, -1) && !lua_toboolean(script_state, -1))
			{
				allowed = false;
			}
			else if (number_result && lua_type(script_state, -1) == LUA_TNUMBER)
			{
				*number_result = lua_tonumber(script_state, -1);
			}
		}

		lua_pop(script_state, 1);
	}

	lua_pop(script_state, 1);
	return allowed;
}

static qboolean
Script_Fire(const char *event, int player, const char *text)
{
	if (player > 0 && text)
	{
		return Script_FireV(event, NULL, "is", player, text);
	}
	else if (player > 0)
	{
		return Script_FireV(event, NULL, "i", player);
	}

	return Script_FireV(event, NULL, text ? "s" : "", text);
}

/* who did it: a player's number, else 0 */
static int
Script_PlayerNumber(const edict_t *ent)
{
	return (ent && ent->client) ? (int)(ent - g_edicts) : 0;
}

/* what did it: "player" for players, else the classname ("worldspawn"
   for falling, lava, ...) */
static const char *
Script_AttackerName(const edict_t *ent)
{
	if (!ent)
	{
		return "";
	}

	return ent->client ? "player" : (ent->classname ? ent->classname : "");
}

static int
Script_Damage(double result, int damage)
{
	if (result != (double)damage)
	{
		damage = (int)Q_clamp(result, 0.0, 100000.0);
	}

	return damage;
}

int
Script_PlayerDamage(edict_t *player, edict_t *attacker, int damage)
{
	double result = damage;

	Script_FireV("player_damage", &result, "iis", (int)(player - g_edicts), damage, Script_AttackerName(attacker));
	return Script_Damage(result, damage);
}

int
Script_MonsterDamage(edict_t *monster, edict_t *attacker, int damage)
{
	double result = damage;

	Script_FireV("monster_damage", &result, "sii", monster->classname ? monster->classname : "", damage,
		Script_PlayerNumber(attacker));
	return Script_Damage(result, damage);
}

void
Script_PlayerDeath(edict_t *player, edict_t *attacker)
{
	Script_FireV("player_death", NULL, "is", (int)(player - g_edicts), Script_AttackerName(attacker));
}

void
Script_MonsterDeath(edict_t *monster, edict_t *attacker)
{
	Script_FireV("monster_death", NULL, "sinnn", monster->classname ? monster->classname : "",
		Script_PlayerNumber(attacker), (double)monster->s.origin[0], (double)monster->s.origin[1],
		(double)monster->s.origin[2]);
}

void
Script_LevelStart(void)
{
	Script_Fire("level_start", 0, level.mapname);
}

void
Script_PlayerSpawn(edict_t *player)
{
	Script_Fire("player_spawn", (int)(player - g_edicts), NULL);
}

qboolean
Script_ItemPickup(edict_t *player, const gitem_t *item)
{
	return Script_Fire("item_pickup", (int)(player - g_edicts), item->pickup_name);
}

void
Script_ItemTaken(edict_t *player, const gitem_t *item)
{
	Script_Fire("item_taken", (int)(player - g_edicts), item->pickup_name);
}

/* "lua <code>": run a line of Lua (cheat, for testing scripts) */
void
Script_Command(edict_t *ent)
{
	if (!script_state)
	{
		gi.cprintf(ent, PRINT_HIGH, "scripts aren't running\n");
		return;
	}

	if (luaL_loadbufferx(script_state, gi.args(), strlen(gi.args()), "=(console)", "t") != LUA_OK)
	{
		gi.cprintf(ent, PRINT_HIGH, "%s\n", lua_tostring(script_state, -1));
		lua_pop(script_state, 1);
		return;
	}

	Script_Call(0, "console");
	lua_pop(script_state, 1);
}
