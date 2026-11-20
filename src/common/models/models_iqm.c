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
 * optech2: the IQM (Inter-Quake Model, version 2) model format, the
 * project's standard animated model format.
 *
 * Format by Lee Salzman (public domain / MIT), written from its published
 * description. Like the MD5 and MDR loaders, the skeleton is evaluated at
 * load time and every animation frame is baked into vertex positions and
 * normals, so all renderers draw it as a normal frame-animated model. Each
 * IQM animation becomes an animation group named after it (frames
 * "<name>001", "<name>002", ...).
 *
 * =======================================================================
 */

#include "models.h"

#define IQM_MAGIC "INTERQUAKEMODEL"
#define IQM_VERSION 2

/* vertex array types */
#define IQM_POSITION 0
#define IQM_TEXCOORD 1
#define IQM_NORMAL 2
#define IQM_BLENDINDEXES 4
#define IQM_BLENDWEIGHTS 5

/* vertex array formats */
#define IQM_UBYTE 1
#define IQM_FLOAT 7

typedef struct
{
	char magic[16];
	unsigned int version;
	unsigned int filesize;
	unsigned int flags;
	unsigned int num_text, ofs_text;
	unsigned int num_meshes, ofs_meshes;
	unsigned int num_vertexarrays, num_vertexes, ofs_vertexarrays;
	unsigned int num_triangles, ofs_triangles, ofs_adjacency;
	unsigned int num_joints, ofs_joints;
	unsigned int num_poses, ofs_poses;
	unsigned int num_anims, ofs_anims;
	unsigned int num_frames, num_framechannels, ofs_frames, ofs_bounds;
	unsigned int num_comment, ofs_comment;
	unsigned int num_extensions, ofs_extensions;
} iqm_header_t;

typedef struct
{
	unsigned int name;
	unsigned int material;
	unsigned int first_vertex, num_vertexes;
	unsigned int first_triangle, num_triangles;
} iqm_mesh_t;

typedef struct
{
	unsigned int type;
	unsigned int flags;
	unsigned int format;
	unsigned int size;
	unsigned int offset;
} iqm_vertexarray_t;

typedef struct
{
	unsigned int name;
	int parent;
	float translate[3];
	float rotate[4]; /* quaternion x y z w */
	float scale[3];
} iqm_joint_t;

typedef struct
{
	int parent;
	unsigned int mask;
	float channeloffset[10];
	float channelscale[10];
} iqm_pose_t;

typedef struct
{
	unsigned int name;
	unsigned int first_frame, num_frames;
	float framerate;
	unsigned int flags;
} iqm_anim_t;

/* a 3x4 affine matrix: rotation/scale in [0..2][0..2], translation in [i][3] */
typedef float iqm_matrix_t[3][4];

/* true when count items of size bytes at offset fit in the file */
static qboolean
IQM_InFile(unsigned int offset, unsigned int count, size_t size, int filelen)
{
	return ((size_t)offset + (size_t)count * size) <= (size_t)filelen;
}

/* text from the file's string table, "" when out of range */
static const char *
IQM_Text(const iqm_header_t *header, const byte *buffer, unsigned int ofs)
{
	if (!header->num_text || ofs >= header->num_text)
	{
		return "";
	}

	/* the table ends with a zero byte, checked when loading */
	return (const char *)buffer + header->ofs_text + ofs;
}

/* matrix from translation, rotation quaternion and scale */
static void
IQM_PoseMatrix(const float translate[3], const float rotate[4],
	const float scale[3], iqm_matrix_t out)
{
	float x, y, z, w, length;
	int i;

	x = rotate[0];
	y = rotate[1];
	z = rotate[2];
	w = rotate[3];

	length = sqrtf(x * x + y * y + z * z + w * w);
	if (length > 0)
	{
		x /= length;
		y /= length;
		z /= length;
		w /= length;
	}

	out[0][0] = 1 - 2 * (y * y + z * z);
	out[0][1] = 2 * (x * y - w * z);
	out[0][2] = 2 * (x * z + w * y);
	out[1][0] = 2 * (x * y + w * z);
	out[1][1] = 1 - 2 * (x * x + z * z);
	out[1][2] = 2 * (y * z - w * x);
	out[2][0] = 2 * (x * z - w * y);
	out[2][1] = 2 * (y * z + w * x);
	out[2][2] = 1 - 2 * (x * x + y * y);

	for (i = 0; i < 3; i++)
	{
		/* scale applies before rotation: scale the columns */
		out[i][0] *= scale[0];
		out[i][1] *= scale[1];
		out[i][2] *= scale[2];
		out[i][3] = translate[i];
	}
}

