/*
 * gst-terminal.c - GST Terminal Class Implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Full terminal emulation ported from st.c (suckless terminal).
 * Handles screen buffers, cursor control, escape sequence parsing
 * (CSI, OSC, DCS), SGR attributes, mode management, and charsets.
 */

#include "gst-terminal.h"
#include "gst-escape-parser.h"
#include "../util/gst-utf8.h"
#include <string.h>
#include <stdio.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <pango/pango.h>

/* ===== Macros and constants ===== */

#define ISCONTROL(c)   ((c) < 0x20 || (c) == 0x7f || ((c) >= 0x80 && (c) <= 0x9f))
#define BETWEEN(x, a, b) ((x) >= (a) && (x) <= (b))
#define DEFAULT(a, b)  ((a) != 0 ? (a) : (b))

/* Size of CSI buffer */
#define CSI_BUF_SIZ    (256)

/* Alternate-key reporting needs layout metadata not supplied by the driver. */
#define GST_KEYBOARD_SUPPORTED_FLAGS (1u | 2u | 8u | 16u)
#define GST_KEYBOARD_STACK_SIZE (32)

/* Size of string escape buffer (OSC, DCS, etc.) initial alloc */
#define STR_BUF_SIZ    (256)

/* True color macros are now in gst-types.h as GST_TRUECOLOR_FLAG, etc. */

/*
 * VT100 graphics mode character translation table.
 * Maps 0x41-0x7e to Unicode box-drawing characters.
 */
static const GstRune vt100_graphic0[62] = {
	0x2191, 0x2193, 0x2192, 0x2190, 0x2588, 0x259a, 0x2603, /* A-G */
	0,      0,      0,      0,      0,      0,      0,      0, /* H-O */
	0,      0,      0,      0,      0,      0,      0,      0, /* P-W */
	0,      0,      0,      0,      0,      0,      0, 0x0020, /* X-_ */
	0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1, /* `-g */
	0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba, /* h-o */
	0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c, /* p-w */
	0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,        /* x-~ */
};

/* ===== Private data structure ===== */

struct _GstTerminalPrivate {
	/* Dimensions */
	gint cols;
	gint rows;

	/* Screen buffers (primary and alternate) */
	GstLine **screen;       /* Current active screen */
	GstLine **primary;      /* Primary screen buffer */
	GstLine **alt;          /* Alternate screen buffer */

	/* Cursor state */
	GstCursor cursor;
	GstCursor saved_cursors[2];  /* [0]=primary, [1]=alt */
	gboolean saved_cursor_valid[2];
	gboolean saved_cursor_reflowed[2];

	/* Mode flags */
	GstTermMode mode;
	/* Independent bounded stacks; depth counts saved states, not current state. */
	guint keyboard_flags[2];
	guint keyboard_stack[2][GST_KEYBOARD_STACK_SIZE];
	guint keyboard_depth[2];

	/* Escape state (bit flags) */
	guint esc;

	/* Scroll region */
	gint scroll_top;
	gint scroll_bot;

	/* Tab stops */
	gint tabstop;
	gboolean *tabs;

	/* Charset state (G0-G3) */
	GstCharset charsets[4];
	gint charset_gl;    /* Current GL charset (0-3) */
	gint icharset;      /* Intermediate charset for ESC ( etc */

	/* CSI escape buffer */
	gchar csi_buf[CSI_BUF_SIZ + 1];
	gsize csi_len;
	gint csi_priv;      /* Private mode flag ('?') */
	gint csi_args[GST_MAX_ARGS];
	gint csi_nargs;
	gchar csi_mode[2];  /* Final command bytes */

	/* String escape (OSC/DCS/APC/PM) */
	gchar str_type;
	gchar *str_buf;
	gsize str_siz;
	gsize str_len;
	gchar *str_args[GST_MAX_ARGS];
	gint str_nargs;

	/* Window properties */
	gchar *title;
	gchar *icon;

	/* Last printed character (for REP) */
	GstRune lastc;
	GstGlyph last_glyph;
	gint cluster_x;
	gint cluster_y;
	gboolean cluster_valid;

	/* Partial UTF-8 sequence saved across write() boundaries */
	guchar utf8_partial[4];
	gint utf8_partial_len;

	/* Dirty tracking */
	gboolean dirty;
};

/* ===== Properties and Signals ===== */

enum {
	PROP_0,
	PROP_COLS,
	PROP_ROWS,
	PROP_TITLE,
	PROP_ICON,
	PROP_MODE,
	PROP_TABSTOP,
	N_PROPS
};

enum {
	SIGNAL_BELL,
	SIGNAL_TITLE_CHANGED,
	SIGNAL_ICON_CHANGED,
	SIGNAL_MODE_CHANGED,
	SIGNAL_RESIZE,
	SIGNAL_CONTENTS_CHANGED,
	SIGNAL_RESPONSE,
	SIGNAL_LINE_SCROLLED_OUT,
	SIGNAL_ESCAPE_STRING,
	SIGNAL_REGION_ERASED,
	SIGNAL_REGION_SCROLLED,
	N_SIGNALS
};

static GParamSpec *props[N_PROPS] = { NULL };
static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE(GstTerminal, gst_terminal, G_TYPE_OBJECT)

/* ===== Forward declarations ===== */

static void gst_terminal_finalize(GObject *object);
static void gst_terminal_get_property(GObject *object, guint prop_id,
                                      GValue *value, GParamSpec *pspec);
static void gst_terminal_set_property(GObject *object, guint prop_id,
                                      const GValue *value, GParamSpec *pspec);
static void gst_terminal_init_screen(GstTerminal *term);
static void gst_terminal_free_screen(GstLine **screen, gint rows);
static GstLine **gst_terminal_alloc_screen(gint cols, gint rows);

/* Escape parser internal functions */
static void term_controlcode(GstTerminal *term, guchar c);
static gint term_eschandle(GstTerminal *term, guchar c);
static void term_csiparse(GstTerminal *term);
static void term_csihandle(GstTerminal *term);
static void term_strparse(GstTerminal *term);
static void term_strhandle(GstTerminal *term);
static void term_strsequence(GstTerminal *term, guchar c);
static void term_setattr(GstTerminal *term, const gint *attr, gint l);
static void term_setmode(GstTerminal *term, gint priv, gint set,
                         const gint *args, gint narg);
static void term_setchar(GstTerminal *term, GstRune u,
                         const GstGlyph *attr, gint x, gint y);
static void term_deftran(GstTerminal *term, gchar c);
static void term_defutf8(GstTerminal *term, gchar c);
static void term_dectest(GstTerminal *term, gchar c);
static gint32 term_defcolor(const gint *attr, gint *npar, gint l);

/* ===== Class and Instance Init ===== */

