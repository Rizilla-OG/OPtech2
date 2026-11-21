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
 * optech2: the navigation mesh (header/nav.h). Built with Recast from the
 * world's solid brushes (CM_ForEachSolidTriangle), in tiles of 512 units,
 * for a monster the player's size; queried with Detour. The result is
 * cached in <game folder>/navigation/<map>.otnav, keyed by the map's
 * checksum and the build settings, so only the first load builds it.
 *
 * Recast is y-up: a Quake point (x, y, z) is (x, z, -y) here, a rotation,
 * so triangles keep their winding and floors face +y.
 *
 * C++ only because Recast/Detour are; the engine sees the C functions in
 * nav.h. common.h isn't C++-clean, so what it needs is declared below.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "recast/Recast/Include/Recast.h"
#include "recast/Detour/Include/DetourCommon.h"
#include "recast/Detour/Include/DetourNavMesh.h"
#include "recast/Detour/Include/DetourNavMeshBuilder.h"
#include "recast/Detour/Include/DetourNavMeshQuery.h"

#include "../header/nav.h"

extern "C" {
void Com_Printf(const char *fmt, ...);
const char *FS_Gamedir(void);
void FS_CreatePath(const char *path);
int Sys_Milliseconds(void);
void Cmd_AddCommand(const char *cmd_name, void (*function)(void));
int Cmd_Argc(void);
char *Cmd_Argv(int arg);
int CM_ForEachSolidTriangle(int contents_mask, void (*emit)(const float *a, const float *b, const float *c, void *user),
	void *user);
unsigned CM_MapChecksum(void);
float CM_NavTrace(const float *start, const float *end, const float *mins, const float *maxs, int contents_mask,
	float *endpos);
int CM_PointContents(const float *p, int headnode);
int CM_ForEachModelTriangle(int model, float z_offset, int contents_mask,
	void (*emit)(const float *a, const float *b, const float *c, void *user), void *user);
int CM_ModelBounds(int model, float *mins, float *maxs);
const char *CM_EntityString(int *size);
}

/* what monsters walk on and around (shared.h's CONTENTS_*) */
static const int NAV_SOLID = 1 /* SOLID */ | 2 /* WINDOW */ | 0x10000 /* PLAYERCLIP */ | 0x20000 /* MONSTERCLIP */;

/* build settings: steps of 18 and slopes up to the player's 0.7 normal, on
   a grid of 8 x 2 units; one mesh per monster size (NAV_AGENTS) */
static const float NAV_CELL_SIZE = 8.0f;
static const float NAV_CELL_HEIGHT = 2.0f;
static const float NAV_AGENT_CLIMB = 18.0f;
static const float NAV_MAX_SLOPE = 45.57f; /* acos(0.7) */
static const int NAV_TILE_CELLS = 64;      /* 512 units */
static const float NAV_CLEARANCE = 8.0f;   /* kept from walls beyond a monster's half width */
static const int NAV_FILE_VERSION = 5;
static const unsigned short NAV_FLAG_WALK = 1;

/* ledge drops: one-way links from a floor's edge to floor up to
   NAV_MAX_DROP below, flagged by how far they drop (NavDropFlag) so a
   request's drop_height can leave the deeper ones out */
static const float NAV_MAX_DROP = 256.0f;
static const float NAV_DROP_STEP = 64.0f;       /* one flag per 64 units of drop */
static const unsigned short NAV_FLAG_DROP = 2;  /* the first; NAV_FLAG_DROP << 1 drops up to 128, ... */
static const int NAV_DROP_BANDS = 4;
static const unsigned char NAV_AREA_DROP = 1;
static const float NAV_DROP_SPACING = 32.0f;    /* one link per this much of a ledge */
static const int NAV_HAZARD = 8 /* LAVA */ | 16 /* SLIME */ | 32 /* WATER */;

/* lifts (func_plat): in the mesh where they rest, at the bottom, with a
   two-way link from on top of the lift down there to the floor beside its
   top. The game rides it (calling the lift when it's at the other end). */
static const unsigned short NAV_FLAG_LIFT = NAV_FLAG_DROP << NAV_DROP_BANDS;
static const unsigned char NAV_AREA_LIFT = 2;

/* monster sizes: half its width and its height. A monster uses the
   smallest one it fits (Nav_AgentFor) */
struct NavAgent
{
	const char *name;
	float radius;
	float height;
};

static const NavAgent NAV_AGENTS[] = {
	{"small", 16.0f, 56.0f},  /* the player, soldiers, infantry, berserker */
	{"medium", 32.0f, 88.0f}, /* tank, gladiator, mutant, medic */
	{"large", 64.0f, 112.0f}, /* supertank, boss */
};

static const int NAV_AGENT_COUNT = (int)(sizeof(NAV_AGENTS) / sizeof(NAV_AGENTS[0]));

static dtNavMesh *nav_meshes[NAV_AGENT_COUNT];
static dtNavMeshQuery *nav_queries[NAV_AGENT_COUNT];
static char nav_mapname[64];
static int nav_build_msec;
static bool nav_from_cache;

/* ---- coordinates ---- */

static void
ToRecast(const float *quake, float *recast)
{
	recast[0] = quake[0];
	recast[1] = quake[2];
	recast[2] = -quake[1];
}

static void
ToQuake(const float *recast, float *quake)
{
	quake[0] = recast[0];
	quake[1] = -recast[2];
	quake[2] = recast[1];
}

/* ---- building ---- */

/* a func_plat: its brush model at the top (as the map places it) and
   how far down it rests */
struct NavLift
{
	int model;
	float mins[3], maxs[3]; /* Quake coordinates, at the top */
	float travel;
};

struct NavGeometry
{
	std::vector<NavLift> lifts;
	std::vector<float> vertices;  /* Recast coordinates */
	std::vector<int> triangles;
	std::vector<float> bounds;    /* per triangle: min x, min z, max x, max z */
	float bmin[3] = {1e30f, 1e30f, 1e30f};
	float bmax[3] = {-1e30f, -1e30f, -1e30f};
};

static void
CollectTriangle(const float *a, const float *b, const float *c, void *user)
{
	NavGeometry *geometry = static_cast<NavGeometry *>(user);
	const float *corners[3] = {a, b, c};
	float tri_min[3] = {1e30f, 1e30f, 1e30f}, tri_max[3] = {-1e30f, -1e30f, -1e30f};

	for (int i = 0; i < 3; i++)
	{
		float point[3];

		ToRecast(corners[i], point);
		geometry->triangles.push_back((int)(geometry->vertices.size() / 3));

		for (int k = 0; k < 3; k++)
		{
			geometry->vertices.push_back(point[k]);
			tri_min[k] = point[k] < tri_min[k] ? point[k] : tri_min[k];
			tri_max[k] = point[k] > tri_max[k] ? point[k] : tri_max[k];
		}
	}

	for (int k = 0; k < 3; k++)
	{
		geometry->bmin[k] = tri_min[k] < geometry->bmin[k] ? tri_min[k] : geometry->bmin[k];
		geometry->bmax[k] = tri_max[k] > geometry->bmax[k] ? tri_max[k] : geometry->bmax[k];
	}

	geometry->bounds.push_back(tri_min[0]);
	geometry->bounds.push_back(tri_min[2]);
	geometry->bounds.push_back(tri_max[0]);
	geometry->bounds.push_back(tri_max[2]);
}