/* out = a * b */
static void
IQM_MatrixMultiply(iqm_matrix_t a, iqm_matrix_t b, iqm_matrix_t out)
{
	iqm_matrix_t result;
	int i, j;

	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 4; j++)
		{
			result[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] +
						   a[i][2] * b[2][j] + ((j == 3) ? a[i][3] : 0);
		}
	}

	memcpy(out, result, sizeof(result));
}

/* inverse of an affine matrix */
static void
IQM_MatrixInvert(iqm_matrix_t in, iqm_matrix_t out)
{
	float determinant;
	iqm_matrix_t result;
	int i;

	result[0][0] = in[1][1] * in[2][2] - in[1][2] * in[2][1];
	result[0][1] = in[0][2] * in[2][1] - in[0][1] * in[2][2];
	result[0][2] = in[0][1] * in[1][2] - in[0][2] * in[1][1];
	result[1][0] = in[1][2] * in[2][0] - in[1][0] * in[2][2];
	result[1][1] = in[0][0] * in[2][2] - in[0][2] * in[2][0];
	result[1][2] = in[0][2] * in[1][0] - in[0][0] * in[1][2];
	result[2][0] = in[1][0] * in[2][1] - in[1][1] * in[2][0];
	result[2][1] = in[0][1] * in[2][0] - in[0][0] * in[2][1];
	result[2][2] = in[0][0] * in[1][1] - in[0][1] * in[1][0];

	determinant = in[0][0] * result[0][0] + in[0][1] * result[1][0] +
				  in[0][2] * result[2][0];

	if (fabsf(determinant) < 1e-12f)
	{
		/* degenerate joint (zero scale): leave it out of the skin */
		memset(out, 0, sizeof(iqm_matrix_t));
		return;
	}

	for (i = 0; i < 3; i++)
	{
		result[i][0] /= determinant;
		result[i][1] /= determinant;
		result[i][2] /= determinant;
	}

	for (i = 0; i < 3; i++)
	{
		result[i][3] = -(result[i][0] * in[0][3] + result[i][1] * in[1][3] +
						 result[i][2] * in[2][3]);
	}

	memcpy(out, result, sizeof(result));
}

/* the vertex array of this type, or NULL; checks format, size and range */
static const iqm_vertexarray_t *
IQM_FindArray(const iqm_header_t *header, const iqm_vertexarray_t *arrays,
	unsigned int type, unsigned int format, unsigned int size, int filelen)
{
	unsigned int i;

	for (i = 0; i < header->num_vertexarrays; i++)
	{
		size_t component_size;

		if (arrays[i].type != type || arrays[i].format != format ||
			arrays[i].size != size)
		{
			continue;
		}

		component_size = (format == IQM_FLOAT) ? sizeof(float) : 1;

		if (!IQM_InFile(arrays[i].offset, header->num_vertexes,
				component_size * size, filelen))
		{
			return NULL;
		}

		return &arrays[i];
	}

	return NULL;
}

/*
 * Skin matrices (pose * inverse bind pose) for every frame:
 * num_out_frames * num_joints matrices. With no animation, one frame in
 * the bind pose.
 */
