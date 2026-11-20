/*
 * optech2: settings for building the Quake II re-release client game code
 * in this folder into our game library. This header is force-included
 * (-include) ahead of every file here, so id's files stay as released.
 *
 * The files come unchanged from id Software's quake2-rerelease-dll
 * (github.com/id-Software/quake2-rerelease-dll, commit 8dc1fc9,
 * "Update 1 changes", GPL version 2), except q_std.cpp, whose changed
 * lines are marked "optech2".
 */

#ifndef OPTECH2_RERELEASE_PRELUDE_H
#define OPTECH2_RERELEASE_PRELUDE_H

/* glibc's limits.h defines CHAR_WIDTH, which cg_screen.cpp uses as a name */
#include <climits>
#undef CHAR_WIDTH

/* the C++20 <format> instead of the fmt library */
#define USE_CPP20_FORMAT 1

/* build as a game library that exports GetCGameAPI, as id's DLL build does */
#define KEX_Q2GAME_DYNAMIC
#define KEX_Q2GAME_EXPORTS

/* __declspec(dllexport) is Windows only; elsewhere the same thing is default visibility */
#ifndef _WIN32
#define __declspec(x) __attribute__((visibility("default")))
#endif

#endif