static void
gst_terminal_class_init(GstTerminalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = gst_terminal_finalize;
	object_class->get_property = gst_terminal_get_property;
	object_class->set_property = gst_terminal_set_property;

	/* Properties */
	props[PROP_COLS] = g_param_spec_int(
	    "cols", "Columns", "Number of columns",
	    1, GST_MAX_COLS, GST_DEFAULT_COLS,
	    G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

	props[PROP_ROWS] = g_param_spec_int(
	    "rows", "Rows", "Number of rows",
	    1, GST_MAX_ROWS, GST_DEFAULT_ROWS,
	    G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

	props[PROP_TITLE] = g_param_spec_string(
	    "title", "Title", "Window title", NULL,
	    G_PARAM_READABLE);

	props[PROP_ICON] = g_param_spec_string(
	    "icon", "Icon", "Icon name", NULL,
	    G_PARAM_READABLE);

	props[PROP_MODE] = g_param_spec_flags(
	    "mode", "Mode", "Terminal mode flags",
	    GST_TYPE_TERM_MODE, GST_MODE_WRAP | GST_MODE_UTF8,
	    G_PARAM_READWRITE);

	props[PROP_TABSTOP] = g_param_spec_int(
	    "tabstop", "Tab Stop", "Tab stop width in columns",
	    1, 32, GST_DEFAULT_TABSTOP,
	    G_PARAM_READWRITE);

	g_object_class_install_properties(object_class, N_PROPS, props);

	/* Signals */
	signals[SIGNAL_BELL] = g_signal_new(
	    "bell", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    G_STRUCT_OFFSET(GstTerminalClass, bell),
	    NULL, NULL, NULL, G_TYPE_NONE, 0);

	signals[SIGNAL_TITLE_CHANGED] = g_signal_new(
	    "title-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    G_STRUCT_OFFSET(GstTerminalClass, title_changed),
	    NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	signals[SIGNAL_ICON_CHANGED] = g_signal_new(
	    "icon-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    G_STRUCT_OFFSET(GstTerminalClass, icon_changed),
	    NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	signals[SIGNAL_MODE_CHANGED] = g_signal_new(
	    "mode-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    G_STRUCT_OFFSET(GstTerminalClass, mode_changed),
	    NULL, NULL, NULL, G_TYPE_NONE, 2, GST_TYPE_TERM_MODE, G_TYPE_BOOLEAN);

	signals[SIGNAL_RESIZE] = g_signal_new(
	    "resize", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

	signals[SIGNAL_CONTENTS_CHANGED] = g_signal_new(
	    "contents-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/*
	 * response signal: emitted when the terminal needs to send data
	 * back to the PTY (e.g., DA responses, cursor position reports).
	 * Connect to this from the PTY to write responses.
	 */
	signals[SIGNAL_RESPONSE] = g_signal_new(
	    "response", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    0, NULL, NULL, NULL,
	    G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_LONG);

	/*
	 * line-scrolled-out signal: emitted when a line scrolls off the top
	 * of the screen. The scrollback module connects to this to capture
	 * history lines. Parameters: (GstLine *line, gint cols).
	 */
	signals[SIGNAL_LINE_SCROLLED_OUT] = g_signal_new(
	    "line-scrolled-out", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    0, NULL, NULL, NULL,
	    G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_INT);

	/*
	 * escape-string signal: emitted when a string-type escape sequence
	 * (APC, DCS, PM) is fully received and parsed.
	 * Parameters: (gchar str_type, gchar *buf, gulong len)
	 * Modules that handle escape strings (e.g. kitty graphics) connect
	 * via module manager dispatch.
	 */
	signals[SIGNAL_ESCAPE_STRING] = g_signal_new(
	    "escape-string", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    0, NULL, NULL, NULL,
	    G_TYPE_NONE, 3, G_TYPE_CHAR, G_TYPE_STRING, G_TYPE_ULONG);

	/**
	 * GstTerminal::region-erased:
	 * @term: the terminal
	 * @x1: first column, inclusive
	 * @y1: first row, inclusive
	 * @x2: last column, inclusive
	 * @y2: last row, inclusive
	 *
	 * Cells being overwritten or destroyed on the active screen. Coordinates
	 * are zero-based. Wide-cell repair is included; redraws are not erasures.
	 */
	signals[SIGNAL_REGION_ERASED] = g_signal_new(
	    "region-erased", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    0, NULL, NULL, NULL, G_TYPE_NONE, 4,
	    G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

	/**
	 * GstTerminal::region-scrolled:
	 * @term: the terminal
	 * @top: first row, inclusive
	 * @bottom: last row, inclusive
	 * @amount: positive for upward scrolling, negative for downward
	 *
	 * Moves graphics before newly exposed rows emit region-erased. Retained
	 * rows are moved, not erased. Coordinates are zero-based.
	 */
	signals[SIGNAL_REGION_SCROLLED] = g_signal_new(
	    "region-scrolled", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
	    0, NULL, NULL, NULL, G_TYPE_NONE, 3,
	    G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
}

static void
gst_terminal_init(GstTerminal *term)
{
	GstTerminalPrivate *priv;

	priv = gst_terminal_get_instance_private(term);
	term->priv = priv;

	priv->cols = GST_DEFAULT_COLS;
	priv->rows = GST_DEFAULT_ROWS;
	priv->mode = GST_MODE_WRAP | GST_MODE_UTF8;
	priv->tabstop = GST_DEFAULT_TABSTOP;
	priv->esc = 0;

	/* Initialize cursor */
	priv->cursor.x = 0;
	priv->cursor.y = 0;
	priv->cursor.state = GST_CURSOR_STATE_VISIBLE;
	priv->cursor.shape = GST_CURSOR_SHAPE_BLOCK;
	gst_glyph_reset(&priv->cursor.glyph);

	priv->saved_cursor_valid[0] = FALSE;
	priv->saved_cursor_valid[1] = FALSE;

	/* Initialize charsets */
	priv->charsets[0] = GST_CHARSET_USA;
	priv->charsets[1] = GST_CHARSET_USA;
	priv->charsets[2] = GST_CHARSET_USA;
	priv->charsets[3] = GST_CHARSET_USA;
	priv->charset_gl = 0;
	priv->icharset = 0;

	priv->screen = NULL;
	priv->primary = NULL;
	priv->alt = NULL;
	priv->title = NULL;
	priv->icon = NULL;
	priv->tabs = NULL;

	/* String escape buffer */
	priv->str_buf = NULL;
	priv->str_siz = 0;
	priv->str_len = 0;

	priv->lastc = 0;
	priv->dirty = TRUE;
}

static void
gst_terminal_finalize(GObject *object)
{
	GstTerminal *term = GST_TERMINAL(object);
	GstTerminalPrivate *priv = term->priv;

	if (priv->primary != NULL) {
		gst_terminal_free_screen(priv->primary, priv->rows);
	}
	if (priv->alt != NULL) {
		gst_terminal_free_screen(priv->alt, priv->rows);
	}

	g_free(priv->title);
	g_free(priv->icon);
	g_free(priv->tabs);
	g_free(priv->str_buf);
	gst_glyph_clear(&priv->last_glyph);
	gst_glyph_clear(&priv->cursor.glyph);
	gst_glyph_clear(&priv->saved_cursors[0].glyph);
	gst_glyph_clear(&priv->saved_cursors[1].glyph);

	G_OBJECT_CLASS(gst_terminal_parent_class)->finalize(object);
}

static void
gst_terminal_get_property(
    GObject     *object,
    guint       prop_id,
    GValue      *value,
    GParamSpec  *pspec
){
	GstTerminal *term = GST_TERMINAL(object);
	GstTerminalPrivate *priv = term->priv;

	switch (prop_id) {
	case PROP_COLS:
		g_value_set_int(value, priv->cols);
		break;
	case PROP_ROWS:
		g_value_set_int(value, priv->rows);
		break;
	case PROP_TITLE:
		g_value_set_string(value, priv->title);
		break;
	case PROP_ICON:
		g_value_set_string(value, priv->icon);
		break;
	case PROP_MODE:
		g_value_set_flags(value, priv->mode);
		break;
	case PROP_TABSTOP:
		g_value_set_int(value, priv->tabstop);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
gst_terminal_set_property(
    GObject         *object,
    guint           prop_id,
    const GValue    *value,
    GParamSpec      *pspec
){
	GstTerminal *term = GST_TERMINAL(object);
	GstTerminalPrivate *priv = term->priv;

	switch (prop_id) {
	case PROP_COLS:
		{
			gint new_cols = g_value_get_int(value);
			if (new_cols != priv->cols) {
				gst_terminal_resize(term, new_cols, priv->rows);
			}
		}
		break;
	case PROP_ROWS:
		{
			gint new_rows = g_value_get_int(value);
			if (new_rows != priv->rows) {
				gst_terminal_resize(term, priv->cols, new_rows);
			}
		}
		break;
	case PROP_MODE:
		priv->mode = g_value_get_flags(value);
		break;
	case PROP_TABSTOP:
		priv->tabstop = g_value_get_int(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

/* ===== Screen Buffer Management ===== */

static GstLine **
gst_terminal_alloc_screen(
    gint cols,
    gint rows
){
	GstLine **screen;
	gint i;

	screen = g_new(GstLine *, rows);
	for (i = 0; i < rows; i++) {
		screen[i] = gst_line_new(cols);
	}
	return screen;
}

static void
gst_terminal_free_screen(
    GstLine **screen,
    gint    rows
){
	gint i;

	if (screen == NULL) {
		return;
	}
	for (i = 0; i < rows; i++) {
		gst_line_free(screen[i]);
	}
	g_free(screen);
}

static void
gst_terminal_init_screen(GstTerminal *term)
{
	GstTerminalPrivate *priv = term->priv;

	if (priv->primary != NULL) {
		return;
	}

	priv->primary = gst_terminal_alloc_screen(priv->cols, priv->rows);
	priv->alt = gst_terminal_alloc_screen(priv->cols, priv->rows);
	priv->screen = priv->primary;

	priv->scroll_top = 0;
	priv->scroll_bot = priv->rows - 1;

	priv->tabs = g_new0(gboolean, priv->cols);
	{
		gint i;
		for (i = priv->tabstop; i < priv->cols; i += priv->tabstop) {
			priv->tabs[i] = TRUE;
		}
	}
}

/* ===== Helper: send response to PTY ===== */

static void
term_response(
    GstTerminal *term,
    const gchar *data,
    gssize      len
){
	if (len < 0) {
		len = (gssize)strlen(data);
	}
	g_signal_emit(term, signals[SIGNAL_RESPONSE], 0, data, (glong)len);
}

/* ===== Construction ===== */

GstTerminal *
gst_terminal_new(
    gint cols,
    gint rows
){
	GstTerminal *term;

	g_return_val_if_fail(cols > 0 && cols <= GST_MAX_COLS, NULL);
	g_return_val_if_fail(rows > 0 && rows <= GST_MAX_ROWS, NULL);

	term = g_object_new(GST_TYPE_TERMINAL,
	                     "cols", cols,
	                     "rows", rows,
	                     NULL);

	gst_terminal_init_screen(term);
	return term;
}

/* ===== Dimensions ===== */

/* Repack only soft-wrapped primary paragraphs; hard line breaks survive. */
static GstLine **
term_reflow_primary(GstTerminal *term, gint cols, gint rows)
{
	GstTerminalPrivate *priv;
	GPtrArray *packed;
	GstLine **screen;
	GstLine *out;
	GstCursor *cursors[3];
	GstCursor cluster_cursor = { 0 };
	gint mapped_x[3];
	gint mapped_y[3];
	gint last_row;
	gint y, x, end, pos, width, k, drop;
	gboolean wrapped;

	priv = term->priv;
	cursors[0] = (priv->mode & GST_MODE_ALTSCREEN) ? NULL : &priv->cursor;
	cursors[1] = priv->saved_cursor_valid[0] ? &priv->saved_cursors[0] : NULL;
	cluster_cursor.x = priv->cluster_x;
	cluster_cursor.y = priv->cluster_y;
	cursors[2] = priv->cluster_valid && cursors[0] != NULL ? &cluster_cursor : NULL;
	for (k = 0; k < 3; k++) {
		mapped_x[k] = 0;
		mapped_y[k] = 0;
	}
	/* Remove unused bottom padding, never content or cursor-bearing rows. */
	last_row = priv->rows - 1;
	while (last_row > 0) {
		GstLine *line;
		gboolean used;

		line = priv->primary[last_row];
		used = line->used > 0;
		for (x = 0; x < line->len; x++) {
			GstGlyph *g = &line->glyphs[x];
			if (!gst_glyph_is_empty(g) || g->attr != 0 ||
			    g->bg != GST_COLOR_DEFAULT_BG || g->fg != GST_COLOR_DEFAULT_FG) {
				used = TRUE;
				break;
			}
		}
		for (k = 0; k < 3; k++) {
			if (cursors[k] != NULL && cursors[k]->y >= last_row) {
				used = TRUE;
			}
		}
		if (used || gst_line_is_wrapped(priv->primary[last_row - 1])) {
			break;
		}
		last_row--;
	}
	packed = g_ptr_array_new();
	out = gst_line_new(cols);
	g_ptr_array_add(packed, out);
	pos = 0;
	for (y = 0; y <= last_row; y++) {
		GstLine *line = priv->primary[y];

		wrapped = gst_line_is_wrapped(line) ||
		    (line->glyphs[line->len - 1].attr & GST_GLYPH_ATTR_WRAP);
		end = line->len;
		if (!wrapped) {
			while (end > 0) {
				GstGlyph *g = &line->glyphs[end - 1];
				if (!gst_glyph_is_empty(g) || g->attr != 0 ||
				    g->bg != GST_COLOR_DEFAULT_BG || g->fg != GST_COLOR_DEFAULT_FG) {
					break;
				}
				end--;
			}
			end = MAX(end, line->used);
		}
		for (k = 0; k < 3; k++) {
			if (cursors[k] != NULL && cursors[k]->y == y) {
				end = MAX(end, cursors[k]->x +
				    ((cursors[k]->state & GST_CURSOR_STATE_WRAPNEXT) ? 1 : 0));
			}
		}
		for (x = 0; x <= end; x++) {
			GstGlyph *g;

			g = x < end ? &line->glyphs[x] : NULL;
			width = g != NULL && (g->attr & GST_GLYPH_ATTR_WIDE) ? MIN(2, cols) : 1;
			if (g != NULL && (g->attr & GST_GLYPH_ATTR_WDUMMY)) {
				continue;
			}
			/* Padding has an insertion anchor but consumes no paragraph cells. */
			if (g != NULL && g->rune == 0 && (g->attr & GST_GLYPH_ATTR_WRAP)) {
				width = 0;
			}
			if (g != NULL && pos + width > cols) {
				gst_line_set_wrapped(out, TRUE);
				out->glyphs[cols - 1].attr |= GST_GLYPH_ATTR_WRAP;
				if (pos < cols) {
					out->glyphs[pos].rune = 0;
				}
				out = gst_line_new(cols);
				g_ptr_array_add(packed, out);
				pos = 0;
			}
			for (k = 0; k < 3; k++) {
				gint anchor;

				if (cursors[k] == NULL || cursors[k]->y != y) {
					continue;
				}
				anchor = cursors[k]->x +
				    ((cursors[k]->state & GST_CURSOR_STATE_WRAPNEXT) ? 1 : 0);
				if (anchor == x || (g != NULL && (g->attr & GST_GLYPH_ATTR_WIDE) &&
				    x + 1 < line->len && anchor == x + 1)) {
					mapped_x[k] = pos + MIN(anchor - x, MAX(width - 1, 0));
					mapped_y[k] = (gint)packed->len - 1;
				}
			}
			if (g == NULL) {
				break;
			}
			if (width == 0) {
				continue;
			}
			gst_glyph_assign(&out->glyphs[pos], g);
			out->glyphs[pos].attr &= ~GST_GLYPH_ATTR_WRAP;
			if (width == 2) {
				out->glyphs[pos + 1].rune = 0;
				out->glyphs[pos + 1].attr = GST_GLYPH_ATTR_WDUMMY;
				out->glyphs[pos + 1].fg = g->fg;
				out->glyphs[pos + 1].bg = g->bg;
			}
			pos += width;
			out->used = pos;
		}
		if (!wrapped && y < last_row) {
			out = gst_line_new(cols);
			g_ptr_array_add(packed, out);
			pos = 0;
		}
	}
	drop = MAX(0, (gint)packed->len - rows);
	/* Signal arguments describe the new-width line, not the old grid. */
	for (y = 0; y < drop; y++) {
		out = (GstLine *)g_ptr_array_index(packed, y);
		g_signal_emit(term, signals[SIGNAL_LINE_SCROLLED_OUT], 0, out, cols);
		gst_line_free(out);
	}
	screen = g_new(GstLine *, rows);
	for (y = 0; y < rows; y++) {
		screen[y] = y + drop < (gint)packed->len
		    ? (GstLine *)g_ptr_array_index(packed, y + drop) : gst_line_new(cols);
	}
	for (k = 0; k < 3; k++) {
		if (cursors[k] != NULL) {
			cursors[k]->x = MIN(mapped_x[k], cols - 1);
			cursors[k]->y = CLAMP(mapped_y[k] - drop, 0, rows - 1);
			cursors[k]->state &= ~GST_CURSOR_STATE_WRAPNEXT;
			if (mapped_x[k] == cols && (priv->mode & GST_MODE_WRAP)) {
				cursors[k]->state |= GST_CURSOR_STATE_WRAPNEXT;
			}
		}
	}
	priv->cluster_valid = cursors[2] != NULL && mapped_y[2] >= drop;
	if (cursors[1] != NULL) {
		priv->saved_cursor_reflowed[0] = TRUE;
	}
	priv->cluster_x = cluster_cursor.x;
	priv->cluster_y = cluster_cursor.y;
	g_ptr_array_free(packed, TRUE);
	return screen;
}

void
gst_terminal_resize(
    GstTerminal *term,
    gint        cols,
    gint        rows
){
	GstTerminalPrivate *priv;
	GstLine **new_primary;
	GstLine **new_alt;
	gint copy_rows;
	gint i;
	gint x;

	g_return_if_fail(GST_IS_TERMINAL(term));
	g_return_if_fail(cols > 0 && cols <= GST_MAX_COLS);
	g_return_if_fail(rows > 0 && rows <= GST_MAX_ROWS);

	priv = term->priv;

	if (cols == priv->cols && rows == priv->rows) {
		return;
	}

	gst_terminal_init_screen(term);

	new_primary = term_reflow_primary(term, cols, rows);
	new_alt = gst_terminal_alloc_screen(cols, rows);

	copy_rows = MIN(priv->rows, rows);

	for (i = 0; i < copy_rows; i++) {
		gst_line_free(new_alt[i]);
		new_alt[i] = gst_line_copy(priv->alt[i]);
		gst_line_resize(new_alt[i], cols);
		/* Alternate is a fixed grid: no logical reflow or history. */
		gst_line_set_wrapped(new_alt[i], FALSE);
		for (x = 0; x < cols; x++) {
			new_alt[i]->glyphs[x].attr &= ~GST_GLYPH_ATTR_WRAP;
		}
		if (new_alt[i]->glyphs[cols - 1].attr & GST_GLYPH_ATTR_WIDE) {
			gst_glyph_reset(&new_alt[i]->glyphs[cols - 1]);
		}
	}

	gst_terminal_free_screen(priv->primary, priv->rows);
	gst_terminal_free_screen(priv->alt, priv->rows);

	priv->primary = new_primary;
	priv->alt = new_alt;
	priv->screen = (priv->mode & GST_MODE_ALTSCREEN) ? priv->alt : priv->primary;

	priv->cols = cols;
	priv->rows = rows;
	priv->scroll_top = 0;
	priv->scroll_bot = rows - 1;

	priv->cursor.x = MIN(priv->cursor.x, cols - 1);
	priv->cursor.y = MIN(priv->cursor.y, rows - 1);
	if (priv->mode & GST_MODE_ALTSCREEN) {
		priv->cursor.state &= ~GST_CURSOR_STATE_WRAPNEXT;
	}
	priv->saved_cursors[1].x = MIN(priv->saved_cursors[1].x, cols - 1);
	priv->saved_cursors[1].y = MIN(priv->saved_cursors[1].y, rows - 1);
	priv->saved_cursors[1].state &= ~GST_CURSOR_STATE_WRAPNEXT;

	g_free(priv->tabs);
	priv->tabs = g_new0(gboolean, cols);
	for (i = priv->tabstop; i < cols; i += priv->tabstop) {
		priv->tabs[i] = TRUE;
	}

	priv->dirty = TRUE;
	g_signal_emit(term, signals[SIGNAL_RESIZE], 0, cols, rows);
}

void
gst_terminal_get_size(
    GstTerminal *term,
    gint        *cols,
    gint        *rows
){
	g_return_if_fail(GST_IS_TERMINAL(term));
	if (cols != NULL) *cols = term->priv->cols;
	if (rows != NULL) *rows = term->priv->rows;
}

gint
gst_terminal_get_cols(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), 0);
	return term->priv->cols;
}

gint
gst_terminal_get_rows(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), 0);
	return term->priv->rows;
}

/* ===== Cursor Movement ===== */

/**
 * gst_terminal_move_to:
 *
 * Move cursor with bounds checking. ORIGIN mode constrains to scroll region.
 */
void
gst_terminal_move_to(
    GstTerminal *term,
    gint        x,
    gint        y
){
	GstTerminalPrivate *priv;
	gint miny, maxy;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	if (priv->cursor.state & GST_CURSOR_STATE_ORIGIN) {
		miny = priv->scroll_top;
		maxy = priv->scroll_bot;
	} else {
		miny = 0;
		maxy = priv->rows - 1;
	}

	priv->cursor.state &= ~GST_CURSOR_STATE_WRAPNEXT;
	priv->cluster_valid = FALSE;
	priv->cursor.x = CLAMP(x, 0, priv->cols - 1);
	priv->cursor.y = CLAMP(y, miny, maxy);
}

/**
 * gst_terminal_move_to_abs:
 *
 * Move cursor to absolute position, adjusted for ORIGIN mode.
 */
void
gst_terminal_move_to_abs(
    GstTerminal *term,
    gint        x,
    gint        y
){
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	gst_terminal_move_to(term, x,
	    CLAMP(y, 0, priv->rows - 1) +
	    ((priv->cursor.state & GST_CURSOR_STATE_ORIGIN) ? priv->scroll_top : 0));
}

void
gst_terminal_set_cursor_pos(
    GstTerminal *term,
    gint        x,
    gint        y
){
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	priv->cursor.x = CLAMP(x, 0, priv->cols - 1);
	priv->cluster_valid = FALSE;
	priv->cursor.y = CLAMP(y, 0, priv->rows - 1);
	priv->cursor.state &= ~GST_CURSOR_STATE_WRAPNEXT;
}

GstCursor *
gst_terminal_get_cursor(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), NULL);
	return &term->priv->cursor;
}

void
gst_terminal_cursor_save(GstTerminal *term)
{
	GstTerminalPrivate *priv;
	gint idx;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	idx = (priv->mode & GST_MODE_ALTSCREEN) ? 1 : 0;
	gst_cursor_restore(&priv->saved_cursors[idx], &priv->cursor);
	priv->saved_cursor_valid[idx] = TRUE;
	priv->saved_cursor_reflowed[idx] = FALSE;
}

void
gst_terminal_cursor_restore(GstTerminal *term)
{
	GstTerminalPrivate *priv;
	gint idx;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	idx = (priv->mode & GST_MODE_ALTSCREEN) ? 1 : 0;
	if (priv->saved_cursor_valid[idx]) {
		gboolean pending = priv->saved_cursor_reflowed[idx] &&
		    (priv->saved_cursors[idx].state & GST_CURSOR_STATE_WRAPNEXT) != 0;

		gst_cursor_restore(&priv->cursor, &priv->saved_cursors[idx]);
		/* Reflow can map an insertion point exactly onto the new right edge.
		 * Do not turn that saved insertion point into an overwrite on restore. */
		gst_terminal_move_to(term, priv->cursor.x, priv->cursor.y);
		if (pending && priv->cursor.x == priv->cols - 1 && (priv->mode & GST_MODE_WRAP)) {
			priv->cursor.state |= GST_CURSOR_STATE_WRAPNEXT;
		}
	}
}

/* ===== Screen Buffer Access ===== */

GstLine *
gst_terminal_get_line(
    GstTerminal *term,
    gint        row
){
	g_return_val_if_fail(GST_IS_TERMINAL(term), NULL);
	gst_terminal_init_screen(term);
	if (row < 0 || row >= term->priv->rows) {
		return NULL;
	}
	return term->priv->screen[row];
}

GstGlyph *
gst_terminal_get_glyph(
    GstTerminal *term,
    gint        col,
    gint        row
){
	GstLine *line;

	g_return_val_if_fail(GST_IS_TERMINAL(term), NULL);
	line = gst_terminal_get_line(term, row);
	if (line == NULL) {
		return NULL;
	}
	return gst_line_get_glyph(line, col);
}

gint
gst_terminal_line_len(
    GstTerminal *term,
    gint        row
){
	GstTerminalPrivate *priv;
	GstLine *line;
	gint i;

	g_return_val_if_fail(GST_IS_TERMINAL(term), 0);
	priv = term->priv;
	gst_terminal_init_screen(term);

	if (row < 0 || row >= priv->rows) {
		return 0;
	}

	line = priv->screen[row];

	/* If wrapped, line uses full width */
	if (line->glyphs[line->len - 1].attr & GST_GLYPH_ATTR_WRAP) {
		return line->len;
	}

	/* Find last non-space */
	i = line->len;
	while (i > 0 && line->glyphs[i - 1].rune == ' ' &&
	       line->glyphs[i - 1].cluster == NULL) {
		i--;
	}
	return i;
}

/* ===== Mode Management ===== */

GstTermMode
gst_terminal_get_mode(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), 0);
	return term->priv->mode;
}

void
gst_terminal_set_mode(
    GstTerminal *term,
    GstTermMode mode,
    gboolean    enable
){
	GstTerminalPrivate *priv;
	GstTermMode old_mode;

	g_return_if_fail(GST_IS_TERMINAL(term));

	priv = term->priv;
	old_mode = priv->mode;

	if (enable) {
		priv->mode |= mode;
	} else {
		priv->mode &= ~mode;
	}

	/* Handle altscreen toggle */
	if ((mode & GST_MODE_ALTSCREEN) &&
	    (old_mode & GST_MODE_ALTSCREEN) != (priv->mode & GST_MODE_ALTSCREEN)) {
		/* swap_screen updates the bit itself along with the active buffer. */
		priv->mode ^= GST_MODE_ALTSCREEN;
		gst_terminal_swap_screen(term);
	}

	/* ALTSCREEN was already reported by swap_screen, exactly once. */
	if ((old_mode ^ priv->mode) & ~GST_MODE_ALTSCREEN) {
		g_signal_emit(term, signals[SIGNAL_MODE_CHANGED], 0,
		    mode & ~GST_MODE_ALTSCREEN, enable);
	}
}

gboolean
gst_terminal_has_mode(
    GstTerminal *term,
    GstTermMode mode
){
	g_return_val_if_fail(GST_IS_TERMINAL(term), FALSE);
	return (term->priv->mode & mode) == mode;
}

void
gst_terminal_swap_screen(GstTerminal *term)
{
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	if (priv->screen == priv->primary) {
		priv->screen = priv->alt;
	} else {
		priv->screen = priv->primary;
	}

	/* Switching buffers is not a text erasure. */
	priv->mode ^= GST_MODE_ALTSCREEN;
	priv->cluster_valid = FALSE;
	priv->dirty = TRUE;
	gst_terminal_mark_dirty(term, -1);
	g_signal_emit(term, signals[SIGNAL_MODE_CHANGED], 0,
	    GST_MODE_ALTSCREEN, (priv->mode & GST_MODE_ALTSCREEN) != 0);
}

/* ===== Screen Manipulation ===== */

void
gst_terminal_reset(
    GstTerminal *term,
    gboolean    full
){
	GstTerminalPrivate *priv;
	gint i;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	if (priv->mode & GST_MODE_ALTSCREEN) {
		gst_terminal_swap_screen(term);
	}
	memset(priv->keyboard_flags, 0, sizeof(priv->keyboard_flags));
	memset(priv->keyboard_depth, 0, sizeof(priv->keyboard_depth));
	memset(priv->saved_cursor_reflowed, 0, sizeof(priv->saved_cursor_reflowed));

	priv->cursor.x = 0;
	priv->cursor.y = 0;
	priv->cursor.state = GST_CURSOR_STATE_VISIBLE;
	priv->cursor.shape = GST_CURSOR_SHAPE_BLOCK;
	gst_glyph_reset(&priv->cursor.glyph);

	priv->mode = GST_MODE_WRAP | GST_MODE_UTF8;
	priv->esc = 0;
	/* No input bytes or repeat character survive a terminal reset. */
	priv->utf8_partial_len = 0;
	priv->lastc = 0;
	gst_glyph_reset(&priv->last_glyph);
	priv->cluster_valid = FALSE;
	priv->scroll_top = 0;
	priv->scroll_bot = priv->rows - 1;

	priv->charsets[0] = GST_CHARSET_USA;
	priv->charsets[1] = GST_CHARSET_USA;
	priv->charsets[2] = GST_CHARSET_USA;
	priv->charsets[3] = GST_CHARSET_USA;
	priv->charset_gl = 0;
	priv->icharset = 0;

	priv->saved_cursor_valid[0] = FALSE;
	priv->saved_cursor_valid[1] = FALSE;

	gst_terminal_init_screen(term);
	gst_cursor_reset(&priv->saved_cursors[0]);
	gst_cursor_reset(&priv->saved_cursors[1]);
	priv->screen = priv->primary;

	/* Reset tabs */
	if (priv->tabs != NULL) {
		memset(priv->tabs, 0, sizeof(gboolean) * priv->cols);
		for (i = priv->tabstop; i < priv->cols; i += priv->tabstop) {
			priv->tabs[i] = TRUE;
		}
	}

	if (full) {
		/* Clear both screens */
		g_signal_emit(term, signals[SIGNAL_REGION_ERASED], 0,
		    0, 0, priv->cols - 1, priv->rows - 1);
		for (i = 0; i < priv->rows; i++) {
			gst_line_clear(priv->primary[i]);
			gst_line_clear(priv->alt[i]);
		}
	}

	priv->dirty = TRUE;
}

void
gst_terminal_clear(GstTerminal *term)
{
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	gst_terminal_clear_region(term, 0, 0, priv->cols - 1, priv->rows - 1);
}

void
gst_terminal_clear_region(
    GstTerminal *term,
    gint        x1,
    gint        y1,
    gint        x2,
    gint        y2
){
	GstTerminalPrivate *priv;
	gint tmp, x, y;
	GstGlyph blank;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
	if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }

	x1 = CLAMP(x1, 0, priv->cols - 1);
	x2 = CLAMP(x2, 0, priv->cols - 1);
	y1 = CLAMP(y1, 0, priv->rows - 1);
	y2 = CLAMP(y2, 0, priv->rows - 1);

	/* BCE blanks retain SGR colors, but never text or wide-cell attributes. */
	blank.rune = ' ';
	blank.attr = 0;
	blank.fg = priv->cursor.glyph.fg;
	blank.bg = priv->cursor.glyph.bg;
	blank.cluster = NULL;
	blank.cluster_len = 0;
	blank.cluster_capacity = 0;
	priv->cluster_valid = FALSE;
	for (y = y1; y <= y2; y++) {
		gint start, end, used;

		start = x1;
		end = x2;
		used = priv->screen[y]->used;
		if (start > 0 && (priv->screen[y]->glyphs[start].attr & GST_GLYPH_ATTR_WDUMMY)) {
			start--;
		}
		if (end + 1 < priv->cols && (priv->screen[y]->glyphs[end].attr & GST_GLYPH_ATTR_WIDE)) {
			end++;
		}
		g_signal_emit(term, signals[SIGNAL_REGION_ERASED], 0, start, y, end, y);
		for (x = start; x <= end; x++) {
			gst_line_set_glyph(priv->screen[y], x, &blank);
		}
		priv->screen[y]->used = end + 1 >= used ? MIN(used, start) : used;
		if (end == priv->cols - 1) {
			gst_line_set_wrapped(priv->screen[y], FALSE);
		}
	}
	priv->dirty = TRUE;
}

void
gst_terminal_scroll_up(
    GstTerminal *term,
    gint        orig,
    gint        n
){
	GstTerminalPrivate *priv;
	gint i;
	GstLine *tmp;

	g_return_if_fail(GST_IS_TERMINAL(term));
	g_return_if_fail(n > 0);

	priv = term->priv;
	gst_terminal_init_screen(term);

	orig = CLAMP(orig, priv->scroll_top, priv->scroll_bot);
	n = MIN(n, priv->scroll_bot - orig + 1);

	/* Emit line-scrolled-out for lines about to be overwritten */
	if (orig == 0 && priv->scroll_top == 0 &&
	    priv->scroll_bot == priv->rows - 1 &&
	    !(priv->mode & GST_MODE_ALTSCREEN))
	{
		for (i = orig; i < orig + n; i++)
		{
			g_signal_emit(term, signals[SIGNAL_LINE_SCROLLED_OUT], 0,
				priv->screen[i], priv->cols);
		}
	}

	/* Rotate lines up within the scroll region */
	g_signal_emit(term, signals[SIGNAL_REGION_SCROLLED], 0,
	    orig, priv->scroll_bot, n);
	for (i = orig; i <= priv->scroll_bot - n; i++) {
		tmp = priv->screen[i];
		priv->screen[i] = priv->screen[i + n];
		priv->screen[i + n] = tmp;
		gst_line_set_dirty(priv->screen[i], TRUE);
	}

	/* Clear the bottom lines */
	gst_terminal_clear_region(term, 0, priv->scroll_bot - n + 1,
	    priv->cols - 1, priv->scroll_bot);
	for (i = priv->scroll_bot - n + 1; i <= priv->scroll_bot; i++) {
		gst_line_set_wrapped(priv->screen[i], FALSE);
	}

	priv->dirty = TRUE;
}

void
gst_terminal_scroll_down(
    GstTerminal *term,
    gint        orig,
    gint        n
){
	GstTerminalPrivate *priv;
	gint i;
	GstLine *tmp;

	g_return_if_fail(GST_IS_TERMINAL(term));
	g_return_if_fail(n > 0);

	priv = term->priv;
	gst_terminal_init_screen(term);

	orig = CLAMP(orig, priv->scroll_top, priv->scroll_bot);
	n = MIN(n, priv->scroll_bot - orig + 1);

	g_signal_emit(term, signals[SIGNAL_REGION_SCROLLED], 0,
	    orig, priv->scroll_bot, -n);
	for (i = priv->scroll_bot; i >= orig + n; i--) {
		tmp = priv->screen[i];
		priv->screen[i] = priv->screen[i - n];
		priv->screen[i - n] = tmp;
		gst_line_set_dirty(priv->screen[i], TRUE);
	}

	gst_terminal_clear_region(term, 0, orig, priv->cols - 1, orig + n - 1);
	for (i = orig; i < orig + n; i++) {
		gst_line_set_wrapped(priv->screen[i], FALSE);
	}

	priv->dirty = TRUE;
}

void
gst_terminal_newline(
    GstTerminal *term,
    gboolean    first_col
){
	GstTerminalPrivate *priv;
	gint y;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	y = priv->cursor.y;

	if (y == priv->scroll_bot) {
		gst_terminal_scroll_up(term, priv->scroll_top, 1);
	} else {
		y++;
	}

	gst_terminal_move_to(term, first_col ? 0 : priv->cursor.x, y);
}

void
gst_terminal_insert_blanks(
    GstTerminal *term,
    gint        n
){
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	n = CLAMP(n, 0, priv->cols - priv->cursor.x);
	if (n == 0) {
		return;
	}
	/* Shifting text destroys ordinary screen-coordinate placements. */
	g_signal_emit(term, signals[SIGNAL_REGION_ERASED], 0,
	    priv->cursor.x > 0 && (priv->screen[priv->cursor.y]->glyphs[priv->cursor.x].attr & GST_GLYPH_ATTR_WDUMMY)
	        ? priv->cursor.x - 1 : priv->cursor.x,
	    priv->cursor.y, priv->cols - 1, priv->cursor.y);
	gst_line_insert_blanks(priv->screen[priv->cursor.y], priv->cursor.x, n);
	gst_terminal_clear_region(term, priv->cursor.x, priv->cursor.y,
	    priv->cursor.x + n - 1, priv->cursor.y);
}

void
gst_terminal_delete_chars(
    GstTerminal *term,
    gint        n
){
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	n = CLAMP(n, 0, priv->cols - priv->cursor.x);
	if (n == 0) {
		return;
	}
	g_signal_emit(term, signals[SIGNAL_REGION_ERASED], 0,
	    priv->cursor.x > 0 && (priv->screen[priv->cursor.y]->glyphs[priv->cursor.x].attr & GST_GLYPH_ATTR_WDUMMY)
	        ? priv->cursor.x - 1 : priv->cursor.x,
	    priv->cursor.y, priv->cols - 1, priv->cursor.y);
	gst_line_delete_chars(priv->screen[priv->cursor.y], priv->cursor.x, n);
	gst_terminal_clear_region(term, priv->cols - n, priv->cursor.y,
	    priv->cols - 1, priv->cursor.y);
}

void
gst_terminal_insert_blank_lines(
    GstTerminal *term,
    gint        n
){
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	if (BETWEEN(priv->cursor.y, priv->scroll_top, priv->scroll_bot)) {
		gst_terminal_scroll_down(term, priv->cursor.y, n);
	}
}

void
gst_terminal_delete_lines(
    GstTerminal *term,
    gint        n
){
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	if (BETWEEN(priv->cursor.y, priv->scroll_top, priv->scroll_bot)) {
		gst_terminal_scroll_up(term, priv->cursor.y, n);
	}
}

void
gst_terminal_put_tab(
    GstTerminal *term,
    gint        n
){
	GstTerminalPrivate *priv;
	gint x;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	x = priv->cursor.x;
	priv->cluster_valid = FALSE;

	if (n > 0) {
		while (x < priv->cols && n--) {
			for (++x; x < priv->cols && !priv->tabs[x]; ++x)
				;
		}
	} else if (n < 0) {
		while (x > 0 && n++) {
			for (--x; x > 0 && !priv->tabs[x]; --x)
				;
		}
	}

	priv->cursor.x = CLAMP(x, 0, priv->cols - 1);
}

/* ===== Scroll Region ===== */

void
gst_terminal_set_scroll_region(
    GstTerminal *term,
    gint        top,
    gint        bottom
){
	GstTerminalPrivate *priv;
	gint tmp;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	top = CLAMP(top, 0, priv->rows - 1);
	bottom = CLAMP(bottom, 0, priv->rows - 1);

	if (top > bottom) {
		tmp = top;
		top = bottom;
		bottom = tmp;
	}

	priv->scroll_top = top;
	priv->scroll_bot = bottom;
}

void
gst_terminal_get_scroll_region(
    GstTerminal *term,
    gint        *top,
    gint        *bottom
){
	g_return_if_fail(GST_IS_TERMINAL(term));
	if (top != NULL) *top = term->priv->scroll_top;
	if (bottom != NULL) *bottom = term->priv->scroll_bot;
}

/* ===== Tab Stops ===== */

gint
gst_terminal_get_tabstop(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), GST_DEFAULT_TABSTOP);
	return term->priv->tabstop;
}

void
gst_terminal_set_tabstop(
    GstTerminal *term,
    gint        tabstop
){
	GstTerminalPrivate *priv;
	gint i;

	g_return_if_fail(GST_IS_TERMINAL(term));
	g_return_if_fail(tabstop >= 1 && tabstop <= 32);

	priv = term->priv;
	priv->tabstop = tabstop;

	if (priv->tabs != NULL) {
		memset(priv->tabs, 0, sizeof(gboolean) * priv->cols);
		for (i = tabstop; i < priv->cols; i += tabstop) {
			priv->tabs[i] = TRUE;
		}
	}
}

/* ===== Window Properties ===== */

const gchar *
gst_terminal_get_title(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), NULL);
	return term->priv->title;
}

