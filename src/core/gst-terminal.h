/*
 * gst-terminal.h - GST Terminal Class
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The main terminal emulation class. GstTerminal handles:
 * - Screen buffer management (primary and alternate)
 * - VT100/ANSI escape sequence processing
 * - Terminal mode flags
 * - Cursor state and movement
 * - Character output, scrolling, and screen manipulation
 */

#ifndef GST_TERMINAL_H
#define GST_TERMINAL_H

#include <glib-object.h>
#include "../gst-types.h"
#include "../gst-enums.h"
#include "../boxed/gst-glyph.h"
#include "../boxed/gst-cursor.h"
#include "gst-line.h"

G_BEGIN_DECLS

#define GST_TYPE_TERMINAL             (gst_terminal_get_type())
#define GST_TERMINAL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_TERMINAL, GstTerminal))
#define GST_TERMINAL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_TERMINAL, GstTerminalClass))
#define GST_IS_TERMINAL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_TERMINAL))
#define GST_IS_TERMINAL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_TERMINAL))
#define GST_TERMINAL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_TERMINAL, GstTerminalClass))

typedef struct _GstTerminalPrivate GstTerminalPrivate;

/**
 * GstTerminal:
 *
 * The terminal emulation class. This is the core component that
 * processes input data and maintains terminal state.
 */
struct _GstTerminal {
    GObject parent_instance;

    /*< private >*/
    GstTerminalPrivate *priv;
};

/**
 * GstTerminalClass:
 * @parent_class: parent class
 * @bell: virtual function for bell signal
 * @title_changed: virtual function for title change
 * @icon_changed: virtual function for icon change
 * @mode_changed: virtual function for mode changes
 * @reset: virtual function for terminal reset
 *
 * Class structure for GstTerminal.
 * Subclasses can override virtual functions to customize behavior.
 */
struct _GstTerminalClass {
    GObjectClass parent_class;

    /* Virtual functions */
    void (*bell)(GstTerminal *term);
    void (*title_changed)(GstTerminal *term, const gchar *title);
    void (*icon_changed)(GstTerminal *term, const gchar *icon);
    void (*mode_changed)(GstTerminal *term, GstTermMode mode, gboolean enabled);
    void (*reset)(GstTerminal *term, gboolean full);

    /*< private >*/
    gpointer padding[8];
};

GType gst_terminal_get_type(void) G_GNUC_CONST;

/* Construction */

/**
 * gst_terminal_new:
 * @cols: number of columns
 * @rows: number of rows
 *
 * Creates a new terminal with the specified dimensions.
 *
 * Returns: (transfer full): a new GstTerminal
 */
GstTerminal *gst_terminal_new(gint cols, gint rows);

/* Dimensions */

void gst_terminal_resize(GstTerminal *term, gint cols, gint rows);
void gst_terminal_get_size(GstTerminal *term, gint *cols, gint *rows);
gint gst_terminal_get_cols(GstTerminal *term);
gint gst_terminal_get_rows(GstTerminal *term);

/* Input processing */

/**
 * gst_terminal_write:
 * @term: a GstTerminal
 * @data: data to write (from PTY output)
 * @len: length of data, or -1 if NUL-terminated
 *
 * Writes data to the terminal for processing. This is the main
 * entry point for PTY output. Data is parsed for escape sequences
 * and control codes, with printable characters placed on screen.
 */
void gst_terminal_write(GstTerminal *term, const gchar *data, gssize len);

/**
 * gst_terminal_put_char:
 * @term: a GstTerminal
 * @rune: Unicode code point
 *
 * Processes a single character through the escape parser state machine.
 * Handles control codes, escape sequences, and normal character output.
 */
void gst_terminal_put_char(GstTerminal *term, GstRune rune);

/* Screen buffer access */

GstLine *gst_terminal_get_line(GstTerminal *term, gint row);
GstGlyph *gst_terminal_get_glyph(GstTerminal *term, gint col, gint row);

/* Cursor */

GstCursor *gst_terminal_get_cursor(GstTerminal *term);
void gst_terminal_set_cursor_pos(GstTerminal *term, gint x, gint y);

/**
 * gst_terminal_move_to:
 * @term: a GstTerminal
 * @x: column position
 * @y: row position
 *
 * Moves the cursor with bounds checking. If ORIGIN mode is active,
 * movement is constrained to the scroll region.
 */
void gst_terminal_move_to(GstTerminal *term, gint x, gint y);

/**
 * gst_terminal_move_to_abs:
 * @term: a GstTerminal
 * @x: column position
 * @y: row position (added to scroll_top if ORIGIN mode)
 *
 * Moves the cursor to an absolute position, adjusted for ORIGIN mode.
 */
void gst_terminal_move_to_abs(GstTerminal *term, gint x, gint y);

/**
 * gst_terminal_cursor_save:
 * @term: a GstTerminal
 *
 * Saves the current cursor state (position, attributes, charsets).
 * Separate save buffers are maintained for primary and alternate screens.
 */
