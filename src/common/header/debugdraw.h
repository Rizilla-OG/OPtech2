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
 * optech2: debug drawing. Game code (the re-release game API's Draw_*
 * calls) adds lines and world texts that live for a while; the client
 * draws them over its view (hidden by walls or, without the depth test,
 * on top of everything). Kept in this process, so a local game
 * (single player or a listen server) shows them, not remote clients.
 *
 * =======================================================================
 */

#ifndef CO_DEBUGDRAW_H
#define CO_DEBUGDRAW_H

/* colors are 0xAARRGGBB; lifetimes in seconds */
void DebugDraw_Line(const float *start, const float *end, unsigned int color, float seconds,
	qboolean depth_test);
void DebugDraw_Text(const float *origin, const char *text, unsigned int color, float seconds,
	qboolean depth_test);
void DebugDraw_Clear(void);

void DebugDraw_ForEachLine(void (*line)(const float *start, const float *end, unsigned int color,
							   qboolean depth_test, void *user),
	void *user);
void DebugDraw_ForEachText(void (*text)(const float *origin, const char *message, unsigned int color,
							   qboolean depth_test, void *user),
	void *user);

#endif