const gchar *
gst_terminal_get_icon(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), NULL);
	return term->priv->icon;
}

void
gst_terminal_set_title(
    GstTerminal *term,
    const gchar *title
){
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	g_free(priv->title);
	priv->title = g_strdup(title);
	g_signal_emit(term, signals[SIGNAL_TITLE_CHANGED], 0, title);
}

void
gst_terminal_set_icon(
    GstTerminal *term,
    const gchar *icon
){
	GstTerminalPrivate *priv;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;

	g_free(priv->icon);
	priv->icon = g_strdup(icon);
	g_signal_emit(term, signals[SIGNAL_ICON_CHANGED], 0, icon);
}

/* ===== Dirty Tracking ===== */

gboolean
gst_terminal_is_dirty(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), FALSE);
	return term->priv->dirty;
}

void
gst_terminal_mark_dirty(
    GstTerminal *term,
    gint        row
){
	GstTerminalPrivate *priv;
	gint i;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	if (row < 0) {
		for (i = 0; i < priv->rows; i++) {
			gst_line_set_dirty(priv->screen[i], TRUE);
		}
	} else if (row < priv->rows) {
		gst_line_set_dirty(priv->screen[row], TRUE);
	}
	priv->dirty = TRUE;
}

void
gst_terminal_clear_dirty(GstTerminal *term)
{
	GstTerminalPrivate *priv;
	gint i;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	for (i = 0; i < priv->rows; i++) {
		gst_line_set_dirty(priv->screen[i], FALSE);
	}
	priv->dirty = FALSE;
}