/* Recast's errors to the console */
class NavContext : public rcContext
{
protected:
	void doLog(const rcLogCategory category, const char *message, const int) override
	{
		if (category == RC_LOG_ERROR)
		{
			Com_Printf("nav: %s\n", message);
		}
	}
};

static rcConfig
NavConfig(const NavGeometry &geometry, int agent)
{
	rcConfig config;

	memset(&config, 0, sizeof(config));
	config.cs = NAV_CELL_SIZE;
	config.ch = NAV_CELL_HEIGHT;
	config.walkableSlopeAngle = NAV_MAX_SLOPE;
	config.walkableHeight = (int)ceilf(NAV_AGENTS[agent].height / NAV_CELL_HEIGHT);
	config.walkableClimb = (int)floorf(NAV_AGENT_CLIMB / NAV_CELL_HEIGHT);
	/* the mesh keeps the monster's half width and a little more from walls,
	   so a monster on a path corner doesn't scrape the wall and get stuck */
	config.walkableRadius = (int)ceilf((NAV_AGENTS[agent].radius + NAV_CLEARANCE) / NAV_CELL_SIZE);
	config.maxEdgeLen = (int)(96.0f / NAV_CELL_SIZE);
	config.maxSimplificationError = 1.3f;
	config.minRegionArea = 8 * 8;
	config.mergeRegionArea = 20 * 20;
	config.maxVertsPerPoly = 6;
	config.tileSize = NAV_TILE_CELLS;
	config.borderSize = config.walkableRadius + 3;
	config.width = config.tileSize + config.borderSize * 2;
	config.height = config.tileSize + config.borderSize * 2;
	config.detailSampleDist = NAV_CELL_SIZE * 6;
	config.detailSampleMaxError = NAV_CELL_HEIGHT;
	rcVcopy(config.bmin, geometry.bmin);
	rcVcopy(config.bmax, geometry.bmax);
	return config;
}

/* a tile's polygons, kept until its links are known */
struct NavTile
{
	int x, y;
	rcPolyMesh *polymesh;
	rcPolyMeshDetail *detail;
};

/* a ledge drop (from a floor's edge down to the floor below, one way) or a
   lift (two way), in Recast coordinates */
struct NavLink
{
	float start[3];
	float end[3];
	bool lift;
};

static unsigned short
NavDropFlag(float drop)
{
	int band = (int)ceilf(drop / NAV_DROP_STEP) - 1;

	band = band < 0 ? 0 : (band >= NAV_DROP_BANDS ? NAV_DROP_BANDS - 1 : band);
	return (unsigned short)(NAV_FLAG_DROP << band);
}

/* the drop flags a request may use: the bands no deeper than drop_height */
static unsigned short
NavDropFlags(float drop_height)
{
	unsigned short flags = 0;

	for (int band = 0; band < NAV_DROP_BANDS; band++)
	{
		if ((band + 1) * NAV_DROP_STEP <= drop_height + 0.5f)
		{
			flags |= (unsigned short)(NAV_FLAG_DROP << band);
		}
	}

	return flags;
}

/* one tile's polygons into tile->polymesh and tile->detail; false if it
   has no floor */
static bool
BuildTile(NavContext &context, const NavGeometry &geometry, rcConfig config, NavTile *tile)
{
	const float tile_world = NAV_TILE_CELLS * NAV_CELL_SIZE;
	const float border = config.borderSize * config.cs;
	const int tile_x = tile->x, tile_y = tile->y;
	bool built = false;

	tile->polymesh = nullptr;
	tile->detail = nullptr;
	config.bmin[0] = geometry.bmin[0] + tile_x * tile_world - border;
	config.bmin[2] = geometry.bmin[2] + tile_y * tile_world - border;
	config.bmax[0] = geometry.bmin[0] + (tile_x + 1) * tile_world + border;
	config.bmax[2] = geometry.bmin[2] + (tile_y + 1) * tile_world + border;

	/* the triangles that reach into this tile */
	std::vector<int> triangles;

	for (size_t i = 0; i < geometry.bounds.size() / 4; i++)
	{
		const float *b = &geometry.bounds[i * 4];

		if (b[2] >= config.bmin[0] && b[0] <= config.bmax[0] && b[3] >= config.bmin[2] && b[1] <= config.bmax[2])
		{
			triangles.push_back(geometry.triangles[i * 3]);
			triangles.push_back(geometry.triangles[i * 3 + 1]);
			triangles.push_back(geometry.triangles[i * 3 + 2]);
		}
	}

	if (triangles.empty())
	{
		return false;
	}

	const int triangle_count = (int)(triangles.size() / 3);
	const int vertex_count = (int)(geometry.vertices.size() / 3);
	rcHeightfield *heightfield = rcAllocHeightfield();
	rcCompactHeightfield *compact = nullptr;
	rcContourSet *contours = nullptr;
	rcPolyMesh *polymesh = nullptr;
	rcPolyMeshDetail *detail = nullptr;
	std::vector<unsigned char> areas(triangle_count, 0);

	if (!heightfield ||
		!rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin, config.bmax, config.cs,
			config.ch))
	{
		goto done;
	}

	rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, geometry.vertices.data(), vertex_count,
		triangles.data(), triangle_count, areas.data());

	if (!rcRasterizeTriangles(&context, geometry.vertices.data(), vertex_count, triangles.data(), areas.data(),
		triangle_count, *heightfield, config.walkableClimb))
	{
		goto done;
	}

	rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
	rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
	rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

	compact = rcAllocCompactHeightfield();

	if (!compact ||
		!rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact) ||
		!rcErodeWalkableArea(&context, config.walkableRadius, *compact) ||
		!rcBuildDistanceField(&context, *compact) ||
		!rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea))
	{
		goto done;
	}

	contours = rcAllocContourSet();

	if (!contours ||
		!rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours) ||
		contours->nconts == 0)
	{
		goto done;
	}

	polymesh = rcAllocPolyMesh();
	detail = rcAllocPolyMeshDetail();

	if (!polymesh || !detail || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polymesh) ||
		!rcBuildPolyMeshDetail(&context, *polymesh, *compact, config.detailSampleDist, config.detailSampleMaxError,
			*detail) || polymesh->npolys == 0)
	{
		goto done;
	}

	for (int i = 0; i < polymesh->npolys; i++)
	{
		polymesh->flags[i] = (polymesh->areas[i] == RC_WALKABLE_AREA) ? NAV_FLAG_WALK : 0;
	}

	tile->polymesh = polymesh;
	tile->detail = detail;
	polymesh = nullptr;
	detail = nullptr;
	built = true;

done:
	rcFreeHeightField(heightfield);
	rcFreeCompactHeightfield(compact);
	rcFreeContourSet(contours);
	rcFreePolyMesh(polymesh);
	rcFreePolyMeshDetail(detail);
	return built;
}