static iqm_matrix_t *
IQM_BuildSkinMatrices(const char *mod_name, const iqm_header_t *header,
	const byte *buffer, int num_out_frames)
{
	const iqm_joint_t *joints;
	iqm_matrix_t *bind_world, *inverse_bind, *skin, *pose_world;
	unsigned int j;
	int f;

	joints = (const iqm_joint_t *)(buffer + header->ofs_joints);

	bind_world = calloc(header->num_joints * 2, sizeof(iqm_matrix_t));
	skin = calloc((size_t)num_out_frames * header->num_joints, sizeof(iqm_matrix_t));
	pose_world = calloc(header->num_joints, sizeof(iqm_matrix_t));

	if (!bind_world || !skin || !pose_world)
	{
		free(bind_world);
		free(skin);
		free(pose_world);
		Com_Printf("%s: %s: out of memory\n", __func__, mod_name);
		return NULL;
	}

	inverse_bind = bind_world + header->num_joints;

	/* bind pose: joints are listed parents first */
	for (j = 0; j < header->num_joints; j++)
	{
		iqm_matrix_t local;

		IQM_PoseMatrix(joints[j].translate, joints[j].rotate, joints[j].scale, local);

		if (joints[j].parent >= 0 && (unsigned int)joints[j].parent < j)
		{
			IQM_MatrixMultiply(bind_world[joints[j].parent], local, bind_world[j]);
		}
		else
		{
			memcpy(bind_world[j], local, sizeof(local));
		}

		IQM_MatrixInvert(bind_world[j], inverse_bind[j]);
	}

	if (!header->num_frames || header->num_poses != header->num_joints)
	{
		/* no animation: the bind pose itself */
		for (j = 0; j < header->num_joints; j++)
		{
			IQM_MatrixMultiply(bind_world[j], inverse_bind[j], skin[j]);
		}
	}
	else
	{
		const iqm_pose_t *poses;
		const unsigned short *framedata;

		poses = (const iqm_pose_t *)(buffer + header->ofs_poses);
		framedata = (const unsigned short *)(buffer + header->ofs_frames);

		for (f = 0; f < num_out_frames; f++)
		{
			for (j = 0; j < header->num_poses; j++)
			{
				float channel[10];
				iqm_matrix_t local;
				int c;

				/* channels: translate xyz, rotate xyzw, scale xyz */
				for (c = 0; c < 10; c++)
				{
					channel[c] = poses[j].channeloffset[c];

					if (poses[j].mask & (1 << c))
					{
						/* frame data is unsigned 16 bit */
						channel[c] += (unsigned short)LittleShort(*framedata++) *
									  poses[j].channelscale[c];
					}
				}

				IQM_PoseMatrix(channel, channel + 3, channel + 7, local);

				if (poses[j].parent >= 0 && (unsigned int)poses[j].parent < j)
				{
					IQM_MatrixMultiply(pose_world[poses[j].parent], local, pose_world[j]);
				}
				else
				{
					memcpy(pose_world[j], local, sizeof(local));
				}

				IQM_MatrixMultiply(pose_world[j], inverse_bind[j],
					skin[(size_t)f * header->num_joints + j]);
			}
		}
	}

	free(bind_world);
	free(pose_world);

	return skin;
}

