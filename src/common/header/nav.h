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
 * optech2: navigation for walking monsters (src/common/nav/nav.cpp): a
 * Recast/Detour navigation mesh built from the map's solid brushes when a
 * map loads, cached in <game folder>/navigation/<map>.otnav.
 *
 * Positions are Quake coordinates (z up).
 */

#ifndef CO_NAV_H
#define CO_NAV_H

#include "optech2_path.h"

#ifdef __cplusplus
extern "C"
{
#endif

	/* builds (or loads from the cache) the mesh for the loaded map */
	void Nav_LoadMap(const char *mapname);
	void Nav_Free(void);
	void Nav_Init(void); /* console commands and cvars */

	/* a walking path from start to goal: up to max_points corners into points
	   (the start's floor position first, the goal's last); returns their count,
	   0 if there's no mesh or no path. *complete is false when the goal isn't
	   reachable and the path ends at the nearest reachable point. */
	int Nav_FindPath(const float *start, const float *goal, float (*points)[3], int max_points, int *complete);

	/* the re-release's GetPathToGoal (optech2_path.h): fills info; true if
	   the return code isn't an error. For a player-sized monster; ..Sized
	   picks the mesh for a monster of that half width and height. */
	int Nav_GetPathToGoal(const optech2_path_request_t *request, optech2_path_info_t *info);
	int Nav_GetPathToGoalSized(const optech2_path_request_t *request, optech2_path_info_t *info, float agent_radius,
		float agent_height);

	/* calls segment for every segment of the paths asked to be drawn
	   (draw_time) that haven't expired */
	void Nav_ForEachDebugSegment(void (*segment)(const float *a, const float *b, void *user), void *user);

	/* calls edge for every polygon edge of a size's mesh (0 small, 1 medium,
	   2 large) within radius of center (for drawing it); returns the number
	   of edges */
	int Nav_ForEachEdge(int agent, const float *center, float radius,
		void (*edge)(const float *a, const float *b, void *user), void *user);

#ifdef __cplusplus
}
#endif

#endif