/* a built tile as Detour data, with the drops that start in it; nullptr
   if it fails */
static unsigned char *
TileData(const NavTile &tile, int agent, const std::vector<NavLink> &links, int *size)
{
	const rcPolyMesh *polymesh = tile.polymesh;
	const rcPolyMeshDetail *detail = tile.detail;
	const int tile_x = tile.x, tile_y = tile.y;
	std::vector<float> link_verts, link_radii;
	std::vector<unsigned short> link_flags;
	std::vector<unsigned char> link_areas, link_dirs;
	std::vector<unsigned int> link_ids;
	unsigned char *data = nullptr;

	*size = 0;

	for (size_t i = 0; i < links.size(); i++)
	{
		link_verts.insert(link_verts.end(), links[i].start, links[i].start + 3);
		link_verts.insert(link_verts.end(), links[i].end, links[i].end + 3);
		link_radii.push_back(NAV_CELL_SIZE);
		link_flags.push_back(links[i].lift ? NAV_FLAG_LIFT : NavDropFlag(links[i].start[1] - links[i].end[1]));
		link_areas.push_back(links[i].lift ? NAV_AREA_LIFT : NAV_AREA_DROP);
		link_dirs.push_back(links[i].lift ? DT_OFFMESH_CON_BIDIR : 0); /* drops: one way, down */
		link_ids.push_back((unsigned int)i);
	}

	{
		dtNavMeshCreateParams params;

		memset(&params, 0, sizeof(params));
		params.verts = polymesh->verts;
		params.vertCount = polymesh->nverts;
		params.polys = polymesh->polys;
		params.polyAreas = polymesh->areas;
		params.polyFlags = polymesh->flags;
		params.polyCount = polymesh->npolys;
		params.nvp = polymesh->nvp;
		params.detailMeshes = detail->meshes;
		params.detailVerts = detail->verts;
		params.detailVertsCount = detail->nverts;
		params.detailTris = detail->tris;
		params.detailTriCount = detail->ntris;
		params.walkableHeight = NAV_AGENTS[agent].height;
		params.walkableRadius = NAV_AGENTS[agent].radius;
		params.walkableClimb = NAV_AGENT_CLIMB;
		params.tileX = tile_x;
		params.tileY = tile_y;
		params.tileLayer = 0;
		rcVcopy(params.bmin, polymesh->bmin);
		rcVcopy(params.bmax, polymesh->bmax);
		params.cs = NAV_CELL_SIZE;
		params.ch = NAV_CELL_HEIGHT;
		params.buildBvTree = true;

		if (!links.empty())
		{
			params.offMeshConVerts = link_verts.data();
			params.offMeshConRad = link_radii.data();
			params.offMeshConFlags = link_flags.data();
			params.offMeshConAreas = link_areas.data();
			params.offMeshConDir = link_dirs.data();
			params.offMeshConUserID = link_ids.data();
			params.offMeshConCount = (int)links.size();
		}

		if (!dtCreateNavMeshData(&params, &data, size))
		{
			data = nullptr;
			*size = 0;
		}
	}

	return data;
}

/* is this edge of a polygon the floor's end (no polygon beyond it)? */
static bool
NavOpenEdge(const dtMeshTile *tile, const dtPoly *poly, int edge)
{
	if (poly->neis[edge] == 0)
	{
		return true;
	}

	if (!(poly->neis[edge] & DT_EXT_LINK))
	{
		return false;
	}

	/* on the tile's side: open unless a polygon in the next tile joins it */
	for (unsigned int k = poly->firstLink; k != DT_NULL_LINK; k = tile->links[k].next)
	{
		if (tile->links[k].edge == edge)
		{
			return false;
		}
	}

	return true;
}

/* the ledge drops for a size's mesh: along every open edge of the floor,
   step out past the edge (a wall in the way: none), trace down to floor
   no more than NAV_MAX_DROP below that isn't water, lava or slime and has
   mesh near it */
static void
FindDrops(const dtNavMesh *mesh, const dtNavMeshQuery *query, int agent, std::vector<NavLink> &links)
{
	const float radius = NAV_AGENTS[agent].radius;
	const float clear = radius + NAV_CLEARANCE;                     /* how far the mesh stays from a ledge's lip */
	const float out = clear * 2 + NAV_CELL_SIZE;                     /* past the lip, onto the lower floor's mesh */
	const float mins[3] = {-radius, -radius, 0};
	const float maxs[3] = {radius, radius, NAV_AGENTS[agent].height - NAV_AGENT_CLIMB};
	const float extents[3] = {clear + 16.0f, NAV_AGENT_CLIMB, clear + 16.0f};
	dtQueryFilter filter;

	filter.setIncludeFlags(NAV_FLAG_WALK);

	for (int t = 0; t < mesh->getMaxTiles(); t++)
	{
		const dtMeshTile *tile = mesh->getTile(t);

		if (!tile || !tile->header)
		{
			continue;
		}

		for (int p = 0; p < tile->header->polyCount; p++)
		{
			const dtPoly *poly = &tile->polys[p];
			float center[3] = {0, 0, 0};

			if (poly->getType() != DT_POLYTYPE_GROUND)
			{
				continue;
			}

			for (int v = 0; v < poly->vertCount; v++)
			{
				dtVadd(center, center, &tile->verts[poly->verts[v] * 3]);
			}

			dtVscale(center, center, 1.0f / poly->vertCount);

			for (int v = 0; v < poly->vertCount; v++)
			{
				const float *a = &tile->verts[poly->verts[v] * 3];
				const float *b = &tile->verts[poly->verts[(v + 1) % poly->vertCount] * 3];
				const float length = sqrtf(dtSqr(b[0] - a[0]) + dtSqr(b[2] - a[2]));

				if (length < 1.0f || !NavOpenEdge(tile, poly, v))
				{
					continue;
				}

				/* the edge's outward side, in Recast's x and z */
				float normal[2] = {(b[2] - a[2]) / length, (a[0] - b[0]) / length};

				if (normal[0] * ((a[0] + b[0]) * 0.5f - center[0]) + normal[1] * ((a[2] + b[2]) * 0.5f - center[2]) < 0)
				{
					normal[0] = -normal[0];
					normal[1] = -normal[1];
				}

				const int samples = length > NAV_DROP_SPACING ? (int)(length / NAV_DROP_SPACING) : 1;

				for (int i = 0; i < samples; i++)
				{
					const float along = (i + 0.5f) / samples;
					float edge_r[3], edge_q[3], top[3], below[3], outside[3], land[3], ground[3], landing_r[3],
						on_mesh[3], on_mesh_q[3];
					dtPolyRef landing = 0;

					dtVlerp(edge_r, a, b, along);
					/* a little inside, so the link starts in this tile and on this polygon */
					edge_r[0] -= normal[0];
					edge_r[2] -= normal[1];
					ToQuake(edge_r, edge_q);

					/* the floor under the edge */
					memcpy(top, edge_q, sizeof(top));
					top[2] += NAV_AGENT_CLIMB;
					memcpy(below, edge_q, sizeof(below));
					below[2] -= NAV_AGENT_CLIMB;

					const float floor = CM_NavTrace(top, below, mins, maxs, NAV_SOLID, ground);

					if (floor < 0 || floor >= 1)
					{
						continue;
					}

					/* step out past the lip at step height: no wall in the way */
					ground[2] += NAV_AGENT_CLIMB;
					outside[0] = ground[0] + normal[0] * out;
					outside[1] = ground[1] - normal[1] * out; /* Recast z is -y */
					outside[2] = ground[2];

					if (CM_NavTrace(ground, outside, mins, maxs, NAV_SOLID, nullptr) != 1.0f)
					{
						continue;
					}

					/* down to the floor below */
					memcpy(below, outside, sizeof(below));
					below[2] -= NAV_MAX_DROP + NAV_AGENT_CLIMB;

					const float fall = CM_NavTrace(outside, below, mins, maxs, NAV_SOLID, land);
					const float drop = ground[2] - NAV_AGENT_CLIMB - land[2];

					if (fall <= 0 || fall >= 1 || drop <= NAV_AGENT_CLIMB + 1.0f || drop > NAV_MAX_DROP)
					{
						continue;
					}

					land[2] += 1.0f;

					if (CM_PointContents(land, 0) & NAV_HAZARD)
					{
						continue;
					}

					/* the lower floor's mesh near where it lands, in reach */
					land[2] -= 1.0f;
					ToRecast(land, landing_r);

					if (dtStatusFailed(query->findNearestPoly(landing_r, extents, &filter, &landing, on_mesh)) ||
						!landing || fabsf(on_mesh[1] - landing_r[1]) > NAV_AGENT_CLIMB)
					{
						continue;
					}

					ToQuake(on_mesh, on_mesh_q);
					land[2] += NAV_AGENT_CLIMB;
					on_mesh_q[2] += NAV_AGENT_CLIMB;

					if (CM_NavTrace(land, on_mesh_q, mins, maxs, NAV_SOLID, nullptr) != 1.0f)
					{
						continue;
					}

					NavLink link;

					memcpy(link.start, edge_r, sizeof(link.start));
					memcpy(link.end, on_mesh, sizeof(link.end));
					link.lift = false;
					links.push_back(link);
				}
			}
		}
	}
}

