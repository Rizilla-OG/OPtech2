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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 * =======================================================================
 *
 * This file implements the console
 *
 * =======================================================================
 */

#include "header/client.h"
#include "sound/header/local.h"
#include <time.h>

console_t con;
cvar_t *con_notifytime;
cvar_t *con_style; /* optech2: 1 = a dark see-through panel, 0 = the classic picture */
cvar_t *con_alpha; /* optech2: how solid that panel is, 0..1 */

/* optech2: TrueType text is taller than Quake II's 8x8 bitmap characters
   (its glyphs are drawn 12 units high, with descenders): with it, console
   lines are 13 units apart and the text sits 2 units down in its line, so
   nothing reaches into the next line. The bitmap font keeps 8 and 0. */
static qboolean
Con_TrueTypeFont(void)
{
	const char *font = Cvar_VariableString("r_ttffont");

	return font && font[0];
}

static int
Con_LinePitch(void)
{
	return Con_TrueTypeFont() ? 13 : 8;
}

static int
Con_TextOffset(void)
{
	return Con_TrueTypeFont() ? 2 : 0;
}

/* optech2: how far apart characters are across (the TrueType font's
   width, else the bitmap font's 8), and a column's position in pixels */
static float
Con_CharWidth(void)
{
	return Con_TrueTypeFont() ? Draw_CharAdvance() : 8.0f;
}

static int
Con_Column(int column, float scale)
{
	return (int)(column * Con_CharWidth() * scale);
}

extern char key_lines[NUM_KEY_LINES][MAXCMDLINE];
extern int edit_line;
extern int key_linepos;

void
Key_ClearTyping(void)
{
	key_lines[edit_line][0] = '\0';
	key_linepos = 0;
}

void
Con_ToggleConsole_f(void)
{
	SCR_EndLoadingPlaque(); /* get rid of loading plaque */

	if (cl.attractloop)
	{
		Cbuf_AddText("killserver\n");
		return;
	}

	if (cls.state == ca_disconnected)
	{
		/* start the demo loop again */
		Cbuf_AddText("d1\n");
		return;
	}

	Key_ClearTyping();
	Con_ClearNotify();

#ifdef USE_OPENAL
	if (cl.cinematic_file)
	{
		AL_UnqueueRawSamples();
	}
#endif

	if (cls.key_dest == key_console)
	{
		M_ForceMenuOff();
		Cvar_Set("paused", "0");

		/* play music */
		if (Cvar_VariableValue("ogg_pausewithgame") == 1 &&
		    OGG_Status() == PAUSE)
		{
		    Cbuf_AddText("ogg toggle\n");
		}
	}
	else
	{
		M_ForceMenuOff();
		cls.key_dest = key_console;

		if ((Cvar_VariableValue("maxclients") == 1) &&
			Com_ServerState())
		{
			Cvar_Set("paused", "1");

			/* pause music */
			if (Cvar_VariableValue("ogg_pausewithgame") == 1 &&
			    OGG_Status() == PLAY)
			{
			    Cbuf_AddText("ogg toggle\n");
			}
		}
	}
}

static void
Con_ToggleChat_f(void)
{
	Key_ClearTyping();

	if (cls.key_dest == key_console)
	{
		if (cls.state == ca_active)
		{
			M_ForceMenuOff();
			cls.key_dest = key_game;
		}
	}
	else
	{
		cls.key_dest = key_console;
	}

	Con_ClearNotify();
}

void
Con_Clear_f(void)
{
	memset(con.text, ' ', CON_TEXTSIZE);
}

/*
 * Save the console contents out to a file
 */
