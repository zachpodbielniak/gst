/*
 * gst-kbselect-module.c - Vim-like keyboard selection module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Vim-like modal editing for the terminal. When activated by a
 * configurable trigger key (default: Ctrl+Shift+Escape), the module
 * enters NORMAL mode and consumes all keyboard input. The user can
 * then navigate (hjkl, w/b/e, 0/$, gg/G), enter visual selection
 * (v/V), search (//?), and yank selected text to the clipboard.
 *
 * State machine:
 *              trigger key
 * [INACTIVE] ──────────> [NORMAL] ──v/V──> [VISUAL/V-LINE]
 *     ^                     |                    |
 *     | Esc/i/Enter         | / or ?             | y (yank)
 *     |<────────────────────|                    |
 *     |                     v                    |
 *     |               [SEARCH]                   |
 *     |<─────────────────────────────────────────|
 *
 * The module implements GstInputHandler (high priority, consumes all
 * keys when active) and GstRenderOverlay (draws cursor, selection
 * highlight, search matches, and mode indicator).
 */

#include "gst-kbselect-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/boxed/gst-glyph.h"
#include "../../src/rendering/gst-render-context.h"
#include "../../src/window/gst-window.h"
#include "../../src/gst-enums.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/X.h>
#include <string.h>
#include <ctype.h>

/**
 * SECTION:gst-kbselect-module
 * @title: GstKbselectModule
 * @short_description: Vim-like modal keyboard selection
 *
 * #GstKbselectModule provides vim-like navigation and text selection
 * on the terminal screen and scrollback buffer.
 */

/* Mode states for the keyboard select state machine */
typedef enum {
	KBS_MODE_INACTIVE = 0,
	KBS_MODE_NORMAL,
	KBS_MODE_VISUAL,
	KBS_MODE_VISUAL_LINE,
	KBS_MODE_SEARCH
} KbsMode;

/* Search direction */
typedef enum {
	KBS_SEARCH_FORWARD = 0,
	KBS_SEARCH_BACKWARD
} KbsSearchDir;

/* Maximum search buffer length */
#define KBS_SEARCH_MAX (256)

/* Highlight color for visual selection (RGBA) */
#define KBS_HIGHLIGHT_R (0xFF)
#define KBS_HIGHLIGHT_G (0x88)
#define KBS_HIGHLIGHT_B (0x00)

/* Search match highlight color */
#define KBS_SEARCH_R (0xFF)
#define KBS_SEARCH_G (0xFF)
#define KBS_SEARCH_B (0x00)

struct _GstKbselectModule
{
	GstModule parent_instance;

	/* Current mode */
	KbsMode mode;

	/* Cursor position (column, row in visible screen coordinates) */
	gint cx;
	gint cy;

	/* Visual selection anchor (where 'v' or 'V' was pressed) */
	gint anchor_x;
	gint anchor_y;

	/* Search state */
	gchar search_buf[KBS_SEARCH_MAX];
	gint search_len;
	KbsSearchDir search_dir;

	/* Count prefix for motions (e.g., "5j" = move down 5) */
	gint count;

	/* Configurable trigger keysym and modifier */
	guint trigger_keysym;
	guint trigger_mods;

	/* Config: overlay colors */
	guint32 highlight_color;
	guint8 highlight_alpha;
	guint32 search_color;
	guint8 search_alpha;
	gboolean show_crosshair;

	/* 'g' key pending (for gg command) */
	gboolean g_pending;
};

/* Forward declarations */
static void
gst_kbselect_module_input_init(GstInputHandlerInterface *iface);
static void
gst_kbselect_module_overlay_init(GstRenderOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstKbselectModule, gst_kbselect_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER,
		gst_kbselect_module_input_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_kbselect_module_overlay_init))

/* ===== Internal helpers ===== */

/*
 * get_terminal_size:
 *
 * Gets the terminal dimensions from the module manager.
 */
static void
get_terminal_size(gint *cols, gint *rows)
{
	GstModuleManager *mgr;
	GstTerminal *term;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term != NULL)
	{
		gst_terminal_get_size(term, cols, rows);
	}
	else
	{
		*cols = 80;
		*rows = 24;
	}
}

/*
 * mark_all_dirty:
 *
 * Marks all terminal lines dirty to force a full redraw.
 */
static void
mark_all_dirty(void)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	gint rows;
	gint y;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL)
	{
		return;
	}

	rows = gst_terminal_get_rows(term);
	for (y = 0; y < rows; y++)
	{
		gst_terminal_mark_dirty(term, y);
	}
}

/*
 * clamp_cursor:
 * @self: the module instance
 *
 * Clamps cursor position to valid terminal bounds.
 */
static void
clamp_cursor(GstKbselectModule *self)
{
	gint cols;
	gint rows;

	get_terminal_size(&cols, &rows);

	if (self->cx < 0) self->cx = 0;
	if (self->cx >= cols) self->cx = cols - 1;
	if (self->cy < 0) self->cy = 0;
	if (self->cy >= rows) self->cy = rows - 1;
}

