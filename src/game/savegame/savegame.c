/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2011 Knightmare
 * Copyright (C) 2011 Yamagi Burmeister
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
 * The savegame system. Unused by the CTF game but nevertheless called
 * during game initialization. Therefor no new savegame code ist
 * imported.
 *
 * =======================================================================
 */

/*
 * This is the Quake 2 savegame system, fixed by Yamagi
 * based on an idea by Knightmare of kmquake2. This major
 * rewrite of the original g_save.c is much more robust
 * and portable since it doesn't use any function pointers.
 *
 * Inner workings:
 * When the game is saved all function pointers are
 * translated into human readable function definition strings.
 * The same way all mmove_t pointers are translated. This
 * human readable strings are then written into the file.
 * At game load the human readable strings are retranslated
 * into the actual function pointers and struct pointers. The
 * pointers are generated at each compilation / start of the
 * client, thus the pointers are always correct.
 *
 * Limitations:
 * While savegames survive recompilations of the game source
 * and bigger changes in the source, there are some limitation
 * which a nearly impossible to fix without a object orientated
 * rewrite of the game.
 *  - If functions or mmove_t structs that a referencenced
 *    inside savegames are added or removed (e.g. the files
 *    in tables/ are altered) the load functions cannot
 *    reconnect all pointers and thus not restore the game.
 *  - If the operating system is changed internal structures
 *    may change in an unrepairable way.
 *  - If the architecture is changed pointer length and
 *    other internal datastructures change in an
 *    incompatible way.
 *  - If the edict_t struct is changed, savegames
 *    will break.
 * This is not so bad as it looks since functions and
 * struct won't be added and edict_t won't be changed
 * if no big, sweeping changes are done. The operating
 * system and architecture are in the hands of the user.
 */

#include "../../common/header/common.h" // YQ2ARCH
#include "../header/local.h"
#include "savegame.h"
#include <stddef.h> /* optech2: offsetof for the save field table */

/*
 * When ever the savegame version is changed, q2 will refuse to
 * load older savegames. This should be bumped if the files
 * in tables/ are changed, otherwise strange things may happen.
 */
#define SAVEGAMEVER "OPT2-1" /* optech2: tagged format (was "YQ2-6") */

/* optech2: first version number of the tagged format; older numbers are
   Yamagi's raw struct format, still read */
#define SAVE_VERSION_TAGGED 100

/* optech2: a tagged level file starts with this (old ones start with
   sizeof(edict_t), always positive) */
#define LEVEL_FILE_TAGGED (-0x4F505432) /* -"OPT2" */

/* optech2: set while reading Yamagi's raw struct format. That format only
   loads in a build with the same struct layout; broken data then means an
   older build, which the error message should say. */
static qboolean reading_raw_format;

#define OLD_SAVE_MESSAGE "This savegame is from an older build in the old save format, "          \
						 "which only loads in a build with the same game structures. Saves made " \
						 "now keep loading after updates."

#ifndef BUILD_DATE
#define BUILD_DATE __DATE__
#endif

/*
 * This macros are used to prohibit loading of savegames
 * created on other systems or architectures. This will
 * crash q2 in spectacular ways
 */
#ifndef YQ2OSTYPE
#error YQ2OSTYPE should be defined by the build system
#endif

#ifndef YQ2ARCH
#error YQ2ARCH should be defined by the build system
#endif

/*
 * Older operating system and architecture detection
 * macros, implemented by savegame version YQ2-1.
 */
#if defined(__APPLE__)
#define OSTYPE_1 "MacOS X"
#elif defined(__FreeBSD__)
#define OSTYPE_1 "FreeBSD"
#elif defined(__OpenBSD__)
#define OSTYPE_1 "OpenBSD"
#elif defined(__linux__)
 #define OSTYPE_1 "Linux"
#elif defined(_WIN32)
 #define OSTYPE_1 "Windows"
#else
 #define OSTYPE_1 "Unknown"
#endif

#if defined(__i386__)
#define ARCH_1 "i386"
#elif defined(__x86_64__)
#define ARCH_1 "amd64"
#elif defined(__sparc__)
#define ARCH_1 "sparc64"
#elif defined(__ia64__)
 #define ARCH_1 "ia64"
#else
 #define ARCH_1 "unknown"
#endif

/* ========================================================= */

/*
 * Prototypes for forward
 * declaration for all game
 * functions.
 */
#include "tables/gamefunc_decs.h"

/*
 * List with function pointer
 * to each of the functions
 * prototyped above.
 */
#include "tables/gamefunc_list.h"

/*
 * Prototypes for forward
 * declaration for all game
 * mmove_t functions.
 */
#include "tables/gamemmove_decs.h"

/*
 * List with pointers to
 * each of the mmove_t
 * functions prototyped
 * above.
 */
static const mmoveList_t mmoveList[] = {
	#include "tables/gamemmove_list.h"
};

/*
 * Spawntemp fields for entity spawning
 */
static const field_t stfields[] = {
	#include "tables/stfields.h"
};

/*
 * Entity fields to be saved
 */
static const fplist_entry_t fpentries_ent[] =
{
	{FOFS(die), &fnlist_die},
	{FOFS(pain), &fnlist_pain},
	{FOFS(prethink), &fnlist_prethink},
	{FOFS(think), &fnlist_think},
	{FOFS(touch), &fnlist_touch},
	{FOFS(use), &fnlist_use},
	{FOFS(blocked), &fnlist_blocked},
	{FOFS(monsterinfo.sight), &fnlist_mi_sight},
	{FOFS(monsterinfo.search), &fnlist_mi_search},
	{FOFS(monsterinfo.stand), &fnlist_mi_stand},
	{FOFS(monsterinfo.idle), &fnlist_mi_idle},
	{FOFS(monsterinfo.walk), &fnlist_mi_walk},
	{FOFS(monsterinfo.run), &fnlist_mi_run},
	{FOFS(monsterinfo.melee), &fnlist_mi_melee},
	{FOFS(monsterinfo.attack), &fnlist_mi_attack},
	{FOFS(monsterinfo.checkattack), &fnlist_mi_checkattack},
	{FOFS(monsterinfo.dodge), &fnlist_mi_dodge},
	{FOFS(monsterinfo.blocked), &fnlist_mi_blocked},
	{FOFS(monsterinfo.duck), &fnlist_mi_duck},
	{FOFS(monsterinfo.unduck), &fnlist_mi_unduck},
	{FOFS(monsterinfo.sidestep), &fnlist_mi_sidestep},
	{FOFS(moveinfo.endfunc), &fnlist_mv_end}
};
static const fptrList_t fplist_ent =
{
	fpentries_ent, ARREND(fpentries_ent)
};