static void
Con_Dump_f(void)
{
	const char *line;
	int l, x;
	FILE *f;
	char buffer[1024];
	char name[MAX_OSPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf("usage: condump <filename>\n");
		return;
	}

	if (con.linewidth >= 1024)
	{
		Com_Printf("con.linewidth too large!\n");
		return;
	}

	Com_sprintf(name, sizeof(name), "%s/%s.txt", FS_Gamedir(), Cmd_Argv(1));

	Com_Printf("Dumped console text to %s.\n", name);
	FS_CreatePath(name);
	f = Q_fopen(name, "w");

	if (!f)
	{
		Com_Printf("ERROR: couldn't open.\n");
		return;
	}

	/* skip empty lines */
	for (l = con.current - con.totallines + 1; l <= con.current; l++)
	{
		line = con.text + (l % con.totallines) * con.linewidth;

		for (x = 0; x < con.linewidth; x++)
		{
			if (line[x] != ' ')
			{
				break;
			}
		}

		if (x != con.linewidth)
		{
			break;
		}
	}

	/* write the remaining lines */
	buffer[con.linewidth] = 0;

	for ( ; l <= con.current; l++)
	{
		line = con.text + (l % con.totallines) * con.linewidth;
		memcpy(buffer, line, con.linewidth);

		for (x = con.linewidth - 1; x >= 0; x--)
		{
			if (buffer[x] == ' ')
			{
				buffer[x] = 0;
			}

			else
			{
				break;
			}
		}

		for (x = 0; buffer[x]; x++)
		{
			buffer[x] &= 0x7f;
		}

		fprintf(f, "%s\n", buffer);
	}

	fclose(f);
}

void
Con_ClearNotify(void)
{
	int i;

	for (i = 0; i < NUM_CON_TIMES; i++)
	{
		con.times[i] = 0;
	}
}

static void
Con_MessageMode_f(void)
{
	chat_team = false;
	cls.key_dest = key_message;
}

static void
Con_MessageMode2_f(void)
{
	chat_team = true;
	cls.key_dest = key_message;
}

/*
 * If the line width has changed, reformat the buffer.
 */
void
Con_CheckResize(void)
{
	int width;
	float scale = SCR_GetConsoleScale();

	/* We need to clamp the line width to MAXCMDLINE - 2,
	   otherwise we may overflow the text buffer if the
	   vertical resultion / 8 (one char == 8 pixels) is
	   bigger then MAXCMDLINE.
	   MAXCMDLINE - 2 because 1 for the prompt and 1 for
	   the terminating \0. */
	width = (int)((viddef.width / scale) / Con_CharWidth()) - 2; /* optech2: the font's width */
	width = width > MAXCMDLINE - 2 ? MAXCMDLINE - 2 : width;

	if (width == con.linewidth)
	{
		return;
	}

	/* video hasn't been initialized yet */
	if (width < 1)
	{
		width = 38;
		con.linewidth = width;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		memset(con.text, ' ', CON_TEXTSIZE);
	}
	else
	{
		int i, oldwidth, oldtotallines, numlines, numchars;
		char tbuf[CON_TEXTSIZE];

		oldwidth = con.linewidth;
		con.linewidth = width;
		oldtotallines = con.totallines;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		numlines = oldtotallines;

		if (con.totallines < numlines)
		{
			numlines = con.totallines;
		}

		numchars = oldwidth;

		if (con.linewidth < numchars)
		{
			numchars = con.linewidth;
		}

		memcpy(tbuf, con.text, sizeof(tbuf));
		memset(con.text, ' ', CON_TEXTSIZE);

		for (i = 0; i < numlines; i++)
		{
			int j;

			for (j = 0; j < numchars; j++)
			{
				con.text[(con.totallines - 1 - i) * con.linewidth + j] =
					tbuf[((con.current - i + oldtotallines) %
						  oldtotallines) * oldwidth + j];
			}
		}

		Con_ClearNotify();
	}

	con.current = con.totallines - 1;
	con.display = con.current;
}