/*
 * get_rune_at:
 *
 * Gets the Unicode codepoint at a given screen position.
 * Returns space if out of bounds or if glyph is empty.
 */
static GstRune
get_rune_at(gint col, gint row)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	GstGlyph *g;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL)
	{
		return (GstRune)' ';
	}

	g = gst_terminal_get_glyph(term, col, row);
	if (g == NULL || g->rune == 0)
	{
		return (GstRune)' ';
	}

	return g->rune;
}

/*
 * is_word_char:
 *
 * Returns TRUE if the codepoint is a "word" character (alnum or underscore).
 */
static gboolean
is_word_char(GstRune r)
{
	if (r < 128)
	{
		return (gboolean)(g_ascii_isalnum((gchar)r) || r == '_');
	}
	/* Non-ASCII: treat as word character */
	return TRUE;
}

/*
 * is_bigword_delim:
 *
 * Returns TRUE if the codepoint is a WORD delimiter (whitespace).
 */
static gboolean
is_bigword_delim(GstRune r)
{
	return (r == ' ' || r == '\t' || r == '\n' || r == '\r');
}

/*
 * exit_mode:
 *
 * Returns to INACTIVE mode and triggers a redraw to remove overlays.
 */
static void
exit_mode(GstKbselectModule *self)
{
	self->mode = KBS_MODE_INACTIVE;
	self->count = 0;
	self->g_pending = FALSE;
	self->search_len = 0;
	mark_all_dirty();
}

/*
 * enter_normal:
 *
 * Enters NORMAL mode, placing cursor at center of screen.
 */
static void
enter_normal(GstKbselectModule *self)
{
	gint cols;
	gint rows;

	get_terminal_size(&cols, &rows);

	self->mode = KBS_MODE_NORMAL;
	self->cx = cols / 2;
	self->cy = rows / 2;
	self->count = 0;
	self->g_pending = FALSE;
	mark_all_dirty();
}

/*
 * yank_selection:
 *
 * Copies the selected text (visual or visual-line) to the clipboard.
 */
static void
yank_selection(GstKbselectModule *self)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	GstWindow *win;
	GString *text;
	gint cols;
	gint rows;
	gint start_y;
	gint end_y;
	gint start_x;
	gint end_x;
	gint y;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	win = (GstWindow *)gst_module_manager_get_window(mgr);
	if (term == NULL || win == NULL)
	{
		return;
	}

	get_terminal_size(&cols, &rows);
	text = g_string_new(NULL);

	if (self->mode == KBS_MODE_VISUAL_LINE)
	{
		/* Visual line: entire rows between anchor_y and cy */
		start_y = (self->anchor_y < self->cy)
			? self->anchor_y : self->cy;
		end_y = (self->anchor_y > self->cy)
			? self->anchor_y : self->cy;

		for (y = start_y; y <= end_y; y++)
		{
			GstLine *line;

			line = gst_terminal_get_line(term, y);
			if (line != NULL)
			{
				g_autofree gchar *s = gst_line_to_string(line);
				if (s != NULL)
				{
					/* Trim trailing spaces */
					gint slen;

					slen = (gint)strlen(s);
					while (slen > 0 && s[slen - 1] == ' ')
					{
						slen--;
					}
					g_string_append_len(text, s, slen);
				}
			}
			if (y < end_y)
			{
				g_string_append_c(text, '\n');
			}
		}
	}
	else if (self->mode == KBS_MODE_VISUAL)
	{
		/* Visual character: from anchor to cursor */
		if (self->anchor_y < self->cy ||
		    (self->anchor_y == self->cy &&
		     self->anchor_x <= self->cx))
		{
			start_y = self->anchor_y;
			start_x = self->anchor_x;
			end_y = self->cy;
			end_x = self->cx;
		}
		else
		{
			start_y = self->cy;
			start_x = self->cx;
			end_y = self->anchor_y;
			end_x = self->anchor_x;
		}

		for (y = start_y; y <= end_y; y++)
		{
			GstLine *line;
			gint from;
			gint to;

			line = gst_terminal_get_line(term, y);
			if (line == NULL)
			{
				continue;
			}

			from = (y == start_y) ? start_x : 0;
			to = (y == end_y) ? end_x + 1 : cols;
			if (to > cols) to = cols;

			{
				g_autofree gchar *s =
					gst_line_to_string_range(line, from, to);
				if (s != NULL)
				{
					g_string_append(text, s);
				}
			}
			if (y < end_y)
			{
				g_string_append_c(text, '\n');
			}
		}
	}

	/* Copy to clipboard */
	if (text->len > 0)
	{
		gst_window_set_selection(win, text->str, FALSE);
		gst_window_copy_to_clipboard(win);
		g_debug("keyboard_select: yanked %lu bytes", (gulong)text->len);
	}

	g_string_free(text, TRUE);
}

