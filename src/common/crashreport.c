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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * optech2: crash reports. A fatal error (Com_Error ERR_FATAL, Sys_Error)
 * or a crash (signal on Unix, unhandled exception on Windows) writes
 * <home dir>/crashes/crash-<date>-<time>.txt with the reason, a
 * backtrace (module + offset, for addr2line), the game state and the
 * last lines of console output. The console tail is kept in memory, so
 * it's there even when the log file is off or was already closed.
 *
 * =======================================================================
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* dladdr */
#endif

#include "header/common.h"
#include <time.h>

#if defined(HAVE_EXECINFO)
#include <execinfo.h>
#endif

#if defined(HAVE_EXECINFO) && !defined(_WIN32)
#include <dlfcn.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

/* optech2: compiler string for the report; MSVC has no __VERSION__ */
#if defined(_MSC_VER)
#define CRASH_STR2(x) #x
#define CRASH_STR(x) CRASH_STR2(x)
#define CRASH_COMPILER "MSVC " CRASH_STR(_MSC_FULL_VER)
#elif defined(__VERSION__)
#define CRASH_COMPILER __VERSION__
#else
#define CRASH_COMPILER "unknown"
#endif

#define CONSOLE_TAIL_SIZE 16384 /* bytes of recent console output kept */
#define BACKTRACE_DEPTH 48

static char console_tail[CONSOLE_TAIL_SIZE];
static size_t console_tail_next; /* where the next byte goes */
static qboolean console_tail_wrapped;

static char command_line[1024]; /* as started, before +set args are used up */

static qboolean report_written; /* one report per run */
static char report_path[MAX_OSPATH];

/*
 * Keeps the command line as given. Called from COM_InitArgv.
 */
void
Crash_SaveCommandLine(int argc, char **argv)
{
	int i;

	command_line[0] = '\0';

	for (i = 0; i < argc; i++)
	{
		qboolean quote = argv[i] && strchr(argv[i], ' ');

		if (i)
		{
			Q_strlcat(command_line, " ", sizeof(command_line));
		}

		if (quote)
		{
			Q_strlcat(command_line, "\"", sizeof(command_line));
		}

		Q_strlcat(command_line, argv[i] ? argv[i] : "", sizeof(command_line));

		if (quote)
		{
			Q_strlcat(command_line, "\"", sizeof(command_line));
		}
	}
}

/*
 * Keeps a copy of everything printed to the console. Called from
 * Com_VPrintf with the cleaned up text.
 */
void
Crash_RecordConsole(const char *text)
{
	for (; *text; text++)
	{
		console_tail[console_tail_next++] = *text;

		if (console_tail_next == CONSOLE_TAIL_SIZE)
		{
			console_tail_next = 0;
			console_tail_wrapped = true;
		}
	}
}

static void
WriteConsoleTail(FILE *report)
{
	size_t start = 0;
	size_t length = console_tail_next;

	if (console_tail_wrapped)
	{
		/* oldest byte is at console_tail_next; skip the cut first line */
		size_t skipped = 0;

		start = console_tail_next;

		while (skipped < CONSOLE_TAIL_SIZE && console_tail[start] != '\n')
		{
			start = (start + 1) % CONSOLE_TAIL_SIZE;
			skipped++;
		}

		start = (start + 1) % CONSOLE_TAIL_SIZE;
		length = CONSOLE_TAIL_SIZE - skipped - 1;
	}

	if (start + length > CONSOLE_TAIL_SIZE)
	{
		fwrite(console_tail + start, 1, CONSOLE_TAIL_SIZE - start, report);
		fwrite(console_tail, 1, start + length - CONSOLE_TAIL_SIZE, report);
	}
	else
	{
		fwrite(console_tail + start, 1, length, report);
	}

	if (console_tail_next > 0 && console_tail[console_tail_next - 1] != '\n')
	{
		fputc('\n', report);
	}
}

#if defined(_WIN32)

static void
WriteBacktrace(FILE *report)
{
	void *frames[BACKTRACE_DEPTH];
	USHORT frame_count;
	USHORT i;

	frame_count = CaptureStackBackTrace(0, BACKTRACE_DEPTH, frames, NULL);

	for (i = 0; i < frame_count; i++)
	{
		HMODULE module = NULL;
		char module_path[MAX_PATH] = "?";

		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
								   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCSTR)frames[i], &module))
		{
			GetModuleFileNameA(module, module_path, sizeof(module_path));
		}

		fprintf(report, "  #%-2u %s+0x%llx\n", (unsigned)i, module_path,
			(unsigned long long)((char *)frames[i] - (char *)module));
	}
}

#elif defined(HAVE_EXECINFO)