/* each lift's link: from on top of it at the bottom (where the mesh has it)
   to the nearest floor beside its top */
static void
FindLifts(const NavGeometry &geometry, const dtNavMeshQuery *query, int agent, std::vector<NavLink> &links)
{
	const float reach = NAV_AGENTS[agent].radius + NAV_CLEARANCE + 32.0f; /* past the lift's sides */
	dtQueryFilter filter;

	filter.setIncludeFlags(NAV_FLAG_WALK);

	for (const NavLift &lift : geometry.lifts)
	{
		const float half_x = (lift.maxs[0] - lift.mins[0]) * 0.5f, half_y = (lift.maxs[1] - lift.mins[1]) * 0.5f;
		const float center[2] = {lift.mins[0] + half_x, lift.mins[1] + half_y};
		const float bottom_q[3] = {center[0], center[1], lift.maxs[2] - lift.travel};
		const float top_q[3] = {center[0], center[1], lift.maxs[2]};
		float bottom_r[3], top_r[3], on_bottom[3], on_top[3], on_bottom_q[3];
		dtPolyRef bottom = 0, top = 0;

		/* on the lift, at the bottom: too small for this size, no link */
		const float on_extents[3] = {half_x, NAV_AGENT_CLIMB, half_y};

		ToRecast(bottom_q, bottom_r);

		ToQuake(bottom_r, on_bottom_q);

		if (dtStatusFailed(query->findNearestPoly(bottom_r, on_extents, &filter, &bottom, on_bottom)) || !bottom ||
			(ToQuake(on_bottom, on_bottom_q), on_bottom_q[0] < lift.mins[0]) || on_bottom_q[0] > lift.maxs[0] ||
			on_bottom_q[1] < lift.mins[1] || on_bottom_q[1] > lift.maxs[1] ||
			fabsf(on_bottom_q[2] - bottom_q[2]) > NAV_AGENT_CLIMB)
		{
			if (agent == 0)
			{
				Com_Printf("Navigation: lift *%d at %.0f %.0f %.0f: no floor on it\n", lift.model, bottom_q[0],
					bottom_q[1], bottom_q[2]);
			}

			continue;
		}

		/* beside it at the top */
		const float beside_extents[3] = {half_x + reach, NAV_AGENT_CLIMB, half_y + reach};

		ToRecast(top_q, top_r);

		if (dtStatusFailed(query->findNearestPoly(top_r, beside_extents, &filter, &top, on_top)) || !top ||
			fabsf(on_top[1] - top_r[1]) > NAV_AGENT_CLIMB)
		{
			if (agent == 0)
			{
				Com_Printf("Navigation: lift *%d at %.0f %.0f %.0f: no floor beside its top\n", lift.model,
					top_q[0], top_q[1], top_q[2]);
			}

			continue;
		}

		NavLink link;

		memcpy(link.start, on_bottom, sizeof(link.start));
		memcpy(link.end, on_top, sizeof(link.end));
		link.lift = true;
		links.push_back(link);
	}
}

/* the map's func_plats from its entity string (the ones the game leaves at
   the bottom: not targeted) */
static void
FindMapLifts(std::vector<NavLift> &lifts)
{
	int size = 0;
	const char *text = CM_EntityString(&size);
	const char *end = text ? text + size : nullptr;

	while (text && text < end && *text)
	{
		char key[64] = "", value[256] = "";
		char classname[64] = "", model[16] = "";
		float lip = 0, height = 0, origin[3] = {0, 0, 0};
		bool targeted = false, in_entity = false;

		/* one entity: { "key" "value" ... } */
		while (text < end && *text && *text != '{')
		{
			text++;
		}

		if (text >= end || !*text)
		{
			break;
		}

		text++;
		in_entity = true;

		while (in_entity && text < end && *text)
		{
			char *fields[2] = {key, value};
			const size_t sizes[2] = {sizeof(key), sizeof(value)};

			for (int f = 0; f < 2 && in_entity; f++)
			{
				size_t used = 0;

				while (text < end && *text && *text != '"' && *text != '}')
				{
					text++;
				}

				if (text >= end || !*text || *text == '}')
				{
					in_entity = false;
					break;
				}

				text++;

				while (text < end && *text && *text != '"')
				{
					if (used + 1 < sizes[f])
					{
						fields[f][used++] = *text;
					}

					text++;
				}

				fields[f][used] = 0;

				if (text < end && *text)
				{
					text++;
				}
			}

			if (!in_entity)
			{
				break;
			}

			if (!strcmp(key, "classname"))
			{
				snprintf(classname, sizeof(classname), "%s", value);
			}
			else if (!strcmp(key, "model"))
			{
				snprintf(model, sizeof(model), "%s", value);
			}
			else if (!strcmp(key, "lip"))
			{
				lip = (float)atof(value);
			}
			else if (!strcmp(key, "height"))
			{
				height = (float)atof(value);
			}
			else if (!strcmp(key, "targetname"))
			{
				targeted = true;
			}
			else if (!strcmp(key, "origin"))
			{
				sscanf(value, "%f %f %f", &origin[0], &origin[1], &origin[2]);
			}
		}

		if (text < end && *text == '}')
		{
			text++;
		}

		if (strcmp(classname, "func_plat") || targeted || model[0] != '*')
		{
			continue;
		}

		/* as the game's SP_func_plat works it out */
		NavLift lift;

		lift.model = atoi(model + 1);

		if (!CM_ModelBounds(lift.model, lift.mins, lift.maxs))
		{
			continue;
		}

		for (int k = 0; k < 3; k++)
		{
			lift.mins[k] += origin[k];
			lift.maxs[k] += origin[k];
		}

		lift.travel = height ? height : (lift.maxs[2] - lift.mins[2]) - (lip ? lip : 8.0f);

		if (lift.travel > NAV_AGENT_CLIMB)
		{
			lifts.push_back(lift);
		}
	}
}