/*
 * yank_line:
 *
 * Yanks the current line to clipboard (yy or Y).
 */
static void
yank_line(GstKbselectModule *self)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	GstWindow *win;
	GstLine *line;
	g_autofree gchar *s = NULL;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	win = (GstWindow *)gst_module_manager_get_window(mgr);
	if (term == NULL || win == NULL)
	{
		return;
	}

	line = gst_terminal_get_line(term, self->cy);
	if (line == NULL)
	{
		return;
	}

	s = gst_line_to_string(line);
	if (s != NULL)
	{
		/* Trim trailing spaces */
		gint slen;

		slen = (gint)strlen(s);
		while (slen > 0 && s[slen - 1] == ' ')
		{
			slen--;
		}
		s[slen] = '\0';

		gst_window_set_selection(win, s, FALSE);
		gst_window_copy_to_clipboard(win);
	}
}

/*
 * do_search:
 *
 * Searches for the current search_buf pattern on the visible screen.
 * Moves the cursor to the first match found.
 *
 * Returns: TRUE if a match was found
 */
static gboolean
do_search(GstKbselectModule *self)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	gint cols;
	gint rows;
	gint y;
	gint x;
	gint start_y;
	gint start_x;
	gint dy;

	if (self->search_len <= 0)
	{
		return FALSE;
	}

	self->search_buf[self->search_len] = '\0';

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL)
	{
		return FALSE;
	}

	get_terminal_size(&cols, &rows);
	start_y = self->cy;
	start_x = self->cx;

	/* Direction-dependent iteration */
	dy = (self->search_dir == KBS_SEARCH_FORWARD) ? 1 : -1;

	/*
	 * Search from cursor position in the specified direction.
	 * Skip the current cursor position on the first iteration.
	 */
	for (y = start_y; y >= 0 && y < rows; y += dy)
	{
		GstLine *line;
		g_autofree gchar *line_str = NULL;
		const gchar *found;
		gint search_from;

		line = gst_terminal_get_line(term, y);
		if (line == NULL)
		{
			continue;
		}

		line_str = gst_line_to_string(line);
		if (line_str == NULL)
		{
			continue;
		}

		/* Determine starting column for this line's search */
		if (y == start_y)
		{
			search_from = start_x + dy;
		}
		else
		{
			search_from = (dy > 0) ? 0 : (gint)strlen(line_str) - 1;
		}

		if (search_from < 0) search_from = 0;
		if (search_from >= (gint)strlen(line_str))
		{
			continue;
		}

		if (dy > 0)
		{
			found = strstr(line_str + search_from, self->search_buf);
		}
		else
		{
			/* Reverse search: find last occurrence before search_from */
			const gchar *p;
			const gchar *last;

			last = NULL;
			p = line_str;
			while ((p = strstr(p, self->search_buf)) != NULL)
			{
				if ((gint)(p - line_str) <= search_from)
				{
					last = p;
				}
				p++;
			}
			found = last;
		}

		if (found != NULL)
		{
			/* Calculate column offset (byte offset works for ASCII) */
			x = (gint)(found - line_str);
			if (x < cols)
			{
				self->cx = x;
				self->cy = y;
				return TRUE;
			}
		}
	}

	return FALSE;
}

/* ===== GstInputHandler interface ===== */

/*
 * handle_search_key:
 *
 * Handles key input in SEARCH mode. Accumulates search string,
 * Enter/Escape commits/cancels.
 *
 * Returns: TRUE (always consumes in search mode)
 */
static gboolean
handle_search_key(
	GstKbselectModule *self,
	guint              keyval,
	guint              state
){
	(void)state;

	switch (keyval)
	{
	case XK_Return:
	case XK_KP_Enter:
		/* Commit search and return to normal mode */
		if (do_search(self))
		{
			self->mode = KBS_MODE_NORMAL;
		}
		else
		{
			/* No match found, stay in normal mode */
			self->mode = KBS_MODE_NORMAL;
		}
		mark_all_dirty();
		return TRUE;

	case XK_Escape:
		self->mode = KBS_MODE_NORMAL;
		self->search_len = 0;
		mark_all_dirty();
		return TRUE;

	case XK_BackSpace:
		if (self->search_len > 0)
		{
			self->search_len--;
		}
		mark_all_dirty();
		return TRUE;

	default:
		/* Append printable characters to search buffer */
		if (keyval >= 0x20 && keyval <= 0x7e &&
		    self->search_len < KBS_SEARCH_MAX - 1)
		{
			self->search_buf[self->search_len++] = (gchar)keyval;
			mark_all_dirty();
		}
		return TRUE;
	}
}

/*
 * get_effective_count:
 *
 * Returns the count prefix, defaulting to 1 if no count was entered.
 */
