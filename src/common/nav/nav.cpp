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
}

/* what monsters walk on and around (shared.h's CONTENTS_*) */
static const int NAV_SOLID = 1 /* SOLID */ | 2 /* WINDOW */ | 0x10000 /* PLAYERCLIP */ | 0x20000 /* MONSTERCLIP */;

/* build settings: a player-sized monster (32 x 32 x 56, steps of 18,
   slopes up to the player's 0.7 normal) on a grid of 8 x 2 units */
static const float NAV_CELL_SIZE = 8.0f;
static const float NAV_CELL_HEIGHT = 2.0f;
static const float NAV_AGENT_HEIGHT = 56.0f;
static const float NAV_AGENT_RADIUS = 16.0f;
static const float NAV_AGENT_CLIMB = 18.0f;
static const float NAV_MAX_SLOPE = 45.57f; /* acos(0.7) */
static const int NAV_TILE_CELLS = 64;      /* 512 units */
static const int NAV_FILE_VERSION = 1;
static const unsigned short NAV_FLAG_WALK = 1;

static dtNavMesh *nav_mesh;
static dtNavMeshQuery *nav_query;
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

struct NavGeometry
{
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
NavConfig(const NavGeometry &geometry)
{
	rcConfig config;

	memset(&config, 0, sizeof(config));
	config.cs = NAV_CELL_SIZE;
	config.ch = NAV_CELL_HEIGHT;
	config.walkableSlopeAngle = NAV_MAX_SLOPE;
	config.walkableHeight = (int)ceilf(NAV_AGENT_HEIGHT / NAV_CELL_HEIGHT);
	config.walkableClimb = (int)floorf(NAV_AGENT_CLIMB / NAV_CELL_HEIGHT);
	config.walkableRadius = (int)ceilf(NAV_AGENT_RADIUS / NAV_CELL_SIZE);
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

/* one tile's navigation data (Detour's format), or nullptr if it has no
   floor */
static unsigned char *
BuildTile(NavContext &context, const NavGeometry &geometry, rcConfig config, int tile_x, int tile_y, int *size)
{
	const float tile_world = NAV_TILE_CELLS * NAV_CELL_SIZE;
	const float border = config.borderSize * config.cs;
	unsigned char *data = nullptr;

	*size = 0;
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
		return nullptr;
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
		params.walkableHeight = NAV_AGENT_HEIGHT;
		params.walkableRadius = NAV_AGENT_RADIUS;
		params.walkableClimb = NAV_AGENT_CLIMB;
		params.tileX = tile_x;
		params.tileY = tile_y;
		params.tileLayer = 0;
		rcVcopy(params.bmin, polymesh->bmin);
		rcVcopy(params.bmax, polymesh->bmax);
		params.cs = config.cs;
		params.ch = config.ch;
		params.buildBvTree = true;

		if (!dtCreateNavMeshData(&params, &data, size))
		{
			data = nullptr;
			*size = 0;
		}
	}

done:
	rcFreeHeightField(heightfield);
	rcFreeCompactHeightfield(compact);
	rcFreeContourSet(contours);
	rcFreePolyMesh(polymesh);
	rcFreePolyMeshDetail(detail);
	return data;
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
BuildMesh(void)
{
	NavGeometry geometry;
	NavContext context;

	CM_ForEachSolidTriangle(NAV_SOLID, CollectTriangle, &geometry);

	if (geometry.triangles.empty())
	{
		return nullptr;
	}

	const rcConfig config = NavConfig(geometry);
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

	for (int y = 0; y < tiles_y; y++)
	{
		for (int x = 0; x < tiles_x; x++)
		{
			int size;
			unsigned char *data = BuildTile(context, geometry, config, x, y, &size);

			if (data && dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr)))
			{
				dtFree(data);
			}
		}
	}

	return mesh;
}

/* ---- the cache file ---- */

struct NavFileHeader
{
	char magic[4];
	int version;
	unsigned checksum;
	float settings[6]; /* cell size and height, agent height, radius, climb, slope */
	int tile_count;
	dtNavMeshParams params;
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
	settings[2] = NAV_AGENT_HEIGHT;
	settings[3] = NAV_AGENT_RADIUS;
	settings[4] = NAV_AGENT_CLIMB;
	settings[5] = NAV_MAX_SLOPE;
}

static void
SaveCache(const dtNavMesh *mesh)
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
	memcpy(&header.params, mesh->getParams(), sizeof(header.params));

	for (int i = 0; i < mesh->getMaxTiles(); i++)
	{
		const dtMeshTile *tile = mesh->getTile(i);

		if (tile && tile->header && tile->dataSize)
		{
			header.tile_count++;
		}
	}

	fwrite(&header, sizeof(header), 1, file);

	for (int i = 0; i < mesh->getMaxTiles(); i++)
	{
		const dtMeshTile *tile = mesh->getTile(i);

		if (tile && tile->header && tile->dataSize)
		{
			fwrite(&tile->dataSize, sizeof(tile->dataSize), 1, file);
			fwrite(tile->data, tile->dataSize, 1, file);
		}
	}

	fclose(file);
}