void
Con_Init(void)
{
	con.linewidth = -1;

	Con_CheckResize();

	Com_Printf("Console initialized.\n");

	/* register our commands */
	con_notifytime = Cvar_Get("con_notifytime", "3", 0);
	con_style = Cvar_Get("con_style", "1", CVAR_ARCHIVE);
	con_alpha = Cvar_Get("con_alpha", "0.82", CVAR_ARCHIVE);

	Cmd_AddCommand("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand("togglechat", Con_ToggleChat_f);
	Cmd_AddCommand("messagemode", Con_MessageMode_f);
	Cmd_AddCommand("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand("clear", Con_Clear_f);
	Cmd_AddCommand("condump", Con_Dump_f);
	con.initialized = true;
}

static void
Con_Linefeed(void)
{
	con.x = 0;

	if (con.display == con.current)
	{
		con.display++;
	}

	con.current++;
	memset(&con.text[(con.current % con.totallines) * con.linewidth],
			' ', con.linewidth);
}

/*
 * Handles cursor positioning, line wrapping, etc All console printing
 * must go through this in order to be logged to disk If no console is
 * visible, the text will appear at the top of the game window
 */
void
Con_Print(const char *txt)
{
	int y;
	int c, l;
	static int cr;
	int mask;

	if (!con.initialized)
	{
		return;
	}

	if ((txt[0] == 1) || (txt[0] == 2))
	{
		mask = 128; /* go to colored text */
		txt++;
	}
	else
	{
		mask = 0;
	}

	while ((c = *txt))
	{
		/* count word length */
		for (l = 0; l < con.linewidth; l++)
		{
			if (txt[l] <= ' ')
			{
				break;
			}
		}

		/* word wrap */
		if ((l != con.linewidth) && (con.x + l > con.linewidth))
		{
			con.x = 0;
		}

		txt++;

		if (cr)
		{
			con.current--;
			cr = false;
		}

		if (!con.x)
		{
			Con_Linefeed();

			/* mark time for transparent overlay */
			if (con.current >= 0)
			{
				con.times[con.current % NUM_CON_TIMES] = cls.realtime;
			}
		}

		switch (c)
		{
			case '\n':
				con.x = 0;
				break;

			case '\r':
				con.x = 0;
				cr = 1;
				break;

			default: /* display character and advance */
				y = con.current % con.totallines;
				con.text[y * con.linewidth + con.x] = c | mask | con.ormask;
				con.x++;

				if (con.x >= con.linewidth)
				{
					con.x = 0;
				}

				break;
		}
	}
}

/*
 * The input line scrolls horizontally if
 * typing goes beyond the right edge
 */
static void
Con_DrawInput(void)
{
	int i;
	float scale;
	char *text;
	char ch;
	size_t txtlen;
	int linepos;
	int draw_icon;

	if (cls.key_dest == key_menu)
	{
		return;
	}

	/* don't draw anything (always draw if not active) */
	if ((cls.key_dest != key_console) && (cls.state == ca_active))
	{
		return;
	}

	scale = SCR_GetConsoleScale();
	text = key_lines[edit_line];
	linepos = key_linepos;

	/* prestep if horizontally scrolling */
	if (linepos >= (con.linewidth - 1))
	{
		int ofs = 1 + linepos - con.linewidth;

		text += ofs;
		linepos -= ofs;

		draw_icon = 0;
	}
	else
	{
		Draw_CharScaled(Con_Column(1, scale), con.vislines - (22 - Con_TextOffset()) * scale, CON_INPUT_INDICATOR, scale);
		draw_icon = 1;
	}

	txtlen = strlen(text);

	for (i = 0; i < (con.linewidth - draw_icon); i++)
	{
		if (i == linepos)
		{
			if ((cls.realtime >> 8) & 1)
			{
				ch = CON_INPUT_CURSOR;
			}
			else
			{
				ch = (text[i] == '\0') ? 10 : text[i];
			}
		}
		else if (i >= txtlen)
		{
			ch = ' ';
		}
		else
		{
			ch = text[i];
		}

		Draw_CharScaled(Con_Column(i + 1 + draw_icon, scale), con.vislines - (22 - Con_TextOffset()) * scale, ch, scale);
	}
}

/*
 * Draws the last few lines of output transparently over the game top
 */
void
Con_DrawNotify(void)
{
	int x, v;
	const char *text;
	int i;
	int time;
	float scale;

	v = 0;
	scale = SCR_GetConsoleScale();

	for (i = con.current - NUM_CON_TIMES + 1; i <= con.current; i++)
	{
		/* optech2: a client game draws its own notify lines (kill feed) */
		if (i < 0 || CL_CGame_HasNotify())
		{
			continue;
		}

		time = con.times[i % NUM_CON_TIMES];

		if (time == 0)
		{
			continue;
		}

		time = cls.realtime - time;

		if (time > con_notifytime->value * 1000)
		{
			continue;
		}

		text = con.text + (i % con.totallines) * con.linewidth;

		for (x = 0; x < con.linewidth; x++)
		{
			Draw_CharScaled(Con_Column(x + 1, scale), (v + Con_TextOffset()) * scale, text[x], scale);
		}

		v += Con_LinePitch();
	}

	/* optech2: a client game draws the chat line it gets (CL_GetTextInput) */
	if ((cls.key_dest == key_message) && !CL_CGame_HasNotify())
	{
		int skip;
		char *s;

		if (chat_team)
		{
			Draw_StringScaled(8 * scale, (v + Con_TextOffset()) * scale, scale, false, "say_team:");
			skip = 11;
		}
		else
		{
			Draw_StringScaled(8 * scale, (v + Con_TextOffset()) * scale, scale, false, "say:");
			skip = 5;
		}

		s = chat_buffer;

		{
			const int columns = (int)((viddef.width / scale) / Con_CharWidth()); /* optech2 */

			if (chat_bufferlen > columns - (skip + 1))
			{
				s += chat_bufferlen - (columns - (skip + 1));
			}
		}

		x = 0;

		while (s[x])
		{
			Draw_CharScaled(Con_Column(x + skip, scale), (v + Con_TextOffset()) * scale, s[x], scale);
			x++;
		}

		Draw_CharScaled(Con_Column(x + skip, scale), (v + Con_TextOffset()) * scale, 10 + ((cls.realtime >> 8) & 1), scale);
		v += Con_LinePitch();
	}

	if (v)
	{
		SCR_AddDirtyPoint(0, 0);
		SCR_AddDirtyPoint(viddef.width - 1, v);
	}
}

/*
 * Draws the console with the solid background
 */
void
Con_DrawConsole(float frac)
{
	int i, j, x, y, n;
	int rows;
	size_t verLen;
	const char *text;
	int row;
	int lines;
	float scale;
	char version[48];
	char dlbar[1024];
	char timebuf[48];
	char tmpbuf[48];

	time_t t;
	const struct tm *today;

	scale = SCR_GetConsoleScale();
	lines = viddef.height * frac;

	if (lines <= 0)
	{
		return;
	}

	if (lines > viddef.height)
	{
		lines = viddef.height;
	}

	/* draw the background (optech2: by default a dark see-through panel with
	   an accent line along its bottom edge; con_style 0: the classic picture) */
	if (con_style && con_style->value)
	{
		const float panel_alpha = con_alpha ? Q_clamp(con_alpha->value, 0.0f, 1.0f) : 0.82f;
		const float panel[4] = {0.03f, 0.04f, 0.06f, panel_alpha};
		const float accent[4] = {0.95f, 0.55f, 0.15f, 1.0f};
		const int edge = (int)Q_max(2.0f, 2.0f * scale);

		if (cls.state == ca_active)
		{
			Draw_FillColor(0, 0, viddef.width, lines - edge, panel);
			Draw_FillColor(0, lines - edge, viddef.width, edge, accent);
		}
		else
		{
			/* no game behind it (menus, loading): solid, with the plain
			   palette fill every renderer draws the same way (the blended
			   fill came out white there on GL4); palette 1 and 56 are the
			   nearest to the panel's and the accent's colors */
			Draw_Fill(0, 0, viddef.width, lines - edge, 1);
			Draw_Fill(0, lines - edge, viddef.width, edge, 56);
		}
	}
	else
	{
		Draw_StretchPic(0, -viddef.height + lines, viddef.width,
			viddef.height, "conback");
	}
	SCR_AddDirtyPoint(0, 0);
	SCR_AddDirtyPoint(viddef.width - 1, lines - 1);

	Com_sprintf(version, sizeof(version), ENGINE_NAME " v%s", OPTECH2_VERSION);

	verLen = strlen(version);

	Draw_StringScaled(viddef.width - ((verLen * 8 + 5) * scale), lines - 35 * scale, scale, true, version);

	t = time(NULL);
	{
		static struct tm today_buffer; /* optech2: for Q_localtime_r */

		today = Q_localtime_r(&t, &today_buffer);
	}

	if (today)
	{
		strftime(timebuf, sizeof(timebuf), "%H:%M:%S - %m/%d/%Y", today);
	}
	else
	{
		timebuf[0] = '\0';
	}

	Com_sprintf(tmpbuf, sizeof(tmpbuf), "%s", timebuf);

	Draw_StringScaled(viddef.width - (173 * scale), lines - 25 * scale, scale, true, tmpbuf);

	/* draw the text */
	con.vislines = lines;

	/* rows of text to draw (optech2: lines can be < 22, don't shift a negative;
	   rows are Con_LinePitch apart, the bottom one right above the input line) */
	rows = (int)((lines / scale - 22) / Con_LinePitch()) + 1;
	y = (lines - (22 + Con_LinePitch()) * scale) / scale;

	/* draw from the bottom up */
	if (con.display != con.current)
	{
		/* draw arrows to show the buffer is backscrolled */
		for (x = 0; x < con.linewidth; x += 4)
		{
			Draw_CharScaled(Con_Column(x + 1, scale), (y + Con_TextOffset()) * scale, '^', scale);
		}

		y -= Con_LinePitch();
		rows--;
	}

	row = con.display;

	for (i = 0; i < rows; i++, y -= Con_LinePitch(), row--)
	{
		if (row < 0)
		{
			break;
		}

		if (con.current - row >= con.totallines)
		{
			break; /* past scrollback wrap point */
		}

		text = con.text + (row % con.totallines) * con.linewidth;

		for (x = 0; x < con.linewidth; x++)
		{
			Draw_CharScaled(Con_Column(x + 1, scale), (y + Con_TextOffset()) * scale, text[x], scale);
		}
	}

	/* draw the download bar, figure out width */
#ifdef USE_CURL
	if (cls.downloadname[0] && (cls.download || cls.downloadposition))
#else
	if (cls.download)
#endif
	{
		if ((text = strrchr(cls.downloadname, '/')) != NULL)
		{
			text++;
		}

		else
		{
			text = cls.downloadname;
		}

		x = con.linewidth - ((con.linewidth * 7) / 40);
		y = x - strlen(text) - 8;
		i = con.linewidth / 3;

		if (strlen(text) > i)
		{
			y = x - i - 11;
			memcpy(dlbar, text, i);
			dlbar[i] = 0;
			strcat(dlbar, "...");
		}
		else
		{
			strcpy(dlbar, text);
		}

		strcat(dlbar, ": ");
		i = strlen(dlbar);
		dlbar[i++] = '\x80';

		/* where's the dot gone? */
		if (cls.downloadpercent == 0)
		{
			n = 0;
		}

		else
		{
			n = y * cls.downloadpercent / 100;
		}

		for (j = 0; j < y; j++)
		{
			if (j == n)
			{
				dlbar[i++] = '\x83';
			}

			else
			{
				dlbar[i++] = '\x81';
			}
		}

		dlbar[i++] = '\x82';
		dlbar[i] = 0;

		sprintf(dlbar + strlen(dlbar), " %02d%%", cls.downloadpercent);

		/* draw it */
		y = (lines - 12 * scale) / scale;

		for (i = 0; i < strlen(dlbar); i++)
		{
			Draw_CharScaled(((i + 1) << 3) * scale, y * scale, dlbar[i], scale);
		}
	}

	/* draw the input prompt, user text, and cursor if desired */
	Con_DrawInput();
}