static void
WriteBacktrace(FILE *report)
{
	void *frames[BACKTRACE_DEPTH];
	int frame_count;
	int i;

	frame_count = backtrace(frames, BACKTRACE_DEPTH);

	for (i = 0; i < frame_count; i++)
	{
		Dl_info info;

		if (dladdr(frames[i], &info) && info.dli_fname)
		{
			/* offset into the module: addr2line -f -C -e <module> <offset> */
			unsigned long offset = (unsigned long)((char *)frames[i] - (char *)info.dli_fbase);

			fprintf(report, "  #%-2d %s+0x%lx (%s)\n", i, info.dli_fname, offset,
				info.dli_sname ? info.dli_sname : "?");
		}
		else
		{
			fprintf(report, "  #%-2d %p\n", i, frames[i]);
		}
	}
}

#else

static void
WriteBacktrace(FILE *report)
{
	fprintf(report, "  Not available on this platform.\n");
}

#endif

static void
WriteCvar(FILE *report, const char *label, const char *name)
{
	fprintf(report, "%-14s%s\n", label, Cvar_VariableString(name));
}

/*
 * Writes the crash report. reason is what happened ("Fatal error",
 * "Signal 11 (SIGSEGV)"), message the error text or NULL. Returns the
 * report's path, or NULL if none was written (already written, or the
 * file couldn't be made).
 */
const char *
Crash_WriteReport(const char *reason, const char *message)
{
	char dir[MAX_OSPATH];
	char stamp[64];
	const char *home;
	time_t now;
	struct tm local;
	FILE *report;

	if (report_written)
	{
		return NULL;
	}

	report_written = true;

	now = time(NULL);
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);

	home = Sys_GetHomeDir();

	if (!home)
	{
		return NULL;
	}

	Com_sprintf(dir, sizeof(dir), "%scrashes", home);
	Sys_Mkdir(dir);
	Com_sprintf(report_path, sizeof(report_path), "%s/crash-%s.txt", dir, stamp);

	report = Q_fopen(report_path, "w");

	if (!report)
	{
		return NULL;
	}

	strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);

	fprintf(report, "OpenTech2 crash report\n\n");
	fprintf(report, "%-14s%s\n", "Reason:", reason);
	fprintf(report, "%-14s%s\n", "Message:", message ? message : "-");
	fprintf(report, "%-14s%s\n", "Time:", stamp);
	fprintf(report, "%-14s%s\n", "Version:", YQ2VERSION);
	fprintf(report, "%-14s%s %s\n", "Platform:", YQ2OSTYPE, YQ2ARCH);
	fprintf(report, "%-14s%s\n", "Built:", __DATE__ " " __TIME__);
	fprintf(report, "%-14s%s\n", "Compiler:", CRASH_COMPILER);
	fprintf(report, "%-14s%.1f s\n", "Uptime:", Sys_Milliseconds() / 1000.0f);

	/* backtrace first: if the game state below is broken, this is out */
	fprintf(report, "\nBacktrace (names: addr2line -f -C -e <module> <offset>):\n");
	fflush(report);
	WriteBacktrace(report);
	fflush(report);

	fprintf(report, "\nGame state:\n");
	WriteCvar(report, "Map:", "mapname");
	WriteCvar(report, "Game:", "game");
	fprintf(report, "%-14s%s\n", "Game dir:", FS_Gamedir());
	WriteCvar(report, "Renderer:", "vid_renderer");
	WriteCvar(report, "Mode:", "r_mode");
	WriteCvar(report, "Tick rate:", "sv_tickrate");
	WriteCvar(report, "Skill:", "skill");
	WriteCvar(report, "Coop:", "coop");
	WriteCvar(report, "Deathmatch:", "deathmatch");
	WriteCvar(report, "Max clients:", "maxclients");
	WriteCvar(report, "Client game:", "cl_cgame");

	fprintf(report, "%-14s%s\n", "Command line:", command_line);
	fflush(report);

	fprintf(report, "\nLast console output:\n");
	WriteConsoleTail(report);

	fclose(report);

	fprintf(stderr, "Crash report written to %s\n", report_path);

	return report_path;
}

/*
 * crashtest fatal|drop|segv|abort: makes the named kind of crash, to
 * check the report. Needs developer 1.
 */
static void
Crash_Test_f(void)
{
	const char *kind = Cmd_Argv(1);

	if (!Cvar_VariableValue("developer"))
	{
		Com_Printf("crashtest needs developer 1\n");
		return;
	}

	if (!strcmp(kind, "fatal"))
	{
		Com_Error(ERR_FATAL, "crashtest: fatal error");
	}
	else if (!strcmp(kind, "drop"))
	{
		Com_Error(ERR_DROP, "crashtest: drop");
	}
	else if (!strcmp(kind, "segv"))
	{
		volatile int *nowhere = NULL;

		/* the crash is the point */
		// cppcheck-suppress nullPointer
		*nowhere = 1;
	}
	else if (!strcmp(kind, "abort"))
	{
		abort();
	}
	else
	{
		Com_Printf("usage: crashtest fatal|drop|segv|abort\n");
	}
}

void
Crash_Init(void)
{
	Cmd_AddCommand("crashtest", Crash_Test_f);
}