gboolean
gst_terminal_is_altscreen(GstTerminal *term)
{
	g_return_val_if_fail(GST_IS_TERMINAL(term), FALSE);
	return (term->priv->mode & GST_MODE_ALTSCREEN) != 0;
}

/* ===== Character Placement ===== */

/*
 * term_setchar:
 *
 * Place a character on screen with charset translation and
 * wide character handling. Direct port of st's tsetchar().
 */
static void
term_setchar(
    GstTerminal *term,
    GstRune     u,
    const GstGlyph *attr,
    gint        x,
    gint        y
){
	GstTerminalPrivate *priv = term->priv;
	GstLine *line;
	GstGlyph *g;

	/* VT100 graphics charset translation */
	if (priv->charsets[priv->charset_gl] == GST_CHARSET_GRAPHIC0) {
		if (BETWEEN(u, 0x41, 0x7e) && vt100_graphic0[u - 0x41] != 0) {
			u = vt100_graphic0[u - 0x41];
		}
	}

	if (y < 0 || y >= priv->rows || x < 0 || x >= priv->cols) {
		return;
	}

	line = priv->screen[y];
	g = gst_line_get_glyph(line, x);
	if (g == NULL) {
		return;
	}

	/* Handle wide character cleanup */
	g_signal_emit(term, signals[SIGNAL_REGION_ERASED], 0,
	    x > 0 && (g->attr & GST_GLYPH_ATTR_WDUMMY) ? x - 1 : x, y,
	    x + 1 < priv->cols && (g->attr & GST_GLYPH_ATTR_WIDE) ? x + 1 : x, y);
	if (g->attr & GST_GLYPH_ATTR_WIDE) {
		/* Current cell is wide; blank the dummy cell */
		if (x + 1 < priv->cols) {
			GstGlyph *next = gst_line_get_glyph(line, x + 1);
			if (next != NULL) {
				gst_glyph_clear(next);
				next->rune = ' ';
				next->attr &= ~GST_GLYPH_ATTR_WDUMMY;
			}
		}
	} else if (g->attr & GST_GLYPH_ATTR_WDUMMY) {
		/* Current cell is a dummy; blank the wide cell */
		if (x > 0) {
			GstGlyph *prev = gst_line_get_glyph(line, x - 1);
			if (prev != NULL) {
				gst_glyph_clear(prev);
				prev->rune = ' ';
				prev->attr &= ~GST_GLYPH_ATTR_WIDE;
			}
		}
	}

	gst_line_set_dirty(line, TRUE);
	line->used = MAX(line->used, x + 1);

	gst_glyph_clear(g);
	g->rune = u;
	g->attr = attr->attr;
	g->fg = attr->fg;
	g->bg = attr->bg;
}

/* ===== Escape Parser Internal Functions ===== */

/*
 * term_defcolor:
 *
 * Parse extended color codes (38;2;r;g;b or 38;5;n).
 * Returns color value or -1 on error.
 */
static gint32
term_defcolor(
    const gint  *attr,
    gint        *npar,
    gint        l
){
	gint32 idx = -1;
	gint r, g, b;

	switch (attr[*npar + 1]) {
	case 2: /* direct color: 38;2;r;g;b */
		if (*npar + 4 >= l) {
			break;
		}
		r = attr[*npar + 2];
		g = attr[*npar + 3];
		b = attr[*npar + 4];
		*npar += 4;
		if (!BETWEEN(r, 0, 255) || !BETWEEN(g, 0, 255) || !BETWEEN(b, 0, 255)) {
			break;
		}
		idx = (gint32)GST_TRUECOLOR(r, g, b);
		break;
	case 5: /* indexed color: 38;5;n */
		if (*npar + 2 >= l) {
			break;
		}
		*npar += 2;
		if (!BETWEEN(attr[*npar], 0, 255)) {
			break;
		}
		idx = attr[*npar];
		break;
	case 0: /* implemented defined (only foreground) */
	case 1: /* transparent */
	case 3: /* direct color in CMY space */
	case 4: /* direct color in CMYK space */
	default:
		break;
	}

	return idx;
}

/*
 * term_setattr:
 *
 * Set text attributes from SGR (Select Graphic Rendition) parameters.
 * Port of st's tsetattr().
 */
static void
term_setattr(
    GstTerminal *term,
    const gint  *attr,
    gint        l
){
	GstTerminalPrivate *priv = term->priv;
	gint i;
	gint32 idx;

	for (i = 0; i < l; i++) {
		switch (attr[i]) {
		case 0:
			priv->cursor.glyph.attr &= ~(
			    GST_GLYPH_ATTR_BOLD | GST_GLYPH_ATTR_FAINT |
			    GST_GLYPH_ATTR_ITALIC | GST_GLYPH_ATTR_UNDERLINE |
			    GST_GLYPH_ATTR_BLINK | GST_GLYPH_ATTR_REVERSE |
			    GST_GLYPH_ATTR_INVISIBLE | GST_GLYPH_ATTR_STRUCK |
			    GST_GLYPH_ATTR_UNDERCURL | GST_GLYPH_ATTR_DUNDERLINE |
			    GST_GLYPH_ATTR_OVERLINE);
			priv->cursor.glyph.fg = GST_COLOR_DEFAULT_FG;
			priv->cursor.glyph.bg = GST_COLOR_DEFAULT_BG;
			break;
		case 1:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_BOLD;
			break;
		case 2:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_FAINT;
			break;
		case 3:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_ITALIC;
			break;
		case 4:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_UNDERLINE;
			break;
		case 5: /* FALLTHROUGH */
		case 6:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_BLINK;
			break;
		case 7:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_REVERSE;
			break;
		case 8:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_INVISIBLE;
			break;
		case 9:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_STRUCK;
			break;
		case 21:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_DUNDERLINE;
			break;
		case 22:
			priv->cursor.glyph.attr &= ~(GST_GLYPH_ATTR_BOLD | GST_GLYPH_ATTR_FAINT);
			break;
		case 23:
			priv->cursor.glyph.attr &= ~GST_GLYPH_ATTR_ITALIC;
			break;
		case 24:
			priv->cursor.glyph.attr &= ~(GST_GLYPH_ATTR_UNDERLINE | GST_GLYPH_ATTR_DUNDERLINE);
			break;
		case 25:
			priv->cursor.glyph.attr &= ~GST_GLYPH_ATTR_BLINK;
			break;
		case 27:
			priv->cursor.glyph.attr &= ~GST_GLYPH_ATTR_REVERSE;
			break;
		case 28:
			priv->cursor.glyph.attr &= ~GST_GLYPH_ATTR_INVISIBLE;
			break;
		case 29:
			priv->cursor.glyph.attr &= ~GST_GLYPH_ATTR_STRUCK;
			break;
		case 38:
			idx = term_defcolor(attr, &i, l);
			if (idx >= 0) {
				priv->cursor.glyph.fg = (guint32)idx;
			}
			break;
		case 39:
			priv->cursor.glyph.fg = GST_COLOR_DEFAULT_FG;
			break;
		case 48:
			idx = term_defcolor(attr, &i, l);
			if (idx >= 0) {
				priv->cursor.glyph.bg = (guint32)idx;
			}
			break;
		case 49:
			priv->cursor.glyph.bg = GST_COLOR_DEFAULT_BG;
			break;
		case 53:
			priv->cursor.glyph.attr |= GST_GLYPH_ATTR_OVERLINE;
			break;
		case 55:
			priv->cursor.glyph.attr &= ~GST_GLYPH_ATTR_OVERLINE;
			break;
		default:
			if (BETWEEN(attr[i], 30, 37)) {
				priv->cursor.glyph.fg = (guint32)(attr[i] - 30);
			} else if (BETWEEN(attr[i], 40, 47)) {
				priv->cursor.glyph.bg = (guint32)(attr[i] - 40);
			} else if (BETWEEN(attr[i], 90, 97)) {
				priv->cursor.glyph.fg = (guint32)(attr[i] - 90 + 8);
			} else if (BETWEEN(attr[i], 100, 107)) {
				priv->cursor.glyph.bg = (guint32)(attr[i] - 100 + 8);
			}
			break;
		}
	}
}

/*
 * term_setmode:
 *
 * Enable/disable terminal modes from CSI h/l commands.
 * Port of st's tsetmode().
 */
static void
term_setmode(
    GstTerminal *term,
    gint        priv_flag,
    gint        set,
    const gint  *args,
    gint        narg
){
	GstTerminalPrivate *priv = term->priv;
	gint i;

	for (i = 0; i < narg; i++) {
		if (priv_flag) {
			/* DEC private modes (CSI ? h/l) */
			switch (args[i]) {
			case 1: /* DECCKM - cursor key mode */
				gst_terminal_set_mode(term, GST_MODE_APPCURSOR, set);
				break;
			case 5: /* DECSCNM - reverse video */
				{
					GstTermMode old = priv->mode;
					gst_terminal_set_mode(term, GST_MODE_REVERSE, set);
					if (old != priv->mode) {
						/* TODO: redraw needed */
					}
				}
				break;
			case 6: /* DECOM - origin mode */
				if (set) {
					priv->cursor.state |= GST_CURSOR_STATE_ORIGIN;
				} else {
					priv->cursor.state &= ~GST_CURSOR_STATE_ORIGIN;
				}
				gst_terminal_move_to_abs(term, 0, 0);
				break;
			case 7: /* DECAWM - auto wrap */
				gst_terminal_set_mode(term, GST_MODE_WRAP, set);
				break;
			case 0:  /* error: ignored */
			case 2:  /* DECANM: ANSI/VT52 */
			case 3:  /* DECCOLM: column */
			case 4:  /* DECSCLM: scroll */
			case 8:  /* DECARM: auto repeat */
			case 18: /* DECPFF: printer */
			case 19: /* DECPEX: printer extent */
			case 42: /* DECNRCM: national characters */
			case 12: /* att610: start blinking cursor */
				break;
			case 25: /* DECTCEM - cursor visible */
				gst_terminal_set_mode(term, GST_MODE_HIDE, !set);
				break;
			case 9: /* X10 mouse compatibility */
				gst_terminal_set_mode(term,
				    GST_MODE_MOUSE_X10 | GST_MODE_MOUSE_BTN |
				    GST_MODE_MOUSE_MOTION | GST_MODE_MOUSE_MANY, 0);
				gst_terminal_set_mode(term, GST_MODE_MOUSE_X10, set);
				break;
			case 1000: /* Report button press */
				gst_terminal_set_mode(term,
				    GST_MODE_MOUSE_X10 | GST_MODE_MOUSE_BTN |
				    GST_MODE_MOUSE_MOTION | GST_MODE_MOUSE_MANY, 0);
				gst_terminal_set_mode(term, GST_MODE_MOUSE_BTN, set);
				break;
			case 1002: /* Report motion on button press */
				gst_terminal_set_mode(term,
				    GST_MODE_MOUSE_X10 | GST_MODE_MOUSE_BTN |
				    GST_MODE_MOUSE_MOTION | GST_MODE_MOUSE_MANY, 0);
				gst_terminal_set_mode(term, GST_MODE_MOUSE_MOTION, set);
				break;
			case 1003: /* Report all mouse motion */
				gst_terminal_set_mode(term,
				    GST_MODE_MOUSE_X10 | GST_MODE_MOUSE_BTN |
				    GST_MODE_MOUSE_MOTION | GST_MODE_MOUSE_MANY, 0);
				gst_terminal_set_mode(term, GST_MODE_MOUSE_MANY, set);
				break;
			case 1004: /* Focus events */
				gst_terminal_set_mode(term, GST_MODE_FOCUS, set);
				break;
			case 1006: /* SGR extended mouse */
				gst_terminal_set_mode(term, GST_MODE_MOUSE_SGR, set);
				break;
			case 1034: /* 8bit input mode */
				gst_terminal_set_mode(term, GST_MODE_8BIT, set);
				break;
			case 1049: /* swap screen + cursor save */
			case 47:   /* swap screen */
			case 1047:
				{
					gboolean is_alt = (priv->mode & GST_MODE_ALTSCREEN) != 0;
					/*
					 * 1049: save cursor BEFORE swap (on enter),
					 * restore AFTER swap (on exit). The cursor
					 * slot is indexed by ALTSCREEN state, so
					 * restoring after swap ensures idx=0 (primary
					 * slot) which is where we saved on enter.
					 */
					if (args[i] == 1049 && set) {
						gst_terminal_cursor_save(term);
					}
					if (is_alt != (gboolean)set) {
						if (set) {
							/* Entering alt: swap, then clear */
							gst_terminal_swap_screen(term);
							gst_terminal_clear(term);
						} else {
							/* Leaving alt: clear, then swap */
							gst_terminal_clear(term);
							gst_terminal_swap_screen(term);
						}
					}
					if (args[i] == 1049 && !set) {
						gst_terminal_cursor_restore(term);
					}
				}
				break;
			case 1048:
				if (set) {
					gst_terminal_cursor_save(term);
				} else {
					gst_terminal_cursor_restore(term);
				}
				break;
			case 2004: /* Bracketed paste */
				gst_terminal_set_mode(term, GST_MODE_BRCKTPASTE, set);
				break;
			case 2026: /* Synchronized update */
				gst_terminal_set_mode(term, GST_MODE_SYNC_UPDATE, set);
				break;
			default:
				break;
			}
		} else {
			/* ANSI modes (CSI h/l) */
			switch (args[i]) {
			case 0: /* error: ignored */
				break;
			case 2:
				gst_terminal_set_mode(term, GST_MODE_KBDLOCK, set);
				break;
			case 4: /* IRM - insert/replace mode */
				gst_terminal_set_mode(term, GST_MODE_INSERT, set);
				break;
			case 12: /* SRM - send/receive mode (inverted) */
				gst_terminal_set_mode(term, GST_MODE_ECHO, !set);
				break;
			case 20: /* LNM - linefeed/newline mode */
				gst_terminal_set_mode(term, GST_MODE_CRLF, set);
				break;
			default:
				break;
			}
		}
	}
}

/*
 * term_csiparse:
 *
 * Parse CSI buffer into args and mode bytes.
 */
static void
term_csiparse(GstTerminal *term)
{
	GstTerminalPrivate *priv = term->priv;
	gchar *p = priv->csi_buf;
	glong v;

	priv->csi_nargs = 0;
	priv->csi_priv = 0;
	memset(priv->csi_args, 0, sizeof(priv->csi_args));

	if (*p == '?') {
		priv->csi_priv = 1;
		p++;
	}

	priv->csi_buf[priv->csi_len] = '\0';

	while (p < priv->csi_buf + priv->csi_len) {
		v = strtol(p, &p, 10);
		/* Parameters are nonnegative; saturate before narrowing to gint. */
		priv->csi_args[priv->csi_nargs++] = (gint)CLAMP(v, 0, G_MAXINT);
		if (*p != ';' || priv->csi_nargs == GST_MAX_ARGS) {
			break;
		}
		p++;
	}

	priv->csi_mode[0] = *p++;
	priv->csi_mode[1] = (p < priv->csi_buf + priv->csi_len) ? *p : '\0';
}

/*
 * term_csihandle:
 *
 * Process a parsed CSI command. Port of st's csihandle().
 */
