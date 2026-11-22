#!/usr/bin/env python3
"""Layout check for src/server/header/game_rerelease.h: the C copy of the
re-release game API must match id's C++ game.h byte for byte. Builds two
small programs, one with id's headers (g_local.h of quake2-rerelease-dll)
and one with ours, each printing the size of every struct and the offset
of every field listed below, and compares the two outputs.

Usage:
  layout_check.py <quake2-rerelease-dll>/rerelease [fmt include dir]
Needs g++ and cc. id's headers use the fmt library: give its include dir
(e.g. <stock game build>/_deps/fmt-src/include) unless it is installed.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent.parent
OUR_HEADER = REPO_DIR / "src" / "server" / "header" / "game_rerelease.h"
PRELUDE = REPO_DIR / "tools" / "rerelease" / "linux_game_prelude.h"

# (id's type, our type, fields)
STRUCTS = [
    ("cplane_t", "rr_cplane_t", "normal dist type signbits pad"),
    ("csurface_t", "rr_csurface_t", "name flags value id material"),
    ("trace_t", "rr_trace_t",
     "allsolid startsolid fraction endpos plane surface contents ent plane2 surface2"),
    ("pmove_state_t", "rr_pmove_state_t",
     "pm_type origin velocity pm_flags pm_time gravity delta_angles viewheight"),
    ("usercmd_t", "rr_usercmd_t", "msec buttons angles forwardmove sidemove server_frame"),
    ("touch_list_t", "rr_touch_list_t", "num traces"),
    ("pmove_t", "rr_pmove_t",
     "s cmd snapinitial touch viewangles mins maxs groundentity groundplane watertype "
     "waterlevel player trace clip pointcontents viewoffset screen_blend rdflags "
     "jump_sound step_clip impact_delta"),
    ("entity_state_t", "rr_entity_state_t",
     "number origin angles old_origin modelindex modelindex2 modelindex3 modelindex4 "
     "frame skinnum effects renderfx solid sound event alpha scale instance_bits "
     "loop_volume loop_attenuation owner old_frame"),
    ("player_state_t", "rr_player_state_t",
     "pmove viewangles viewoffset kick_angles gunangles gunoffset gunindex gunskin "
     "gunframe gunrate screen_blend damage_blend fov rdflags stats team_id"),
    ("gclient_shared_t", "rr_gclient_t", "ps ping"),
    ("sv_entity_t", "rr_sv_entity_t",
     "init ent_flags buttons spawnflags item_id armor_type armor_value health "
     "max_health starting_health weapon team lobby_usernum respawntime viewheight "
     "last_attackertime waterlevel viewangles viewforward velocity start_origin "
     "end_origin enemy ground_entity classname targetname netname inventory armor_info"),
    ("edict_shared_t", "rr_edict_t",
     "s client sv inuse linked linkcount areanum areanum2 svflags mins maxs absmin "
     "absmax size solid clipmask owner"),
    ("PathRequest", "rr_path_request_t",
     "start goal pathFlags moveDist debugging nodeSearch traversals pathPoints"),
    ("PathInfo", "rr_path_info_t",
     "numPathPoints pathDistSqr firstMovePoint secondMovePoint pathLinkType returnCode"),
    ("shadow_light_data_t", "rr_shadow_light_data_t",
     "lighttype radius resolution intensity fade_start fade_end lightstyle coneangle "
     "conedirection"),
    ("game_import_t", "rr_game_import_t",
     "tick_rate frame_time_s frame_time_ms Broadcast_Print Com_Print Client_Print "
     "Center_Print sound positioned_sound local_sound configstring get_configstring "
     "Com_Error modelindex soundindex imageindex setmodel trace clip pointcontents "
     "inPVS inPHS SetAreaPortalState AreasConnected linkentity unlinkentity BoxEdicts "
     "multicast unicast WriteChar WriteByte WriteShort WriteLong WriteFloat WriteString "
     "WritePosition WriteDir WriteAngle WriteEntity TagMalloc TagFree FreeTags cvar "
     "cvar_set cvar_forceset argc argv args AddCommandString DebugGraph GetExtension "
     "Bot_RegisterEdict Bot_UnRegisterEdict Bot_MoveToPoint Bot_FollowActor "
     "GetPathToGoal Loc_Print Draw_Line Draw_Point Draw_Circle Draw_Bounds Draw_Sphere "
     "Draw_OrientedWorldText Draw_StaticWorldText Draw_Cylinder Draw_Ray Draw_Arrow "
     "ReportMatchDetails_Multicast ServerFrame SendToClipBoard Info_ValueForKey "
     "Info_RemoveKey Info_SetValueForKey"),
    ("game_export_t", "rr_game_export_t",
     "apiversion PreInit Init Shutdown SpawnEntities WriteGameJson ReadGameJson "
     "WriteLevelJson ReadLevelJson CanSave ClientChooseSlot ClientConnect ClientBegin "
     "ClientUserinfoChanged ClientDisconnect ClientCommand ClientThink RunFrame "
     "PrepFrame ServerCommand edicts edict_size num_edicts max_edicts server_flags "
     "Pmove GetExtension Bot_SetWeapon Bot_TriggerEdict Bot_UseItem Bot_GetItemID "
     "Edict_ForceLookAtPoint Bot_PickedUpItem Entity_IsVisibleToPlayer "
     "GetShadowLightData"),
    ("cvar_t", "cvar_t", "name string latched_string flags modified_count:modified value integer next"),
]


def dumper(includes, which):
    lines = [includes, "#include <stdio.h>", "#include <stddef.h>", "int main(void) {"]
    for theirs, ours, fields in STRUCTS:
        name = theirs if which == 0 else ours
        # the engine allocates cvars and adds its own fields after id's
        if theirs != "cvar_t":
            lines.append(f'printf("{theirs} size %zu\\n", sizeof({name}));')
        for field in fields.split():
            # "theirs:ours" when the field is named differently here
            label = field.split(":")[0]
            field = field.split(":")[which] if ":" in field else field
            lines.append(f'printf("{theirs}.{label} %zu\\n", offsetof({name}, {field}));')
    lines.append("return 0; }")
    return "\n".join(lines) + "\n"


def build_and_run(compiler, source, flags, work_dir, name):
    source_path = work_dir / f"{name}.{'cpp' if compiler == 'g++' else 'c'}"
    source_path.write_text(source)
    binary = work_dir / name
    result = subprocess.run([compiler, *flags, str(source_path), "-o", str(binary)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr[-4000:])
        sys.exit(f"{name}: build failed")
    return subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rerelease_dir = Path(sys.argv[1]).resolve()
    flags_cpp = ["-std=c++17", "-w", "-include", str(PRELUDE), "-I", str(rerelease_dir),
                 "-DKEX_Q2_GAME", "-DNO_FMT_SOURCE"]
    if len(sys.argv) > 2:
        flags_cpp += ["-I", sys.argv[2]]
    with tempfile.TemporaryDirectory() as temp:
        work_dir = Path(temp)
        theirs = build_and_run("g++", dumper('#include "g_local.h"', 0), flags_cpp,
                               work_dir, "theirs")
        ours = build_and_run("cc", dumper(f'#include "{OUR_HEADER}"', 1),
                             ["-std=c11", "-w"], work_dir, "ours")
    theirs_lines, ours_lines = theirs.splitlines(), ours.splitlines()
    bad = [(a, b) for a, b in zip(theirs_lines, ours_lines) if a != b]
    for a, b in bad:
        print(f"MISMATCH  id: {a}   ours: {b}")
    print(f"{len(theirs_lines)} sizes/offsets compared, {len(bad)} mismatches")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
