/*
 * OpenTech2 test client game module (re-release cgame API 2022), used by
 * tests/cgame/cgame_check.py. It calls the engine's imports and prints what
 * it gets, so the check can compare, and draws a small HUD of its own:
 * a line of text and the health icon tinted red.
 *
 * Build: cc -shared -fPIC -I<engine> -o test_cgame.so tests/cgame/test_cgame.c
 */

#include <stdio.h>
#include <string.h>

#include "src/client/header/cgame_rerelease.h"
#include "src/common/header/optech2_ext.h"

#define STAT_HEALTH 1 /* the re-release and stock games both keep health here */

static cgame_import_t cgi;
static cgame_export_t cglobals;
static cvar_t *test_cvar;
static int draw_calls, reported, center_prints;
static char last_center[256];

static void
Print(const char *format, ...)
{
	char text[512];
	__builtin_va_list args;

	__builtin_va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	__builtin_va_end(args);
	cgi.Com_Print(text);
}

static void
Init(void)
{
	int32_t modified_before;

	/* a cvar the module owns; its integer field must follow the value */
	test_cvar = cgi.cvar("cg_testvalue", "42", 0);
	modified_before = test_cvar->modified;
	cgi.cvar_set("cg_testvalue", "43");

	Print("cgtest init: tick_rate %u frame_time_ms %u cvar integer %d value %.0f change counted %s\n",
		cgi.tick_rate, cgi.frame_time_ms, test_cvar->integer, test_cvar->value,
		test_cvar->modified != modified_before ? "yes" : "no");

	{
		const char *exit_args[1] = {"Tester"};

		/* "$key" texts come from localization/loc_<language>.txt (re-release data) */
		Print("cgtest localize: '%s' | '%s'\n", cgi.Localize("$g_pc_primary_objective", NULL, 0),
			cgi.Localize("$g_exited_level", exit_args, 1));
	}
}

static void
Shutdown(void)
{
	Print("cgtest shutdown after %d HUD draws\n", draw_calls);
}

static void
DrawHUD(int32_t isplit, const cg_server_data_t *data, cg_vrect_t hud_vrect, cg_vrect_t hud_safe,
	int32_t scale, int32_t playernum, const cg_player_state_t *ps)
{
	static const cg_rgba_t red = {255, 64, 64, 255};
	char text[128];
	int pic_w, pic_h;

	draw_calls++;

	snprintf(text, sizeof(text), "TEST CGAME  health %d", ps->stats[STAT_HEALTH]);
	/* hud_vrect is in HUD units: multiply by scale for pixels */
	cgi.SCR_DrawFontString(text, (hud_vrect.x + hud_vrect.width / 2) * scale,
		(hud_vrect.y + hud_vrect.height - 40) * scale, scale, &red, false, 1 /* centered */);

	cgi.Draw_GetPicSize(&pic_w, &pic_h, "i_health");
	cgi.SCR_DrawColorPic((hud_vrect.x + 8) * scale, (hud_vrect.y + hud_vrect.height - 40) * scale,
		pic_w * scale, pic_h * scale, "i_health", &red);

	if (center_prints)
	{
		cgi.SCR_DrawFontString(last_center, (hud_vrect.x + hud_vrect.width / 2) * scale,
			(hud_vrect.y + hud_vrect.height / 3) * scale, scale, &red, false, 1);
	}

	/* report once, after some frames */
	if (draw_calls == 30 && !reported)
	{
		reported = 1;
		Print("cgtest hud: map '%s' health %d frame_valid %d server_frame %d client_time %llu scale %d "
			"screen %dx%d playernum %d pic i_health %dx%d registered %d attack key '%s'\n",
			cgi.get_configstring(0), ps->stats[STAT_HEALTH], cgi.CL_FrameValid(), cgi.CL_ServerFrame(),
			(unsigned long long)cgi.CL_ClientTime(), scale, hud_vrect.width, hud_vrect.height, playernum,
			pic_w, pic_h, cgi.Draw_RegisterPic("i_health"), cgi.CL_GetKeyBinding("+attack"));
	}
}

static void
TouchPics(void)
{
	cgi.Draw_RegisterPic("i_health");
}

static int16_t
LayoutFlags(const cg_player_state_t *ps)
{
	return 0;
}

