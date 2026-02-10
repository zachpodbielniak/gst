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

/* ===== Macros and constants ===== */

#define ISCONTROL(c)   ((c) < 0x20 || (c) == 0x7f || ((c) >= 0x80 && (c) <= 0x9f))
#define BETWEEN(x, a, b) ((x) >= (a) && (x) <= (b))
#define DEFAULT(a, b)  ((a) != 0 ? (a) : (b))

/* Size of CSI buffer */
#define CSI_BUF_SIZ    (256)

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

	/* Mode flags */
	GstTermMode mode;

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

	g_return_if_fail(GST_IS_TERMINAL(term));
	g_return_if_fail(cols > 0 && cols <= GST_MAX_COLS);
	g_return_if_fail(rows > 0 && rows <= GST_MAX_ROWS);

	priv = term->priv;

	if (cols == priv->cols && rows == priv->rows) {
		return;
	}

	gst_terminal_init_screen(term);

	new_primary = gst_terminal_alloc_screen(cols, rows);
	new_alt = gst_terminal_alloc_screen(cols, rows);

	copy_rows = MIN(priv->rows, rows);

	for (i = 0; i < copy_rows; i++) {
		gst_line_free(new_primary[i]);
		new_primary[i] = gst_line_copy(priv->primary[i]);
		gst_line_resize(new_primary[i], cols);

		gst_line_free(new_alt[i]);
		new_alt[i] = gst_line_copy(priv->alt[i]);
		gst_line_resize(new_alt[i], cols);
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
	    y + ((priv->cursor.state & GST_CURSOR_STATE_ORIGIN) ? priv->scroll_top : 0));
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
	priv->saved_cursors[idx] = priv->cursor;
	priv->saved_cursor_valid[idx] = TRUE;
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
		priv->cursor = priv->saved_cursors[idx];
		/*
		 * move_to clamps position and clears WRAPNEXT.
		 * This matches st: tmoveto() is the final call in
		 * tcursor(CURSOR_LOAD), so WRAPNEXT is always cleared.
		 */
		gst_terminal_move_to(term, priv->cursor.x, priv->cursor.y);
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
	while (i > 0 && line->glyphs[i - 1].rune == ' ') {
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
		gst_terminal_swap_screen(term);
	}

	if (old_mode != priv->mode) {
		g_signal_emit(term, signals[SIGNAL_MODE_CHANGED], 0, mode, enable);
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
	GstLine **tmp;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	tmp = priv->screen;

	if (priv->screen == priv->primary) {
		priv->screen = priv->alt;
	} else {
		priv->screen = priv->primary;
	}

	/* Clear what was the inactive screen (now active) */
	priv->mode ^= GST_MODE_ALTSCREEN;
	priv->dirty = TRUE;
	gst_terminal_mark_dirty(term, -1);
	(void)tmp;
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

	priv->cursor.x = 0;
	priv->cursor.y = 0;
	priv->cursor.state = GST_CURSOR_STATE_VISIBLE;
	priv->cursor.shape = GST_CURSOR_SHAPE_BLOCK;
	gst_glyph_reset(&priv->cursor.glyph);

	priv->mode = GST_MODE_WRAP | GST_MODE_UTF8;
	priv->esc = 0;
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
	gint i;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	for (i = 0; i < priv->rows; i++) {
		gst_line_clear(priv->screen[i]);
	}
	priv->dirty = TRUE;
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
	gint tmp, y;

	g_return_if_fail(GST_IS_TERMINAL(term));
	priv = term->priv;
	gst_terminal_init_screen(term);

	if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
	if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }

	x1 = CLAMP(x1, 0, priv->cols - 1);
	x2 = CLAMP(x2, 0, priv->cols - 1);
	y1 = CLAMP(y1, 0, priv->rows - 1);
	y2 = CLAMP(y2, 0, priv->rows - 1);

	for (y = y1; y <= y2; y++) {
		gst_line_clear_range(priv->screen[y], x1, x2 + 1);
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
	if (orig == priv->scroll_top)
	{
		for (i = orig; i < orig + n; i++)
		{
			g_signal_emit(term, signals[SIGNAL_LINE_SCROLLED_OUT], 0,
				priv->screen[i], priv->cols);
		}
	}

	/* Rotate lines up within the scroll region */
	for (i = orig; i <= priv->scroll_bot - n; i++) {
		tmp = priv->screen[i];
		priv->screen[i] = priv->screen[i + n];
		priv->screen[i + n] = tmp;
		gst_line_set_dirty(priv->screen[i], TRUE);
	}

	/* Clear the bottom lines */
	for (i = priv->scroll_bot - n + 1; i <= priv->scroll_bot; i++) {
		gst_line_clear(priv->screen[i]);
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

	for (i = priv->scroll_bot; i >= orig + n; i--) {
		tmp = priv->screen[i];
		priv->screen[i] = priv->screen[i - n];
		priv->screen[i - n] = tmp;
		gst_line_set_dirty(priv->screen[i], TRUE);
	}

	for (i = orig; i < orig + n; i++) {
		gst_line_clear(priv->screen[i]);
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
	gst_line_insert_blanks(priv->screen[priv->cursor.y], priv->cursor.x, n);
	priv->dirty = TRUE;
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
	gst_line_delete_chars(priv->screen[priv->cursor.y], priv->cursor.x, n);
	priv->dirty = TRUE;
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
	if (g->attr & GST_GLYPH_ATTR_WIDE) {
		/* Current cell is wide; blank the dummy cell */
		if (x + 1 < priv->cols) {
			GstGlyph *next = gst_line_get_glyph(line, x + 1);
			if (next != NULL) {
				next->rune = ' ';
				next->attr &= ~GST_GLYPH_ATTR_WDUMMY;
			}
		}
	} else if (g->attr & GST_GLYPH_ATTR_WDUMMY) {
		/* Current cell is a dummy; blank the wide cell */
		if (x > 0) {
			GstGlyph *prev = gst_line_get_glyph(line, x - 1);
			if (prev != NULL) {
				prev->rune = ' ';
				prev->attr &= ~GST_GLYPH_ATTR_WIDE;
			}
		}
	}

	gst_line_set_dirty(line, TRUE);

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
					 * 1049: save/restore cursor BEFORE swap.
					 * Matches st: tcursor() runs before tswapscreen().
					 * On set: save to primary slot (idx=0).
					 * On reset: restore from alt slot (idx=1).
					 */
					if (args[i] == 1049) {
						if (set) {
							gst_terminal_cursor_save(term);
						} else {
							gst_terminal_cursor_restore(term);
						}
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
		if (v == G_MAXLONG || v == G_MINLONG) {
			v = -1;
		}
		priv->csi_args[priv->csi_nargs++] = (gint)v;
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
	gint i;

	term_csiparse(term);
	cmd = priv->csi_mode[0];

	switch (cmd) {
	case '@': /* ICH - Insert Character */
		gst_terminal_insert_blanks(term, DEFAULT(priv->csi_args[0], 1));
		break;

	case 'A': /* CUU - Cursor Up */
		gst_terminal_move_to(term, priv->cursor.x,
		    priv->cursor.y - DEFAULT(priv->csi_args[0], 1));
		break;

	case 'B': /* CUD - Cursor Down */
	case 'e': /* VPR - Vertical Position Relative */
		gst_terminal_move_to(term, priv->cursor.x,
		    priv->cursor.y + DEFAULT(priv->csi_args[0], 1));
		break;

	case 'C': /* CUF - Cursor Forward */
	case 'a': /* HPR - Horizontal Position Relative */
		gst_terminal_move_to(term,
		    priv->cursor.x + DEFAULT(priv->csi_args[0], 1),
		    priv->cursor.y);
		break;

	case 'D': /* CUB - Cursor Backward */
		gst_terminal_move_to(term,
		    priv->cursor.x - DEFAULT(priv->csi_args[0], 1),
		    priv->cursor.y);
		break;

	case 'E': /* CNL - Cursor Next Line */
		gst_terminal_move_to(term, 0,
		    priv->cursor.y + DEFAULT(priv->csi_args[0], 1));
		break;

	case 'F': /* CPL - Cursor Previous Line */
		gst_terminal_move_to(term, 0,
		    priv->cursor.y - DEFAULT(priv->csi_args[0], 1));
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
		    priv->cursor.x + DEFAULT(priv->csi_args[0], 1) - 1,
		    priv->cursor.y);
		break;

	case 'Z': /* CBT - Cursor Backward Tabulation */
		gst_terminal_put_tab(term, -DEFAULT(priv->csi_args[0], 1));
		break;

	case 'b': /* REP - Repeat previous character */
		if (priv->lastc != 0) {
			gint count = DEFAULT(priv->csi_args[0], 1);
			while (count-- > 0) {
				gst_terminal_put_char(term, priv->lastc);
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
			    priv->cursor.y + 1, priv->cursor.x + 1);
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

	/*
	 * APC sequences must be dispatched with the raw buffer intact.
	 * term_strparse() replaces ';' with '\0' which corrupts protocols
	 * like kitty graphics that use ';' as a payload separator.
	 * Handle APC before parsing to preserve the raw buffer.
	 */
	if (priv->str_type == '_') {
		if (priv->str_buf != NULL && priv->str_len > 0) {
			priv->str_buf[priv->str_len] = '\0';
			g_signal_emit(term, signals[SIGNAL_ESCAPE_STRING], 0,
				(gchar)'_', priv->str_buf,
				(gulong)priv->str_len);
		}
		return;
	}

	term_strparse(term);

	if (priv->str_nargs == 0) {
		return;
	}

	switch (priv->str_type) {
	case ']': /* OSC - Operating System Command */
		par = (gint)strtol(priv->str_args[0], NULL, 10);
		switch (par) {
		case 0: /* Set icon and window title */
			if (priv->str_nargs > 1) {
				gst_terminal_set_title(term, priv->str_args[1]);
				gst_terminal_set_icon(term, priv->str_args[1]);
			}
			break;
		case 1: /* Set icon title */
			if (priv->str_nargs > 1) {
				gst_terminal_set_icon(term, priv->str_args[1]);
			}
			break;
		case 2: /* Set window title */
			if (priv->str_nargs > 1) {
				gst_terminal_set_title(term, priv->str_args[1]);
			}
			break;
		case 4:  /* Set color (index; spec) */
		case 10: /* Set foreground color */
		case 11: /* Set background color */
		case 12: /* Set cursor color */
		case 52: /* Set clipboard */
		case 104: /* Reset color */
			/* TODO: color and clipboard operations */
			break;
		default:
			break;
		}
		break;

	case 'k': /* Old title set */
		if (priv->str_nargs > 0) {
			gst_terminal_set_title(term, priv->str_args[0]);
		}
		break;

	case 'P': /* DCS */
	case '^': /* PM */
		/* Ignored for now */
		break;

	default:
		break;
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

		/* Accumulate byte into string buffer */
		if (priv->str_buf != NULL && priv->str_len < GST_MAX_STR_LEN) {
			/* Grow buffer if needed */
			if (priv->str_len + 1 >= priv->str_siz) {
				priv->str_siz *= 2;
				priv->str_buf = g_realloc(priv->str_buf, priv->str_siz);
			}
			priv->str_buf[priv->str_len++] = (gchar)rune;
		}
		return;
	}

	/* Handle control characters (< 0x20, 0x7f, or C1 0x80-0x9f) */
	if (ISCONTROL(rune)) {
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

	/*
	 * Combining character: overlay on previous cell without
	 * advancing the cursor. Matches st's tputc behavior where
	 * wcwidth()==0 chars are composed onto the preceding glyph.
	 */
	if (width == 0) {
		if (priv->cursor.x > 0) {
			GstGlyph *prev = gst_terminal_get_glyph(term,
			    priv->cursor.x - 1, priv->cursor.y);
			if (prev != NULL) {
				prev->rune = rune;
			}
			line = priv->screen[priv->cursor.y];
			if (line != NULL) {
				gst_line_set_dirty(line, TRUE);
			}
		}
		return;
	}

	/* Handle WRAPNEXT state */
	if (priv->cursor.state & GST_CURSOR_STATE_WRAPNEXT) {
		/* Mark current line as wrapped */
		line = priv->screen[priv->cursor.y];
		if (line != NULL) {
			GstGlyph *last = gst_line_get_glyph(line, priv->cols - 1);
			if (last != NULL) {
				last->attr |= GST_GLYPH_ATTR_WRAP;
			}
		}

		gst_terminal_newline(term, TRUE);
		priv->cursor.state &= ~GST_CURSOR_STATE_WRAPNEXT;
	}

	/* Insert mode: shift characters right */
	if (priv->mode & GST_MODE_INSERT) {
		gst_terminal_insert_blanks(term, width);
	}

	/* Check if wide char fits */
	if (priv->cursor.x + width > priv->cols) {
		/* No room for wide char; fill rest with space and wrap */
		gst_terminal_newline(term, TRUE);
	}

	/* Place the character */
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
				dummy->rune = '\0';
				dummy->attr = GST_GLYPH_ATTR_WDUMMY;
			}
		}
	}

	/* Advance cursor */
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

		if (priv->mode & GST_MODE_UTF8) {
			/* Decode UTF-8 */
			rune = g_utf8_get_char_validated(p, end - p);
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