static void
term_csihandle(GstTerminal *term)
{
	GstTerminalPrivate *priv = term->priv;
	gchar cmd;

	/* Parse Kitty commands separately: plain CSI u remains DECRC. Reject
	 * malformed/overflowing parameters rather than partially changing state. */
	if (priv->csi_len >= 2 && priv->csi_buf[priv->csi_len - 1] == 'u' &&
	    strchr("?><=", priv->csi_buf[0]) != NULL) {
		guint values[2] = { 0, 0 };
		guint field = 0;
		guint idx = (priv->mode & GST_MODE_ALTSCREEN) ? 1 : 0;
		guint *depth = &priv->keyboard_depth[idx];
		guint *flags = &priv->keyboard_flags[idx];
		gsize pos;
		gchar prefix = priv->csi_buf[0];

		if (prefix == '?') {
			if (priv->csi_len == 2) {
				gchar reply[32];

				g_snprintf(reply, sizeof(reply), "\033[?%uu", *flags);
				term_response(term, reply, -1);
			}
			return;
		}
		for (pos = 1; pos + 1 < priv->csi_len; pos++) {
			gchar c = priv->csi_buf[pos];

			if (c == ';' && prefix == '=' && field == 0) {
				field++;
			} else if (c >= '0' && c <= '9') {
				guint digit = (guint)(c - '0');

				if (values[field] > (G_MAXUINT - digit) / 10) {
					return;
				}
				values[field] = values[field] * 10 + digit;
			} else {
				return;
			}
		}
		if (prefix == '<') {
			guint count = DEFAULT(values[0], 1);

			if (count > *depth) {
				*depth = 0;
				*flags = 0;
			} else {
				*depth -= count;
				*flags = priv->keyboard_stack[idx][*depth];
			}
		} else if (prefix == '>') {
			if (*depth == GST_KEYBOARD_STACK_SIZE) {
				memmove(priv->keyboard_stack[idx], priv->keyboard_stack[idx] + 1,
				    (GST_KEYBOARD_STACK_SIZE - 1) * sizeof(guint));
				(*depth)--;
			}
			priv->keyboard_stack[idx][(*depth)++] = *flags;
			*flags = values[0] & GST_KEYBOARD_SUPPORTED_FLAGS;
		} else {
			switch (DEFAULT(values[1], 1)) {
			case 1:
				*flags = values[0] & GST_KEYBOARD_SUPPORTED_FLAGS;
				break;
			case 2:
				*flags |= values[0] & GST_KEYBOARD_SUPPORTED_FLAGS;
				break;
			case 3:
				*flags &= ~values[0];
				break;
			default:
				break;
			}
		}
		return;
	}

	term_csiparse(term);
	cmd = priv->csi_mode[0];

	switch (cmd) {
	case '@': /* ICH - Insert Character */
		gst_terminal_insert_blanks(term, DEFAULT(priv->csi_args[0], 1));
		break;

	case 'A': /* CUU - Cursor Up */
		gst_terminal_move_to(term, priv->cursor.x,
		    priv->cursor.y - MIN(DEFAULT(priv->csi_args[0], 1), priv->rows));
		break;

	case 'B': /* CUD - Cursor Down */
	case 'e': /* VPR - Vertical Position Relative */
		gst_terminal_move_to(term, priv->cursor.x,
		    priv->cursor.y + MIN(DEFAULT(priv->csi_args[0], 1), priv->rows));
		break;

	case 'C': /* CUF - Cursor Forward */
	case 'a': /* HPR - Horizontal Position Relative */
		gst_terminal_move_to(term,
		    priv->cursor.x + MIN(DEFAULT(priv->csi_args[0], 1), priv->cols),
		    priv->cursor.y);
		break;

	case 'D': /* CUB - Cursor Backward */
		gst_terminal_move_to(term,
		    priv->cursor.x - MIN(DEFAULT(priv->csi_args[0], 1), priv->cols),
		    priv->cursor.y);
		break;

	case 'E': /* CNL - Cursor Next Line */
		gst_terminal_move_to(term, 0,
		    priv->cursor.y + MIN(DEFAULT(priv->csi_args[0], 1), priv->rows));
		break;

	case 'F': /* CPL - Cursor Previous Line */
		gst_terminal_move_to(term, 0,
		    priv->cursor.y - MIN(DEFAULT(priv->csi_args[0], 1), priv->rows));
		break;

	case 'G': /* CHA - Cursor Horizontal Absolute */
	case '`': /* HPA - Horizontal Position Absolute */
		gst_terminal_move_to(term,
		    DEFAULT(priv->csi_args[0], 1) - 1,
		    priv->cursor.y);
		break;

	case 'H': /* CUP - Cursor Position */
	case 'f': /* HVP - Horizontal and Vertical Position */
		gst_terminal_move_to_abs(term,
		    DEFAULT(priv->csi_args[1], 1) - 1,
		    DEFAULT(priv->csi_args[0], 1) - 1);
		break;

	case 'I': /* CHT - Cursor Forward Tabulation */
		gst_terminal_put_tab(term, DEFAULT(priv->csi_args[0], 1));
		break;

	case 'J': /* ED - Erase Display */
		switch (priv->csi_args[0]) {
		case 0: /* below */
			gst_terminal_clear_region(term, priv->cursor.x, priv->cursor.y,
			    priv->cols - 1, priv->cursor.y);
			if (priv->cursor.y < priv->rows - 1) {
				gst_terminal_clear_region(term, 0, priv->cursor.y + 1,
				    priv->cols - 1, priv->rows - 1);
			}
			break;
		case 1: /* above */
			if (priv->cursor.y > 0) {
				gst_terminal_clear_region(term, 0, 0,
				    priv->cols - 1, priv->cursor.y - 1);
			}
			gst_terminal_clear_region(term, 0, priv->cursor.y,
			    priv->cursor.x, priv->cursor.y);
			break;
		case 2: /* all */
			gst_terminal_clear_region(term, 0, 0,
			    priv->cols - 1, priv->rows - 1);
			break;
		default:
			break;
		}
		break;

	case 'K': /* EL - Erase Line */
		switch (priv->csi_args[0]) {
		case 0: /* right */
			gst_terminal_clear_region(term, priv->cursor.x, priv->cursor.y,
			    priv->cols - 1, priv->cursor.y);
			break;
		case 1: /* left */
			gst_terminal_clear_region(term, 0, priv->cursor.y,
			    priv->cursor.x, priv->cursor.y);
			break;
		case 2: /* all */
			gst_terminal_clear_region(term, 0, priv->cursor.y,
			    priv->cols - 1, priv->cursor.y);
			break;
		default:
			break;
		}
		break;

	case 'L': /* IL - Insert Lines */
		gst_terminal_insert_blank_lines(term, DEFAULT(priv->csi_args[0], 1));
		break;

	case 'M': /* DL - Delete Lines */
		gst_terminal_delete_lines(term, DEFAULT(priv->csi_args[0], 1));
		break;

	case 'P': /* DCH - Delete Character */
		gst_terminal_delete_chars(term, DEFAULT(priv->csi_args[0], 1));
		break;

	case 'S': /* SU - Scroll Up */
		gst_terminal_scroll_up(term, priv->scroll_top,
		    DEFAULT(priv->csi_args[0], 1));
		break;

	case 'T': /* SD - Scroll Down */
		gst_terminal_scroll_down(term, priv->scroll_top,
		    DEFAULT(priv->csi_args[0], 1));
		break;

	case 'X': /* ECH - Erase Character */
		gst_terminal_clear_region(term, priv->cursor.x, priv->cursor.y,
		    priv->cursor.x + MIN(DEFAULT(priv->csi_args[0], 1), priv->cols - priv->cursor.x) - 1,
		    priv->cursor.y);
		break;

	case 'Z': /* CBT - Cursor Backward Tabulation */
		gst_terminal_put_tab(term, -DEFAULT(priv->csi_args[0], 1));
		break;

	case 'b': /* REP - Repeat previous character */
		if (priv->lastc != 0) {
			gint count = DEFAULT(priv->csi_args[0], 1);
			g_autofree gchar *text = g_strdup(priv->last_glyph.cluster);
			GstRune first = priv->lastc;
			const gchar *p;

			while (count-- > 0) {
				priv->cluster_valid = FALSE;
				if (text != NULL) {
					for (p = text; *p != '\0'; p = g_utf8_next_char(p)) {
						gst_terminal_put_char(term, g_utf8_get_char(p));
					}
				} else {
					gst_terminal_put_char(term, first);
				}
			}
		}
		break;

	case 'c': /* DA - Device Attributes */
		if (priv->csi_args[0] == 0) {
			term_response(term, "\033[?6c", -1);
		}
		break;

	case 'd': /* VPA - Vertical Position Absolute */
		gst_terminal_move_to_abs(term, priv->cursor.x,
		    DEFAULT(priv->csi_args[0], 1) - 1);
		break;

	case 'g': /* TBC - Tabulation Clear */
		switch (priv->csi_args[0]) {
		case 0: /* clear current tab stop */
			priv->tabs[priv->cursor.x] = FALSE;
			break;
		case 3: /* clear all tab stops */
			memset(priv->tabs, 0, sizeof(gboolean) * priv->cols);
			break;
		default:
			break;
		}
		break;

	case 'h': /* SM - Set Mode */
		term_setmode(term, priv->csi_priv, 1,
		    priv->csi_args, priv->csi_nargs);
		break;

	case 'l': /* RM - Reset Mode */
		term_setmode(term, priv->csi_priv, 0,
		    priv->csi_args, priv->csi_nargs);
		break;

	case 'm': /* SGR - Select Graphic Rendition */
		term_setattr(term, priv->csi_args,
		    priv->csi_nargs > 0 ? priv->csi_nargs : 1);
		break;

	case 'n': /* DSR - Device Status Report */
		if (priv->csi_args[0] == 6) {
			/* Cursor position report */
			gchar buf[40];
			g_snprintf(buf, sizeof(buf), "\033[%d;%dR",
			    priv->cursor.y + 1 - ((priv->cursor.state & GST_CURSOR_STATE_ORIGIN)
			        ? priv->scroll_top : 0), priv->cursor.x + 1);
			term_response(term, buf, -1);
		}
		break;

	case 'r': /* DECSTBM - Set Scrolling Region */
		{
			gint top, bot;
			/*
			 * CSI r with no numeric args resets scroll region.
			 * strtol always produces nargs >= 1 (parsing the
			 * final byte yields 0), so check args[0] == 0 too.
			 */
			if (priv->csi_nargs <= 1 && priv->csi_args[0] == 0) {
				top = 0;
				bot = priv->rows - 1;
			} else {
				top = DEFAULT(priv->csi_args[0], 1) - 1;
				bot = (priv->csi_nargs >= 2) ?
				    DEFAULT(priv->csi_args[1], priv->rows) - 1 :
				    priv->rows - 1;
			}
			gst_terminal_set_scroll_region(term, top, bot);
			gst_terminal_move_to_abs(term, 0, 0);
		}
		break;

	case 's': /* DECSC - Save Cursor */
		gst_terminal_cursor_save(term);
		break;

	case 'u': /* DECRC - Restore Cursor */
		gst_terminal_cursor_restore(term);
		break;

	case ' ':
		/* CSI <n> SP q - Set cursor style (DECSCUSR) */
		if (priv->csi_mode[1] == 'q') {
			switch (priv->csi_args[0]) {
			case 0: /* FALLTHROUGH */
			case 1: /* FALLTHROUGH */
			case 2:
				priv->cursor.shape = GST_CURSOR_SHAPE_BLOCK;
				break;
			case 3: /* FALLTHROUGH */
			case 4:
				priv->cursor.shape = GST_CURSOR_SHAPE_UNDERLINE;
				break;
			case 5: /* FALLTHROUGH */
			case 6:
				priv->cursor.shape = GST_CURSOR_SHAPE_BAR;
				break;
			default:
				break;
			}
		}
		break;

	default:
		break;
	}
}

/*
 * term_strparse:
 *
 * Parse string escape buffer into semicolon-separated arguments.
 */
static void
term_strparse(GstTerminal *term)
{
	GstTerminalPrivate *priv = term->priv;
	gchar *p;
	gint c;

	priv->str_nargs = 0;
	if (priv->str_buf == NULL || priv->str_len == 0) {
		return;
	}

	priv->str_buf[priv->str_len] = '\0';
	p = priv->str_buf;

	if (*p == '\0') {
		return;
	}

	while (priv->str_nargs < GST_MAX_ARGS) {
		priv->str_args[priv->str_nargs++] = p;
		while ((c = *p) != ';' && c != '\0') {
			++p;
		}
		if (c == '\0') {
			return;
		}
		*p++ = '\0';
	}
}

/*
 * term_strhandle:
 *
 * Process a parsed string escape (OSC/DCS/APC/PM).
 */
static void
term_strhandle(GstTerminal *term)
{
	GstTerminalPrivate *priv = term->priv;
	gint par;

	priv->esc &= ~(GST_ESC_STR_END | GST_ESC_STR);

	g_debug("term_strhandle: type='%c' len=%zu buf=%.40s",
		priv->str_type, priv->str_len,
		(priv->str_buf && priv->str_len > 0)
			? priv->str_buf : "(empty)");

	/*
	 * APC and DCS sequences must be dispatched with the raw buffer
	 * intact. term_strparse() replaces ';' with '\0' which corrupts
	 * protocols like kitty graphics and sixel that use ';' as a
	 * payload separator. Handle these before parsing.
	 */
	if (priv->str_type == '_' || priv->str_type == 'P') {
		if (priv->str_buf != NULL && priv->str_len > 0) {
			priv->str_buf[priv->str_len] = '\0';
			g_debug("term_strhandle: dispatching %s (len=%zu)",
				priv->str_type == 'P' ? "DCS" : "APC",
				priv->str_len);
			g_signal_emit(term, signals[SIGNAL_ESCAPE_STRING], 0,
				priv->str_type, priv->str_buf,
				(gulong)priv->str_len);
		}
		return;
	}

	/*
	 * For OSC sequences, save the raw buffer before term_strparse()
	 * corrupts semicolons. Modules receive the raw buffer for parsing.
	 */
	{
		g_autofree gchar *raw_buf = NULL;
		gsize raw_len;

		raw_buf = NULL;
		raw_len = 0;
		if (priv->str_type == ']' && priv->str_buf != NULL &&
		    priv->str_len > 0)
		{
			raw_len = priv->str_len;
			raw_buf = g_strndup(priv->str_buf, raw_len);
		}

		term_strparse(term);
		/* Titles use the entire payload after the first separator. */
		if (raw_buf != NULL && priv->str_nargs > 1) {
			gchar *title;

			title = strchr(raw_buf, ';');
			if (title != NULL) {
				priv->str_args[1] = title + 1;
			}
		}

		if (priv->str_nargs == 0) {
			return;
		}

		switch (priv->str_type) {
		case ']': /* OSC - Operating System Command */
			par = (gint)strtol(priv->str_args[0], NULL, 10);
			switch (par) {
			case 0: /* Set icon and window title */
				if (priv->str_nargs > 1) {
					gst_terminal_set_title(term,
						priv->str_args[1]);
					gst_terminal_set_icon(term,
						priv->str_args[1]);
				}
				break;
			case 1: /* Set icon title */
				if (priv->str_nargs > 1) {
					gst_terminal_set_icon(term,
						priv->str_args[1]);
				}
				break;
			case 2: /* Set window title */
				if (priv->str_nargs > 1) {
					gst_terminal_set_title(term,
						priv->str_args[1]);
				}
				break;
			default:
				/* Dispatch unhandled OSC to modules */
				g_debug("term_strhandle: OSC %d unhandled, "
					"dispatching to modules (raw_len=%zu)",
					par, raw_len);
				if (raw_buf != NULL) {
					g_signal_emit(term,
						signals[SIGNAL_ESCAPE_STRING], 0,
						(gchar)']', raw_buf,
						(gulong)raw_len);
				}
				break;
			}
			break;

		case 'k': /* Old title set */
			if (priv->str_nargs > 0) {
				gst_terminal_set_title(term,
					priv->str_args[0]);
			}
			break;

		case '^': /* PM */
			/* Ignored */
			break;

		default:
			break;
		}
	}
}