static const field_t entfields[] = {
	#include "tables/entfields.h"
};
static const structdef_t sd_ent =
{
	entfields, ARREND(entfields),
	&fplist_ent,
	sizeof(edict_t)
};

/*
 * Level fields to be saved
 */
static const field_t levelfields[] = {
	#include "tables/levelfields.h"
};
static const structdef_t sd_level =
{
	levelfields, ARREND(levelfields),
	NULL,
	sizeof(level_locals_t)
};

/*
 * Client fields to be saved
 */
static const field_t clientfields[] = {
	#include "tables/clientfields.h"
};
static const structdef_t sd_client =
{
	clientfields, ARREND(clientfields),
	NULL,
	sizeof(gclient_t)
};

/*
 * Game struct to be saved
 */
static const structdef_t sd_game =
{
	NULL, NULL,
	NULL,
	sizeof(game_locals_t)
};

/* optech2: every field of the tagged format, generated */
#include "tables/savefields.h"

/* ========================================================= */

static void
sg_fread(void *dest, size_t n, FILE *f)
{
	if (fread(dest, n, 1, f) != 1)
	{
		fclose(f);
		gi.error("Error reading " YQ2_COM_PRIdS " bytes from save file", n);
	}
}

static void
sg_fwrite(const void *src, size_t n, FILE *f)
{
	if (fwrite(src, n, 1, f) != 1)
	{
		fclose(f);
		gi.error("Error writing " YQ2_COM_PRIdS " bytes to save file", n);
	}
}

const field_t *
FindSpawntempField(const char *key)
{
	const field_t *f;

	for (f = stfields; f < ARREND(stfields); f++)
	{
		if (!Q_strcasecmp(f->name, key))
		{
			return f;
		}
	}

	return NULL;
}

const field_t *
FindSpawnfield(const char *key)
{
	const field_t *f;

	for (f = entfields; f < ARREND(entfields); f++)
	{
		if (!(f->flags & FFL_NOSPAWN) && !Q_strcasecmp(f->name, key))
		{
			return f;
		}
	}

	return NULL;
}

static void
InitAllocations(void)
{
	int num_c = maxclients->value;
	int num_e = maxentities->value;

	if (num_c < 1)
	{
		num_c = 1;
		gi.cvar_forceset("maxclients", "1");
	}

	if (num_e < (num_c + 1))
	{
		num_e = num_c + 1;
		gi.cvar_forceset("maxentities", va("%d", num_c + 1));
	}

	/* optech2: engine and game lists hold at most MAX_EDICTS */
	if (num_e > MAX_EDICTS)
	{
		num_e = MAX_EDICTS;
		gi.cvar_forceset("maxentities", va("%d", MAX_EDICTS));
	}

	g_edicts = gi.TagMalloc (num_e * sizeof(g_edicts[0]), TAG_GAME);
	game.maxentities = num_e;

	globals.edicts = g_edicts;
	globals.num_edicts = num_c + 1;
	globals.max_edicts = num_e;

	game.clients = gi.TagMalloc (num_c * sizeof(game.clients[0]), TAG_GAME);
	game.maxclients = num_c;
}

void
ReinitGameEntities(int ent_cnt)
{
	int num_c = maxclients->value;
	int num_e = maxentities->value;
	edict_t *edicts;

	if (num_c < 1)
	{
		num_c = 1;
	}

	if (num_e < (num_c + 1 + ent_cnt * 2))
	{
		num_e = num_c + 1 + ent_cnt * 2;
	}

	/* optech2: engine and game lists hold at most MAX_EDICTS */
	if (num_e > MAX_EDICTS)
	{
		num_e = MAX_EDICTS;
	}

	if (num_e <= game.maxentities)
	{
		return;
	}

	edicts = gi.TagRealloc(g_edicts, num_e * sizeof(g_edicts[0]), TAG_GAME);
	if (!edicts)
	{
		gi.error("Can't extend edict list to %d\n", num_e);
		return;
	}

	g_edicts = edicts;

	game.maxentities = num_e;

	globals.edicts = g_edicts;
	globals.num_edicts = num_c + 1;
	globals.max_edicts = num_e;
}

/*
 * This will be called when the dll is first loaded,
 * which only happens when a new game is started or
 * a save game is loaded.
 */
