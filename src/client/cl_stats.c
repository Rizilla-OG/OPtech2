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
 * optech2: performance overlay, "cl_perf 1". Frame rate and frame time,
 * CPU time per frame part (server, game, client, render submission),
 * process and system CPU load, GPU load and video memory. Values are
 * averaged over half a second. CPU and GPU load are read from /proc and
 * /sys on Linux (GPU load and memory: amdgpu only); elsewhere "n/a".
 *
 * =======================================================================
 */

#include "header/client.h"

#ifdef __linux__
#include <unistd.h> /* sysconf */
#endif

#define STATS_WINDOW_US 500000 /* averaging window */
#define STATS_LINES 7
#define CHAR_SIZE 8 /* console font cell, as in cl_screen.c */

extern cvar_t *cl_perf;

/* sums over the current window */
static struct
{
	long long start; /* window start, microseconds */
	long long last_render; /* time of the last rendered frame */
	int rendered_frames;
	int game_frames;
	long long frame_time_sum; /* between rendered frames */
	long long frame_time_max;
	long long server_sum; /* engine server part, all passes */
	long long game_sum; /* game library */
	long long client_sum; /* client without render submission */
	long long render_sum; /* render submission (CPU side) */
} window;

/* last finished window, as drawn */
static char stats_lines[STATS_LINES][128];
static int stats_line_count;

#ifdef __linux__
static char gpu_sysfs_dir[64]; /* /sys/class/drm/cardN/device with gpu_busy_percent, or "" */
static qboolean gpu_sysfs_checked;
static long long last_process_ticks, last_system_total, last_system_idle;

/* reads the first number from a small file, -1 on failure */
static long long
Stats_ReadNumber(const char *path)
{
	FILE *f = fopen(path, "r");
	long long value = -1;

	if (f)
	{
		if (fscanf(f, "%lld", &value) != 1)
		{
			value = -1;
		}

		fclose(f);
	}

	return value;
}

static void
Stats_FindGpu(void)
{
	int card;

	gpu_sysfs_checked = true;
	gpu_sysfs_dir[0] = 0;

	for (card = 0; card < 8; card++)
	{
		char path[96];

		snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/gpu_busy_percent", card);

		if (Stats_ReadNumber(path) >= 0)
		{
			snprintf(gpu_sysfs_dir, sizeof(gpu_sysfs_dir), "/sys/class/drm/card%d/device", card);
			return;
		}
	}
}

/* process CPU (percent of one core) and whole system CPU (percent of all) since the last call */
static void
Stats_CpuLoad(double seconds, double *process_percent, double *system_percent)
{
	char buffer[1024];
	long long utime = 0, stime = 0, ticks, total = 0, idle = 0;
	FILE *f;

	*process_percent = *system_percent = -1;

	f = fopen("/proc/self/stat", "r");
	if (f)
	{
		size_t len = fread(buffer, 1, sizeof(buffer) - 1, f);
		const char *p;
		int field;

		fclose(f);
		buffer[len] = 0;

		/* fields after the ")" of the process name; utime and stime are 14 and 15 */
		p = strrchr(buffer, ')');

		for (field = 2; p && field < 14; field++)
		{
			p = strchr(p + 1, ' ');
		}

		if (p && sscanf(p, " %lld %lld", &utime, &stime) == 2)
		{
			ticks = utime + stime;

			if (last_process_ticks && seconds > 0)
			{
				*process_percent = 100.0 * (ticks - last_process_ticks) /
								   (sysconf(_SC_CLK_TCK) * seconds);
			}

			last_process_ticks = ticks;
		}
	}

	f = fopen("/proc/stat", "r");
	if (f)
	{
		long long user, nice, system, idle_time, iowait, irq, softirq, steal;

		if (fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
				&user, &nice, &system, &idle_time, &iowait, &irq, &softirq, &steal) == 8)
		{
			total = user + nice + system + idle_time + iowait + irq + softirq + steal;
			idle = idle_time + iowait;

			if (last_system_total && total > last_system_total)
			{
				*system_percent = 100.0 * (1.0 - (double)(idle - last_system_idle) /
													 (total - last_system_total));
			}

			last_system_total = total;
			last_system_idle = idle;
		}

		fclose(f);
	}
}
#endif