static gint
get_effective_count(GstKbselectModule *self)
{
	gint n;

	n = (self->count > 0) ? self->count : 1;
	self->count = 0;
	return n;
}

/*
 * handle_normal_key:
 *
 * Handles key input in NORMAL mode (and VISUAL/VISUAL_LINE).
 *
 * Returns: TRUE (always consumes when mode != INACTIVE)
 */
static gboolean
handle_normal_key(
	GstKbselectModule *self,
	guint              keyval,
	guint              state
){
	gint cols;
	gint rows;
	gint n;

	get_terminal_size(&cols, &rows);

	/* Handle count prefix digits */
	if (keyval >= XK_1 && keyval <= XK_9 && self->count == 0 &&
	    !self->g_pending)
	{
		self->count = (gint)(keyval - XK_0);
		return TRUE;
	}
	if (keyval >= XK_0 && keyval <= XK_9 && self->count > 0)
	{
		self->count = self->count * 10 + (gint)(keyval - XK_0);
		if (self->count > 99999) self->count = 99999;
		return TRUE;
	}

	n = get_effective_count(self);

	/* Handle 'g' prefix (for gg) */
	if (self->g_pending)
	{
		self->g_pending = FALSE;
		if (keyval == XK_g)
		{
			/* gg: go to top of screen */
			self->cy = 0;
			clamp_cursor(self);
			mark_all_dirty();
			return TRUE;
		}
		/* Any other key after 'g': ignore the 'g' and process normally */
	}

	switch (keyval)
	{
	/* === Exit keys === */
	case XK_Escape:
	case XK_i:
		if (self->mode == KBS_MODE_VISUAL ||
		    self->mode == KBS_MODE_VISUAL_LINE)
		{
			/* Exit visual mode back to normal */
			self->mode = KBS_MODE_NORMAL;
			mark_all_dirty();
			return TRUE;
		}
		exit_mode(self);
		return TRUE;

	case XK_Return:
		exit_mode(self);
		return TRUE;

	/* === Navigation: hjkl === */
	case XK_h:
	case XK_Left:
		self->cx -= n;
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	case XK_j:
	case XK_Down:
		self->cy += n;
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	case XK_k:
	case XK_Up:
		self->cy -= n;
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	case XK_l:
	case XK_Right:
		self->cx += n;
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	/* === Line navigation: 0, $, ^ === */
	case XK_0:
		self->cx = 0;
		mark_all_dirty();
		return TRUE;

	case XK_dollar:
		self->cx = cols - 1;
		mark_all_dirty();
		return TRUE;

	case XK_asciicircum:
		/* ^ : first non-space character on line */
		{
			gint x;
			for (x = 0; x < cols; x++)
			{
				GstRune r;

				r = get_rune_at(x, self->cy);
				if (r != ' ' && r != '\t')
				{
					self->cx = x;
					break;
				}
			}
		}
		mark_all_dirty();
		return TRUE;

	/* === Word motion: w, b, e, W, B, E === */
	case XK_w:
		/* Forward word */
		{
			gint i;
			for (i = 0; i < n; i++)
			{
				GstRune r;

				/* Skip current word chars */
				r = get_rune_at(self->cx, self->cy);
				if (is_word_char(r))
				{
					while (self->cx < cols - 1 &&
					       is_word_char(get_rune_at(self->cx, self->cy)))
					{
						self->cx++;
					}
				}
				/* Skip non-word, non-space */
				else if (r != ' ')
				{
					while (self->cx < cols - 1 &&
					       !is_word_char(get_rune_at(self->cx, self->cy)) &&
					       get_rune_at(self->cx, self->cy) != ' ')
					{
						self->cx++;
					}
				}
				/* Skip spaces */
				while (self->cx < cols - 1 &&
				       get_rune_at(self->cx, self->cy) == ' ')
				{
					self->cx++;
				}
			}
		}
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	case XK_b:
		/* Backward word */
		{
			gint i;
			for (i = 0; i < n; i++)
			{
				/* Skip spaces backward */
				while (self->cx > 0 &&
				       get_rune_at(self->cx - 1, self->cy) == ' ')
				{
					self->cx--;
				}
				/* Skip word chars backward */
				if (self->cx > 0 &&
				    is_word_char(get_rune_at(self->cx - 1, self->cy)))
				{
					while (self->cx > 0 &&
					       is_word_char(get_rune_at(self->cx - 1, self->cy)))
					{
						self->cx--;
					}
				}
				else if (self->cx > 0)
				{
					while (self->cx > 0 &&
					       !is_word_char(get_rune_at(self->cx - 1, self->cy)) &&
					       get_rune_at(self->cx - 1, self->cy) != ' ')
					{
						self->cx--;
					}
				}
			}
		}
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	case XK_e:
		/* End of word */
		{
			gint i;
			for (i = 0; i < n; i++)
			{
				if (self->cx < cols - 1)
				{
					self->cx++;
				}
				/* Skip spaces */
				while (self->cx < cols - 1 &&
				       get_rune_at(self->cx, self->cy) == ' ')
				{
					self->cx++;
				}
				/* Skip word chars */
				if (is_word_char(get_rune_at(self->cx, self->cy)))
				{
					while (self->cx < cols - 1 &&
					       is_word_char(get_rune_at(self->cx + 1, self->cy)))
					{
						self->cx++;
					}
				}
				else
				{
					while (self->cx < cols - 1 &&
					       !is_word_char(get_rune_at(self->cx + 1, self->cy)) &&
					       get_rune_at(self->cx + 1, self->cy) != ' ')
					{
						self->cx++;
					}
				}
			}
		}
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	case XK_W:
		/* Forward WORD (whitespace-delimited) */
		{
			gint i;
			for (i = 0; i < n; i++)
			{
				while (self->cx < cols - 1 &&
				       !is_bigword_delim(get_rune_at(self->cx, self->cy)))
				{
					self->cx++;
				}
				while (self->cx < cols - 1 &&
				       is_bigword_delim(get_rune_at(self->cx, self->cy)))
				{
					self->cx++;
				}
			}
		}
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	case XK_B:
		/* Backward WORD */
		{
			gint i;
			for (i = 0; i < n; i++)
			{
				while (self->cx > 0 &&
				       is_bigword_delim(get_rune_at(self->cx - 1, self->cy)))
				{
					self->cx--;
				}
				while (self->cx > 0 &&
				       !is_bigword_delim(get_rune_at(self->cx - 1, self->cy)))
				{
					self->cx--;
				}
			}
		}
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	case XK_E:
		/* End of WORD */
		{
			gint i;
			for (i = 0; i < n; i++)
			{
				if (self->cx < cols - 1)
				{
					self->cx++;
				}
				while (self->cx < cols - 1 &&
				       is_bigword_delim(get_rune_at(self->cx, self->cy)))
				{
					self->cx++;
				}
				while (self->cx < cols - 1 &&
				       !is_bigword_delim(get_rune_at(self->cx + 1, self->cy)))
				{
					self->cx++;
				}
			}
		}
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	/* === Screen position: H, M, L === */
	case XK_H:
		self->cy = 0;
		mark_all_dirty();
		return TRUE;

	case XK_M:
		self->cy = rows / 2;
		mark_all_dirty();
		return TRUE;

	case XK_L:
		self->cy = rows - 1;
		mark_all_dirty();
		return TRUE;

	/* === Page scrolling: Ctrl+u/d/f/b === */
	case XK_u:
		if (state & ControlMask)
		{
			self->cy -= rows / 2;
			clamp_cursor(self);
			mark_all_dirty();
			return TRUE;
		}
		return TRUE;

	case XK_d:
		if (state & ControlMask)
		{
			self->cy += rows / 2;
			clamp_cursor(self);
			mark_all_dirty();
			return TRUE;
		}
		return TRUE;

	case XK_f:
		if (state & ControlMask)
		{
			self->cy += rows;
			clamp_cursor(self);
			mark_all_dirty();
			return TRUE;
		}
		return TRUE;

	/* === gg and G === */
	case XK_g:
		self->g_pending = TRUE;
		return TRUE;

	case XK_G:
		self->cy = rows - 1;
		clamp_cursor(self);
		mark_all_dirty();
		return TRUE;

	/* === Visual mode entry === */
	case XK_v:
		if (self->mode == KBS_MODE_VISUAL)
		{
			/* Toggle off: back to normal */
			self->mode = KBS_MODE_NORMAL;
		}
		else
		{
			self->mode = KBS_MODE_VISUAL;
			self->anchor_x = self->cx;
			self->anchor_y = self->cy;
		}
		mark_all_dirty();
		return TRUE;

	case XK_V:
		if (self->mode == KBS_MODE_VISUAL_LINE)
		{
			self->mode = KBS_MODE_NORMAL;
		}
		else
		{
			self->mode = KBS_MODE_VISUAL_LINE;
			self->anchor_x = 0;
			self->anchor_y = self->cy;
		}
		mark_all_dirty();
		return TRUE;

	/* === Yank === */
	case XK_y:
		if (self->mode == KBS_MODE_VISUAL ||
		    self->mode == KBS_MODE_VISUAL_LINE)
		{
			yank_selection(self);
			exit_mode(self);
			return TRUE;
		}
		/* 'y' in normal mode: start pending yank (yy = yank line) */
		/* For simplicity, treat as yank_line */
		yank_line(self);
		exit_mode(self);
		return TRUE;

	case XK_Y:
		yank_line(self);
		exit_mode(self);
		return TRUE;

	/* === Search === */
	case XK_slash:
		self->mode = KBS_MODE_SEARCH;
		self->search_dir = KBS_SEARCH_FORWARD;
		self->search_len = 0;
		mark_all_dirty();
		return TRUE;

	case XK_question:
		self->mode = KBS_MODE_SEARCH;
		self->search_dir = KBS_SEARCH_BACKWARD;
		self->search_len = 0;
		mark_all_dirty();
		return TRUE;

	case XK_n:
		/* Next search match */
		if (self->search_len > 0)
		{
			do_search(self);
			mark_all_dirty();
		}
		return TRUE;

	case XK_N:
		/* Previous search match (reverse direction) */
		if (self->search_len > 0)
		{
			KbsSearchDir orig;

			orig = self->search_dir;
			self->search_dir = (orig == KBS_SEARCH_FORWARD)
				? KBS_SEARCH_BACKWARD : KBS_SEARCH_FORWARD;
			do_search(self);
			self->search_dir = orig;
			mark_all_dirty();
		}
		return TRUE;

	default:
		/* Unknown key: consume but ignore */
		return TRUE;
	}
}

