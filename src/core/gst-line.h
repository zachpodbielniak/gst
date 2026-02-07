/*
 * gst-line.h - GST Line Management
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A line represents a row of glyphs in the terminal buffer.
 * Lines are managed in a screen buffer and may have associated
 * metadata for wrapping, dirty state, etc.
 */

#ifndef GST_LINE_H
#define GST_LINE_H

#include <glib-object.h>
#include "../gst-types.h"
#include "../boxed/gst-glyph.h"

G_BEGIN_DECLS

#define GST_TYPE_LINE (gst_line_get_type())

/**
 * GstLineFlags:
 * @GST_LINE_FLAG_NONE: No flags set
 * @GST_LINE_FLAG_DIRTY: Line has been modified and needs redraw
 * @GST_LINE_FLAG_WRAPPED: Line is continuation of previous line
 * @GST_LINE_FLAG_SELECTED: Line contains selected text
 *
 * Flags for line state.
 */
typedef enum {
    GST_LINE_FLAG_NONE     = 0,
    GST_LINE_FLAG_DIRTY    = 1 << 0,
    GST_LINE_FLAG_WRAPPED  = 1 << 1,
    GST_LINE_FLAG_SELECTED = 1 << 2
} GstLineFlags;

GType gst_line_flags_get_type(void) G_GNUC_CONST;
#define GST_TYPE_LINE_FLAGS (gst_line_flags_get_type())

/**
 * GstLine:
 *
 * Represents a single row in the terminal buffer.
 * Contains an array of glyphs and line metadata.
 */
struct _GstLine {
    GstGlyph    *glyphs;    /* Array of glyphs */
    gint        len;        /* Number of glyphs (columns) */
    GstLineFlags flags;     /* Line flags */
};

/**
 * gst_line_get_type:
 *
 * Gets the GType for the GstLine boxed type.
 *
 * Returns: the GType
 */
GType gst_line_get_type(void) G_GNUC_CONST;

/**
 * gst_line_new:
 * @cols: number of columns
 *
 * Creates a new line with the specified number of columns.
 * All glyphs are initialized to empty spaces.
 *
 * Returns: (transfer full): a new GstLine
 */
GstLine *gst_line_new(gint cols);

/**
 * gst_line_copy:
 * @line: a GstLine
 *
 * Creates a deep copy of the line.
 *
 * Returns: (transfer full): a copy of @line
 */
GstLine *gst_line_copy(const GstLine *line);

/**
 * gst_line_free:
 * @line: a GstLine
 *
 * Frees the memory allocated for the line.
 */
void gst_line_free(GstLine *line);

/**
 * gst_line_resize:
 * @line: a GstLine
 * @new_cols: new number of columns
 *
 * Resizes the line to the specified number of columns.
 * If growing, new cells are initialized to empty.
 * If shrinking, excess cells are discarded.
 */
void gst_line_resize(GstLine *line, gint new_cols);

/**
 * gst_line_get_glyph:
 * @line: a GstLine
 * @col: column index (0-based)
 *
 * Gets a pointer to the glyph at the specified column.
 *
 * Returns: (transfer none): pointer to the glyph, or NULL if out of bounds
 */
GstGlyph *gst_line_get_glyph(GstLine *line, gint col);

/**
 * gst_line_get_glyph_const:
 * @line: a GstLine
 * @col: column index (0-based)
 *
 * Gets a const pointer to the glyph at the specified column.
 *
 * Returns: (transfer none): const pointer to the glyph, or NULL if out of bounds
 */
const GstGlyph *gst_line_get_glyph_const(const GstLine *line, gint col);

/**
 * gst_line_set_glyph:
 * @line: a GstLine
 * @col: column index (0-based)
 * @glyph: the glyph to copy
 *
 * Copies a glyph to the specified column.
 * The line is marked dirty.
 */
void gst_line_set_glyph(GstLine *line, gint col, const GstGlyph *glyph);

/**
 * gst_line_clear:
 * @line: a GstLine
 *
 * Clears all glyphs in the line to empty spaces.
 * Preserves line flags.
 */
void gst_line_clear(GstLine *line);

/**
 * gst_line_clear_range:
 * @line: a GstLine
 * @start: starting column (inclusive)
 * @end: ending column (exclusive)
 *
 * Clears a range of columns to empty spaces.
 */
void gst_line_clear_range(GstLine *line, gint start, gint end);

/**
 * gst_line_clear_to_end:
 * @line: a GstLine
 * @col: starting column
 *
 * Clears from the specified column to the end of the line.
 */
void gst_line_clear_to_end(GstLine *line, gint col);

/**
 * gst_line_clear_to_start:
 * @line: a GstLine
 * @col: ending column (inclusive)
 *
 * Clears from the start of the line to the specified column.
 */
void gst_line_clear_to_start(GstLine *line, gint col);

/**
 * gst_line_delete_chars:
 * @line: a GstLine
 * @col: starting column
 * @n: number of characters to delete
 *
 * Deletes characters at the specified position, shifting
 * remaining characters left. Empty space is added at the end.
 */
void gst_line_delete_chars(GstLine *line, gint col, gint n);

/**
 * gst_line_insert_blanks:
 * @line: a GstLine
 * @col: starting column
 * @n: number of blank characters to insert
 *
 * Inserts blank characters at the specified position,
 * shifting remaining characters right. Characters that
 * would go beyond the line length are discarded.
 */
void gst_line_insert_blanks(GstLine *line, gint col, gint n);

/**
 * gst_line_is_dirty:
 * @line: a GstLine
 *
 * Checks if the line is marked dirty (needs redraw).
 *
 * Returns: %TRUE if the line is dirty
 */
gboolean gst_line_is_dirty(const GstLine *line);

/**
 * gst_line_set_dirty:
 * @line: a GstLine
 * @dirty: whether the line is dirty
 *
 * Sets the dirty flag on the line.
 */
void gst_line_set_dirty(GstLine *line, gboolean dirty);

/**
 * gst_line_is_wrapped:
 * @line: a GstLine
 *
 * Checks if the line is a continuation of the previous line.
 *
 * Returns: %TRUE if the line is wrapped
 */
gboolean gst_line_is_wrapped(const GstLine *line);

/**
 * gst_line_set_wrapped:
 * @line: a GstLine
 * @wrapped: whether the line is wrapped
 *
 * Sets the wrapped flag on the line.
 */
void gst_line_set_wrapped(GstLine *line, gboolean wrapped);

/**
 * gst_line_to_string:
 * @line: a GstLine
 *
 * Converts the line to a UTF-8 string.
 * Trailing spaces are preserved.
 *
 * Returns: (transfer full): UTF-8 string representation
 */
gchar *gst_line_to_string(const GstLine *line);

/**
 * gst_line_to_string_range:
 * @line: a GstLine
 * @start: starting column
 * @end: ending column (exclusive)
 *
 * Converts a range of columns to a UTF-8 string.
 *
 * Returns: (transfer full): UTF-8 string representation
 */
gchar *gst_line_to_string_range(const GstLine *line, gint start, gint end);

/**
 * gst_line_find_last_nonspace:
 * @line: a GstLine
 *
 * Finds the column index of the last non-space character.
 *
 * Returns: column index (0-based), or -1 if line is all spaces
 */
gint gst_line_find_last_nonspace(const GstLine *line);

G_END_DECLS

#endif /* GST_LINE_H */
