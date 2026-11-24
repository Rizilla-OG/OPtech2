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
 * optech2: Lua for data menus. A page menus/<name>.menu may come with
 * menus/<name>.lua, run each time the page opens in an environment of its
 * own (globals it sets stay in it; the standard functions and the menu
 * table are read through). The page then names its functions:
 *
 *   "type" "list" "source" "fn"   fn() returns entries, each one becoming
 *                                 an action: { label = "...", command =
 *                                 "...", alttext = "$key", status = "...",
 *                                 action = function() ... end }
 *   "type" "action" "lua" "fn"    fn() runs on enter (after "command")
 *   "if" "lua:fn"                 the item or page is there when fn()
 *                                 returns true
 *   page "open" "fn"              fn() runs before the items are built
 *
 * The same sandbox as the game's scripts: base (no files: dofile,
 * loadfile and require are gone; load takes text only), table, string,
 * math, utf8 and coroutine; every call stops after 1,000,000 instructions;
 * the state may use 16 MB. Errors are printed and the item or page goes
 * on without it.
 *
 * The menu table:
 *   menu.cvar(name)          a cvar's value as text ("" if unset)
 *   menu.value(name)         as a number
 *   menu.set(name, value)    sets it
 *   menu.command(text)       runs a console command (after this call)
 *   menu.open(page)          opens a page (data or stock: "options")
 *   menu.condition(name)     a menu "if" name: gamepad, episodes, ingame...
 *   menu.episodes()          { { name = "Quake II", index = 0 }, ... }
 *   menu.mods()              { "baseq2", "xatrix", ... }
 *   menu.print(...)          to the console
 */

#include "../header/client.h"
#include "header/qmenu.h"

#include "../../game/lua/lua.h"
#include "../../game/lua/lauxlib.h"
#include "../../game/lua/lualib.h"

#define MENULUA_MAX_MEMORY (16 * 1024 * 1024)
#define MENULUA_MAX_INSTRUCTIONS 1000000
#define MENULUA_HOOK_STEP 10000

static lua_State *menulua_state;
static size_t menulua_memory_used;
static int menulua_instructions_left;

/* optech2: an allocator that keeps the state under its memory limit */
static void *
MenuLua_Alloc(void *unused, void *block, size_t old_size, size_t new_size)
{
	if (!block)
	{
		old_size = 0; /* optech2: for a new block old_size is its type, not a size */
	}

	if (new_size == 0)
	{
		free(block);
		menulua_memory_used -= old_size;
		return NULL;
	}

	if ((new_size > old_size) && (menulua_memory_used + (new_size - old_size) > MENULUA_MAX_MEMORY))
	{
		return NULL;
	}

	block = realloc(block, new_size);

	if (block)
	{
		menulua_memory_used = menulua_memory_used - old_size + new_size;
	}

	return block;
}

static void
MenuLua_CountHook(lua_State *L, lua_Debug *unused)
{
	menulua_instructions_left -= MENULUA_HOOK_STEP;

	if (menulua_instructions_left <= 0)
	{
		luaL_error(L, "too many instructions (an endless loop?)");
	}
}

/* optech2: ---- the menu table ---- */

static int
MenuLua_Cvar(lua_State *L)
{
	lua_pushstring(L, Cvar_VariableString(luaL_checkstring(L, 1)));
	return 1;
}

static int
MenuLua_Value(lua_State *L)
{
	lua_pushnumber(L, Cvar_VariableValue(luaL_checkstring(L, 1)));
	return 1;
}

static int
MenuLua_Set(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);

	/* optech2: numbers as the console writes them (1, not 1.0) */
	if (lua_type(L, 2) == LUA_TNUMBER)
	{
		Cvar_SetValue(name, (float)lua_tonumber(L, 2));
	}
	else
	{
		Cvar_Set(name, luaL_tolstring(L, 2, NULL));
	}

	return 0;
}

static int
MenuLua_Command(lua_State *L)
{
	Cbuf_AddText(luaL_checkstring(L, 1));
	Cbuf_AddText("\n");
	return 0;
}