/*
 * handle_key_event:
 *
 * GstInputHandler interface implementation. Consumes all keys
 * when the module is in an active mode.
 */
static gboolean
gst_kbselect_module_handle_key_event(
	GstInputHandler *handler,
	guint            keyval,
	guint            keycode,
	guint            state
){
	GstKbselectModule *self;

	(void)keycode;

	self = GST_KBSELECT_MODULE(handler);

	/* Check trigger key to activate */
	if (self->mode == KBS_MODE_INACTIVE)
	{
		guint clean_state;

		/*
		 * Strip lock bits (Num/Caps/Scroll lock) for reliable matching.
		 * Keep only Shift, Control, Alt (Mod1), Super (Mod4).
		 */
		clean_state = state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);

		if (keyval == self->trigger_keysym &&
		    clean_state == self->trigger_mods)
		{
			enter_normal(self);
			return TRUE;
		}

		return FALSE;
	}

	/* In search mode, handle differently */
	if (self->mode == KBS_MODE_SEARCH)
	{
		return handle_search_key(self, keyval, state);
	}

	/* Normal / Visual / Visual-Line mode */
	return handle_normal_key(self, keyval, state);
}

static void
gst_kbselect_module_input_init(GstInputHandlerInterface *iface)
{
	iface->handle_key_event = gst_kbselect_module_handle_key_event;
}