static unsigned
NextPowerOfTwo(unsigned v)
{
	unsigned p = 1;

	while (p < v)
	{
		p <<= 1;
	}

	return p;
}

static unsigned
Log2(unsigned v)
{
	unsigned r = 0;

	while (v >>= 1)
	{
		r++;
	}

	return r;
}

static dtNavMesh *
BuildMesh(const NavGeometry &geometry, int agent)
{
	NavContext context;
	const rcConfig config = NavConfig(geometry, agent);
	const float tile_world = NAV_TILE_CELLS * NAV_CELL_SIZE;
	const int tiles_x = (int)ceilf((geometry.bmax[0] - geometry.bmin[0]) / tile_world);
	const int tiles_y = (int)ceilf((geometry.bmax[2] - geometry.bmin[2]) / tile_world);
	const unsigned tile_bits = Log2(NextPowerOfTwo((unsigned)(tiles_x * tiles_y))) < 14 ?
		Log2(NextPowerOfTwo((unsigned)(tiles_x * tiles_y))) : 14;
	dtNavMeshParams params;
	dtNavMesh *mesh = dtAllocNavMesh();

	memset(&params, 0, sizeof(params));
	rcVcopy(params.orig, geometry.bmin);
	params.tileWidth = tile_world;
	params.tileHeight = tile_world;
	params.maxTiles = 1 << tile_bits;
	params.maxPolys = 1 << (22 - tile_bits);

	if (!mesh || dtStatusFailed(mesh->init(&params)))
	{
		dtFreeNavMesh(mesh);
		return nullptr;
	}

	/* the floor, tile by tile */
	std::vector<NavTile> tiles;
	const std::vector<NavLink> no_links;

	for (int y = 0; y < tiles_y; y++)
	{
		for (int x = 0; x < tiles_x; x++)
		{
			NavTile tile = {x, y, nullptr, nullptr};
			int size;

			if (!BuildTile(context, geometry, config, &tile))
			{
				continue;
			}

			unsigned char *data = TileData(tile, agent, no_links, &size);

			if (data && dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr)))
			{
				dtFree(data);
			}

			tiles.push_back(tile);
		}
	}

	/* then the ledge drops, found on that floor: each tile with some is
	   made again with its drops (a link belongs to the tile it starts in) */
	dtNavMeshQuery *query = dtAllocNavMeshQuery();
	std::vector<NavLink> drops;

	if (query && dtStatusSucceed(query->init(mesh, 2048)))
	{
		FindDrops(mesh, query, agent, drops);
		FindLifts(geometry, query, agent, drops);
	}

	dtFreeNavMeshQuery(query);

	for (NavTile &tile : tiles)
	{
		std::vector<NavLink> tile_drops;

		for (const NavLink &drop : drops)
		{
			int x, y;

			mesh->calcTileLoc(drop.start, &x, &y);

			if (x == tile.x && y == tile.y)
			{
				tile_drops.push_back(drop);
			}
		}

		if (!tile_drops.empty())
		{
			int size;
			unsigned char *data = TileData(tile, agent, tile_drops, &size);

			if (data)
			{
				mesh->removeTile(mesh->getTileRefAt(tile.x, tile.y, 0), nullptr, nullptr);

				if (dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr)))
				{
					dtFree(data);
				}
			}
		}

		rcFreePolyMesh(tile.polymesh);
		rcFreePolyMeshDetail(tile.detail);
	}

	return mesh;
}

/* ---- the cache file ---- */

/* the file: this header, then for each size its mesh's params, its tile
   count and its tiles (size, data) */
struct NavFileHeader
{
	char magic[4];
	int version;
	unsigned checksum;
	float settings[5 + NAV_AGENT_COUNT * 2]; /* cell size and height, climb, slope, clearance; each size's radius, height */
};

static void
NavCachePath(char *path, size_t size)
{
	snprintf(path, size, "%s/navigation/%s.otnav", FS_Gamedir(), nav_mapname);
}

static void
NavSettings(float *settings)
{
	settings[0] = NAV_CELL_SIZE;
	settings[1] = NAV_CELL_HEIGHT;
	settings[2] = NAV_AGENT_CLIMB;
	settings[3] = NAV_MAX_SLOPE;
	settings[4] = NAV_CLEARANCE;

	for (int a = 0; a < NAV_AGENT_COUNT; a++)
	{
		settings[5 + a * 2] = NAV_AGENTS[a].radius;
		settings[6 + a * 2] = NAV_AGENTS[a].height;
	}
}

static void
SaveMesh(FILE *file, const dtNavMesh *mesh)
{
	int tile_count = 0;

	fwrite(mesh->getParams(), sizeof(dtNavMeshParams), 1, file);

	for (int i = 0; i < mesh->getMaxTiles(); i++)
	{
		const dtMeshTile *tile = mesh->getTile(i);

		if (tile && tile->header && tile->dataSize)
		{
			tile_count++;
		}
	}

	fwrite(&tile_count, sizeof(tile_count), 1, file);

	for (int i = 0; i < mesh->getMaxTiles(); i++)
	{
		const dtMeshTile *tile = mesh->getTile(i);

		if (tile && tile->header && tile->dataSize)
		{
			fwrite(&tile->dataSize, sizeof(tile->dataSize), 1, file);
			fwrite(tile->data, tile->dataSize, 1, file);
		}
	}
}

static void
SaveCache(void)
{
	char path[1024];
	NavFileHeader header;
	FILE *file;

	NavCachePath(path, sizeof(path));
	FS_CreatePath(path);
	file = fopen(path, "wb");

	if (!file)
	{
		return;
	}

	memset(&header, 0, sizeof(header));
	memcpy(header.magic, "OTNV", 4);
	header.version = NAV_FILE_VERSION;
	header.checksum = CM_MapChecksum();
	NavSettings(header.settings);
	fwrite(&header, sizeof(header), 1, file);

	for (int a = 0; a < NAV_AGENT_COUNT; a++)
	{
		SaveMesh(file, nav_meshes[a]);
	}

	fclose(file);
}