/*
 * term_strsequence:
 *
 * Initialize a string escape sequence from a C1 code.
 */
static void
term_strsequence(
    GstTerminal *term,
    guchar      c
){
	GstTerminalPrivate *priv = term->priv;

	priv->esc &= ~(GST_ESC_CSI | GST_ESC_ALTCHARSET | GST_ESC_TEST);
	priv->esc |= GST_ESC_STR;

	switch (c) {
	case 0x90: /* DCS */
		priv->str_type = 'P';
		break;
	case 0x9d: /* OSC */
		priv->str_type = ']';
		break;
	case 0x9e: /* PM */
		priv->str_type = '^';
		break;
	case 0x9f: /* APC */
		priv->str_type = '_';
		break;
	default:
		priv->str_type = c;
		break;
	}

	priv->str_len = 0;
	priv->str_nargs = 0;

	/* Ensure buffer is allocated */
	if (priv->str_buf == NULL) {
		priv->str_siz = STR_BUF_SIZ;
		priv->str_buf = g_malloc(priv->str_siz);
	}
}

/*
 * term_deftran:
 *
 * Define alternate character set for G0-G3.
 */
static void
term_deftran(
    GstTerminal *term,
    gchar       c
){
	GstTerminalPrivate *priv = term->priv;
	GstCharset cs;

	switch (c) {
	case '0':
		cs = GST_CHARSET_GRAPHIC0;
		break;
	case 'B':
		cs = GST_CHARSET_USA;
		break;
	case 'A':
		cs = GST_CHARSET_UK;
		break;
	default:
		cs = GST_CHARSET_USA;
		break;
	}

	priv->charsets[priv->icharset] = cs;
}

/*
 * term_defutf8:
 *
 * Handle ESC % G (enable UTF-8) / ESC % @ (disable UTF-8).
 */
static void
term_defutf8(
    GstTerminal *term,
    gchar       c
){
	GstTerminalPrivate *priv = term->priv;

	if (c == 'G') {
		priv->mode |= GST_MODE_UTF8;
	} else if (c == '@') {
		priv->mode &= ~GST_MODE_UTF8;
	}
}

/*
 * term_dectest:
 *
 * DEC screen alignment test (ESC # 8): fill screen with 'E'.
 */
static void
term_dectest(
    GstTerminal *term,
    gchar       c
){
	GstTerminalPrivate *priv = term->priv;
	gint x, y;

	if (c == '8') {
		for (y = 0; y < priv->rows; y++) {
			for (x = 0; x < priv->cols; x++) {
				term_setchar(term, 'E', &priv->cursor.glyph, x, y);
			}
		}
	}
}

/*
 * term_eschandle:
 *
 * Handle ESC-prefixed sequences. Returns 1 if the sequence is complete,
 * 0 if more bytes are needed.
 */
static gint
term_eschandle(
    GstTerminal *term,
    guchar      c
){
	GstTerminalPrivate *priv = term->priv;

	switch (c) {
	case '[': /* CSI */
		priv->esc |= GST_ESC_CSI;
		return 0;

	case '#': /* DEC test */
		priv->esc |= GST_ESC_TEST;
		return 0;

	case '%': /* UTF-8 mode */
		priv->esc |= GST_ESC_UTF8;
		return 0;

	case 'P': /* DCS - Device Control String */
	case '_': /* APC - Application Program Command */
	case '^': /* PM - Privacy Message */
	case ']': /* OSC - Operating System Command */
	case 'k': /* Old title set */
		term_strsequence(term, c);
		return 0;

	case '(': /* GZD4 - set G0 charset */
		priv->icharset = 0;
		priv->esc |= GST_ESC_ALTCHARSET;
		return 0;
	case ')': /* G1D4 - set G1 charset */
		priv->icharset = 1;
		priv->esc |= GST_ESC_ALTCHARSET;
		return 0;
	case '*': /* G2D4 - set G2 charset */
		priv->icharset = 2;
		priv->esc |= GST_ESC_ALTCHARSET;
		return 0;
	case '+': /* G3D4 - set G3 charset */
		priv->icharset = 3;
		priv->esc |= GST_ESC_ALTCHARSET;
		return 0;

	case 'D': /* IND - Index (move cursor down, scroll if at bottom) */
		if (priv->cursor.y == priv->scroll_bot) {
			gst_terminal_scroll_up(term, priv->scroll_top, 1);
		} else {
			gst_terminal_move_to(term, priv->cursor.x, priv->cursor.y + 1);
		}
		return 1;

	case 'E': /* NEL - Next Line */
		gst_terminal_newline(term, TRUE);
		return 1;

	case 'H': /* HTS - Horizontal Tab Stop */
		priv->tabs[priv->cursor.x] = TRUE;
		return 1;

	case 'M': /* RI - Reverse Index */
		if (priv->cursor.y == priv->scroll_top) {
			gst_terminal_scroll_down(term, priv->scroll_top, 1);
		} else {
			gst_terminal_move_to(term, priv->cursor.x, priv->cursor.y - 1);
		}
		return 1;

	case 'Z': /* DECID - Identify terminal */
		term_response(term, "\033[?6c", -1);
		return 1;

	case 'c': /* RIS - Full reset */
		gst_terminal_reset(term, TRUE);
		return 1;

	case '=': /* DECPAM - Application keypad */
		gst_terminal_set_mode(term, GST_MODE_APPKEYPAD, TRUE);
		return 1;

	case '>': /* DECPNM - Normal keypad */
		gst_terminal_set_mode(term, GST_MODE_APPKEYPAD, FALSE);
		return 1;

	case '7': /* DECSC - Save cursor */
		gst_terminal_cursor_save(term);
		return 1;

	case '8': /* DECRC - Restore cursor */
		gst_terminal_cursor_restore(term);
		return 1;

	case 'n': /* LS2 - Locking Shift 2 */
		priv->charset_gl = 2;
		return 1;

	case 'o': /* LS3 - Locking Shift 3 */
		priv->charset_gl = 3;
		return 1;

	case '\\': /* ST - String Terminator */
		if (priv->esc & GST_ESC_STR_END) {
			term_strhandle(term);
		}
		return 1;

	default:
		return 1;
	}
}

/*
 * term_controlcode:
 *
 * Handle C0/C1 control codes. Port of st's tcontrolcode().
 */
static void
term_controlcode(
    GstTerminal *term,
    guchar      c
){
	GstTerminalPrivate *priv = term->priv;

	switch (c) {
	case '\t': /* TAB (HT) */
		gst_terminal_put_tab(term, 1);
		return;

	case '\n':   /* LF */
	case '\x0b': /* VT */
	case '\x0c': /* FF */
		/* CRLF mode: also do CR */
		gst_terminal_newline(term, (priv->mode & GST_MODE_CRLF) != 0);
		return;

	case '\r': /* CR */
		gst_terminal_move_to(term, 0, priv->cursor.y);
		return;

	case '\b': /* BS */
		gst_terminal_move_to(term, priv->cursor.x - 1, priv->cursor.y);
		return;

	case '\a': /* BEL */
		if (priv->esc & GST_ESC_STR) {
			/* BEL terminates OSC string */
			priv->esc &= ~(GST_ESC_START | GST_ESC_STR);
			priv->esc |= GST_ESC_STR_END;
			term_strhandle(term);
		} else {
			g_signal_emit(term, signals[SIGNAL_BELL], 0);
		}
		return;

	case '\x1b': /* ESC */
		priv->csi_len = 0;
		priv->csi_mode[0] = 0;
		priv->csi_mode[1] = 0;
		priv->esc &= ~(GST_ESC_CSI | GST_ESC_ALTCHARSET | GST_ESC_TEST);
		priv->esc |= GST_ESC_START;
		return;

	case '\x00': /* NUL - ignored */
	case '\x05': /* ENQ - ignored */
	case '\x11': /* XON - ignored */
	case '\x13': /* XOFF - ignored */
		return;

	case '\x18': /* CAN */
	case '\x1a': /* SUB */
		priv->esc = 0;
		return;

	case 0x7f: /* DEL - ignored */
		return;

	default:
		break;
	}

	/* C1 control codes (0x80 - 0x9f) */
	if (BETWEEN(c, 0x80, 0x9f)) {
		switch (c) {
		case 0x84: /* IND */
			if (priv->cursor.y == priv->scroll_bot) {
				gst_terminal_scroll_up(term, priv->scroll_top, 1);
			} else {
				gst_terminal_move_to(term, priv->cursor.x, priv->cursor.y + 1);
			}
			break;
		case 0x85: /* NEL */
			gst_terminal_newline(term, TRUE);
			break;
		case 0x88: /* HTS */
			priv->tabs[priv->cursor.x] = TRUE;
			break;
		case 0x8d: /* RI */
			if (priv->cursor.y == priv->scroll_top) {
				gst_terminal_scroll_down(term, priv->scroll_top, 1);
			} else {
				gst_terminal_move_to(term, priv->cursor.x, priv->cursor.y - 1);
			}
			break;
		case 0x9a: /* DECID */
			term_response(term, "\033[?6c", -1);
			break;
		case 0x90: /* DCS */
		case 0x9d: /* OSC */
		case 0x9e: /* PM */
		case 0x9f: /* APC */
			term_strsequence(term, c);
			break;
		default:
			break;
		}
	}
}

/* ===== Main Character Input (tputc equivalent) ===== */

/* Pango supplies Unicode grapheme boundaries, including emoji and Indic rules. */
static gboolean
term_cluster_joins(const GstGlyph *glyph, GstRune rune)
{
	gchar buffer[7];
	gchar suffix[7];
	g_autofree gchar *text = NULL;
	PangoLogAttr *attrs;
	glong length;
	gboolean joins;
	GUnicodeType type;
	GstRune last;

	/* Common ASCII output needs no allocation or boundary analysis. */
	if (glyph->cluster == NULL && glyph->rune < 0x7f && rune < 0x7f) {
		return FALSE;
	}
	/* GB9 extends non-control graphemes with nonspacing/enclosing marks.
	 * Avoid reanalyzing the entire growing string for every such mark. */
	type = g_unichar_type(rune);
	if (type == G_UNICODE_NON_SPACING_MARK || type == G_UNICODE_ENCLOSING_MARK) {
		last = glyph->cluster != NULL
		    ? g_utf8_get_char(g_utf8_find_prev_char(glyph->cluster,
		        glyph->cluster + glyph->cluster_len)) : glyph->rune;
		type = g_unichar_type(last);
		if ((type != G_UNICODE_CONTROL && type != G_UNICODE_FORMAT &&
		     type != G_UNICODE_LINE_SEPARATOR && type != G_UNICODE_PARAGRAPH_SEPARATOR) ||
		    last == 0x200c || last == 0x200d) {
			return TRUE;
		}
	}
	suffix[g_unichar_to_utf8(rune, suffix)] = '\0';
	text = g_strconcat(gst_glyph_get_text(glyph, buffer), suffix, NULL);
	length = g_utf8_strlen(text, -1);
	attrs = g_new0(PangoLogAttr, (gsize)length + 1);
	pango_get_log_attrs(text, -1, 0, pango_language_from_string("und"),
	    attrs, (gint)length + 1);
	joins = !attrs[length - 1].is_cursor_position;
	g_free(attrs);
	return joins;
}

