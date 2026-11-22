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
 * optech2: collision results in the re-release's form, for re-release
 * game libraries: the server's game bridge and the client's prediction
 * (with the re-release player movement) both use this, so they see the
 * same surfaces and contents.
 *
 * =======================================================================
 */

#include "header/common.h"
#include "header/rerelease.h"

/* the re-release csurface_t of each surface of the loaded map */
static rr_csurface_t *rr_surfaces;
static int rr_surfaces_count;
static const mapsurface_t *rr_surfaces_map; /* the map they were built for */
static rr_csurface_t rr_null_surface;

static void
RR_BuildSurfaces(void)
{
	int i, count;

	RR_FreeSurfaces();

	count = CM_MapSurfacesNum();

	if (count <= 0)
	{
		return;
	}

	rr_surfaces = Z_Malloc(count * sizeof(*rr_surfaces));
	rr_surfaces_count = count;
	rr_surfaces_map = CM_MapSurfaces(0);

	for (i = 0; i < count; i++)
	{
		const mapsurface_t *surface = CM_MapSurfaces(i);

		Q_strlcpy(rr_surfaces[i].name, surface->rname, sizeof(rr_surfaces[i].name));
		rr_surfaces[i].flags = (uint32_t)surface->c.flags;
		rr_surfaces[i].value = 0;
		rr_surfaces[i].id = (uint32_t)(i + 1); /* offset by 1: 0 is "none" */
		Q_strlcpy(rr_surfaces[i].material, surface->c.material,
			sizeof(rr_surfaces[i].material));
	}
}

void
RR_FreeSurfaces(void)
{
	if (rr_surfaces)
	{
		Z_Free(rr_surfaces);
	}

	rr_surfaces = NULL;
	rr_surfaces_count = 0;
	rr_surfaces_map = NULL;
}

static rr_csurface_t *
RR_ConvertSurface(const csurface_t *surface)
{
	ptrdiff_t index;

	if (!surface)
	{
		return &rr_null_surface;
	}

	/* a new map since the table was built: build it again */
	if (!rr_surfaces || (rr_surfaces_map != CM_MapSurfaces(0)) ||
		(rr_surfaces_count != CM_MapSurfacesNum()))
	{
		RR_BuildSurfaces();
	}

	if (!rr_surfaces)
	{
		return &rr_null_surface;
	}

	index = (const mapsurface_t *)surface - rr_surfaces_map;

	if ((index < 0) || (index >= rr_surfaces_count))
	{
		return &rr_null_surface;
	}

	return &rr_surfaces[index];
}

rr_trace_t
RR_TraceFromClassic(const trace_t *trace, rr_edict_t *ent)
{
	rr_trace_t result;

	memset(&result, 0, sizeof(result));
	result.allsolid = trace->allsolid;
	result.startsolid = trace->startsolid;
	result.fraction = trace->fraction;
	VectorCopy(trace->endpos, result.endpos);
	memcpy(&result.plane, &trace->plane, sizeof(result.plane));
	result.surface = RR_ConvertSurface(trace->surface);
	result.contents = (uint32_t)trace->contents;
	result.ent = ent;
	/* no second-best plane from our collision code (on both sides alike) */
	result.plane2 = result.plane;
	result.surface2 = NULL;

	return result;
}