static void
ParseCenterPrint(const char *str, int isplit, bool instant)
{
	center_prints++;
	snprintf(last_center, sizeof(last_center), "%s", str);
	Print("cgtest centerprint: '%s'\n", str);
}

static void
ParseConfigString(int32_t i, const char *s)
{
}

static void
ClearNotify(int32_t isplit)
{
}

static void
ClearCenterprint(int32_t isplit)
{
}

static void
NotifyMessage(int32_t isplit, const char *msg, bool is_chat)
{
}

/* ------------------------------------------------------------------ */
/* effects extension: draws temp entity type 200 (sent by "fxtest") */

#define TEST_EFFECT_TYPE 200

static const optech2_fx_import_t *fxi;
static int effects_drawn;

static void
FxInit(const optech2_fx_import_t *import)
{
	fxi = import;
}

static qboolean
FxParseTempEntity(int type)
{
	vec3_t origin, dir;
	optech2_dlight_t *light;
	int i, particles = 0;

	if (type != TEST_EFFECT_TYPE)
	{
		return false; /* stock effects stay with the engine */
	}

	fxi->ReadPos(origin);
	fxi->ReadDir(dir);

	/* a ring of green particles flying outwards */
	for (i = 0; i < 64; i++)
	{
		optech2_particle_t *p = fxi->AllocParticle();
		float angle = i * (6.2831853f / 64);

		if (!p)
		{
			break;
		}

		p->time = (float)fxi->Time();
		p->color = 0xff00ff00;
		p->org[0] = origin[0];
		p->org[1] = origin[1];
		p->org[2] = origin[2];
		p->vel[0] = __builtin_cosf(angle) * 120;
		p->vel[1] = __builtin_sinf(angle) * 120;
		p->vel[2] = 40;
		p->accel[0] = p->accel[1] = 0;
		p->accel[2] = -40;
		p->alpha = 1.0f;
		p->alphavel = -0.8f;
		particles++;
	}

	light = fxi->AllocDlight(0);
	light->origin[0] = origin[0];
	light->origin[1] = origin[1];
	light->origin[2] = origin[2];
	light->radius = 300;
	light->color[0] = 0.2f;
	light->color[1] = 1.0f;
	light->color[2] = 0.2f;
	light->die = (float)fxi->Time() + 500;

	effects_drawn++;
	Print("cgtest fx: type %d at %.0f %.0f %.0f dir %.2f %.2f %.2f, %d particles\n", type,
		origin[0], origin[1], origin[2], dir[0], dir[1], dir[2], particles);
	return true;
}

static void
FxNothing(void)
{
}

static qboolean
FxNoMuzzleFlash(int kind)
{
	return false;
}

static qboolean
FxNoTrails(optech2_fx_entity_t *ent)
{
	return false;
}

static const optech2_fx_ext_t fx_extension = {
	OPTECH2_FX_EXT_VERSION,
	FxInit,
	FxParseTempEntity,
	FxNoMuzzleFlash, /* ParseMuzzleFlash: leave them to the engine */
	FxNoTrails, /* EntityTrails: the engine's */
	FxNothing, /* RegisterMedia */
	FxNothing, /* AddFrameEffects */
	FxNothing /* ClearEffects */
};

void *
OPtech2_GetExtension(const char *name)
{
	if (name && !strcmp(name, OPTECH2_EXT_FX))
	{
		return (void *)&fx_extension;
	}

	return NULL;
}

cgame_export_t *
GetCGameAPI(cgame_import_t *import)
{
	cgi = *import;

	memset(&cglobals, 0, sizeof(cglobals));
	cglobals.apiversion = CGAME_API_VERSION;
	cglobals.Init = Init;
	cglobals.Shutdown = Shutdown;
	cglobals.DrawHUD = DrawHUD;
	cglobals.TouchPics = TouchPics;
	cglobals.LayoutFlags = LayoutFlags;
	cglobals.ParseCenterPrint = ParseCenterPrint;
	cglobals.ParseConfigString = ParseConfigString;
	cglobals.ClearNotify = ClearNotify;
	cglobals.ClearCenterprint = ClearCenterprint;
	cglobals.NotifyMessage = NotifyMessage;
	return &cglobals;
}
