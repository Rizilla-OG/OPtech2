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
 * optech2: a path request and its answer, in plain C: the same layout,
 * flags and codes as the 2023 re-release's PathRequest and PathInfo
 * (src/game/rerelease/game.h), which the game asks the engine with
 * GetPathToGoal. Our game library asks through the optech2_server
 * extension (optech2_ext.h); a re-release game library will be handed the
 * same function.
 */

#ifndef COMMON_OPTECH2_PATH_H
#define COMMON_OPTECH2_PATH_H

#include <stdint.h>

/* PathFlags: what the path may use */
#define OPTECH2_PATH_WATER 0x01 /* swim (fish, gekk) */
#define OPTECH2_PATH_WALK 0x02 /* walk */
#define OPTECH2_PATH_WALK_OFF_LEDGE 0x04 /* walk off ledges */
#define OPTECH2_PATH_LONG_JUMP 0x08 /* jump over gaps */
#define OPTECH2_PATH_BARRIER_JUMP 0x10 /* jump over low barriers */
#define OPTECH2_PATH_ELEVATOR 0x20 /* ride lifts */
#define OPTECH2_PATH_ALL 0xffffffffu

/* PathReturnCode */
#define OPTECH2_PATH_REACHED_GOAL 0 /* at the goal */
#define OPTECH2_PATH_REACHED_PATH_END 1 /* as close to the goal as a path gets */
#define OPTECH2_PATH_TRAVERSAL_PENDING 2
#define OPTECH2_PATH_RAW_PATH_FOUND 3
#define OPTECH2_PATH_IN_PROGRESS 4 /* on the way: move to first_move_point */
#define OPTECH2_PATH_START_ERRORS 5 /* this and up are errors */
#define OPTECH2_PATH_INVALID_START 6
#define OPTECH2_PATH_INVALID_GOAL 7
#define OPTECH2_PATH_NO_NAV_AVAILABLE 8 /* no navigation for this map */
#define OPTECH2_PATH_NO_START_NODE 9 /* the start isn't near walkable floor */
#define OPTECH2_PATH_NO_GOAL_NODE 10 /* the goal isn't near walkable floor */
#define OPTECH2_PATH_NO_PATH_FOUND 11
#define OPTECH2_PATH_MISSING_WALK_OR_SWIM_FLAG 12

/* PathLinkType: how to get to the first move point (WALK_OFF_LEDGE: it's
   below a ledge the mover is at, step off it) */
#define OPTECH2_PATH_LINK_WALK 0
#define OPTECH2_PATH_LINK_WALK_OFF_LEDGE 1
#define OPTECH2_PATH_LINK_LONG_JUMP 2
#define OPTECH2_PATH_LINK_BARRIER_JUMP 3
#define OPTECH2_PATH_LINK_ELEVATOR 4

typedef struct
{
	float start[3];
	float goal[3];
	uint32_t path_flags; /* OPTECH2_PATH_ flags; WALK or WATER is required */
	float move_dist; /* how far the mover goes per step: a corner closer than this is passed */

	float draw_time; /* > 0: draw the path for this many seconds (single player, listen server) */

	uint8_t ignore_node_flags;
	float min_height; /* <= 0: defaults */
	float max_height; /* how far above or below the start and goal the floor may be */
	float radius; /* how far sideways the floor may be */

	float drop_height; /* 0 = don't drop down */
	float jump_height; /* 0 = don't jump up */

	float (*points)[3]; /* if set: the path's corners go here */
	int64_t point_count; /* room in points */
} optech2_path_request_t;

typedef struct
{
	int32_t point_count; /* corners in the path, the start first */
	float path_dist_sqr; /* the path's length, squared */
	float first_move_point[3];
	float second_move_point[3];
	int32_t link_type; /* OPTECH2_PATH_LINK_: how to get to first_move_point */
	int32_t return_code; /* OPTECH2_PATH_ */
} optech2_path_info_t;

#endif