/* ===== GstRenderOverlay interface ===== */

/*
 * render:
 *
 * Draws the cursor block, visual selection highlight,
 * search matches, and mode indicator string when active.
 */
static void
gst_kbselect_module_render(
	GstRenderOverlay *overlay,
	gpointer          render_context,
	gint              width,
	gint              height
){
	GstKbselectModule *self;
	GstRenderContext *ctx;
	gint cols;
	gint rows;
	gint px;
	gint py;
	const gchar *mode_str;
	gint mode_len;
	gint i;

	self = GST_KBSELECT_MODULE(overlay);

	if (self->mode == KBS_MODE_INACTIVE)
	{
		return;
	}

	ctx = (GstRenderContext *)render_context;
	get_terminal_size(&cols, &rows);

	/*
	 * Draw visual selection highlight.
	 * Uses fill_rect_rgba for semi-transparent overlay.
	 */
	if (self->mode == KBS_MODE_VISUAL ||
	    self->mode == KBS_MODE_VISUAL_LINE)
	{
		gint start_y;
		gint end_y;
		gint start_x;
		gint end_x;
		gint y;

		if (self->mode == KBS_MODE_VISUAL_LINE)
		{
			start_y = (self->anchor_y < self->cy)
				? self->anchor_y : self->cy;
			end_y = (self->anchor_y > self->cy)
				? self->anchor_y : self->cy;
			start_x = 0;
			end_x = cols - 1;

			for (y = start_y; y <= end_y; y++)
			{
				gst_render_context_fill_rect_rgba(ctx,
					ctx->borderpx + start_x * ctx->cw,
					ctx->borderpx + y * ctx->ch,
					(end_x - start_x + 1) * ctx->cw,
					ctx->ch,
					KBS_HIGHLIGHT_R, KBS_HIGHLIGHT_G,
					KBS_HIGHLIGHT_B, self->highlight_alpha);
			}
		}
		else
		{
			/* Character-wise visual */
			if (self->anchor_y < self->cy ||
			    (self->anchor_y == self->cy &&
			     self->anchor_x <= self->cx))
			{
				start_y = self->anchor_y;
				start_x = self->anchor_x;
				end_y = self->cy;
				end_x = self->cx;
			}
			else
			{
				start_y = self->cy;
				start_x = self->cx;
				end_y = self->anchor_y;
				end_x = self->anchor_x;
			}

			for (y = start_y; y <= end_y; y++)
			{
				gint fx;
				gint tx;

				fx = (y == start_y) ? start_x : 0;
				tx = (y == end_y) ? end_x : cols - 1;

				gst_render_context_fill_rect_rgba(ctx,
					ctx->borderpx + fx * ctx->cw,
					ctx->borderpx + y * ctx->ch,
					(tx - fx + 1) * ctx->cw,
					ctx->ch,
					KBS_HIGHLIGHT_R, KBS_HIGHLIGHT_G,
					KBS_HIGHLIGHT_B, self->highlight_alpha);
			}
		}
	}

	/* Draw crosshair (if enabled) */
	if (self->show_crosshair)
	{
		/* Horizontal line through cursor row */
		gst_render_context_fill_rect_rgba(ctx,
			ctx->borderpx,
			ctx->borderpx + self->cy * ctx->ch,
			cols * ctx->cw, ctx->ch,
			0x80, 0x80, 0x80, 40);

		/* Vertical line through cursor column */
		gst_render_context_fill_rect_rgba(ctx,
			ctx->borderpx + self->cx * ctx->cw,
			ctx->borderpx,
			ctx->cw, rows * ctx->ch,
			0x80, 0x80, 0x80, 40);
	}

	/* Draw cursor block */
	px = ctx->borderpx + self->cx * ctx->cw;
	py = ctx->borderpx + self->cy * ctx->ch;
	gst_render_context_fill_rect_rgba(ctx,
		px, py, ctx->cw, ctx->ch,
		0xFF, 0xFF, 0xFF, 180);

	/* Draw mode indicator at bottom-left */
	switch (self->mode)
	{
	case KBS_MODE_NORMAL:
		mode_str = "-- NORMAL --";
		break;
	case KBS_MODE_VISUAL:
		mode_str = "-- VISUAL --";
		break;
	case KBS_MODE_VISUAL_LINE:
		mode_str = "-- V-LINE --";
		break;
	case KBS_MODE_SEARCH:
		mode_str = "-- SEARCH --";
		break;
	default:
		mode_str = "";
		break;
	}

	mode_len = (gint)strlen(mode_str);
	{
		gint ind_x;
		gint ind_y;

		ind_x = ctx->borderpx;
		ind_y = ctx->borderpx + (rows - 1) * ctx->ch;

		/* Background bar for indicator */
		gst_render_context_fill_rect_rgba(ctx,
			ind_x, ind_y,
			mode_len * ctx->cw, ctx->ch,
			0x00, 0x00, 0x00, 200);

		/* Draw indicator text */
		for (i = 0; i < mode_len; i++)
		{
			gst_render_context_draw_glyph(ctx,
				(GstRune)mode_str[i], GST_FONT_STYLE_BOLD,
				ind_x + i * ctx->cw, ind_y,
				256, 257, GST_GLYPH_ATTR_BOLD);
		}
	}

	/* Draw search string if in search mode */
	if (self->mode == KBS_MODE_SEARCH)
	{
		gint sx;
		gint sy;
		gchar prefix;

		prefix = (self->search_dir == KBS_SEARCH_FORWARD) ? '/' : '?';
		sx = ctx->borderpx + (mode_len + 1) * ctx->cw;
		sy = ctx->borderpx + (rows - 1) * ctx->ch;

		gst_render_context_draw_glyph(ctx,
			(GstRune)prefix, GST_FONT_STYLE_NORMAL,
			sx, sy, 256, 257, 0);
		sx += ctx->cw;

		for (i = 0; i < self->search_len; i++)
		{
			gst_render_context_draw_glyph(ctx,
				(GstRune)self->search_buf[i],
				GST_FONT_STYLE_NORMAL,
				sx + i * ctx->cw, sy,
				256, 257, 0);
		}
	}
}