void
InitGame(void)
{
	gi.dprintf("Game is starting up.\n");
	gi.dprintf("Game is %s built on %s.\n", GAMEVERSION, BUILD_DATE);

	G_ScreenFader_Reset();

	gun_x = gi.cvar("gun_x", "0", 0);
	gun_y = gi.cvar("gun_y", "0", 0);
	gun_z = gi.cvar("gun_z", "0", 0);
	sv_rollspeed = gi.cvar("sv_rollspeed", "200", 0);
	sv_rollangle = gi.cvar("sv_rollangle", "2", 0);
	sv_maxvelocity = gi.cvar("sv_maxvelocity", "2000", 0);
	sv_gravity = gi.cvar("sv_gravity", "800", 0);
	sv_tickrate = gi.cvar("sv_tickrate", "60", CVAR_SERVERINFO | CVAR_LATCH); /* optech2 */
	g_damagelog = gi.cvar("g_damagelog", "0", 0); /* optech2: 1 = print every hit (tests) */
	g_skip_cameras = gi.cvar("g_skip_cameras", "0", CVAR_ARCHIVE); /* optech2: off, as the re-release */
	g_classic_balance = gi.cvar("g_classic_balance", "0", CVAR_ARCHIVE); /* optech2: re-release numbers by default */
	g_coop_squad_respawn = gi.cvar("g_coop_squad_respawn", "1", CVAR_LATCH); /* optech2: re-release defaults */
	g_coop_enable_lives = gi.cvar("g_coop_enable_lives", "0", CVAR_LATCH);
	g_coop_num_lives = gi.cvar("g_coop_num_lives", "2", CVAR_LATCH);
	g_monster_nav = gi.cvar("g_monster_nav", "1", 0); /* optech2: monsters follow the navigation mesh */
	g_monster_freeze = gi.cvar("g_monster_freeze", "0", 0); /* optech2: monsters stand still, for tests */
	sv_stopspeed = gi.cvar("sv_stopspeed", "100", 0);
	g_showlogic = gi.cvar("g_showlogic", "0", 0);
	huntercam = gi.cvar("huntercam", "1", CVAR_SERVERINFO|CVAR_LATCH);
	strong_mines = gi.cvar("strong_mines", "0", 0);
	randomrespawn = gi.cvar("randomrespawn", "0", 0);

	/* noset vars */
	dedicated = gi.cvar("dedicated", "0", CVAR_NOSET);

	/* latched vars */
	sv_cheats = gi.cvar("cheats", "0", CVAR_SERVERINFO | CVAR_LATCH);
	gi.cvar("gamename", GAMEVERSION, CVAR_SERVERINFO | CVAR_LATCH);
	gi.cvar("gamedate", BUILD_DATE, CVAR_SERVERINFO | CVAR_LATCH);
	maxclients = gi.cvar("maxclients", "4", CVAR_SERVERINFO | CVAR_LATCH);
	maxspectators = gi.cvar("maxspectators", "4", CVAR_SERVERINFO);
	deathmatch = gi.cvar("deathmatch", "0", CVAR_LATCH);
	coop = gi.cvar("coop", "0", CVAR_LATCH);
	coop_pickup_weapons = gi.cvar("coop_pickup_weapons", "1", CVAR_ARCHIVE);
	coop_baseq2 = gi.cvar("coop_baseq2", "0", CVAR_LATCH);
	coop_elevator_delay = gi.cvar("coop_elevator_delay", "1.0", CVAR_ARCHIVE);
	skill = gi.cvar("skill", "1", CVAR_LATCH);
	maxentities = gi.cvar("maxentities", "8192", CVAR_LATCH); /* optech2: was 1024 */
	gamerules = gi.cvar("gamerules", "0", CVAR_LATCH);			//PGM
	g_footsteps = gi.cvar("g_footsteps", "1", CVAR_ARCHIVE);
	g_monsterfootsteps = gi.cvar("g_monsterfootsteps", "0", CVAR_ARCHIVE);
	g_fix_triggered = gi.cvar("g_fix_triggered", "0", 0);
	g_commanderbody_nogod = gi.cvar("g_commanderbody_nogod", "0", CVAR_ARCHIVE);

	/* change anytime vars */
	dmflags = gi.cvar("dmflags", "0", CVAR_SERVERINFO);
	fraglimit = gi.cvar("fraglimit", "0", CVAR_SERVERINFO);
	timelimit = gi.cvar("timelimit", "0", CVAR_SERVERINFO);
	capturelimit = gi.cvar("capturelimit", "0", CVAR_SERVERINFO);
	instantweap = gi.cvar("instantweap", "0", CVAR_SERVERINFO);
	password = gi.cvar("password", "", CVAR_USERINFO);
	spectator_password = gi.cvar("spectator_password", "", CVAR_USERINFO);
	needpass = gi.cvar("needpass", "0", CVAR_SERVERINFO);
	filterban = gi.cvar("filterban", "1", 0);
	g_select_empty = gi.cvar("g_select_empty", "0", CVAR_ARCHIVE);
	run_pitch = gi.cvar("run_pitch", "0.002", 0);
	run_roll = gi.cvar("run_roll", "0.005", 0);
	bob_up = gi.cvar("bob_up", "0.005", 0);
	bob_pitch = gi.cvar("bob_pitch", "0.002", 0);
	bob_roll = gi.cvar("bob_roll", "0.002", 0);

	/* flood control */
	flood_msgs = gi.cvar("flood_msgs", "4", 0);
	flood_persecond = gi.cvar("flood_persecond", "4", 0);
	flood_waitdelay = gi.cvar("flood_waitdelay", "10", 0);

	/* dm map list */
	sv_maplist = gi.cvar("sv_maplist", "", 0);

	/* disruptor availability */
	g_disruptor = gi.cvar("g_disruptor", "0", 0);

	/* others */
	aimfix = gi.cvar("aimfix", "0", CVAR_ARCHIVE);
	g_machinegun_norecoil = gi.cvar("g_machinegun_norecoil", "0", CVAR_ARCHIVE);
	g_quick_weap = gi.cvar("g_quick_weap", "1", CVAR_ARCHIVE);
	g_swap_speed = gi.cvar("g_swap_speed", "1", CVAR_ARCHIVE);
	g_itemsbobeffect = gi.cvar("g_itemsbobeffect", "0", CVAR_ARCHIVE);
	g_thirdperson = gi.cvar("g_thirdperson", "0", CVAR_ARCHIVE);
	g_game = gi.cvar("game", "", 0);
	g_start_items = gi.cvar("g_start_items", "", 0);
	ai_model_scale = gi.cvar("ai_model_scale", "0", 0);

	/* initilize dynamic object spawn */
	SpawnInit();

	memset(&game, 0, sizeof(game));

	InitItems();

	/* initialize entities and clients arrays */
	InitAllocations();

	if (gamerules)
	{
		InitGameRules();
	}

	CTFInit();

	AI_Init();//JABot
}

/* ========================================================= */

static const functionList_t *
GetFunctionList(int field_ofs, const fptrList_t *fpl)
{
	const fplist_entry_t *fpe;

	if (!fpl)
	{
		return NULL;
	}

	for (fpe = fpl->start; fpe < fpl->end; fpe++)
	{
		if (fpe->ofs == field_ofs)
		{
			return fpe->fnlist;
		}
	}

	return NULL;
}

/*
 * Helper function to get
 * the human readable function
 * definition by an address.
 * Called by WriteField1 and
 * WriteField2.
 */
static const fnlist_entry_t *
GetFunctionByAddress(const byte *adr, const functionList_t *fnl)
{
	const fnlist_entry_t *fne;

	if (!fnl)
	{
		return NULL;
	}

	for (fne = fnl->start; fne < fnl->end; fne++)
	{
		if (fne->funcPtr == adr)
		{
			return fne;
		}
	}

	return NULL;
}

/*
 * Helper function to get the
 * pointer to a function by
 * its human readable name.
 * Called by WriteField1 and
 * WriteField2.
 */
static const byte *
FindFunctionByName(const char *name, const functionList_t *fnl)
{
	const fnlist_entry_t *fne;

	if (!fnl)
	{
		return NULL;
	}

	for (fne = fnl->start; fne < fnl->end; fne++)
	{
		if (!strcmp(name, fne->funcStr))
		{
			return fne->funcPtr;
		}
	}

	return NULL;
}

/*
 * Helper function to get the
 * human readable definition of
 * a mmove_t struct by a pointer.
 */