/* called by the main loop after each pass, with the pass's part times in microseconds */
void
SCR_StatsAccumulate(long long server_us, long long game_us, long long client_us, long long render_us,
	long long now)
{
	if (!window.start)
	{
		window.start = now;
	}

	window.server_sum += server_us;
	window.game_sum += game_us;
	window.client_sum += client_us;
	window.render_sum += render_us;

	if (game_us > 0)
	{
		window.game_frames++;
	}

	if (render_us > 0)
	{
		if (window.last_render)
		{
			long long frame_time = now - window.last_render;

			window.frame_time_sum += frame_time;
			window.frame_time_max = Q_max(window.frame_time_max, frame_time);
		}

		window.last_render = now;
		window.rendered_frames++;
	}

	if (now - window.start >= STATS_WINDOW_US)
	{
		double seconds = (now - window.start) / 1000000.0;
		int frames = Q_max(window.rendered_frames, 1);
		int game_frames = Q_max(window.game_frames, 1);
		int line = 0;

		snprintf(stats_lines[line++], sizeof(stats_lines[0]),
			"FPS %4.0f   frame %5.2f ms   worst %5.2f ms",
			window.rendered_frames / seconds,
			window.frame_time_sum / 1000.0 / frames,
			window.frame_time_max / 1000.0);

		snprintf(stats_lines[line++], sizeof(stats_lines[0]),
			"server %5.2f ms/tick   game %5.2f ms/tick   (%2.0f ticks/s)",
			window.server_sum / 1000.0 / game_frames,
			window.game_sum / 1000.0 / game_frames,
			window.game_frames / seconds);

		snprintf(stats_lines[line++], sizeof(stats_lines[0]),
			"client %5.2f ms/frame   render submit %5.2f ms/frame",
			window.client_sum / 1000.0 / frames,
			window.render_sum / 1000.0 / frames);

		snprintf(stats_lines[line++], sizeof(stats_lines[0]),
			"drawn: %d entities   %d lights   %d particles",
			cl.refdef.num_entities, cl.refdef.num_dlights, cl.refdef.num_particles);

#ifdef __linux__
		{
			double process_percent, system_percent;

			Stats_CpuLoad(seconds, &process_percent, &system_percent);

			snprintf(stats_lines[line++], sizeof(stats_lines[0]),
				"CPU  game %3.0f%% of a core   system %3.0f%%",
				process_percent, system_percent);

			if (!gpu_sysfs_checked)
			{
				Stats_FindGpu();
			}

			if (gpu_sysfs_dir[0])
			{
				char path[128];
				long long busy, used, total;

				snprintf(path, sizeof(path), "%s/gpu_busy_percent", gpu_sysfs_dir);
				busy = Stats_ReadNumber(path);
				snprintf(path, sizeof(path), "%s/mem_info_vram_used", gpu_sysfs_dir);
				used = Stats_ReadNumber(path);
				snprintf(path, sizeof(path), "%s/mem_info_vram_total", gpu_sysfs_dir);
				total = Stats_ReadNumber(path);

				snprintf(stats_lines[line++], sizeof(stats_lines[0]),
					"GPU  busy %3lld%%   VRAM %5.2f / %5.2f GB",
					busy, used / 1073741824.0, total / 1073741824.0);
			}
			else
			{
				snprintf(stats_lines[line++], sizeof(stats_lines[0]),
					"GPU  load n/a (needs amdgpu)");
			}
		}
#else
		snprintf(stats_lines[line++], sizeof(stats_lines[0]), "CPU/GPU load n/a on this system");
#endif

		stats_line_count = line;
		memset(&window, 0, sizeof(window));
		window.start = now;
		window.last_render = now;
	}
}

/* draws the overlay at the top left */
void
SCR_DrawStatsOverlay(void)
{
	float scale;
	int i;

	if (!cl_perf || !cl_perf->value || !stats_line_count)
	{
		return;
	}

	scale = SCR_GetConsoleScale();

	for (i = 0; i < stats_line_count; i++)
	{
		int y = (int)(scale * (2 + i * (CHAR_SIZE + 2)));

		Draw_StringScaled((int)(scale * 2), y, scale, false, stats_lines[i]);
		SCR_AddDirtyPoint(0, y);
		SCR_AddDirtyPoint((int)(scale * (2 + strlen(stats_lines[i]) * CHAR_SIZE)), y + (int)(scale * CHAR_SIZE));
	}
}