/* one size's mesh from the file, or nullptr if the file is damaged */
static dtNavMesh *
LoadMesh(FILE *file)
{
	dtNavMeshParams params;
	dtNavMesh *mesh;
	int tile_count;

	if (fread(&params, sizeof(params), 1, file) != 1 || fread(&tile_count, sizeof(tile_count), 1, file) != 1 ||
		tile_count < 0 || !(mesh = dtAllocNavMesh()))
	{
		return nullptr;
	}

	if (dtStatusFailed(mesh->init(&params)))
	{
		dtFreeNavMesh(mesh);
		return nullptr;
	}

	for (int i = 0; i < tile_count; i++)
	{
		int size = 0;
		unsigned char *data;

		if (fread(&size, sizeof(size), 1, file) != 1 || size <= 0 || size > 64 * 1024 * 1024 ||
			!(data = (unsigned char *)dtAlloc(size, DT_ALLOC_PERM)))
		{
			dtFreeNavMesh(mesh);
			return nullptr;
		}

		if (fread(data, size, 1, file) != 1 ||
			dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr)))
		{
			dtFree(data);
			dtFreeNavMesh(mesh);
			return nullptr;
		}
	}

	return mesh;
}

/* every size's mesh from the cache; false (and nothing loaded) if it's
   missing, damaged, for another map version or other settings */
static bool
LoadCache(void)
{
	char path[1024];
	NavFileHeader header;
	float settings[5 + NAV_AGENT_COUNT * 2];
	FILE *file;

	NavCachePath(path, sizeof(path));
	file = fopen(path, "rb");

	if (!file)
	{
		return false;
	}

	NavSettings(settings);

	if (fread(&header, sizeof(header), 1, file) != 1 || memcmp(header.magic, "OTNV", 4) ||
		header.version != NAV_FILE_VERSION || header.checksum != CM_MapChecksum() ||
		memcmp(header.settings, settings, sizeof(settings)))
	{
		fclose(file);
		return false;
	}

	for (int a = 0; a < NAV_AGENT_COUNT; a++)
	{
		nav_meshes[a] = LoadMesh(file);

		if (!nav_meshes[a])
		{
			for (int b = 0; b < a; b++)
			{
				dtFreeNavMesh(nav_meshes[b]);
				nav_meshes[b] = nullptr;
			}

			fclose(file);
			return false; /* damaged: build them again */
		}
	}

	fclose(file);
	return true;
}

/* ---- the C interface ---- */

static int
NavPolygonCount(int agent)
{
	int count = 0;

	const dtNavMesh *mesh = nav_meshes[agent]; /* the const getTile is the public one */

	for (int i = 0; mesh && i < mesh->getMaxTiles(); i++)
	{
		const dtMeshTile *tile = mesh->getTile(i);

		if (tile && tile->header)
		{
			count += tile->header->polyCount - tile->header->offMeshConCount; /* drops are polygons to Detour */
		}
	}

	return count;
}

extern "C" void
Nav_Free(void)
{
	for (int a = 0; a < NAV_AGENT_COUNT; a++)
	{
		dtFreeNavMeshQuery(nav_queries[a]);
		dtFreeNavMesh(nav_meshes[a]);
		nav_queries[a] = nullptr;
		nav_meshes[a] = nullptr;
	}

	nav_mapname[0] = '\0';
}

/* ledge drops in a size's mesh */
static int
NavLinkCount(int agent, bool lifts)
{
	int count = 0;

	const dtNavMesh *mesh = nav_meshes[agent];

	for (int i = 0; mesh && i < mesh->getMaxTiles(); i++)
	{
		const dtMeshTile *tile = mesh->getTile(i);

		for (int c = 0; tile && tile->header && c < tile->header->offMeshConCount; c++)
		{
			const dtPoly *poly = &tile->polys[tile->offMeshCons[c].poly];

			count += ((poly->flags & NAV_FLAG_LIFT) != 0) == lifts;
		}
	}

	return count;
}

/* "Navigation: base1, 1211 polygons, built in 117 ms (medium 900, large 500), 40 drops" */
static void
NavPrintInfo(void)
{
	char sizes[128] = "";

	for (int a = 1; a < NAV_AGENT_COUNT; a++)
	{
		const size_t used = strlen(sizes);

		snprintf(sizes + used, sizeof(sizes) - used, "%s%s %d", a > 1 ? ", " : "", NAV_AGENTS[a].name,
			NavPolygonCount(a));
	}

	Com_Printf("Navigation: %s, %d polygons, %s in %d ms (%s), %d drops, %d lifts\n", nav_mapname,
		NavPolygonCount(0), nav_from_cache ? "loaded" : "built", nav_build_msec, sizes, NavLinkCount(0, false),
		NavLinkCount(0, true));
}

/* the size a monster of this half width and height uses: the smallest it
   fits, else the largest */
static int
NavAgentFor(float radius, float height)
{
	for (int a = 0; a < NAV_AGENT_COUNT; a++)
	{
		if (radius <= NAV_AGENTS[a].radius + 0.5f && height <= NAV_AGENTS[a].height + 0.5f)
		{
			return a;
		}
	}

	return NAV_AGENT_COUNT - 1;
}

extern "C" void
Nav_LoadMap(const char *mapname)
{
	int start = Sys_Milliseconds();

	Nav_Free();
	snprintf(nav_mapname, sizeof(nav_mapname), "%s", mapname);
	nav_from_cache = LoadCache();

	if (!nav_from_cache)
	{
		NavGeometry geometry;
		bool complete = true;

		CM_ForEachSolidTriangle(NAV_SOLID, CollectTriangle, &geometry);

		/* lifts where they rest */
		FindMapLifts(geometry.lifts);

		for (const NavLift &lift : geometry.lifts)
		{
			CM_ForEachModelTriangle(lift.model, -lift.travel, NAV_SOLID, CollectTriangle, &geometry);
		}

		for (int a = 0; a < NAV_AGENT_COUNT && !geometry.triangles.empty(); a++)
		{
			nav_meshes[a] = BuildMesh(geometry, a);
			complete = complete && nav_meshes[a];
		}

		if (complete && nav_meshes[0])
		{
			SaveCache();
		}
	}

	nav_build_msec = Sys_Milliseconds() - start;

	if (!nav_meshes[0])
	{
		Com_Printf("Navigation: no walkable floor in %s\n", mapname);
		Nav_Free();
		return;
	}

	for (int a = 0; a < NAV_AGENT_COUNT; a++)
	{
		nav_queries[a] = nav_meshes[a] ? dtAllocNavMeshQuery() : nullptr;

		if (nav_queries[a] && dtStatusFailed(nav_queries[a]->init(nav_meshes[a], 4096)))
		{
			dtFreeNavMeshQuery(nav_queries[a]);
			nav_queries[a] = nullptr;
		}
	}

	NavPrintInfo();
}