static dtNavMesh *
LoadCache(void)
{
	char path[1024];
	NavFileHeader header;
	float settings[6];
	dtNavMesh *mesh = nullptr;
	FILE *file;

	NavCachePath(path, sizeof(path));
	file = fopen(path, "rb");

	if (!file)
	{
		return nullptr;
	}

	NavSettings(settings);

	if (fread(&header, sizeof(header), 1, file) != 1 || memcmp(header.magic, "OTNV", 4) ||
		header.version != NAV_FILE_VERSION || header.checksum != CM_MapChecksum() ||
		memcmp(header.settings, settings, sizeof(settings)) || header.tile_count < 0 ||
		!(mesh = dtAllocNavMesh()) || dtStatusFailed(mesh->init(&header.params)))
	{
		fclose(file);
		dtFreeNavMesh(mesh);
		return nullptr;
	}

	for (int i = 0; i < header.tile_count; i++)
	{
		int size = 0;
		unsigned char *data;

		if (fread(&size, sizeof(size), 1, file) != 1 || size <= 0 || size > 64 * 1024 * 1024 ||
			!(data = (unsigned char *)dtAlloc(size, DT_ALLOC_PERM)))
		{
			break;
		}

		if (fread(data, size, 1, file) != 1 ||
			dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr)))
		{
			dtFree(data);
			fclose(file);
			dtFreeNavMesh(mesh);
			return nullptr; /* damaged: build it again */
		}
	}

	fclose(file);
	return mesh;
}

/* ---- the C interface ---- */

static int
NavPolygonCount(void)
{
	int count = 0;

	const dtNavMesh *mesh = nav_mesh; /* the const getTile is the public one */

	for (int i = 0; mesh && i < mesh->getMaxTiles(); i++)
	{
		const dtMeshTile *tile = mesh->getTile(i);

		if (tile && tile->header)
		{
			count += tile->header->polyCount;
		}
	}

	return count;
}

extern "C" void
Nav_Free(void)
{
	dtFreeNavMeshQuery(nav_query);
	dtFreeNavMesh(nav_mesh);
	nav_query = nullptr;
	nav_mesh = nullptr;
	nav_mapname[0] = '\0';
}

extern "C" void
Nav_LoadMap(const char *mapname)
{
	int start = Sys_Milliseconds();

	Nav_Free();
	snprintf(nav_mapname, sizeof(nav_mapname), "%s", mapname);
	nav_mesh = LoadCache();
	nav_from_cache = (nav_mesh != nullptr);

	if (!nav_mesh)
	{
		nav_mesh = BuildMesh();

		if (nav_mesh)
		{
			SaveCache(nav_mesh);
		}
	}

	nav_build_msec = Sys_Milliseconds() - start;

	if (!nav_mesh)
	{
		Com_Printf("Navigation: no walkable floor in %s\n", mapname);
		return;
	}

	nav_query = dtAllocNavMeshQuery();

	if (!nav_query || dtStatusFailed(nav_query->init(nav_mesh, 4096)))
	{
		Nav_Free();
		return;
	}

	Com_Printf("Navigation: %s, %d polygons, %s in %d ms\n", mapname, NavPolygonCount(),
		nav_from_cache ? "loaded" : "built", nav_build_msec);
}

extern "C" int
Nav_FindPath(const float *start, const float *goal, float (*points)[3], int max_points, int *complete)
{
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

	filter.setIncludeFlags(NAV_FLAG_WALK);
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
Nav_ForEachEdge(const float *center, float radius, void (*edge)(const float *a, const float *b, void *user),
	void *user)
{
	float center_r[3];
	int edges = 0;

	if (!nav_mesh)
	{
		return 0;
	}

	ToRecast(center, center_r);

	const dtNavMesh *mesh = nav_mesh;

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
	}

	return edges;
}

/* ---- console commands ---- */

static void
Nav_Info_f(void)
{
	if (!nav_mesh)
	{
		Com_Printf("Navigation: none (no map, or no walkable floor)\n");
		return;
	}

	Com_Printf("Navigation: %s, %d polygons, %s in %d ms\n", nav_mapname, NavPolygonCount(),
		nav_from_cache ? "loaded" : "built", nav_build_msec);
}

/* "nav_path x1 y1 z1 x2 y2 z2": a path between two points, for testing */
static void
Nav_Path_f(void)
{
	float start[3], goal[3], points[64][3];
	float length = 0;
	int count, complete;

	if (Cmd_Argc() != 7)
	{
		Com_Printf("usage: nav_path x1 y1 z1 x2 y2 z2\n");
		return;
	}

	for (int i = 0; i < 3; i++)
	{
		start[i] = (float)atof(Cmd_Argv(1 + i));
		goal[i] = (float)atof(Cmd_Argv(4 + i));
	}

	count = Nav_FindPath(start, goal, points, 64, &complete);

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