static const mmoveList_t *
GetMmoveByAddress(const mmove_t *adr)
{
	const mmoveList_t *mml;

	for (mml = mmoveList; mml < ARREND(mmoveList); mml++)
	{
		if (mml->mmovePtr == adr)
		{
			return mml;
		}
	}

	return NULL;
}

/*
 * Helper function to get the
 * pointer to a mmove_t struct
 * by a human readable definition.
 */
static const mmove_t *
FindMmoveByName(const char *name)
{
	const mmoveList_t *mml;

	for (mml = mmoveList; mml < ARREND(mmoveList); mml++)
	{
		if (!strcmp(name, mml->mmoveStr))
		{
			return mml->mmovePtr;
		}
	}

	return NULL;
}

/* ========================================================= */

/* int because that is how it's stored in the file */
static void
ReadStringToBuf(FILE *f, int len, char *out, size_t out_sz)
{
	*out = 0;

	if (!len)
	{
		return;
	}

	if (len < 0)
	{
		fclose(f);
		if (reading_raw_format)
		{
			gi.error(OLD_SAVE_MESSAGE);
			return;
		}

		gi.error("%s: string length < 0", __func__);
		return;
	}

	if (len >= (int)out_sz)
	{
		fclose(f);
		if (reading_raw_format)
		{
			gi.error(OLD_SAVE_MESSAGE);
			return;
		}

		gi.error("%s: string is too long for buffer: %i > %i ",
				__func__, len, (int)out_sz);
		return;
	}

	sg_fread(out, len, f);
	out[len] = 0;
}

/* int because that is how it's stored in the file */
static char *
ReadString(FILE *f, int len, int tag)
{
	char *s;

	if (!len)
	{
		return NULL;
	}

	/* optech2: up to a script's saved state (1 MB, game.state), which
	   saved fine but failed to load past MAX_TOKEN_CHARS */
	if (len < 0 || len > (1024 * 1024 + 1))
	{
		fclose(f);
		if (reading_raw_format)
		{
			gi.error(OLD_SAVE_MESSAGE);
			return NULL;
		}

		gi.error("%s: bad string length: %i", __func__, len);
		return NULL;
	}

	s = gi.TagMalloc(len + 1, tag);
	if (!s)
	{
		fclose(f);
		gi.error("%s: can't allocate memory for string", __func__);
		return NULL;
	}

	sg_fread(s, len, f);
	s[len] = 0;

	return s;
}

static const byte *
ReadFunction(FILE *f, int len, const functionList_t *fnl)
{
	char funcStr[128];
	const byte *fn;

	if (!len)
	{
		return NULL;
	}

	ReadStringToBuf(f, len, funcStr, sizeof(funcStr));

	fn = FindFunctionByName(funcStr, fnl);
	if (!fn)
	{
		gi.dprintf("%s: function %s not found in %s\n",
			__func__, funcStr, fnl ? fnl->name : "unknown list");
	}

	return fn;
}

static const mmove_t *
ReadMmove(FILE *f, int len)
{
	char mmoveStr[128];
	const mmove_t *mm;

	if (!len)
	{
		return NULL;
	}

	ReadStringToBuf(f, len, mmoveStr, sizeof(mmoveStr));

	mm = FindMmoveByName(mmoveStr);
	if (!mm)
	{
		gi.dprintf("%s: mmove %s not found\n",
			__func__, mmoveStr);
	}

	return mm;
}

/*
 * This function does the dirty
 * work to read the data from a
 * file. The processing of the
 * data is done in the functions
 * below
 */
static void
ReadField(FILE *f, const field_t *field, void *base, const fptrList_t *fpl)
{
	void *p;
	int len;
	int index;

	p = (byte *)base + field->ofs;

	switch (field->type)
	{
		case F_INT:
		case F_FLOAT:
		case F_ANGLEHACK:
		case F_VECTOR:
		case F_IGNORE:
		case F_RGBA:
		case F_SCALE:
			break;

		case F_LSTRING:
		case F_LRAWSTRING:
			len = *(int *)p;
			*(char **)p = ReadString(f, len, TAG_LEVEL);
			break;
		case F_GRAWSTRING:
			len = *(int *)p;
			*(char **)p = ReadString(f, len, TAG_GAME);
			break;
		case F_EDICT:
			index = *(int *)p;

			if ((index < 0) || (index >= game.maxentities))
			{
				*(edict_t **)p = NULL;
			}
			else
			{
				*(edict_t **)p = &g_edicts[index];
			}

			break;
		case F_ITEM:
			index = *(int *)p;
			*(gitem_t **)p = GetItemByIndex(index);
			break;
		case F_FUNCTION:
			*(const byte **)p = ReadFunction(f, *(int *)p, GetFunctionList(field->ofs, fpl));
			break;
		case F_MMOVE:
			*(const mmove_t **)p = ReadMmove(f, *(int *)p);
			break;
		default:
			fclose(f);
			gi.error("%s: unknown field type", __func__);
	}
}

static void
ReadStruct(FILE *f, void *base, const structdef_t *sd, short save_ver)
{
	const field_t *field;

	sg_fread(base, sd->size, f);

	for (field = sd->fields_start; field < sd->fields_end; field++)
	{
		if (field->save_ver <= save_ver)
		{
			ReadField(f, field, base, sd->fplist);
		}
	}
}

/* ========================================================= */

/*
 * optech2: the tagged format. Each struct type is written as a schema
 * (field names, kinds and sizes) followed by records holding only the
 * values, in schema order. On load the schema is matched to the current
 * field table by name: fields the save lacks keep their default, fields
 * the code no longer has (or whose size changed) are skipped. So saves
 * survive added, removed and reordered fields.
 */

typedef struct
{
	const savefield_t *field; /* NULL: not in this build, skip */
	int kind;
	int size;
} schemafield_t;

static void
SG_WriteInt(FILE *f, int value)
{
	sg_fwrite(&value, sizeof(value), f);
}

static int
SG_ReadInt(FILE *f)
{
	int value;

	sg_fread(&value, sizeof(value), f);
	return value;
}

/* a string as its length (with the zero, 0 for NULL) and its bytes */
static void
SG_WriteString(FILE *f, const char *text)
{
	int len = text ? (int)strlen(text) + 1 : 0;

	SG_WriteInt(f, len);

	if (len)
	{
		sg_fwrite(text, len, f);
	}
}

static void
SG_WriteSchema(FILE *f, const savefield_t *fields, int count)
{
	int i;

	SG_WriteInt(f, count);

	for (i = 0; i < count; i++)
	{
		SG_WriteString(f, fields[i].name);
		SG_WriteInt(f, fields[i].kind);
		SG_WriteInt(f, (int)fields[i].size);
	}
}