void
gst_terminal_put_char(
    GstTerminal *term,
    GstRune     rune
){
	GstTerminalPrivate *priv;
	GstLine *line;
	gint width;

	g_return_if_fail(GST_IS_TERMINAL(term));

	priv = term->priv;
	gst_terminal_init_screen(term);

	/*
	 * STR (string) state handling:
	 * Accumulate bytes until terminator (BEL, ESC \, or cancel codes).
	 */
	if (priv->esc & GST_ESC_STR) {
		/* Cancellation must not execute a partially received OSC/DCS/APC. */
		if (rune == 0x18 || rune == 0x1a) {
			priv->esc = 0;
			priv->str_len = 0;
			return;
		}
		if (rune == '\a' || rune == 0x18 || rune == 0x1a ||
		    (rune == 0x1b && !(priv->esc & GST_ESC_STR_END))) {
			/* BEL or cancel terminates the string */
			if (rune == '\a' || rune == 0x18 || rune == 0x1a) {
				priv->esc &= ~(GST_ESC_START | GST_ESC_STR);
				priv->esc |= GST_ESC_STR_END;
			}
			term_strhandle(term);
			return;
		}

		if (priv->esc & GST_ESC_STR_END) {
			/* Expected ESC \ for proper ST */
			priv->esc = 0;
			term_strhandle(term);
			return;
		}

		if (rune == '\033') {
			/* ESC during string -> expect \ for ST */
			priv->esc |= GST_ESC_STR_END;
			return;
		}

		/* Restore UTF-8 bytes after the input decoder has produced a rune. */
		{
			gchar encoded[6];
			gint encoded_len;

			encoded_len = (priv->mode & GST_MODE_UTF8)
				? g_unichar_to_utf8(rune, encoded) : 1;
			if (!(priv->mode & GST_MODE_UTF8)) {
				encoded[0] = (gchar)rune;
			}
			if (priv->str_buf != NULL && priv->str_len <= GST_MAX_STR_LEN - (gsize)encoded_len) {
				/* Grow buffer if needed */
				if (priv->str_len + encoded_len >= priv->str_siz) {
					priv->str_siz *= 2;
					priv->str_buf = g_realloc(priv->str_buf, priv->str_siz);
				}
				memcpy(priv->str_buf + priv->str_len, encoded, (gsize)encoded_len);
				priv->str_len += encoded_len;
			} else if (priv->str_buf != NULL &&
				   priv->str_len <= GST_MAX_STR_LEN)
			{
				g_warning("escape string buffer overflow "
					"(%d bytes, type='%c')",
					GST_MAX_STR_LEN, priv->str_type);
				priv->str_len = GST_MAX_STR_LEN + 1; /* prevent repeated warnings */
			}
		}
		return;
	}

	/* Handle control characters (< 0x20, 0x7f, or C1 0x80-0x9f) */
	if (ISCONTROL(rune)) {
		priv->cluster_valid = FALSE;
		term_controlcode(term, (guchar)rune);
		/* Control chars don't modify lastc */
		return;
	}

	/* If in escape state, handle the escape sequence */
	if (priv->esc & GST_ESC_START) {
		if (priv->esc & GST_ESC_CSI) {
			/* Accumulate CSI bytes */
			priv->csi_buf[priv->csi_len++] = (gchar)rune;

			/* Check for final byte (0x40-0x7e) */
			if (BETWEEN(rune, 0x40, 0x7e) ||
			    priv->csi_len >= (gsize)(CSI_BUF_SIZ - 1)) {
				priv->esc = 0;
				term_csihandle(term);
			}
			return;
		} else if (priv->esc & GST_ESC_UTF8) {
			term_defutf8(term, (gchar)rune);
		} else if (priv->esc & GST_ESC_ALTCHARSET) {
			term_deftran(term, (gchar)rune);
		} else if (priv->esc & GST_ESC_TEST) {
			term_dectest(term, (gchar)rune);
		} else {
			if (!term_eschandle(term, (guchar)rune)) {
				/* Sequence needs more bytes */
				return;
			}
		}

		/* Sequence complete */
		priv->esc = 0;
		return;
	}

	/*
	 * Normal character output
	 */
	if (!g_unichar_validate(rune)) {
		rune = 0xfffd;
	}

	/*
	 * Get Unicode width via wcwidth (through gst_wcwidth).
	 * This matches st's behavior: ambiguous-width characters
	 * (including PUA / Powerline / Nerd Font symbols) are width 1
	 * in non-CJK locales. Using g_unichar_iswide_cjk() treated
	 * these as width 2, causing cursor desync with tmux.
	 */
	width = gst_wcwidth(rune);
	if (width < 0) {
		width = 1;
	}

	if (priv->cluster_valid) {
		GstGlyph *prev;
		gboolean promote;

		prev = gst_terminal_get_glyph(term, priv->cluster_x, priv->cluster_y);
		if (prev != NULL && term_cluster_joins(prev, rune)) {
			promote = !(prev->attr & GST_GLYPH_ATTR_WIDE) &&
			    (rune == 0xfe0f || rune == 0x20e3 || width == 2 ||
			     BETWEEN(rune, 0x1f1e6, 0x1f1ff));
			g_signal_emit(term, signals[SIGNAL_REGION_ERASED], 0,
			    priv->cluster_x, priv->cluster_y,
			    MIN(priv->cluster_x + ((prev->attr & GST_GLYPH_ATTR_WIDE) ? 1 : 0), priv->cols - 1),
			    priv->cluster_y);
			gst_glyph_append(prev, rune);
			if (promote && priv->cols == 1) {
				prev->attr |= GST_GLYPH_ATTR_WIDE;
			}
			if (promote && priv->cols > 1) {
				if (priv->cluster_x == priv->cols - 1 &&
				    (priv->mode & GST_MODE_WRAP)) {
					GstGlyph moved = GST_GLYPH_INIT;

					gst_glyph_assign(&moved, prev);
					gst_glyph_reset(prev);
					prev->rune = 0; /* wrap padding, not a logical space */
					prev->attr = GST_GLYPH_ATTR_WRAP;
					priv->screen[priv->cluster_y]->used = priv->cluster_x;
					gst_line_set_wrapped(priv->screen[priv->cluster_y], TRUE);
					gst_terminal_newline(term, TRUE);
					priv->cluster_x = 0;
					priv->cluster_y = priv->cursor.y;
					prev = gst_terminal_get_glyph(term, 0, priv->cursor.y);
					term_setchar(term, 0, &priv->cursor.glyph, 0, priv->cursor.y);
					gst_glyph_assign(prev, &moved);
					gst_glyph_clear(&moved);
				}
				if (priv->cluster_x + 1 < priv->cols) {
					GstGlyph *dummy;

					prev->attr |= GST_GLYPH_ATTR_WIDE;
					dummy = gst_terminal_get_glyph(term, priv->cluster_x + 1, priv->cluster_y);
					/* Clear an old wide occupant beyond the new dummy as well. */
					g_signal_emit(term, signals[SIGNAL_REGION_ERASED], 0,
					    priv->cluster_x, priv->cluster_y,
					    MIN(priv->cluster_x + ((dummy->attr & GST_GLYPH_ATTR_WIDE) ? 2 : 1), priv->cols - 1),
					    priv->cluster_y);
					if ((dummy->attr & GST_GLYPH_ATTR_WIDE) && priv->cluster_x + 2 < priv->cols) {
						gst_glyph_reset(gst_terminal_get_glyph(term, priv->cluster_x + 2, priv->cluster_y));
					}
					gst_glyph_reset(dummy);
					dummy->rune = 0;
					dummy->attr = GST_GLYPH_ATTR_WDUMMY;
					priv->screen[priv->cluster_y]->used = MAX(
					    priv->screen[priv->cluster_y]->used, priv->cluster_x + 2);
					priv->cursor.x = MIN(priv->cluster_x + 2, priv->cols - 1);
					priv->cursor.state &= ~GST_CURSOR_STATE_WRAPNEXT;
					if (priv->cluster_x + 2 >= priv->cols && (priv->mode & GST_MODE_WRAP)) {
						priv->cursor.state |= GST_CURSOR_STATE_WRAPNEXT;
					}
				}
			}
			priv->cluster_valid = TRUE;
			gst_glyph_append(&priv->last_glyph, rune);
			gst_terminal_mark_dirty(term, priv->cluster_y);
			return;
		}
	}
	/* Preserve unattached zero-width scalars in a cell rather than dropping them. */
	width = MAX(width, 1);

	/* Handle WRAPNEXT state */
	if (priv->cursor.state & GST_CURSOR_STATE_WRAPNEXT) {
		/* Mark current line as wrapped */
		line = priv->screen[priv->cursor.y];
		if (line != NULL) {
			GstGlyph *last = gst_line_get_glyph(line, priv->cols - 1);
			if (last != NULL) {
				last->attr |= GST_GLYPH_ATTR_WRAP;
			}
			gst_line_set_wrapped(line, TRUE);
		}

		gst_terminal_newline(term, TRUE);
		priv->cursor.state &= ~GST_CURSOR_STATE_WRAPNEXT;
	}

	/* Insert mode: shift characters right */
	if (priv->mode & GST_MODE_INSERT) {
		gst_terminal_insert_blanks(term, width);
	}

	/* Check if wide char fits */
	if (priv->cursor.x + MIN(width, priv->cols) > priv->cols) {
		/* No room for wide char; fill rest with space and wrap */
		GstGlyph *padding;

		padding = gst_terminal_get_glyph(term, priv->cursor.x, priv->cursor.y);
		g_signal_emit(term, signals[SIGNAL_REGION_ERASED], 0,
		    priv->cursor.x, priv->cursor.y, priv->cursor.x, priv->cursor.y);
		gst_glyph_reset(padding);
		padding->rune = 0;
		padding->attr = GST_GLYPH_ATTR_WRAP;
		priv->screen[priv->cursor.y]->used = priv->cursor.x;
		gst_line_set_wrapped(priv->screen[priv->cursor.y], TRUE);
		gst_terminal_newline(term, TRUE);
	}

	/* Place the character */
	if (width == 2 && priv->cursor.x + 1 < priv->cols) {
		/* Clean both previous occupants before installing the wide pair. */
		term_setchar(term, 0, &priv->cursor.glyph,
		    priv->cursor.x + 1, priv->cursor.y);
	}
	term_setchar(term, rune, &priv->cursor.glyph,
	    priv->cursor.x, priv->cursor.y);

	/* Handle wide character */
	if (width == 2) {
		GstGlyph *gp = gst_terminal_get_glyph(term,
		    priv->cursor.x, priv->cursor.y);
		if (gp != NULL) {
			gp->attr |= GST_GLYPH_ATTR_WIDE;
		}
		/* Set dummy cell */
		if (priv->cursor.x + 1 < priv->cols) {
			GstGlyph *dummy = gst_terminal_get_glyph(term,
			    priv->cursor.x + 1, priv->cursor.y);
			if (dummy != NULL) {
				gst_glyph_clear(dummy);
				dummy->rune = '\0';
				dummy->attr = GST_GLYPH_ATTR_WDUMMY;
			}
		}
	}

	/* Advance cursor */
	priv->cluster_x = priv->cursor.x;
	priv->cluster_y = priv->cursor.y;
	priv->cluster_valid = TRUE;
	priv->cursor.x += width;
	if (priv->cursor.x >= priv->cols) {
		if (priv->mode & GST_MODE_WRAP) {
			priv->cursor.x = priv->cols - 1;
			priv->cursor.state |= GST_CURSOR_STATE_WRAPNEXT;
		} else {
			priv->cursor.x = priv->cols - 1;
		}
	}

	priv->lastc = rune;
	gst_glyph_reset(&priv->last_glyph);
	priv->last_glyph.rune = gst_terminal_get_glyph(term, priv->cluster_x, priv->cluster_y)->rune;
	priv->dirty = TRUE;
}

/* ===== Terminal Write (Main Input Entry Point) ===== */

void
gst_terminal_write(
    GstTerminal *term,
    const gchar *data,
    gssize      len
){
	const gchar *p;
	const gchar *end;
	GstTerminalPrivate *priv;
	gchar combined[4 + 1];  /* max partial (4) + at least 1 new byte */
	gssize combined_len;

	g_return_if_fail(GST_IS_TERMINAL(term));
	g_return_if_fail(data != NULL);

	priv = term->priv;
	gst_terminal_init_screen(term);

	if (len < 0) {
		len = (gssize)strlen(data);
	}

	p = data;
	end = data + len;

	/*
	 * If we have a partial UTF-8 sequence from the previous write(),
	 * try to complete it with bytes from the new data.
	 */
	if (priv->utf8_partial_len > 0 && (priv->mode & GST_MODE_UTF8) && len > 0) {
		gint need;
		gunichar rune;

		/* Copy saved partial bytes into combined buffer */
		memcpy(combined, priv->utf8_partial, (gsize)priv->utf8_partial_len);
		combined_len = priv->utf8_partial_len;

		/*
		 * Append new bytes one at a time until we get a valid char
		 * or hit an error. Max UTF-8 sequence is 4 bytes total.
		 */
		need = 4 - priv->utf8_partial_len;
		if (need > (end - p)) {
			need = (gint)(end - p);
		}
		memcpy(combined + combined_len, p, (gsize)need);
		combined_len += need;

		rune = g_utf8_get_char_validated(combined, combined_len);
		/* GLib reports a NUL inside a sequence as incomplete, but a PTY
		 * NUL is a control byte; discard the invalid prefix and resume. */
		if (rune == (gunichar)-2 && memchr(combined, '\0', (gsize)combined_len) != NULL) {
			rune = (gunichar)-1;
		}
		if (rune == (gunichar)-2) {
			/*
			 * Still incomplete - save everything and wait for
			 * more data. This handles the rare case of a 4-byte
			 * sequence split across 3+ writes.
			 */
			memcpy(priv->utf8_partial, combined, (gsize)combined_len);
			priv->utf8_partial_len = (gint)combined_len;
			g_signal_emit(term, signals[SIGNAL_CONTENTS_CHANGED], 0);
			return;
		}

		priv->utf8_partial_len = 0;

		if (rune != (gunichar)-1) {
			/* Successfully decoded the combined sequence */
			gint char_len = (gint)(g_utf8_next_char(combined) - combined);
			gint new_consumed = char_len - (gint)(combined_len - need);

			gst_terminal_put_char(term, (GstRune)rune);

			/*
			 * Advance p past the new bytes that were consumed
			 * to complete this character.
			 */
			if (new_consumed < 0) {
				new_consumed = 0;
			}
			p += new_consumed;
		} else {
			/* Invalid sequence, discard the partial bytes */
			/* p stays where it was, process normally */
		}
	}

	while (p < end) {
		gunichar rune;

		/* NUL is a complete ignored control, not an incomplete UTF-8 rune. */
		if (*p == '\0') {
			p++;
			continue;
		}

		if (priv->mode & GST_MODE_UTF8) {
			/* Decode UTF-8 */
			rune = g_utf8_get_char_validated(p, end - p);
			if (rune == (gunichar)-2 && memchr(p, '\0', (gsize)(end - p)) != NULL) {
				rune = (gunichar)-1;
			}
			if (rune == (gunichar)-2) {
				/*
				 * Incomplete sequence at end of buffer.
				 * Save the partial bytes for the next write().
				 */
				gint remaining = (gint)(end - p);
				if (remaining > 0 && remaining <= 4) {
					memcpy(priv->utf8_partial, p, (gsize)remaining);
					priv->utf8_partial_len = remaining;
				}
				break;
			}
			if (rune == (gunichar)-1) {
				/* Invalid byte, skip */
				p++;
				continue;
			}
			gst_terminal_put_char(term, (GstRune)rune);
			p = g_utf8_next_char(p);
		} else {
			/* Single-byte mode */
			rune = (guchar)*p++;
			gst_terminal_put_char(term, (GstRune)rune);
		}
	}

	g_signal_emit(term, signals[SIGNAL_CONTENTS_CHANGED], 0);
}

/* ===== Key-to-escape-sequence mapping ===== */

/**
 * gst_terminal_key_event:
 * @self: a #GstTerminal
 * @keyval: unshifted X11-compatible keysym
 * @keycode: optional hardware keycode (reserved)
 * @state: normalized X11 modifier mask, including post-event lock state
 * @event_type: press (1), repeat (2), or release (3)
 * @text: (nullable): committed UTF-8 text
 *
 * Sends negotiated key events through response. Legacy processing remains
 * with the caller; consuming a release without output prevents text leakage.
 *
 * Returns: whether the event was consumed
 */