static int
NavFindPath(int agent, const float *start, const float *goal, float (*points)[3], int max_points, int *complete)
{
	dtNavMeshQuery *nav_query = nav_queries[agent];
	static const float extents[3] = {32.0f, 64.0f, 32.0f}; /* Recast axes: up is y */
	static const int MAX_POLYS = 512;
	dtPolyRef path[MAX_POLYS], start_ref, goal_ref;
	float start_r[3], goal_r[3], start_on[3], goal_on[3];
	float corners[256 * 3];
	unsigned char corner_flags[256];
	dtPolyRef corner_refs[256];
	int path_count = 0, corner_count = 0;
	dtQueryFilter filter;

	*complete = 0;

	if (!nav_query || max_points <= 0)
	{
		return 0;
	}

	filter.setIncludeFlags(NAV_FLAG_WALK | NavDropFlags(NAV_MAX_DROP) | NAV_FLAG_LIFT); /* drops and lifts too */
	ToRecast(start, start_r);
	ToRecast(goal, goal_r);

	if (dtStatusFailed(nav_query->findNearestPoly(start_r, extents, &filter, &start_ref, start_on)) || !start_ref ||
		dtStatusFailed(nav_query->findNearestPoly(goal_r, extents, &filter, &goal_ref, goal_on)) || !goal_ref)
	{
		return 0;
	}

	if (dtStatusFailed(nav_query->findPath(start_ref, goal_ref, start_on, goal_on, &filter, path, &path_count,
		MAX_POLYS)) || path_count == 0)
	{
		return 0;
	}

	/* not reaching the goal's polygon: stop at the closest point of the last one */
	if (path[path_count - 1] != goal_ref)
	{
		nav_query->closestPointOnPoly(path[path_count - 1], goal_r, goal_on, nullptr);
	}
	else
	{
		*complete = 1;
	}

	if (dtStatusFailed(nav_query->findStraightPath(start_on, goal_on, path, path_count, corners, corner_flags,
		corner_refs, &corner_count, max_points < 256 ? max_points : 256)))
	{
		return 0;
	}

	for (int i = 0; i < corner_count; i++)
	{
		ToQuake(&corners[i * 3], points[i]);
	}

	return corner_count;
}

extern "C" int
Nav_FindPath(const float *start, const float *goal, float (*points)[3], int max_points, int *complete)
{
	return NavFindPath(0, start, goal, points, max_points, complete);
}

extern "C" int
Nav_ForEachEdge(int agent, const float *center, float radius,
	void (*edge)(const float *a, const float *b, void *user), void *user)
{
	float center_r[3];
	int edges = 0;

	agent = agent < 0 ? 0 : (agent >= NAV_AGENT_COUNT ? NAV_AGENT_COUNT - 1 : agent);

	if (!nav_meshes[agent])
	{
		return 0;
	}

	ToRecast(center, center_r);

	const dtNavMesh *mesh = nav_meshes[agent];

	for (int t = 0; t < mesh->getMaxTiles(); t++)
	{
		const dtMeshTile *tile = mesh->getTile(t);

		if (!tile || !tile->header)
		{
			continue;
		}

		for (int p = 0; p < tile->header->polyCount; p++)
		{
			const dtPoly *poly = &tile->polys[p];

			for (int v = 0; v < poly->vertCount; v++)
			{
				const float *a = &tile->verts[poly->verts[v] * 3];
				const float *b = &tile->verts[poly->verts[(v + 1) % poly->vertCount] * 3];
				const float dx = (a[0] + b[0]) * 0.5f - center_r[0];
				const float dy = (a[1] + b[1]) * 0.5f - center_r[1];
				const float dz = (a[2] + b[2]) * 0.5f - center_r[2];
				float qa[3], qb[3];

				if (dx * dx + dy * dy + dz * dz > radius * radius)
				{
					continue;
				}

				ToQuake(a, qa);
				ToQuake(b, qb);
				edge(qa, qb, user);
				edges++;
			}
		}

		/* the ledge drops, from the edge down to where they land */
		for (int i = 0; i < tile->header->offMeshConCount; i++)
		{
			const float *link = tile->offMeshCons[i].pos;
			const float dx = link[0] - center_r[0], dy = link[1] - center_r[1], dz = link[2] - center_r[2];
			float qa[3], qb[3];

			if (dx * dx + dy * dy + dz * dz > radius * radius)
			{
				continue;
			}

			ToQuake(link, qa);
			ToQuake(link + 3, qb);
			edge(qa, qb, user);
			edges++;
		}
	}

	return edges;
}

/* ---- the re-release's GetPathToGoal ---- */

/* paths asked to be drawn, until their time is up */
static const int NAV_DEBUG_PATHS = 32;
static const int NAV_DEBUG_POINTS = 64;

struct NavDebugPath
{
	int expires; /* Sys_Milliseconds */
	int count;
	float points[NAV_DEBUG_POINTS][3];
};

static NavDebugPath nav_debug_paths[NAV_DEBUG_PATHS];
static int nav_debug_next;

static void
NavKeepDebugPath(const float (*points)[3], int count, float seconds)
{
	NavDebugPath &path = nav_debug_paths[nav_debug_next];

	nav_debug_next = (nav_debug_next + 1) % NAV_DEBUG_PATHS;
	path.count = count < NAV_DEBUG_POINTS ? count : NAV_DEBUG_POINTS;
	path.expires = Sys_Milliseconds() + (int)(seconds * 1000.0f);
	memcpy(path.points, points, sizeof(path.points[0]) * path.count);
}

extern "C" void
Nav_ForEachDebugSegment(void (*segment)(const float *a, const float *b, void *user), void *user)
{
	const int now = Sys_Milliseconds();

	for (int i = 0; i < NAV_DEBUG_PATHS; i++)
	{
		const NavDebugPath &path = nav_debug_paths[i];

		for (int p = 1; path.expires > now && p < path.count; p++)
		{
			segment(path.points[p - 1], path.points[p], user);
		}
	}
}

static float
Distance(const float *a, const float *b)
{
	const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};

	return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