/*
 * optech2: saved item number -> current item number, from the name table
 * at the end of the file being read (see WriteItemsNames). Without a table
 * the numbers are used as they are.
 */
#define SG_ITEM_TABLE_MAGIC "OTIT"

static int sg_item_remap[MAX_ITEMS];
static int sg_item_remap_count;
static qboolean sg_item_remap_active;

static int
SG_RemapItem(int saved)
{
	if (!sg_item_remap_active || saved <= 0)
	{
		return saved;
	}

	return (saved < sg_item_remap_count) ? sg_item_remap[saved] : 0;
}

/* reads the item name table at the end of f, if it has one; leaves the
   file position where it was */
static qboolean
SG_LoadItemTable(FILE *f, const char *what)
{
	long start = ftell(f);
	char magic[4];
	char name[MAX_QPATH + 1];
	int count, i;

	sg_item_remap_active = false;

	if (fseek(f, -(long)(sizeof(count) + sizeof(magic)), SEEK_END) != 0 ||
		fread(&count, sizeof(count), 1, f) != 1 || fread(magic, sizeof(magic), 1, f) != 1 ||
		memcmp(magic, SG_ITEM_TABLE_MAGIC, sizeof(magic)) || count < 0 || count >= MAX_ITEMS ||
		fseek(f, -(long)(sizeof(count) + sizeof(magic) + (size_t)count * MAX_QPATH), SEEK_END) != 0)
	{
		fseek(f, start, SEEK_SET);
		return false;
	}

	sg_item_remap[0] = 0;
	sg_item_remap_count = count + 1;

	for (i = 1; i <= count; i++)
	{
		const gitem_t *item = NULL;

		if (fread(name, MAX_QPATH, 1, f) != 1)
		{
			fseek(f, start, SEEK_SET);
			return false;
		}

		name[MAX_QPATH] = '\0';

		if (name[0] == '*')
		{
			item = FindItem(name + 1); /* an item without classname, by pickup name */
		}
		else if (name[0])
		{
			item = FindItemByClassname(name);
		}
		else if (i < itemlist_len && !itemlist[i].classname)
		{
			/* saves from before the "*" names: an item without classname
			   (like "Health") at the same number */
			item = &itemlist[i];
		}

		if (!item && name[0])
		{
			gi.dprintf("%s: the save has an item this game doesn't, %s; dropped\n", what, name);
		}

		sg_item_remap[i] = item ? ITEM_INDEX(item) : 0;
	}

	sg_item_remap_active = true;
	fseek(f, start, SEEK_SET);
	return true;
}

/* a client's item numbers that aren't pointers: inventories and the
   selected item */
static void
SG_RemapClientItems(gclient_t *client)
{
	client_persistant_t *lists[2];
	int saved[MAX_ITEMS];
	int l, i;

	if (!sg_item_remap_active)
	{
		return;
	}

	lists[0] = &client->pers;
	lists[1] = &client->resp.coop_respawn;

	for (l = 0; l < 2; l++)
	{
		memcpy(saved, lists[l]->inventory, sizeof(saved));
		memset(lists[l]->inventory, 0, sizeof(lists[l]->inventory));

		for (i = 1; i < sg_item_remap_count && i < MAX_ITEMS; i++)
		{
			if (sg_item_remap[i] > 0)
			{
				lists[l]->inventory[sg_item_remap[i]] += saved[i];
			}
		}

		lists[l]->selected_item = SG_RemapItem(lists[l]->selected_item);

		/* the weapon in hand isn't in this game any more: the blaster */
		if (!lists[l]->weapon && FindItem("Blaster") && lists[l]->inventory[ITEM_INDEX(FindItem("Blaster"))])
		{
			lists[l]->weapon = FindItem("Blaster");
		}
	}
}

static void
SG_WriteRecord(FILE *f, const void *base, const savefield_t *fields, int count,
	const fptrList_t *fpl)
{
	int i;

	for (i = 0; i < count; i++)
	{
		const savefield_t *field = &fields[i];
		const void *p = (const byte *)base + field->ofs;

		switch (field->kind)
		{
			case SF_RAW:
				sg_fwrite(p, field->size, f);
				break;

			case SF_STRING:
				SG_WriteString(f, *(const char *const *)p);
				break;

			case SF_EDICT:
				SG_WriteInt(f, *(edict_t *const *)p ? (int)(*(edict_t *const *)p - g_edicts) : -1);
				break;

			case SF_CLIENT:
				SG_WriteInt(f, *(gclient_t *const *)p ? (int)(*(gclient_t *const *)p - game.clients) : -1);
				break;

			case SF_ITEM:
				SG_WriteInt(f, *(gitem_t *const *)p ? ITEM_INDEX(*(gitem_t *const *)p) : -1);
				break;

			case SF_FUNCTION:
			{
				const byte *fn = *(const byte *const *)p;
				const fnlist_entry_t *entry = NULL;

				if (fn)
				{
					entry = GetFunctionByAddress(fn, GetFunctionList((int)field->ofs, fpl));

					if (!entry)
					{
						gi.dprintf("%s: function in %s not in the save tables\n",
							__func__, field->name);
					}
				}

				SG_WriteString(f, entry ? entry->funcStr : NULL);
			}
			break;

			case SF_MMOVE:
			{
				const mmove_t *mm = *(const mmove_t *const *)p;
				const mmoveList_t *entry = mm ? GetMmoveByAddress(mm) : NULL;

				SG_WriteString(f, entry ? entry->mmoveStr : NULL);
			}
			break;
		}
	}
}

/* reads a schema and matches it to this build's fields; free() the result */
static schemafield_t *
SG_ReadSchema(FILE *f, const savefield_t *fields, int count, int *schema_count,
	const char *what)
{
	schemafield_t *schema;
	int i, n;

	n = SG_ReadInt(f);

	if (n < 0 || n > 4096)
	{
		fclose(f);
		gi.error("%s: broken %s field list (%d fields)", __func__, what, n);
		return NULL;
	}

	schema = calloc(n ? n : 1, sizeof(*schema));
	if (!schema)
	{
		fclose(f);
		gi.error("%s: out of memory", __func__);
		return NULL;
	}

	for (i = 0; i < n; i++)
	{
		char name[256];
		int j;

		ReadStringToBuf(f, SG_ReadInt(f), name, sizeof(name));
		schema[i].kind = SG_ReadInt(f);
		schema[i].size = SG_ReadInt(f);

		for (j = 0; j < count; j++)
		{
			if (!strcmp(fields[j].name, name))
			{
				break;
			}
		}

		if (j == count)
		{
			gi.dprintf("%s: %s.%s is gone from this build, skipped\n", __func__, what, name);
		}
		else if ((int)fields[j].kind != schema[i].kind ||
				 (fields[j].kind == SF_RAW && (int)fields[j].size != schema[i].size))
		{
			gi.dprintf("%s: %s.%s changed type or size (%d -> %d bytes), skipped\n",
				__func__, what, name, schema[i].size, (int)fields[j].size);
		}
		else
		{
			schema[i].field = &fields[j];
		}
	}

	*schema_count = n;
	return schema;
}

