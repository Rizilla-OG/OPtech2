/*
 * optech2: force-included (-include) ahead of every file when building a
 * Quake II re-release game library (id's quake2-rerelease-dll, or a mod
 * built on it such as q2arpg) natively on Linux, without changing its
 * source. Same fixes as src/game/rerelease/optech2_prelude.h, but keeps the
 * fmt library (re-release mods' CMake builds use it) and adds isnan/isinf,
 * which id's code calls unqualified.
 *
 * Build (out of tree):
 *   cmake -S <mod>/rerelease -B <build dir> -DCMAKE_BUILD_TYPE=Release \
 *     -DCMAKE_CXX_FLAGS="-include <this file> -w -fPIC"
 *   cmake --build <build dir>
 * gives game_x64.so exporting GetGameAPI and GetCGameAPI.
 */

#ifndef OPTECH2_LINUX_GAME_PRELUDE_H
#define OPTECH2_LINUX_GAME_PRELUDE_H

/* glibc's limits.h defines CHAR_WIDTH, which cg_screen.cpp uses as a name */
#include <climits>
#undef CHAR_WIDTH

/* build as a game library that exports GetCGameAPI, as id's DLL build does */
#define KEX_Q2GAME_DYNAMIC
#define KEX_Q2GAME_EXPORTS

/* __declspec(dllexport) is Windows only; elsewhere the same thing is default visibility */
#ifndef _WIN32
#define __declspec(x) __attribute__((visibility("default")))
#endif

/* id's code calls isnan/isinf unqualified */
#include <cmath>
using std::isinf;
using std::isnan;

#endif
