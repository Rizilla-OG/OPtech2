/*
 * Copyright (C) 1997-2001 Id Software, Inc.
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
 * optech2: debug drawing store, see header/debugdraw.h. Two rings: when
 * one is full the oldest entry makes room.
 *
 * =======================================================================
 */

#include "header/common.h"
#include "header/debugdraw.h"

#define DEBUGDRAW_LINES 8192
#define DEBUGDRAW_TEXTS 256
#define DEBUGDRAW_TEXT_LENGTH 128
#define DEBUGDRAW_SLACK_MS 20 /* a line the game redraws every tick doesn't flicker between ticks */

typedef struct
{
	vec3_t start, end;
	unsigned int color;
	qboolean depth_test;
	int expires;
} debug_line_t;

typedef struct
{
	vec3_t origin;
	char message[DEBUGDRAW_TEXT_LENGTH];
	unsigned int color;
	qboolean depth_test;
	int expires;
} debug_text_t;

static debug_line_t debug_lines[DEBUGDRAW_LINES];
static debug_text_t debug_texts[DEBUGDRAW_TEXTS];
static int next_line, next_text;

static int
DebugDraw_Expires(float seconds)
{
	return Sys_Milliseconds() + (int)(seconds * 1000.0f) + DEBUGDRAW_SLACK_MS;
}

void
DebugDraw_Line(const float *start, const float *end, unsigned int color, float seconds,
	qboolean depth_test)
{
	debug_line_t *line = &debug_lines[next_line];

	next_line = (next_line + 1) % DEBUGDRAW_LINES;
	VectorCopy(start, line->start);
	VectorCopy(end, line->end);
	line->color = color;
	line->depth_test = depth_test;
	line->expires = DebugDraw_Expires(seconds);
}

void
DebugDraw_Text(const float *origin, const char *text, unsigned int color, float seconds,
	qboolean depth_test)
{
	debug_text_t *entry = &debug_texts[next_text];

	next_text = (next_text + 1) % DEBUGDRAW_TEXTS;
	VectorCopy(origin, entry->origin);
	Q_strlcpy(entry->message, text ? text : "", sizeof(entry->message));
	entry->color = color;
	entry->depth_test = depth_test;
	entry->expires = DebugDraw_Expires(seconds);
}

/* a new map: nothing from the last one stays */
void
DebugDraw_Clear(void)
{
	memset(debug_lines, 0, sizeof(debug_lines));
	memset(debug_texts, 0, sizeof(debug_texts));
	next_line = next_text = 0;
}

void
DebugDraw_ForEachLine(void (*line)(const float *start, const float *end, unsigned int color,
						  qboolean depth_test, void *user),
	void *user)
{
	const int now = Sys_Milliseconds();
	int i;

	for (i = 0; i < DEBUGDRAW_LINES; i++)
	{
		if (debug_lines[i].expires > now)
		{
			line(debug_lines[i].start, debug_lines[i].end, debug_lines[i].color,
				debug_lines[i].depth_test, user);
		}
	}
}

void
DebugDraw_ForEachText(void (*text)(const float *origin, const char *message, unsigned int color,
						  qboolean depth_test, void *user),
	void *user)
{
	const int now = Sys_Milliseconds();
	int i;

	for (i = 0; i < DEBUGDRAW_TEXTS; i++)
	{
		if (debug_texts[i].expires > now)
		{
			text(debug_texts[i].origin, debug_texts[i].message, debug_texts[i].color,
				debug_texts[i].depth_test, user);
		}
	}
}