static void
SG_ReadRecord(FILE *f, void *base, const schemafield_t *schema, int count,
	int tag, const fptrList_t *fpl)
{
	int i;

	for (i = 0; i < count; i++)
	{
		const savefield_t *field = schema[i].field;
		void *p = field ? (byte *)base + field->ofs : NULL;
		int value;

		switch (schema[i].kind)
		{
			case SF_RAW:
				if (field)
				{
					sg_fread(p, field->size, f);
				}
				else if (schema[i].size < 0 || fseek(f, schema[i].size, SEEK_CUR))
				{
					fclose(f);
					gi.error("%s: broken record", __func__);
					return;
				}
				break;

			case SF_STRING:
			case SF_FUNCTION:
			case SF_MMOVE:
				value = SG_ReadInt(f); /* text length */

				if (!field)
				{
					if (value < 0 || fseek(f, value, SEEK_CUR))
					{
						fclose(f);
						gi.error("%s: broken record", __func__);
						return;
					}
				}
				else if (schema[i].kind == SF_STRING)
				{
					*(char **)p = ReadString(f, value, tag);
				}
				else if (schema[i].kind == SF_FUNCTION)
				{
					*(const byte **)p = ReadFunction(f, value,
						GetFunctionList((int)field->ofs, fpl));
				}
				else
				{
					*(const mmove_t **)p = ReadMmove(f, value);
				}
				break;

			case SF_EDICT:
				value = SG_ReadInt(f);

				if (field)
				{
					*(edict_t **)p = (value >= 0 && value < game.maxentities) ? &g_edicts[value] : NULL;
				}
				break;

			case SF_CLIENT:
				value = SG_ReadInt(f);

				if (field)
				{
					*(gclient_t **)p = (value >= 0 && value < game.maxclients) ? &game.clients[value] : NULL;
				}
				break;

			case SF_ITEM:
				value = SG_ReadInt(f);

				if (field)
				{
					*(gitem_t **)p = GetItemByIndex(SG_RemapItem(value));
				}
				break;

			default:
				fclose(f);
				gi.error("%s: unknown field kind %d", __func__, schema[i].kind);
				return;
		}
	}
}

/*
 * Read the client struct from a file
 */
static void
SanitizeClientStruct(gclient_t *cl)
{
	client_persistant_t *p;

	p = &cl->pers;
	p->userinfo[sizeof(p->userinfo) - 1] = 0;
	p->netname[sizeof(p->netname) - 1] = 0;

	p = &cl->resp.coop_respawn;
	p->userinfo[sizeof(p->userinfo) - 1] = 0;
	p->netname[sizeof(p->netname) - 1] = 0;

	ValidateSelectedItem(cl);
	cl->ammo_index = GetWeaponAmmoIndex(cl->pers.weapon);
	cl->flood_whenhead = 0;
}

static void
ReadClient(FILE *f, gclient_t *client, short save_ver)
{
	ReadStruct(f, client, &sd_client, save_ver);
	SanitizeClientStruct(client);

	if (save_ver < 3)
	{
		InitClientResp(client);
	}
}

/* ========================================================= */

/*
 * Writes the game struct into
 * a file. This is called whenever
 * the game goes to a new level or
 * the user saves the game. The saved
 * information consists of:
 * - cross level data
 * - client states
 * - help computer info
 */
static void
WriteSaveHeader(FILE *f)
{
	savegameHeader_t sv;

	memset(&sv, 0, sizeof(sv));

	Q_strlcpy(sv.ver, SAVEGAMEVER, sizeof(sv.ver) - 1);
	Q_strlcpy(sv.game, GAMEVERSION, sizeof(sv.game) - 1);
	Q_strlcpy(sv.os, YQ2OSTYPE, sizeof(sv.os) - 1);
	Q_strlcpy(sv.arch, YQ2ARCH, sizeof(sv.arch) - 1);

	sg_fwrite(&sv, sizeof(sv), f);
}

/*
 * optech2: the item names, at the end of game and level saves: one
 * MAX_QPATH name per item number from 1 (the classname, or "*" and the
 * pickup name for items without one, like "Health"), then the count and
 * SG_ITEM_TABLE_MAGIC. Loading maps saved item numbers to the current
 * ones by name, so a save keeps working when mods add or remove items.
 * (Yamagi's saves have the names without count and magic; an older reader
 * still reads them as before.)
 */
static void
WriteItemsNames(FILE *f)
{
	size_t i;
	int count = 0;

	for (i = 1; i < itemlist_len; i++)
	{
		char temp[MAX_QPATH + 1] = {0};

		if (itemlist[i].classname)
		{
			Q_strlcpy(temp, itemlist[i].classname, sizeof(temp));
		}
		else if (itemlist[i].pickup_name)
		{
			Com_sprintf(temp, sizeof(temp), "*%s", itemlist[i].pickup_name);
		}

		sg_fwrite(&temp, sizeof(temp) - 1 /* MAX_QPATH */, f);
		count++;
	}

	sg_fwrite(&count, sizeof(count), f);
	sg_fwrite(SG_ITEM_TABLE_MAGIC, 4, f);
}

void
WriteGame(const char *filename, qboolean autosave)
{
	FILE *f;
	int i;

	Script_SaveState(); /* optech2: scripts' game.state into the save */

	if (!autosave)
	{
		SaveClientData();
	}

	f = Q_fopen(filename, "wb");

	if (!f)
	{
		gi.error("%s: Couldn't open %s", __func__, filename);
		return;
	}

	WriteSaveHeader(f);

	/* optech2: tagged format */
	{
		game_locals_t temp = game;

		temp.autosaved = autosave;
		SG_WriteSchema(f, savefields_game, ARRLEN(savefields_game));
		SG_WriteRecord(f, &temp, savefields_game, ARRLEN(savefields_game), NULL);
	}

	SG_WriteSchema(f, savefields_client, ARRLEN(savefields_client));

	for (i = 0; i < game.maxclients; i++)
	{
		SG_WriteRecord(f, &game.clients[i], savefields_client,
			ARRLEN(savefields_client), NULL);
	}

	/* Save items names */
	WriteItemsNames(f);

	fclose(f);
}