/*
=================
Mod_LoadModel_IQM
=================
*/
void *
Mod_LoadModel_IQM(const char *mod_name, const void *buffer, int modfilelen)
{
	const iqm_vertexarray_t *arrays, *positions, *texcoords, *normals;
	const iqm_vertexarray_t *blend_indexes, *blend_weights;
	const iqm_mesh_t *meshes;
	const unsigned int *triangles;
	iqm_matrix_t *skin_matrices = NULL;
	dmdx_t dmdxheader, *pheader;
	dmdxmesh_t *mesh_nodes;
	dtriangle_t *tris;
	dstvert_t *st;
	dmdx_vert_t *vertx;
	void *extradata = NULL;
	iqm_header_t header;
	const byte *data = buffer;
	int num_out_frames, framesize, f;
	unsigned int i;
	char *skin;

	if (modfilelen < (int)sizeof(header))
	{
		Com_Printf("%s: %s is too short for an IQM header\n", __func__, mod_name);
		return NULL;
	}

	memcpy(header.magic, buffer, sizeof(header.magic));
	Mod_LittleHeader((const int *)((const byte *)buffer + sizeof(header.magic)),
		(sizeof(header) - sizeof(header.magic)) / sizeof(int),
		(int *)&header.version);

	if (memcmp(header.magic, IQM_MAGIC, sizeof(IQM_MAGIC)) ||
		header.version != IQM_VERSION)
	{
		Com_Printf("%s: %s is not an IQM version %d file\n", __func__,
			mod_name, IQM_VERSION);
		return NULL;
	}

	/* the model's own tables must lie inside the file */
	if (header.filesize > (unsigned int)modfilelen ||
		!IQM_InFile(header.ofs_text, header.num_text, 1, modfilelen) ||
		!IQM_InFile(header.ofs_meshes, header.num_meshes, sizeof(iqm_mesh_t), modfilelen) ||
		!IQM_InFile(header.ofs_vertexarrays, header.num_vertexarrays, sizeof(iqm_vertexarray_t), modfilelen) ||
		!IQM_InFile(header.ofs_triangles, header.num_triangles, 3 * sizeof(unsigned int), modfilelen) ||
		!IQM_InFile(header.ofs_joints, header.num_joints, sizeof(iqm_joint_t), modfilelen) ||
		!IQM_InFile(header.ofs_poses, header.num_poses, sizeof(iqm_pose_t), modfilelen) ||
		!IQM_InFile(header.ofs_anims, header.num_anims, sizeof(iqm_anim_t), modfilelen) ||
		!IQM_InFile(header.ofs_frames, header.num_frames,
			(size_t)header.num_framechannels * sizeof(unsigned short), modfilelen))
	{
		Com_Printf("%s: %s has a table outside the file\n", __func__, mod_name);
		return NULL;
	}

	if (header.num_text && data[header.ofs_text + header.num_text - 1])
	{
		Com_Printf("%s: %s has an unterminated text table\n", __func__, mod_name);
		return NULL;
	}

	if (!header.num_meshes || !header.num_vertexes || !header.num_triangles ||
		header.num_vertexes > MAX_VERTS || header.num_triangles > MAX_TRIANGLES)
	{
		Com_Printf("%s: %s has %u verts and %u triangles (need 1-%d, 1-%d)\n",
			__func__, mod_name, header.num_vertexes, header.num_triangles,
			MAX_VERTS, MAX_TRIANGLES);
		return NULL;
	}

	/* the tables are read in place, as little-endian */
	if (LittleLong(1) != 1)
	{
		Com_Printf("%s: %s: IQM needs a little-endian system\n", __func__, mod_name);
		return NULL;
	}

	arrays = (const iqm_vertexarray_t *)(data + header.ofs_vertexarrays);
	positions = IQM_FindArray(&header, arrays, IQM_POSITION, IQM_FLOAT, 3, modfilelen);
	texcoords = IQM_FindArray(&header, arrays, IQM_TEXCOORD, IQM_FLOAT, 2, modfilelen);
	normals = IQM_FindArray(&header, arrays, IQM_NORMAL, IQM_FLOAT, 3, modfilelen);
	blend_indexes = IQM_FindArray(&header, arrays, IQM_BLENDINDEXES, IQM_UBYTE, 4, modfilelen);
	blend_weights = IQM_FindArray(&header, arrays, IQM_BLENDWEIGHTS, IQM_UBYTE, 4, modfilelen);

	if (!positions)
	{
		Com_Printf("%s: %s has no float xyz positions\n", __func__, mod_name);
		return NULL;
	}

	meshes = (const iqm_mesh_t *)(data + header.ofs_meshes);
	triangles = (const unsigned int *)(data + header.ofs_triangles);

	for (i = 0; i < header.num_meshes; i++)
	{
		if ((size_t)meshes[i].first_triangle + meshes[i].num_triangles > header.num_triangles ||
			(size_t)meshes[i].first_vertex + meshes[i].num_vertexes > header.num_vertexes)
		{
			Com_Printf("%s: %s has a broken mesh %u\n", __func__, mod_name, i);
			return NULL;
		}
	}

	for (i = 0; i < header.num_triangles * 3; i++)
	{
		if (triangles[i] >= header.num_vertexes)
		{
			Com_Printf("%s: %s has a broken triangle index\n", __func__, mod_name);
			return NULL;
		}
	}

	/* animated when it has a skeleton, poses for it and frames */
	num_out_frames = 1;
	if (header.num_joints && header.num_frames && header.num_poses == header.num_joints)
	{
		num_out_frames = header.num_frames;
	}

	if (header.num_joints)
	{
		skin_matrices = IQM_BuildSkinMatrices(mod_name, &header, data, num_out_frames);
		if (!skin_matrices)
		{
			return NULL;
		}
	}

	framesize = sizeof(daliasxframe_t) + sizeof(dxtrivertx_t) * header.num_vertexes;

	memset(&dmdxheader, 0, sizeof(dmdxheader));
	dmdxheader.framesize = framesize;
	dmdxheader.skinheight = 256;
	dmdxheader.skinwidth = 256;
	dmdxheader.num_skins = header.num_meshes;
	/* (count vert + 3 vert * (2 float + 1 int)) + final zero; */
	dmdxheader.num_glcmds = (10 * header.num_triangles) + header.num_meshes;
	dmdxheader.num_frames = num_out_frames;
	dmdxheader.num_xyz = header.num_vertexes;
	dmdxheader.num_meshes = header.num_meshes;
	dmdxheader.num_st = header.num_vertexes;
	dmdxheader.num_tris = header.num_triangles;
	dmdxheader.num_animgroup = num_out_frames;

	pheader = Mod_LoadAllocate(mod_name, &dmdxheader, &extradata);

	mesh_nodes = (dmdxmesh_t *)((byte *)pheader + pheader->ofs_meshes);
	tris = (dtriangle_t *)((byte *)pheader + pheader->ofs_tris);
	st = (dstvert_t *)((byte *)pheader + pheader->ofs_st);
	skin = (char *)pheader + pheader->ofs_skins;

	/* meshes: one skin each, named by the IQM material */
	{
		int num_tris = 0;

		for (i = 0; i < header.num_meshes; i++)
		{
			unsigned int t;

			Q_strlcpy(skin + i * MAX_SKINNAME,
				IQM_Text(&header, data, meshes[i].material), MAX_SKINNAME);

			mesh_nodes[i].ofs_tris = num_tris;
			mesh_nodes[i].num_tris = meshes[i].num_triangles;

			for (t = 0; t < meshes[i].num_triangles; t++)
			{
				const unsigned int *in = triangles + (meshes[i].first_triangle + t) * 3;
				int k;

				for (k = 0; k < 3; k++)
				{
					tris[num_tris].index_xyz[k] = in[k];
					tris[num_tris].index_st[k] = in[k];
				}

				num_tris++;
			}
		}
	}

	/* texture coordinates: IQM has the origin at the top left, like skins */
	for (i = 0; i < header.num_vertexes; i++)
	{
		if (texcoords)
		{
			const float *uv = (const float *)(data + texcoords->offset) + i * 2;

			st[i].s = LittleFloat(uv[0]) * pheader->skinwidth;
			st[i].t = LittleFloat(uv[1]) * pheader->skinheight;
		}
		else
		{
			st[i].s = st[i].t = 0;
		}
	}

	/* bake every frame */
	vertx = malloc((size_t)num_out_frames * header.num_vertexes * sizeof(dmdx_vert_t));
	if (!vertx)
	{
		YQ2_COM_CHECK_OOM(vertx, "malloc()",
			(size_t)num_out_frames * header.num_vertexes * sizeof(dmdx_vert_t))
		free(skin_matrices);
		return NULL;
	}

	for (f = 0; f < num_out_frames; f++)
	{
		for (i = 0; i < header.num_vertexes; i++)
		{
			const float *in_position = (const float *)(data + positions->offset) + i * 3;
			dmdx_vert_t *out = vertx + (size_t)f * header.num_vertexes + i;
			vec3_t position, normal;
			int n;

			for (n = 0; n < 3; n++)
			{
				position[n] = LittleFloat(in_position[n]);
				normal[n] = 0;
			}

			if (normals)
			{
				const float *in_normal = (const float *)(data + normals->offset) + i * 3;

				for (n = 0; n < 3; n++)
				{
					normal[n] = LittleFloat(in_normal[n]);
				}
			}

			if (skin_matrices && blend_indexes && blend_weights)
			{
				const byte *indexes = data + blend_indexes->offset + i * 4;
				const byte *weights = data + blend_weights->offset + i * 4;
				vec3_t skinned_position = {0, 0, 0}, skinned_normal = {0, 0, 0};
				int b;

				for (b = 0; b < 4; b++)
				{
					float weight = weights[b] / 255.0f;
					float (*m)[4];

					if (!weights[b] || indexes[b] >= header.num_joints)
					{
						continue;
					}

					m = skin_matrices[(size_t)f * header.num_joints + indexes[b]];

					for (n = 0; n < 3; n++)
					{
						skinned_position[n] += weight * (DotProduct(m[n], position) + m[n][3]);
						skinned_normal[n] += weight * DotProduct(m[n], normal);
					}
				}

				VectorCopy(skinned_position, position);
				VectorCopy(skinned_normal, normal);
			}

			VectorNormalize(normal);
			VectorCopy(position, out->xyz);
			VectorCopy(normal, out->norm);
		}
	}

	free(skin_matrices);

	/* frame names: "<animation><nnn>", so animation groups follow IQM's */
	for (f = 0; f < num_out_frames; f++)
	{
		daliasxframe_t *frame = (daliasxframe_t *)((byte *)pheader + pheader->ofs_frames + f * pheader->framesize);
		const iqm_anim_t *anims = (const iqm_anim_t *)(data + header.ofs_anims);
		unsigned int a;

		snprintf(frame->name, sizeof(frame->name), "frame%d", f % 0xFFFF);

		for (a = 0; a < header.num_anims && num_out_frames > 1; a++)
		{
			if ((unsigned int)f >= anims[a].first_frame &&
				(unsigned int)f < anims[a].first_frame + anims[a].num_frames)
			{
				char anim_name[13];

				Q_strlcpy(anim_name, IQM_Text(&header, data, anims[a].name),
					sizeof(anim_name));
				snprintf(frame->name, sizeof(frame->name), "%s%03d",
					anim_name, (f - anims[a].first_frame + 1) % 1000);
				break;
			}
		}

		PrepareFrameVertex(vertx + (size_t)f * header.num_vertexes,
			header.num_vertexes, frame);
	}

	free(vertx);

	Mod_LoadAnimGroupList(pheader, true);
	Mod_LoadCmdGenerate(pheader);
	Mod_LoadFixImages(mod_name, pheader, false);

	return extradata;
}