static void
gst_kbselect_module_overlay_init(GstRenderOverlayInterface *iface)
{
	iface->render = gst_kbselect_module_render;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_kbselect_module_get_name(GstModule *module)
{
	(void)module;
	return "keyboard_select";
}

static const gchar *
gst_kbselect_module_get_description(GstModule *module)
{
	(void)module;
	return "Vim-like modal keyboard selection and navigation";
}

/*
 * parse_trigger_key:
 *
 * Parses a key string like "Ctrl+Shift+Escape" into keysym + mods.
 */
static void
parse_trigger_key(
	const gchar *keystr,
	guint       *keysym_out,
	guint       *mods_out
){
	guint mods;
	const gchar *p;
	g_autofree gchar *keyname = NULL;

	mods = 0;
	p = keystr;

	/* Parse modifier prefixes */
	while (TRUE)
	{
		if (g_str_has_prefix(p, "Ctrl+") ||
		    g_str_has_prefix(p, "ctrl+"))
		{
			mods |= ControlMask;
			p += 5;
		}
		else if (g_str_has_prefix(p, "Shift+") ||
		         g_str_has_prefix(p, "shift+"))
		{
			mods |= ShiftMask;
			p += 6;
		}
		else if (g_str_has_prefix(p, "Alt+") ||
		         g_str_has_prefix(p, "alt+"))
		{
			mods |= Mod1Mask;
			p += 4;
		}
		else if (g_str_has_prefix(p, "Super+") ||
		         g_str_has_prefix(p, "super+"))
		{
			mods |= Mod4Mask;
			p += 6;
		}
		else
		{
			break;
		}
	}

	/* The remainder is the key name */
	keyname = g_strdup(p);

	/* Map common names to X keysyms */
	if (g_ascii_strcasecmp(keyname, "Escape") == 0)
	{
		*keysym_out = XK_Escape;
	}
	else if (g_ascii_strcasecmp(keyname, "Return") == 0 ||
	         g_ascii_strcasecmp(keyname, "Enter") == 0)
	{
		*keysym_out = XK_Return;
	}
	else if (g_ascii_strcasecmp(keyname, "Space") == 0)
	{
		*keysym_out = XK_space;
	}
	else if (strlen(keyname) == 1)
	{
		/* Single character */
		*keysym_out = (guint)keyname[0];
	}
	else
	{
		/* Try XStringToKeysym */
		*keysym_out = XStringToKeysym(keyname);
		if (*keysym_out == NoSymbol)
		{
			g_warning("keyboard_select: unknown key '%s', "
				"using Escape", keyname);
			*keysym_out = XK_Escape;
		}
	}

	*mods_out = mods;
}

static void
gst_kbselect_module_configure(GstModule *module, gpointer config)
{
	GstKbselectModule *self;
	YamlMapping *mod_cfg;

	self = GST_KBSELECT_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "keyboard_select");
	if (mod_cfg == NULL)
	{
		return;
	}

	/* Parse trigger key */
	if (yaml_mapping_has_member(mod_cfg, "key"))
	{
		const gchar *keystr;

		keystr = yaml_mapping_get_string_member(mod_cfg, "key");
		if (keystr != NULL)
		{
			parse_trigger_key(keystr,
				&self->trigger_keysym,
				&self->trigger_mods);
			g_debug("keyboard_select: trigger key set to '%s'",
				keystr);
		}
	}

	/* Crosshair */
	if (yaml_mapping_has_member(mod_cfg, "show_crosshair"))
	{
		self->show_crosshair = yaml_mapping_get_boolean_member(
			mod_cfg, "show_crosshair");
	}

	/* Highlight alpha */
	if (yaml_mapping_has_member(mod_cfg, "highlight_alpha"))
	{
		gint64 val;

		val = yaml_mapping_get_int_member(mod_cfg, "highlight_alpha");
		if (val < 0) val = 0;
		if (val > 255) val = 255;
		self->highlight_alpha = (guint8)val;
	}

	/* Search alpha */
	if (yaml_mapping_has_member(mod_cfg, "search_alpha"))
	{
		gint64 val;

		val = yaml_mapping_get_int_member(mod_cfg, "search_alpha");
		if (val < 0) val = 0;
		if (val > 255) val = 255;
		self->search_alpha = (guint8)val;
	}

	g_debug("keyboard_select: configured");
}