/*
 * Read the game structs from
 * a file. Called when ever a
 * savegames is loaded.
 */
static short
GetSaveVersion(const char *ver)
{
	int i;

	static const struct {
		const char* verstr;
		int vernum;
	} version_mappings[] = {
		{"YQ2-1", 1},
		{"YQ2-2", 2},
		{"YQ2-3", 3},
		{"YQ2-4", 4},
		{"YQ2-5", 5},
		{"YQ2-6", 6},
		{"OPT2-1", SAVE_VERSION_TAGGED}, /* optech2 */
	};

	for (i=0; i < ARRLEN(version_mappings); ++i)
	{
		if (strcmp(version_mappings[i].verstr, ver) == 0)
		{
			return version_mappings[i].vernum;
		}
	}

	return 0;
}

static const char *
CheckSaveCompatibility(const savegameHeader_t *sv, short save_ver)
{
	if (save_ver == 0) // not found in mappings table
	{
		return "Savegame from an incompatible version.";
	}

	if (save_ver == 1)
	{
		if (strcmp(sv->game, GAMEVERSION) != 0)
		{
			return "Savegame from another game.so.";
		}

		if (strcmp(sv->os, OSTYPE_1) != 0)
		{
			return "Savegame from another os.";
		}

#ifdef _WIN32
		/* Windows was forced to i386 */
		if (strcmp(sv->arch, "i386") != 0)
		{
			return "Savegame from another architecture.";
		}
#else
		if (strcmp(sv->arch, ARCH_1) != 0)
		{
			return "Savegame from another architecture.";
		}
#endif
	}
	else // all newer savegame versions
	{
		if (strcmp(sv->game, GAMEVERSION) != 0)
		{
			return "Savegame from another game.so.";
		}

		if (strcmp(sv->os, YQ2OSTYPE) != 0)
		{
			return "Savegame from another os.";
		}

		if (strcmp(sv->arch, YQ2ARCH) != 0)
		{
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
			// before savegame version "YQ2-4" (and after version 1),
			// the official Win32 binaries accidentally had the YQ2ARCH "AMD64"
			// instead of "i386" set due to a bug in the Makefile.
			// This quirk allows loading those savegames anyway
			if (save_ver >= 4 || strcmp(sv->arch, "AMD64") != 0)
#endif
			{
				return "Savegame from another architecture.";
			}
		}
	}

	return NULL;
}

static void
SanitizeGameStruct(void)
{
	/* these values are set by InitAllocations later */
	game.clients = NULL;
	game.maxclients = 0;
	game.maxentities = 0;

	/* ensure inline strings are null terminated */
	game.helpmessage1[sizeof(game.helpmessage1) - 1] = 0;
	game.helpmessage2[sizeof(game.helpmessage2) - 1] = 0;
	game.spawnpoint[sizeof(game.spawnpoint) - 1] = 0;
}

static void
ReadItemsNames(FILE *f)
{
	size_t i;

	for (i = 0; i < itemlist_len; i++)
	{
		if (feof(f))
		{
			/* no more names in save file */
			return;
		}

		if (itemlist[i].classname)
		{
			char temp[MAX_QPATH + 1];

			sg_fread(&temp, sizeof(temp) - 1 /* MAX_QPATH */, f);
			temp[sizeof(temp) - 1] = 0;

			if (strncmp(temp, itemlist[i].classname, sizeof(temp) - 1))
			{
				fclose(f);
				gi.error("%s: mismatch items class %d %s != %s\n",
					__func__, i, itemlist[i].classname, temp);
			}
		}
	}
}

void
ReadGame(const char *filename)
{
	savegameHeader_t sv;
	FILE *f;
	int i;
	const char *errmsg;
	short save_ver;

	gi.FreeTags(TAG_GAME);
	reading_raw_format = false; /* an earlier failed load may have left it set */

	f = Q_fopen(filename, "rb");

	if (!f)
	{
		gi.error("%s: Couldn't open %s", __func__, filename);
		return;
	}

	/* Sanity checks */
	sg_fread(&sv, sizeof(sv), f);
	sv.ver[sizeof(sv.ver) - 1] = 0;
	sv.game[sizeof(sv.game) - 1] = 0;
	sv.os[sizeof(sv.os) - 1] = 0;
	sv.arch[sizeof(sv.arch) - 1] = 0;

	save_ver = GetSaveVersion(sv.ver);
	errmsg = CheckSaveCompatibility(&sv, save_ver);
	if (errmsg)
	{
		fclose(f);
		gi.error("%s", errmsg);
		return;
	}

	SG_LoadItemTable(f, "game"); /* optech2: match items by name */

	if (save_ver >= SAVE_VERSION_TAGGED)
	{
		/* optech2: tagged format */
		schemafield_t *schema;
		int schema_count;

		schema = SG_ReadSchema(f, savefields_game, ARRLEN(savefields_game),
			&schema_count, "game");
		SG_ReadRecord(f, &game, schema, schema_count, TAG_GAME, NULL);
		free(schema);
		SanitizeGameStruct();

		/* initialize entities and clients arrays */
		InitAllocations();

		schema = SG_ReadSchema(f, savefields_client, ARRLEN(savefields_client),
			&schema_count, "client");

		for (i = 0; i < game.maxclients; i++)
		{
			SG_ReadRecord(f, &game.clients[i], schema, schema_count, TAG_GAME, NULL);
			SG_RemapClientItems(&game.clients[i]); /* optech2 */
			SanitizeClientStruct(&game.clients[i]);
		}

		free(schema);
	}
	else
	{
		/* Yamagi's raw struct format */
		reading_raw_format = true;
		ReadStruct(f, &game, &sd_game, save_ver);
		SanitizeGameStruct();

		/* initialize entities and clients arrays */
		InitAllocations();

		for (i = 0; i < game.maxclients; i++)
		{
			ReadClient(f, &game.clients[i], save_ver);
		}

		reading_raw_format = false;
	}

	/* Read and recheck items class names (optech2: only in saves without
	   the name table; with it, items were matched by name) */
	if (!sg_item_remap_active)
	{
		ReadItemsNames(f);
	}

	sg_item_remap_active = false;
	fclose(f);

	Script_LoadingSave(true); /* optech2: the map spawns next, then its saved level */
}