extern "C" int
Nav_GetPathToGoalSized(const optech2_path_request_t *request, optech2_path_info_t *info, float agent_radius,
	float agent_height)
{
	const int agent = NavAgentFor(agent_radius, agent_height);
	dtNavMeshQuery *nav_query = nav_queries[agent];
	static const int MAX_POLYS = 512;
	static const int MAX_CORNERS = 128;
	dtPolyRef path[MAX_POLYS], start_ref = 0, goal_ref = 0;
	float start_r[3], goal_r[3], start_on[3], goal_on[3], extents[3];
	float corners_r[MAX_CORNERS * 3], corners[MAX_CORNERS][3];
	unsigned char corner_flags[MAX_CORNERS];
	dtPolyRef corner_refs[MAX_CORNERS];
	int path_count = 0, corner_count = 0, next;
	dtQueryFilter filter;
	float length = 0;

	memset(info, 0, sizeof(*info));
	info->link_type = OPTECH2_PATH_LINK_WALK;

	if (!(request->path_flags & (OPTECH2_PATH_WALK | OPTECH2_PATH_WATER)))
	{
		info->return_code = OPTECH2_PATH_MISSING_WALK_OR_SWIM_FLAG;
		return 0;
	}

	if (!nav_query)
	{
		info->return_code = OPTECH2_PATH_NO_NAV_AVAILABLE;
		return 0;
	}

	/* how far from the floor the start and goal may be: request, else defaults */
	extents[0] = extents[2] = request->radius > 0 ? request->radius : 32.0f;
	extents[1] = request->max_height > 0 ? request->max_height : 64.0f; /* Recast: y is up */
	/* ledge drops if it may walk off ledges, no deeper than drop_height */
	filter.setIncludeFlags(NAV_FLAG_WALK |
		((request->path_flags & OPTECH2_PATH_WALK_OFF_LEDGE) ? NavDropFlags(request->drop_height) : 0) |
		((request->path_flags & OPTECH2_PATH_ELEVATOR) ? NAV_FLAG_LIFT : 0));
	ToRecast(request->start, start_r);
	ToRecast(request->goal, goal_r);

	if (dtStatusFailed(nav_query->findNearestPoly(start_r, extents, &filter, &start_ref, start_on)) || !start_ref)
	{
		info->return_code = OPTECH2_PATH_NO_START_NODE;
		return 0;
	}

	if (dtStatusFailed(nav_query->findNearestPoly(goal_r, extents, &filter, &goal_ref, goal_on)) || !goal_ref)
	{
		info->return_code = OPTECH2_PATH_NO_GOAL_NODE;
		return 0;
	}

	if (dtStatusFailed(nav_query->findPath(start_ref, goal_ref, start_on, goal_on, &filter, path, &path_count,
		MAX_POLYS)) || path_count == 0)
	{
		info->return_code = OPTECH2_PATH_NO_PATH_FOUND;
		return 0;
	}

	const bool complete = (path[path_count - 1] == goal_ref);

	if (!complete)
	{
		nav_query->closestPointOnPoly(path[path_count - 1], goal_r, goal_on, nullptr);
	}

	if (dtStatusFailed(nav_query->findStraightPath(start_on, goal_on, path, path_count, corners_r, corner_flags,
		corner_refs, &corner_count, MAX_CORNERS)) || corner_count == 0)
	{
		info->return_code = OPTECH2_PATH_NO_PATH_FOUND;
		return 0;
	}

	for (int i = 0; i < corner_count; i++)
	{
		ToQuake(&corners_r[i * 3], corners[i]);

		if (i > 0)
		{
			length += Distance(corners[i - 1], corners[i]);
		}
	}

	info->point_count = corner_count;
	info->path_dist_sqr = length * length;

	if (request->points && request->point_count > 0)
	{
		const int copy = corner_count < request->point_count ? corner_count : (int)request->point_count;

		memcpy(request->points, corners, sizeof(corners[0]) * copy);
	}

	if (request->draw_time > 0)
	{
		NavKeepDebugPath(corners, corner_count, request->draw_time);
	}

	/* corners[0] is where the start meets the floor: move toward the first
	   corner farther than move_dist, and the one after it. A drop's start
	   is flagged: its end, the corner after, is never passed over */
	next = 1;

	while (next < corner_count - 1 && Distance(request->start, corners[next]) <= request->move_dist &&
		!(corner_flags[next - 1] & DT_STRAIGHTPATH_OFFMESH_CONNECTION))
	{
		next++;
	}

	if (next >= corner_count)
	{
		next = corner_count - 1;
	}

	/* at a link's start: walk off the ledge toward where it lands, or ride
	   the lift to its other end */
	if (next > 0 && (corner_flags[next - 1] & DT_STRAIGHTPATH_OFFMESH_CONNECTION))
	{
		unsigned short link_flags = 0;

		nav_meshes[agent]->getPolyFlags(corner_refs[next - 1], &link_flags);
		info->link_type = (link_flags & NAV_FLAG_LIFT) ? OPTECH2_PATH_LINK_ELEVATOR : OPTECH2_PATH_LINK_WALK_OFF_LEDGE;
	}

	memcpy(info->first_move_point, corners[next], sizeof(info->first_move_point));
	memcpy(info->second_move_point, corners[next + 1 < corner_count ? next + 1 : next],
		sizeof(info->second_move_point));

	/* at the end: the goal, or as close as the mesh gets */
	if (Distance(request->start, corners[corner_count - 1]) <= (request->move_dist > 16.0f ? request->move_dist : 16.0f))
	{
		info->return_code = complete ? OPTECH2_PATH_REACHED_GOAL : OPTECH2_PATH_REACHED_PATH_END;
	}
	else
	{
		info->return_code = OPTECH2_PATH_IN_PROGRESS;
	}

	return 1;
}

extern "C" int
Nav_GetPathToGoal(const optech2_path_request_t *request, optech2_path_info_t *info)
{
	return Nav_GetPathToGoalSized(request, info, 0, 0); /* the re-release's: player-sized */
}

/* ---- console commands ---- */

static void
Nav_Info_f(void)
{
	if (!nav_meshes[0])
	{
		Com_Printf("Navigation: none (no map, or no walkable floor)\n");
		return;
	}

	NavPrintInfo();
}

/* "nav_path x1 y1 z1 x2 y2 z2 [small|medium|large]": a path between two
   points for a monster of that size (default small), for testing */
static void
Nav_Path_f(void)
{
	float start[3], goal[3], points[64][3];
	float length = 0;
	int count, complete;

	int agent = 0;

	if (Cmd_Argc() != 7 && Cmd_Argc() != 8)
	{
		Com_Printf("usage: nav_path x1 y1 z1 x2 y2 z2 [small|medium|large]\n");
		return;
	}

	for (int a = 0; Cmd_Argc() == 8 && a < NAV_AGENT_COUNT; a++)
	{
		if (!strcmp(Cmd_Argv(7), NAV_AGENTS[a].name))
		{
			agent = a;
		}
	}

	for (int i = 0; i < 3; i++)
	{
		start[i] = (float)atof(Cmd_Argv(1 + i));
		goal[i] = (float)atof(Cmd_Argv(4 + i));
	}

	count = NavFindPath(agent, start, goal, points, 64, &complete);

	if (count > 1)
	{
		NavKeepDebugPath(points, count, 5.0f); /* cl_shownav or not: drawn for 5 s */
	}

	for (int i = 1; i < count; i++)
	{
		float d[3] = {points[i][0] - points[i - 1][0], points[i][1] - points[i - 1][1], points[i][2] - points[i - 1][2]};

		length += sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	}

	Com_Printf("nav_path %s %d points %.0f units\n", count ? (complete ? "found" : "partial") : "none", count, length);

	for (int i = 0; i < count; i++)
	{
		Com_Printf("  %.0f %.0f %.0f\n", points[i][0], points[i][1], points[i][2]);
	}
}

static void
Nav_Rebuild_f(void)
{
	char path[1024];
	char mapname[64];

	if (!nav_mapname[0])
	{
		Com_Printf("Navigation: no map\n");
		return;
	}

	snprintf(mapname, sizeof(mapname), "%s", nav_mapname);
	NavCachePath(path, sizeof(path));
	remove(path);
	Nav_LoadMap(mapname);
}

extern "C" void
Nav_Init(void)
{
	Cmd_AddCommand("nav_info", Nav_Info_f);
	Cmd_AddCommand("nav_path", Nav_Path_f);
	Cmd_AddCommand("nav_rebuild", Nav_Rebuild_f);
}