static gboolean
gst_kbselect_module_activate(GstModule *module)
{
	(void)module;
	g_debug("keyboard_select: activated");
	return TRUE;
}

static void
gst_kbselect_module_deactivate(GstModule *module)
{
	GstKbselectModule *self;

	self = GST_KBSELECT_MODULE(module);
	self->mode = KBS_MODE_INACTIVE;
	g_debug("keyboard_select: deactivated");
}

/* ===== GObject lifecycle ===== */

static void
gst_kbselect_module_class_init(GstKbselectModuleClass *klass)
{
	GstModuleClass *module_class;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_kbselect_module_get_name;
	module_class->get_description = gst_kbselect_module_get_description;
	module_class->activate = gst_kbselect_module_activate;
	module_class->deactivate = gst_kbselect_module_deactivate;
	module_class->configure = gst_kbselect_module_configure;
}

static void
gst_kbselect_module_init(GstKbselectModule *self)
{
	self->mode = KBS_MODE_INACTIVE;
	self->cx = 0;
	self->cy = 0;
	self->anchor_x = 0;
	self->anchor_y = 0;
	self->search_len = 0;
	self->search_dir = KBS_SEARCH_FORWARD;
	self->count = 0;
	self->g_pending = FALSE;

	/* Default trigger: Ctrl+Shift+Escape */
	self->trigger_keysym = XK_Escape;
	self->trigger_mods = ControlMask | ShiftMask;

	/* Default overlay settings */
	self->highlight_alpha = 100;
	self->search_alpha = 150;
	self->show_crosshair = TRUE;

	/* Set high priority so we consume keys before scrollback */
	gst_module_set_priority(GST_MODULE(self),
		GST_MODULE_PRIORITY_HIGH);
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 *
 * Returns: The #GType for #GstKbselectModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_KBSELECT_MODULE;
}