static int
MenuLua_Open(lua_State *L)
{
	const char *page = luaL_checkstring(L, 1);

	/* optech2: through the command buffer, so a page never rebuilds under its own callback;
	   a stock menu's command already opens its data page or falls back to the stock one */
	if (Cmd_Exists(va("menu_%s", page)))
	{
		Cbuf_AddText(va("menu_%s\n", page));
	}
	else
	{
		Cbuf_AddText(va("menu %s\n", page));
	}

	return 0;
}

static int
MenuLua_ApiCondition(lua_State *L)
{
	qboolean known;
	qboolean holds = M_MenuCondition(luaL_checkstring(L, 1), &known);

	lua_pushboolean(L, known && holds);
	return 1;
}

static int
MenuLua_Episodes(lua_State *L)
{
	int count = M_EpisodeCount();
	int i;

	lua_createtable(L, count, 0);

	for (i = 0; i < count; i++)
	{
		lua_createtable(L, 0, 2);
		lua_pushstring(L, M_EpisodeName(i));
		lua_setfield(L, -2, "name");
		lua_pushinteger(L, i);
		lua_setfield(L, -2, "index");
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

static int
MenuLua_Mods(lua_State *L)
{
	int count = M_ModCount();
	int i;

	lua_createtable(L, count, 0);

	for (i = 0; i < count; i++)
	{
		lua_pushstring(L, M_ModName(i));
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

static int
MenuLua_Print(lua_State *L)
{
	char line[1024];
	int count = lua_gettop(L);
	int i;

	line[0] = '\0';

	for (i = 1; i <= count; i++)
	{
		if (i > 1)
		{
			Q_strlcat(line, " ", sizeof(line));
		}

		Q_strlcat(line, luaL_tolstring(L, i, NULL), sizeof(line));
		lua_pop(L, 1);
	}

	Com_Printf("%s\n", line);
	return 0;
}

/* optech2: load(text [, name]): text chunks only */
static int
MenuLua_Load(lua_State *L)
{
	size_t length;
	const char *text = luaL_checklstring(L, 1, &length);
	const char *name = luaL_optstring(L, 2, "=(load)");

	if (luaL_loadbufferx(L, text, length, name, "t") != LUA_OK)
	{
		luaL_pushfail(L);
		lua_insert(L, -2);
		return 2;
	}

	return 1;
}

static void
MenuLua_OpenState(void)
{
	static const luaL_Reg libraries[] = {
		{LUA_GNAME, luaopen_base},
		{LUA_COLIBNAME, luaopen_coroutine},
		{LUA_TABLIBNAME, luaopen_table},
		{LUA_STRLIBNAME, luaopen_string},
		{LUA_MATHLIBNAME, luaopen_math},
		{LUA_UTF8LIBNAME, luaopen_utf8},
		{NULL, NULL}};
	static const luaL_Reg menu_functions[] = {
		{"cvar", MenuLua_Cvar},
		{"value", MenuLua_Value},
		{"set", MenuLua_Set},
		{"command", MenuLua_Command},
		{"open", MenuLua_Open},
		{"condition", MenuLua_ApiCondition},
		{"episodes", MenuLua_Episodes},
		{"mods", MenuLua_Mods},
		{"print", MenuLua_Print},
		{NULL, NULL}};
	static const char *removed[] = {"dofile", "loadfile", "require", NULL};
	const luaL_Reg *library;
	int i;

	menulua_memory_used = 0;
	menulua_state = lua_newstate(MenuLua_Alloc, NULL);

	if (!menulua_state)
	{
		Com_Printf("menus: couldn't start Lua\n");
		return;
	}

	for (library = libraries; library->func; library++)
	{
		luaL_requiref(menulua_state, library->name, library->func, 1);
		lua_pop(menulua_state, 1);
	}

	for (i = 0; removed[i]; i++)
	{
		lua_pushnil(menulua_state);
		lua_setglobal(menulua_state, removed[i]);
	}

	lua_pushcfunction(menulua_state, MenuLua_Print);
	lua_setglobal(menulua_state, "print");
	lua_pushcfunction(menulua_state, MenuLua_Load);
	lua_setglobal(menulua_state, "load");

	luaL_newlib(menulua_state, menu_functions);
	lua_setglobal(menulua_state, "menu");
}

/* optech2: ---- pages ---- */

/* optech2: runs the function under its arguments; leaves one result (or nil) on the stack */
static qboolean
MenuLua_PCall(int argument_count, const char *what)
{
	int result;

	menulua_instructions_left = MENULUA_MAX_INSTRUCTIONS;
	lua_sethook(menulua_state, MenuLua_CountHook, LUA_MASKCOUNT, MENULUA_HOOK_STEP);
	result = lua_pcall(menulua_state, argument_count, 1, 0);
	lua_sethook(menulua_state, NULL, 0, 0);

	if (result != LUA_OK)
	{
		Com_Printf("menu script error in %s: %s\n", what, lua_tostring(menulua_state, -1));
		lua_pop(menulua_state, 1);
		lua_pushnil(menulua_state);
		return false;
	}

	return true;
}

/* optech2: pushes the page's environment table, or returns false (nothing pushed) */
static qboolean
MenuLua_PushPage(const char *page)
{
	if (!menulua_state)
	{
		return false;
	}

	lua_getfield(menulua_state, LUA_REGISTRYINDEX, va("menupage:%s", page));

	if (!lua_istable(menulua_state, -1))
	{
		lua_pop(menulua_state, 1);
		return false;
	}

	return true;
}

/* optech2: runs menus/<page>.lua in a fresh environment; false if there is none or it failed */
qboolean
MenuLua_LoadPage(const char *page)
{
	char file_name[MAX_QPATH];
	char chunk_name[MAX_QPATH + 1];
	char *text;
	int length;

	Com_sprintf(file_name, sizeof(file_name), "menus/%s.lua", page);
	length = FS_LoadFile(file_name, (void **)&text);

	if (length <= 0 || !text)
	{
		return false;
	}

	if (!menulua_state)
	{
		MenuLua_OpenState();
	}

	if (!menulua_state)
	{
		FS_FreeFile(text);
		return false;
	}

	/* optech2: the environment: its own globals, the shared ones read through */
	lua_newtable(menulua_state);
	lua_newtable(menulua_state);
	lua_pushglobaltable(menulua_state);
	lua_setfield(menulua_state, -2, "__index");
	lua_setmetatable(menulua_state, -2);
	lua_newtable(menulua_state); /* optech2: list entries' action functions, by number */
	lua_setfield(menulua_state, -2, "__menu_actions");
	lua_pushvalue(menulua_state, -1);
	lua_setfield(menulua_state, LUA_REGISTRYINDEX, va("menupage:%s", page));

	Com_sprintf(chunk_name, sizeof(chunk_name), "@%s", file_name);

	if (luaL_loadbufferx(menulua_state, text, length, chunk_name, "t") != LUA_OK)
	{
		Com_Printf("menu script error: %s\n", lua_tostring(menulua_state, -1));
		lua_pop(menulua_state, 2);
		FS_FreeFile(text);
		return false;
	}

	FS_FreeFile(text);

	/* optech2: the environment becomes the chunk's _ENV (its first upvalue) */
	lua_pushvalue(menulua_state, -2);
	lua_setupvalue(menulua_state, -2, 1);

	MenuLua_PCall(0, file_name);
	lua_pop(menulua_state, 2); /* optech2: the result and the environment */
	return true;
}

/* optech2: t[field] without metamethods: runs outside any pcall, where a
   script's __index could panic (abort) or loop forever */
static void
MenuLua_RawField(int index, const char *field)
{
	index = lua_absindex(menulua_state, index);
	lua_pushstring(menulua_state, field);
	lua_rawget(menulua_state, index);
}

/* optech2: pushes the page's function of this name; false (nothing pushed) if it has none */
static qboolean
MenuLua_PushFunction(const char *page, const char *function)
{
	if (!MenuLua_PushPage(page))
	{
		return false;
	}

	MenuLua_RawField(-1, function);
	lua_remove(menulua_state, -2);

	if (!lua_isfunction(menulua_state, -1))
	{
		Com_Printf("menus/%s.lua: no function %s\n", page, function);
		lua_pop(menulua_state, 1);
		return false;
	}

	return true;
}

/* optech2: calls the page's function with no arguments */
void
MenuLua_Call(const char *page, const char *function)
{
	if (MenuLua_PushFunction(page, function))
	{
		MenuLua_PCall(0, function);
		lua_pop(menulua_state, 1);
	}
}

/* optech2: the page's function's answer, false on errors */
qboolean
MenuLua_Condition(const char *page, const char *function)
{
	qboolean holds;

	if (!MenuLua_PushFunction(page, function))
	{
		return false;
	}

	MenuLua_PCall(0, function);
	holds = lua_toboolean(menulua_state, -1);
	lua_pop(menulua_state, 1);
	return holds;
}

/* optech2: a list entry's text field, "" if missing */
static void
MenuLua_EntryText(const char *field, char *out, size_t size)
{
	MenuLua_RawField(-1, field);
	Q_strlcpy(out, lua_isstring(menulua_state, -1) ? lua_tostring(menulua_state, -1) : "", size);
	lua_pop(menulua_state, 1);
}

/* optech2: calls the page's list function and hands each entry to add_entry;
   returns how many it handed */
int
MenuLua_List(const char *page, const char *function, menulua_entry_f add_entry, void *context)
{
	int count, i, handed = 0;

	if (!MenuLua_PushFunction(page, function))
	{
		return 0;
	}

	if (!MenuLua_PCall(0, function) || !lua_istable(menulua_state, -1))
	{
		if (!lua_isnil(menulua_state, -1))
		{
			Com_Printf("menus/%s.lua: %s must return a table of entries\n", page, function);
		}

		lua_pop(menulua_state, 1);
		return 0;
	}

	count = (int)lua_rawlen(menulua_state, -1); /* optech2: raw, see MenuLua_RawField */

	for (i = 1; i <= count; i++)
	{
		menulua_entry_t entry;

		lua_rawgeti(menulua_state, -1, i);

		if (!lua_istable(menulua_state, -1))
		{
			lua_pop(menulua_state, 1);
			continue;
		}

		memset(&entry, 0, sizeof(entry));
		MenuLua_EntryText("label", entry.label, sizeof(entry.label));
		MenuLua_EntryText("alttext", entry.alttext, sizeof(entry.alttext));
		MenuLua_EntryText("status", entry.status, sizeof(entry.status));
		MenuLua_EntryText("command", entry.command, sizeof(entry.command));

		/* optech2: an action function is kept in the page's __menu_actions by number */
		MenuLua_RawField(-1, "action");

		if (lua_isfunction(menulua_state, -1) && MenuLua_PushPage(page))
		{
			MenuLua_RawField(-1, "__menu_actions");

			/* optech2: a script may have replaced it; raw writes to a non-table would panic */
			if (lua_istable(menulua_state, -1))
			{
				entry.action = (int)lua_rawlen(menulua_state, -1) + 1;
				lua_pushvalue(menulua_state, -3);
				lua_rawseti(menulua_state, -2, entry.action);
			}

			lua_pop(menulua_state, 2);
		}

		lua_pop(menulua_state, 2); /* optech2: the action field and the entry */

		if (add_entry(&entry, context))
		{
			handed++;
		}
	}

	lua_pop(menulua_state, 1);
	return handed;
}

/* optech2: runs a list entry's action function */
void
MenuLua_CallAction(const char *page, int action)
{
	if ((action <= 0) || !MenuLua_PushPage(page))
	{
		return;
	}

	MenuLua_RawField(-1, "__menu_actions");

	if (!lua_istable(menulua_state, -1))
	{
		lua_pop(menulua_state, 2);
		return;
	}

	lua_rawgeti(menulua_state, -1, action);
	lua_remove(menulua_state, -2);
	lua_remove(menulua_state, -2);

	if (!lua_isfunction(menulua_state, -1))
	{
		lua_pop(menulua_state, 1);
		return;
	}

	MenuLua_PCall(0, "a list entry's action");
	lua_pop(menulua_state, 1);
}

void
MenuLua_Shutdown(void)
{
	if (menulua_state)
	{
		lua_close(menulua_state);
		menulua_state = NULL;
	}
}