void gst_terminal_cursor_save(GstTerminal *term);

/**
 * gst_terminal_cursor_restore:
 * @term: a GstTerminal
 *
 * Restores a previously saved cursor state.
 */
void gst_terminal_cursor_restore(GstTerminal *term);

/* Mode management */

GstTermMode gst_terminal_get_mode(GstTerminal *term);
void gst_terminal_set_mode(GstTerminal *term, GstTermMode mode, gboolean enable);
gboolean gst_terminal_has_mode(GstTerminal *term, GstTermMode mode);

/* Screen manipulation */

void gst_terminal_reset(GstTerminal *term, gboolean full);
void gst_terminal_clear(GstTerminal *term);
void gst_terminal_clear_region(GstTerminal *term, gint x1, gint y1, gint x2, gint y2);
void gst_terminal_scroll_up(GstTerminal *term, gint orig, gint n);
void gst_terminal_scroll_down(GstTerminal *term, gint orig, gint n);

/**
 * gst_terminal_newline:
 * @term: a GstTerminal
 * @first_col: whether to move to column 0
 *
 * Moves cursor to next line. If at bottom of scroll region,
 * scrolls up instead. Optionally moves to column 0.
 */
void gst_terminal_newline(GstTerminal *term, gboolean first_col);

/**
 * gst_terminal_insert_blanks:
 * @term: a GstTerminal
 * @n: number of blank characters to insert
 *
 * Inserts blank characters at the cursor position.
 */
void gst_terminal_insert_blanks(GstTerminal *term, gint n);

/**
 * gst_terminal_delete_chars:
 * @term: a GstTerminal
 * @n: number of characters to delete
 *
 * Deletes characters at the cursor position.
 */
void gst_terminal_delete_chars(GstTerminal *term, gint n);

/**
 * gst_terminal_insert_blank_lines:
 * @term: a GstTerminal
 * @n: number of lines to insert
 *
 * Inserts blank lines at the cursor row (scrolls down).
 */
void gst_terminal_insert_blank_lines(GstTerminal *term, gint n);

/**
 * gst_terminal_delete_lines:
 * @term: a GstTerminal
 * @n: number of lines to delete
 *
 * Deletes lines at the cursor row (scrolls up).
 */
void gst_terminal_delete_lines(GstTerminal *term, gint n);

/**
 * gst_terminal_put_tab:
 * @term: a GstTerminal
 * @n: number of tab stops to move (negative for backward)
 *
 * Moves the cursor forward or backward by tab stops.
 */
void gst_terminal_put_tab(GstTerminal *term, gint n);

/* Scroll region */

void gst_terminal_set_scroll_region(GstTerminal *term, gint top, gint bottom);
void gst_terminal_get_scroll_region(GstTerminal *term, gint *top, gint *bottom);

/* Tab stops */

gint gst_terminal_get_tabstop(GstTerminal *term);
void gst_terminal_set_tabstop(GstTerminal *term, gint tabstop);

/* Window properties */

const gchar *gst_terminal_get_title(GstTerminal *term);
const gchar *gst_terminal_get_icon(GstTerminal *term);
void gst_terminal_set_title(GstTerminal *term, const gchar *title);
void gst_terminal_set_icon(GstTerminal *term, const gchar *icon);

/* Dirty tracking */

gboolean gst_terminal_is_dirty(GstTerminal *term);
void gst_terminal_mark_dirty(GstTerminal *term, gint row);
void gst_terminal_clear_dirty(GstTerminal *term);

/* Screen state */

gboolean gst_terminal_is_altscreen(GstTerminal *term);

/**
 * gst_terminal_swap_screen:
 * @term: a GstTerminal
 *
 * Swaps between primary and alternate screen buffers.
 */
void gst_terminal_swap_screen(GstTerminal *term);

/**
 * gst_terminal_line_len:
 * @term: a GstTerminal
 * @row: row index
 *
 * Gets the effective length of a line (excluding trailing spaces,
 * unless the line is wrapped).
 *
 * Returns: effective line length
 */
gint gst_terminal_line_len(GstTerminal *term, gint row);

/* Key-to-escape-sequence translation */

/**
 * gst_terminal_key_to_escape:
 * @term: a #GstTerminal
 * @keysym: X11/xkb keysym value
 * @state: modifier mask (ShiftMask, ControlMask, Mod1Mask)
 * @buf: (out): output buffer for the escape sequence
 * @buflen: size of @buf in bytes (must be >= 2)
 *
 * Translates a keysym and modifier state into the corresponding VT
 * escape sequence, accounting for application cursor mode and
 * application keypad mode.  For keys that support xterm-style modifier
 * encoding (arrows, Home/End, F-keys, etc.) the modifier parameter
 * is inserted automatically.
 *
 * Returns: number of bytes written to @buf, or 0 if no mapping exists
 */
gint gst_terminal_key_to_escape(GstTerminal *term, guint keysym,
                                guint state, gchar *buf, gsize buflen);

G_END_DECLS

#endif /* GST_TERMINAL_H */