/* ========================================================== */

/*
 * Writes the current level
 * into a file.
 */
void
WriteLevel(const char *filename)
{
	int i;
	FILE *f;

	Script_SaveState(); /* optech2: scripts' game.state into the save */

	f = Q_fopen(filename, "wb");

	if (!f)
	{
		gi.error("%s: Couldn't open %s", __func__, filename);
		return;
	}

	/* optech2: tagged format marker, then level locals and entity schema */
	SG_WriteInt(f, LEVEL_FILE_TAGGED);
	SG_WriteSchema(f, savefields_level, ARRLEN(savefields_level));
	SG_WriteRecord(f, &level, savefields_level, ARRLEN(savefields_level), NULL);
	SG_WriteSchema(f, savefields_edict, ARRLEN(savefields_edict));

	/* write out all the entities */
	for (i = 0; i < globals.num_edicts; i++)
	{
		const edict_t *ent;

		ent = &g_edicts[i];

		if (!ent->inuse)
		{
			continue;
		}

		sg_fwrite(&i, sizeof(i), f);
		SG_WriteRecord(f, ent, savefields_edict, ARRLEN(savefields_edict), &fplist_ent);
	}

	i = -1;
	sg_fwrite(&i, sizeof(i), f);

	WriteItemsNames(f); /* optech2: so loading can match items by name */

	fclose(f);

	/* Store AI navigation data. optech2: only in deathmatch, where the
	   bots use it: it relinks every node pair, rewrites the node file and
	   reloads navigation, adding the item nodes again each time (the save
	   got slower with every save in single player) */
	if (deathmatch->value)
	{
		AITools_SaveNodes();
	}
}

/* ========================================================== */

/*
 * A helper function to
 * read the level local
 * data from a file.
 * Called by ReadLevel.
 */
static void
SanitizeLevelStruct(void)
{
	/* ensure inline strings are null terminated */
	level.level_name[sizeof(level.level_name) - 1] = 0;
	level.mapname[sizeof(level.mapname) - 1] = 0;
	level.nextmap[sizeof(level.nextmap) - 1] = 0;

	level.pic_health = gi.imageindex("i_health");

	level.body_que %= BODY_QUEUE_SIZE;
}

static void
ReadLevelLocals(FILE *f)
{
	ReadStruct(f, &level, &sd_level, 0);
	SanitizeLevelStruct();
}

/*
 * Reads a level back into the memory.
 * SpawnEntities were already called
 * in the same way when the level was
 * saved. All world links were cleared
 * before this function was called. When
 * this function is called, no clients
 * are connected to the server.
 */
void
ReadLevel(const char *filename)
{
	/* optech2: tagged format schemas */
	schemafield_t *edict_schema = NULL;
	int edict_schema_count = 0;
	int entnum;
	FILE *f;
	int i;
	edict_t *ent;

	G_ScreenFader_Reset();
	reading_raw_format = false; /* an earlier failed load may have left it set */

	f = Q_fopen(filename, "rb");

	if (!f)
	{
		gi.error("%s: Couldn't open %s", __func__, filename);
		return;
	}

	SG_LoadItemTable(f, "level"); /* optech2: match items by name */

	/* free any dynamic memory allocated by
	   loading the level  base state */
	gi.FreeTags(TAG_LEVEL);

	/* wipe all the entities */
	memset(g_edicts, 0, game.maxentities * sizeof(g_edicts[0]));
	globals.num_edicts = maxclients->value + 1;

	/* optech2: tagged format marker, or the old edict size check */
	sg_fread(&i, sizeof(i), f);

	if (i == LEVEL_FILE_TAGGED)
	{
		int level_schema_count = 0;
		schemafield_t *level_schema = SG_ReadSchema(f, savefields_level, ARRLEN(savefields_level),
			&level_schema_count, "level");
		SG_ReadRecord(f, &level, level_schema, level_schema_count, TAG_LEVEL, NULL);
		free(level_schema);
		SanitizeLevelStruct();

		edict_schema = SG_ReadSchema(f, savefields_edict, ARRLEN(savefields_edict),
			&edict_schema_count, "edict");
	}
	else if (i != sizeof(edict_t))
	{
		fclose(f);
		gi.error(OLD_SAVE_MESSAGE);
		return;
	}
	else
	{
		/* load the level locals */
		reading_raw_format = true;
		ReadLevelLocals(f);
	}

	/* load all the entities */
	while (1)
	{
		sg_fread(&entnum, sizeof(entnum), f);

		if ((entnum < -1) || (entnum >= game.maxentities))
		{
			fclose(f);
			gi.error("%s: entnum out of bounds: %d", __func__, entnum);
			return;
		}

		if (entnum == -1)
		{
			break;
		}

		if (entnum >= globals.num_edicts)
		{
			globals.num_edicts = entnum + 1;
		}

		ent = &g_edicts[entnum];

		if (edict_schema)
		{
			SG_ReadRecord(f, ent, edict_schema, edict_schema_count, TAG_LEVEL, &fplist_ent);
		}
		else
		{
			ReadStruct(f, ent, &sd_ent, 0);
		}

		/* sanitize certain field values */
		ent->client = NULL;
		ent->inuse = true;
		ent->s.number = ent - g_edicts;

		if (!ent->classname)
		{
			ent->classname = "noclass";
		}

		/* let the server rebuild world links for this ent */
		memset(&ent->area, 0, sizeof(ent->area));
		gi.linkentity(ent);
	}

	sg_item_remap_active = false; /* optech2 */
	fclose(f);
	free(edict_schema);
	reading_raw_format = false;

	G_ScreenFader_Relink();

	/* mark all clients as unconnected */
	for (i = 0; i < maxclients->value; i++)
	{
		ent = &g_edicts[i + 1];
		ent->client = game.clients + i;
		ent->client->pers.connected = false;
	}

	/* do any load time things at this point */
	for (i = 0; i < globals.num_edicts; i++)
	{
		ent = &g_edicts[i];

		if (!ent->inuse)
		{
			continue;
		}

		/* fire any cross-level triggers */
		if (ent->classname)
		{
			if (strcmp(ent->classname, "target_crosslevel_target") == 0)
			{
				ent->nextthink = level.time + ent->delay;
			}
		}
	}

	/* reload shadow light data from configstrings */
	G_LoadShadowLights();

	/* optech2: scripts start again with their game.state tables from the
	   save (level_start isn't fired: the level is resumed, not started) */
	Script_Init();
	Script_LoadingSave(false);
}