gboolean
gst_terminal_key_event(
	GstTerminal *self,
	guint keyval,
	guint keycode,
	guint state,
	guint event_type,
	const gchar *text
){
	/* Canonical Kitty functional encodings, not PUA aliases for legacy keys. */
	static const struct {
		guint keysym;
		guint code;
		gchar final;
	} functional[] = {
		{ XK_Escape, 27, 'u' }, { XK_Return, 13, 'u' },
		{ XK_Tab, 9, 'u' }, { XK_ISO_Left_Tab, 9, 'u' },
		{ XK_BackSpace, 127, 'u' },
		{ XK_Insert, 2, '~' }, { XK_Delete, 3, '~' },
		{ XK_Left, 1, 'D' }, { XK_Right, 1, 'C' },
		{ XK_Up, 1, 'A' }, { XK_Down, 1, 'B' },
		{ XK_Prior, 5, '~' }, { XK_Next, 6, '~' },
		{ XK_Home, 1, 'H' }, { XK_End, 1, 'F' },
		{ XK_Caps_Lock, 57358, 'u' }, { XK_Scroll_Lock, 57359, 'u' },
		{ XK_Num_Lock, 57360, 'u' }, { XK_Print, 57361, 'u' },
		{ XK_Pause, 57362, 'u' }, { XK_Menu, 57363, 'u' },
		{ XK_F1, 1, 'P' }, { XK_F2, 1, 'Q' },
		{ XK_F3, 13, '~' }, { XK_F4, 1, 'S' },
		{ XK_F5, 15, '~' }, { XK_F6, 17, '~' },
		{ XK_F7, 18, '~' }, { XK_F8, 19, '~' },
		{ XK_F9, 20, '~' }, { XK_F10, 21, '~' },
		{ XK_F11, 23, '~' }, { XK_F12, 24, '~' },
		{ XK_KP_Decimal, 57409, 'u' }, { XK_KP_Divide, 57410, 'u' },
		{ XK_KP_Multiply, 57411, 'u' }, { XK_KP_Subtract, 57412, 'u' },
		{ XK_KP_Add, 57413, 'u' }, { XK_KP_Enter, 57414, 'u' },
		{ XK_KP_Equal, 57415, 'u' }, { XK_KP_Separator, 57416, 'u' },
		{ XK_KP_Left, 57417, 'u' }, { XK_KP_Right, 57418, 'u' },
		{ XK_KP_Up, 57419, 'u' }, { XK_KP_Down, 57420, 'u' },
		{ XK_KP_Prior, 57421, 'u' }, { XK_KP_Next, 57422, 'u' },
		{ XK_KP_Home, 57423, 'u' }, { XK_KP_End, 57424, 'u' },
		{ XK_KP_Insert, 57425, 'u' }, { XK_KP_Delete, 57426, 'u' },
		{ XK_KP_Begin, 57427, 'u' },
		{ XF86XK_AudioPlay, 57428, 'u' }, { XF86XK_AudioPause, 57429, 'u' },
		{ XF86XK_AudioStop, 57432, 'u' }, { XF86XK_AudioForward, 57433, 'u' },
		{ XF86XK_AudioRewind, 57434, 'u' }, { XF86XK_AudioNext, 57435, 'u' },
		{ XF86XK_AudioPrev, 57436, 'u' }, { XF86XK_AudioRecord, 57437, 'u' },
		{ XF86XK_AudioLowerVolume, 57438, 'u' }, { XF86XK_AudioRaiseVolume, 57439, 'u' },
		{ XF86XK_AudioMute, 57440, 'u' },
		{ XK_Shift_L, 57441, 'u' }, { XK_Control_L, 57442, 'u' },
		{ XK_Alt_L, 57443, 'u' }, { XK_Super_L, 57444, 'u' },
		{ XK_Hyper_L, 57445, 'u' }, { XK_Meta_L, 57446, 'u' },
		{ XK_Shift_R, 57447, 'u' }, { XK_Control_R, 57448, 'u' },
		{ XK_Alt_R, 57449, 'u' }, { XK_Super_R, 57450, 'u' },
		{ XK_Hyper_R, 57451, 'u' }, { XK_Meta_R, 57452, 'u' },
		{ XK_ISO_Level3_Shift, 57453, 'u' }, { XK_ISO_Level5_Shift, 57454, 'u' }
	};
	guint flags, code, mods, i;
	gchar final;
	gboolean special, all, valid_text;
	const gchar *p;
	g_autoptr(GString) encoded = NULL;

	g_return_val_if_fail(GST_IS_TERMINAL(self), FALSE);
	g_return_val_if_fail(event_type >= 1 && event_type <= 3, FALSE);
	(void)keycode;
	flags = self->priv->keyboard_flags[(self->priv->mode & GST_MODE_ALTSCREEN) ? 1 : 0];
	if (self->priv->mode & GST_MODE_KBDLOCK) {
		return TRUE;
	}
	if (event_type == 3 && !(flags & 2)) {
		return TRUE;
	}
	if (!(flags & (1 | 2 | 8))) {
		return FALSE;
	}
	all = (flags & 8) != 0;
	code = 0;
	final = 'u';
	special = FALSE;
	for (i = 0; i < G_N_ELEMENTS(functional); i++) {
		if (functional[i].keysym == keyval) {
			code = functional[i].code;
			final = functional[i].final;
			special = TRUE;
			break;
		}
	}
	if (BETWEEN(keyval, XK_F13, XK_F35)) {
		code = 57376 + keyval - XK_F13;
		special = TRUE;
	} else if (BETWEEN(keyval, XK_KP_0, XK_KP_9)) {
		code = 57399 + keyval - XK_KP_0;
		special = TRUE;
	} else if (!special) {
		if (BETWEEN(keyval, 0x20, 0x7e) || BETWEEN(keyval, 0xa0, 0xff)) {
			code = keyval;
		} else if ((keyval & 0xff000000u) == 0x01000000u) {
			code = keyval & 0xffffffu;
		}
		if (code != 0 && (!g_unichar_validate(code) || ISCONTROL(code))) {
			code = 0;
		}
		code = g_unichar_tolower(code);
	}
	/* Normalize masks at the backend boundary; native Mod2/Mod5 mappings
	 * vary by XKB layout and must not be guessed from a hardware keycode. */
	mods = ((state & (1u << 0)) ? 1u : 0u) |
	    ((state & (1u << 3)) ? 2u : 0u) |
	    ((state & (1u << 2)) ? 4u : 0u) |
	    ((state & (1u << 6)) ? 8u : 0u) |
	    ((state & (1u << 5)) ? 16u : 0u) |
	    ((state & (1u << 7)) ? 32u : 0u) |
	    ((state & (1u << 1)) ? 64u : 0u) |
	    ((state & (1u << 4)) ? 128u : 0u);
	if (keyval == XK_ISO_Left_Tab) {
		mods |= 1;
	}
	if (!all) {
		if (BETWEEN(code, 57441, 57454)) {
			return TRUE;
		}
		/* Unmodified recovery keys remain usable after an application crash. */
		if (!(mods & 63) && (keyval == XK_Return || keyval == XK_Tab || keyval == XK_BackSpace)) {
			return event_type == 3;
		}
		/* Text stays on the legacy path; its releases must not leak out.
		 * With flag 2 alone, modified repeats/releases still carry event types. */
		if ((!special && (!(mods & 62) ||
		    (!(flags & 1) && (!(flags & 2) || event_type == 1)))) ||
		    (!(mods & 62) && (BETWEEN(code, 57399, 57413) || BETWEEN(code, 57415, 57416))) ||
		    (!(flags & 1) && (code == 27 || BETWEEN(code, 57399, 57427)))) {
			return event_type == 3;
		}
	}
	valid_text = text != NULL && *text != '\0' && g_utf8_validate(text, -1, NULL);
	if (valid_text) {
		for (p = text; *p != '\0'; p = g_utf8_next_char(p)) {
			if (ISCONTROL(g_utf8_get_char(p))) {
				valid_text = FALSE;
				break;
			}
		}
	}
	/* Unknown keys must not fabricate Unicode from a hardware code. A pure
	 * IME commit (keyval zero) uses key zero and associated text when enabled. */
	if (keyval == 0 && event_type == 3) {
		return TRUE;
	}
	if (code == 0 && !(keyval == 0 && all && (flags & 16) && valid_text)) {
		return event_type == 3 || all;
	}
	encoded = g_string_new("\033[");
	/* A bare final letter is canonical when neither modifiers nor events exist. */
	if (final == 'u' || final == '~' || mods != 0 || ((flags & 2) && event_type != 1)) {
		g_string_append_printf(encoded, "%u", code);
	}
	if (mods != 0 || ((flags & 2) && event_type != 1) ||
	    (all && (flags & 16) && valid_text && event_type != 3 && final == 'u')) {
		g_string_append_printf(encoded, ";%u", mods + 1);
		if ((flags & 2) && event_type != 1) {
			g_string_append_printf(encoded, ":%u", event_type);
		}
	}
	if (all && (flags & 16) && valid_text && event_type != 3 && final == 'u') {
		gchar separator = ';';

		for (p = text; *p != '\0'; p = g_utf8_next_char(p)) {
			g_string_append_printf(encoded, "%c%u", separator, g_utf8_get_char(p));
			separator = ':';
		}
	}
	g_string_append_c(encoded, final);
	term_response(self, encoded->str, (gssize)encoded->len);
	return TRUE;
}

/*
 * X11 modifier masks — also defined by the Wayland window backend,
 * but we guard here for compilation units that include Xlib directly.
 */
#ifndef ShiftMask
#define ShiftMask   (1 << 0)
#endif
#ifndef ControlMask
#define ControlMask (1 << 2)
#endif
#ifndef Mod1Mask
#define Mod1Mask    (1 << 3)
#endif

/*
 * GstKeyMapping:
 *
 * Maps a keysym (optionally constrained by modifiers and terminal modes)
 * to an escape sequence string.
 *
 * @keysym:    X11/xkb keysym value
 * @mask:      required modifier mask (0 = no modifiers required)
 * @string:    escape sequence to send (static string, no trailing NUL issues)
 * @appkey:    +1 = only when APPKEYPAD active, -1 = only when inactive, 0 = either
 * @appcursor: +1 = only when APPCURSOR active, -1 = only when inactive, 0 = either
 */
typedef struct {
	guint        keysym;
	guint        mask;
	const gchar *string;
	gint         appkey;
	gint         appcursor;
} GstKeyMapping;

/*
 * Static key mapping table, modelled after st's key[] array.
 * Entries are matched top-to-bottom; first match wins.
 * Modifier-specific entries come before generic ones.
 */
static const GstKeyMapping key_map[] = {
	/* Backspace */
	{ XK_BackSpace, 0,           "\177",   0,  0 },

	/* Tab / Shift-Tab (backtab) */
	{ XK_ISO_Left_Tab, 0,       "\033[Z", 0,  0 },
	{ XK_Tab,       ShiftMask,  "\033[Z", 0,  0 },
	{ XK_Tab,       0,          "\t",     0,  0 },

	/* Return */
	{ XK_Return,    Mod1Mask,    "\033\r", 0,  0 },
	{ XK_Return,    0,           "\r",     0,  0 },

	/* Escape */
	{ XK_Escape,    0,           "\033",   0,  0 },

	/* Insert / Delete */
	{ XK_Insert,    0,           "\033[2~", 0,  0 },
	{ XK_Delete,    0,           "\033[3~", 0,  0 },

	/* Home / End — mode-dependent */
	{ XK_Home,      0,           "\033[H",  0, -1 },
	{ XK_Home,      0,           "\033OH",  0, +1 },
	{ XK_End,       0,           "\033[F",  0, -1 },
	{ XK_End,       0,           "\033OF",  0, +1 },

	/* Page Up / Page Down */
	{ XK_Prior,     0,           "\033[5~", 0,  0 },
	{ XK_Next,      0,           "\033[6~", 0,  0 },

	/* Arrow keys — mode-dependent */
	{ XK_Up,        0,           "\033[A",  0, -1 },
	{ XK_Up,        0,           "\033OA",  0, +1 },
	{ XK_Down,      0,           "\033[B",  0, -1 },
	{ XK_Down,      0,           "\033OB",  0, +1 },
	{ XK_Right,     0,           "\033[C",  0, -1 },
	{ XK_Right,     0,           "\033OC",  0, +1 },
	{ XK_Left,      0,           "\033[D",  0, -1 },
	{ XK_Left,      0,           "\033OD",  0, +1 },

	/* Function keys F1-F4 (VT style: ESC O P-S) */
	{ XK_F1,        0,           "\033OP",   0,  0 },
	{ XK_F2,        0,           "\033OQ",   0,  0 },
	{ XK_F3,        0,           "\033OR",   0,  0 },
	{ XK_F4,        0,           "\033OS",   0,  0 },

	/* Function keys F5-F12 (xterm style: ESC [ nn ~) */
	{ XK_F5,        0,           "\033[15~", 0,  0 },
	{ XK_F6,        0,           "\033[17~", 0,  0 },
	{ XK_F7,        0,           "\033[18~", 0,  0 },
	{ XK_F8,        0,           "\033[19~", 0,  0 },
	{ XK_F9,        0,           "\033[20~", 0,  0 },
	{ XK_F10,       0,           "\033[21~", 0,  0 },
	{ XK_F11,       0,           "\033[23~", 0,  0 },
	{ XK_F12,       0,           "\033[24~", 0,  0 },

	/* Keypad — application mode sends ESC O {letter} */
	{ XK_KP_Enter,  0,           "\033OM", +1,  0 },
	{ XK_KP_Enter,  0,           "\r",     -1,  0 },
	{ XK_KP_Multiply, 0,        "\033Oj", +1,  0 },
	{ XK_KP_Add,    0,           "\033Ok", +1,  0 },
	{ XK_KP_Separator, 0,       "\033Ol", +1,  0 },
	{ XK_KP_Subtract, 0,        "\033Om", +1,  0 },
	{ XK_KP_Decimal, 0,         "\033On", +1,  0 },
	{ XK_KP_Divide, 0,          "\033Oo", +1,  0 },
	{ XK_KP_0,      0,          "\033Op", +1,  0 },
	{ XK_KP_1,      0,          "\033Oq", +1,  0 },
	{ XK_KP_2,      0,          "\033Or", +1,  0 },
	{ XK_KP_3,      0,          "\033Os", +1,  0 },
	{ XK_KP_4,      0,          "\033Ot", +1,  0 },
	{ XK_KP_5,      0,          "\033Ou", +1,  0 },
	{ XK_KP_6,      0,          "\033Ov", +1,  0 },
	{ XK_KP_7,      0,          "\033Ow", +1,  0 },
	{ XK_KP_8,      0,          "\033Ox", +1,  0 },
	{ XK_KP_9,      0,          "\033Oy", +1,  0 },

	/* Sentinel */
	{ 0, 0, NULL, 0, 0 }
};

/*
 * compute_xterm_mod_param:
 *
 * Computes the xterm modifier parameter value from X11 modifier bits.
 * Returns 0 if no modifiers are held, otherwise the xterm code:
 *   Shift=2, Alt=3, Alt+Shift=4, Ctrl=5, Ctrl+Shift=6,
 *   Ctrl+Alt=7, Ctrl+Alt+Shift=8
 */
static gint
compute_xterm_mod_param(guint state)
{
	gint mod;

	mod = 1;
	if (state & ShiftMask)   mod += 1;
	if (state & Mod1Mask)    mod += 2;
	if (state & ControlMask) mod += 4;

	return (mod > 1) ? mod : 0;
}

/*
 * gst_terminal_key_to_escape:
 * @term: a #GstTerminal (for mode queries)
 * @keysym: X11/xkb keysym
 * @state: modifier mask (ShiftMask, ControlMask, Mod1Mask)
 * @buf: (out): output buffer for escape sequence
 * @buflen: size of @buf
 *
 * Translates a keysym + modifiers into the corresponding VT escape
 * sequence, accounting for application cursor mode and keypad mode.
 * For keys that support xterm-style modifier encoding (arrows, Home,
 * End, Insert, Delete, PgUp, PgDn, F-keys), modifier parameters are
 * inserted automatically.
 *
 * Returns: number of bytes written to @buf, or 0 if no mapping exists
 */
gint
gst_terminal_key_to_escape(
	GstTerminal *term,
	guint        keysym,
	guint        state,
	gchar       *buf,
	gsize        buflen
){
	GstTermMode mode;
	gboolean appcursor;
	gboolean appkeypad;
	gint mod_param;
	const GstKeyMapping *k;

	g_return_val_if_fail(GST_IS_TERMINAL(term), 0);
	g_return_val_if_fail(buf != NULL && buflen >= 2, 0);

	mode = gst_terminal_get_mode(term);
	appcursor = (mode & GST_MODE_APPCURSOR) != 0;
	appkeypad = (mode & GST_MODE_APPKEYPAD) != 0;

	/*
	 * Strip shift/ctrl/alt from the mask used for table lookup —
	 * these modifiers are encoded as xterm parameters instead.
	 * The table's mask field is only for fixed modifier requirements
	 * like Shift+Tab → backtab.
	 */
	mod_param = compute_xterm_mod_param(state);

	for (k = key_map; k->string != NULL; k++) {
		if (k->keysym != keysym)
			continue;

		/* Check modifier constraint (exact match for table entries with mask) */
		if (k->mask != 0 && (state & k->mask) != k->mask)
			continue;
		if (k->mask == 0 && (state & (ShiftMask | ControlMask | Mod1Mask)) != 0) {
			/*
			 * Key has no modifier constraint but modifiers are held.
			 * For keys that support xterm-style modifier encoding,
			 * we'll inject the modifier param below. But we still
			 * need to match the base keysym, so continue checking
			 * mode constraints before deciding.
			 */
		}

		/* Check application cursor mode constraint */
		if (k->appcursor != 0) {
			if (k->appcursor > 0 && !appcursor)
				continue;
			if (k->appcursor < 0 && appcursor)
				continue;
		}

		/* Check application keypad mode constraint */
		if (k->appkey != 0) {
			if (k->appkey > 0 && !appkeypad)
				continue;
			if (k->appkey < 0 && appkeypad)
				continue;
		}

		/*
		 * Match found. If modifiers are held and the base sequence
		 * supports xterm-style encoding, inject modifier parameter.
		 *
		 * CSI sequences ending with ~ : ESC[code~ → ESC[code;mod~
		 * CSI/SS3 sequences with letter: ESC[X or ESCOX → ESC[1;modX
		 * Fixed strings (plain \r, \t, etc.) are sent as-is from table.
		 */
		if (k->mask != 0) {
			/* Entry has explicit modifier handling — use string as-is */
			gsize slen;

			slen = strlen(k->string);
			if (slen >= buflen)
				return 0;
			memcpy(buf, k->string, slen);
			return (gint)slen;
		}

		if (mod_param > 0 && k->string[0] == '\033' &&
		    (k->string[1] == '[' || k->string[1] == 'O')) {
			/*
			 * Inject xterm modifier parameter into the sequence.
			 */
			const gchar *base;
			gsize base_len;
			gint written;

			base = k->string;
			base_len = strlen(base);

			if (base_len >= 3 && base[base_len - 1] == '~') {
				/*
				 * CSI tilde-terminated: ESC[nn~ → ESC[nn;mod~
				 * Extract numeric code between '[' and '~'.
				 */
				written = g_snprintf(buf, (gulong)buflen,
					"\033[%.*s;%d~",
					(gint)(base_len - 3), base + 2,
					mod_param);
				return (written > 0 && (gsize)written < buflen)
					? written : 0;
			} else if (base_len >= 3) {
				/*
				 * SS3 or CSI letter-terminated: ESCOQ → ESC[1;modQ
				 * The final character is the command letter.
				 */
				gchar final_ch;

				final_ch = base[base_len - 1];
				written = g_snprintf(buf, (gulong)buflen,
					"\033[1;%d%c", mod_param, final_ch);
				return (written > 0 && (gsize)written < buflen)
					? written : 0;
			}
		}

		/* No modifier injection — copy string directly */
		{
			gsize slen;

			slen = strlen(k->string);
			if (slen >= buflen)
				return 0;
			memcpy(buf, k->string, slen);
			return (gint)slen;
		}
	}

	return 0;
}
