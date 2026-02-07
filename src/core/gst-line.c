/*
 * gst-line.c - GST Line Management Implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the GstLine boxed type for terminal line management.
 */

#include "gst-line.h"
#include "../util/gst-utf8.h"
#include <string.h>

G_DEFINE_BOXED_TYPE(GstLine, gst_line, gst_line_copy, gst_line_free)

/*
 * gst_line_flags_get_type:
 *
 * Registers the GstLineFlags flags type.
 *
 * Returns: the GType for GstLineFlags
 */
GType
gst_line_flags_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GFlagsValue values[] = {
            { GST_LINE_FLAG_NONE, "GST_LINE_FLAG_NONE", "none" },
            { GST_LINE_FLAG_DIRTY, "GST_LINE_FLAG_DIRTY", "dirty" },
            { GST_LINE_FLAG_WRAPPED, "GST_LINE_FLAG_WRAPPED", "wrapped" },
            { GST_LINE_FLAG_SELECTED, "GST_LINE_FLAG_SELECTED", "selected" },
            { 0, NULL, NULL }
        };

        GType new_type = g_flags_register_static("GstLineFlags", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * init_glyphs:
 * @glyphs: array of glyphs to initialize
 * @n: number of glyphs
 *
 * Initializes an array of glyphs to empty spaces with default attributes.
 */
static void
init_glyphs(
    GstGlyph    *glyphs,
    gint        n
){
    gint i;

    for (i = 0; i < n; i++) {
        glyphs[i].rune = ' ';
        glyphs[i].attr = GST_GLYPH_ATTR_NONE;
        glyphs[i].fg = GST_COLOR_DEFAULT_FG;
        glyphs[i].bg = GST_COLOR_DEFAULT_BG;
    }
}

/**
 * gst_line_new:
 * @cols: number of columns
 *
 * Creates a new line with the specified number of columns.
 * All glyphs are initialized to empty spaces with default colors.
 *
 * Returns: (transfer full): a newly allocated GstLine
 */
GstLine *
gst_line_new(gint cols)
{
    GstLine *line;

    g_return_val_if_fail(cols > 0, NULL);
    g_return_val_if_fail(cols <= GST_MAX_COLS, NULL);

    line = g_slice_new(GstLine);
    line->len = cols;
    line->flags = GST_LINE_FLAG_DIRTY;
    line->glyphs = g_new(GstGlyph, cols);

    init_glyphs(line->glyphs, cols);

    return line;
}

/**
 * gst_line_copy:
 * @line: a GstLine to copy
 *
 * Creates a deep copy of the line, including all glyphs.
 *
 * Returns: (transfer full): a copy of @line
 */
GstLine *
gst_line_copy(const GstLine *line)
{
    GstLine *copy;

    g_return_val_if_fail(line != NULL, NULL);

    copy = g_slice_new(GstLine);
    copy->len = line->len;
    copy->flags = line->flags;
    copy->glyphs = g_new(GstGlyph, line->len);

    memcpy(copy->glyphs, line->glyphs, sizeof(GstGlyph) * line->len);

    return copy;
}

/**
 * gst_line_free:
 * @line: a GstLine to free
 *
 * Frees the memory allocated for the line and its glyphs.
 */
void
gst_line_free(GstLine *line)
{
    if (line != NULL) {
        g_free(line->glyphs);
        g_slice_free(GstLine, line);
    }
}

/**
 * gst_line_resize:
 * @line: a GstLine
 * @new_cols: new number of columns
 *
 * Resizes the line to the specified number of columns.
 * If growing, new cells are initialized to empty spaces.
 * If shrinking, excess cells are discarded.
 */
void
gst_line_resize(
    GstLine *line,
    gint    new_cols
){
    GstGlyph *new_glyphs;
    gint copy_len;

    g_return_if_fail(line != NULL);
    g_return_if_fail(new_cols > 0);
    g_return_if_fail(new_cols <= GST_MAX_COLS);

    if (new_cols == line->len) {
        return;
    }

    new_glyphs = g_new(GstGlyph, new_cols);

    /* Copy existing glyphs (up to the smaller of old/new size) */
    copy_len = MIN(line->len, new_cols);
    memcpy(new_glyphs, line->glyphs, sizeof(GstGlyph) * copy_len);

    /* Initialize new cells if growing */
    if (new_cols > line->len) {
        init_glyphs(new_glyphs + line->len, new_cols - line->len);
    }

    g_free(line->glyphs);
    line->glyphs = new_glyphs;
    line->len = new_cols;
    line->flags |= GST_LINE_FLAG_DIRTY;
}

/**
 * gst_line_get_glyph:
 * @line: a GstLine
 * @col: column index (0-based)
 *
 * Gets a pointer to the glyph at the specified column.
 * Returns NULL if the column is out of bounds.
 *
 * Returns: (transfer none): pointer to the glyph
 */
GstGlyph *
gst_line_get_glyph(
    GstLine *line,
    gint    col
){
    g_return_val_if_fail(line != NULL, NULL);

    if (col < 0 || col >= line->len) {
        return NULL;
    }

    return &line->glyphs[col];
}

/**
 * gst_line_get_glyph_const:
 * @line: a GstLine
 * @col: column index (0-based)
 *
 * Gets a const pointer to the glyph at the specified column.
 *
 * Returns: (transfer none): const pointer to the glyph
 */
const GstGlyph *
gst_line_get_glyph_const(
    const GstLine   *line,
    gint            col
){
    g_return_val_if_fail(line != NULL, NULL);

    if (col < 0 || col >= line->len) {
        return NULL;
    }

    return &line->glyphs[col];
}

/**
 * gst_line_set_glyph:
 * @line: a GstLine
 * @col: column index (0-based)
 * @glyph: the glyph to copy
 *
 * Copies a glyph to the specified column.
 * The line is marked dirty.
 */
void
gst_line_set_glyph(
    GstLine         *line,
    gint            col,
    const GstGlyph  *glyph
){
    g_return_if_fail(line != NULL);
    g_return_if_fail(glyph != NULL);
    g_return_if_fail(col >= 0 && col < line->len);

    line->glyphs[col] = *glyph;
    line->flags |= GST_LINE_FLAG_DIRTY;
}

/**
 * gst_line_clear:
 * @line: a GstLine
 *
 * Clears all glyphs in the line to empty spaces.
 * Line flags are preserved but dirty flag is set.
 */
void
gst_line_clear(GstLine *line)
{
    g_return_if_fail(line != NULL);

    init_glyphs(line->glyphs, line->len);
    line->flags |= GST_LINE_FLAG_DIRTY;
}

/**
 * gst_line_clear_range:
 * @line: a GstLine
 * @start: starting column (inclusive)
 * @end: ending column (exclusive)
 *
 * Clears a range of columns to empty spaces.
 */
void
gst_line_clear_range(
    GstLine *line,
    gint    start,
    gint    end
){
    g_return_if_fail(line != NULL);

    start = CLAMP(start, 0, line->len);
    end = CLAMP(end, 0, line->len);

    if (start >= end) {
        return;
    }

    init_glyphs(&line->glyphs[start], end - start);
    line->flags |= GST_LINE_FLAG_DIRTY;
}

/**
 * gst_line_clear_to_end:
 * @line: a GstLine
 * @col: starting column
 *
 * Clears from the specified column to the end of the line.
 */
void
gst_line_clear_to_end(
    GstLine *line,
    gint    col
){
    g_return_if_fail(line != NULL);

    gst_line_clear_range(line, col, line->len);
}

/**
 * gst_line_clear_to_start:
 * @line: a GstLine
 * @col: ending column (inclusive)
 *
 * Clears from the start of the line to the specified column.
 */
void
gst_line_clear_to_start(
    GstLine *line,
    gint    col
){
    g_return_if_fail(line != NULL);

    gst_line_clear_range(line, 0, col + 1);
}

/**
 * gst_line_delete_chars:
 * @line: a GstLine
 * @col: starting column
 * @n: number of characters to delete
 *
 * Deletes characters starting at the specified position.
 * Remaining characters are shifted left.
 * Empty space is added at the end.
 */
void
gst_line_delete_chars(
    GstLine *line,
    gint    col,
    gint    n
){
    gint move_count;

    g_return_if_fail(line != NULL);
    g_return_if_fail(col >= 0 && col < line->len);
    g_return_if_fail(n > 0);

    /* Limit deletion to end of line */
    if (col + n > line->len) {
        n = line->len - col;
    }

    /* Shift remaining characters left */
    move_count = line->len - col - n;
    if (move_count > 0) {
        memmove(&line->glyphs[col],
                &line->glyphs[col + n],
                sizeof(GstGlyph) * move_count);
    }

    /* Initialize empty space at end */
    init_glyphs(&line->glyphs[line->len - n], n);
    line->flags |= GST_LINE_FLAG_DIRTY;
}

/**
 * gst_line_insert_blanks:
 * @line: a GstLine
 * @col: starting column
 * @n: number of blank characters to insert
 *
 * Inserts blank characters at the specified position.
 * Remaining characters are shifted right.
 * Characters beyond line length are discarded.
 */
void
gst_line_insert_blanks(
    GstLine *line,
    gint    col,
    gint    n
){
    gint move_count;
    gint insert_count;

    g_return_if_fail(line != NULL);
    g_return_if_fail(col >= 0 && col < line->len);
    g_return_if_fail(n > 0);

    /* Calculate how many chars to move and insert */
    insert_count = MIN(n, line->len - col);
    move_count = line->len - col - insert_count;

    /* Shift existing characters right */
    if (move_count > 0) {
        memmove(&line->glyphs[col + insert_count],
                &line->glyphs[col],
                sizeof(GstGlyph) * move_count);
    }

    /* Initialize blank space */
    init_glyphs(&line->glyphs[col], insert_count);
    line->flags |= GST_LINE_FLAG_DIRTY;
}

/**
 * gst_line_is_dirty:
 * @line: a GstLine
 *
 * Checks if the line is marked dirty (needs redraw).
 *
 * Returns: %TRUE if the line is dirty
 */
gboolean
gst_line_is_dirty(const GstLine *line)
{
    g_return_val_if_fail(line != NULL, FALSE);

    return (line->flags & GST_LINE_FLAG_DIRTY) != 0;
}

/**
 * gst_line_set_dirty:
 * @line: a GstLine
 * @dirty: whether the line is dirty
 *
 * Sets or clears the dirty flag on the line.
 */
void
gst_line_set_dirty(
    GstLine     *line,
    gboolean    dirty
){
    g_return_if_fail(line != NULL);

    if (dirty) {
        line->flags |= GST_LINE_FLAG_DIRTY;
    } else {
        line->flags &= ~GST_LINE_FLAG_DIRTY;
    }
}

/**
 * gst_line_is_wrapped:
 * @line: a GstLine
 *
 * Checks if the line is a continuation of the previous line.
 *
 * Returns: %TRUE if the line is wrapped
 */
gboolean
gst_line_is_wrapped(const GstLine *line)
{
    g_return_val_if_fail(line != NULL, FALSE);

    return (line->flags & GST_LINE_FLAG_WRAPPED) != 0;
}

/**
 * gst_line_set_wrapped:
 * @line: a GstLine
 * @wrapped: whether the line is wrapped
 *
 * Sets or clears the wrapped flag on the line.
 */
void
gst_line_set_wrapped(
    GstLine     *line,
    gboolean    wrapped
){
    g_return_if_fail(line != NULL);

    if (wrapped) {
        line->flags |= GST_LINE_FLAG_WRAPPED;
    } else {
        line->flags &= ~GST_LINE_FLAG_WRAPPED;
    }
}

/**
 * gst_line_to_string:
 * @line: a GstLine
 *
 * Converts the line to a UTF-8 string.
 * Each glyph's rune is converted to UTF-8.
 * Wide dummy cells are skipped.
 *
 * Returns: (transfer full): UTF-8 string representation
 */
gchar *
gst_line_to_string(const GstLine *line)
{
    g_return_val_if_fail(line != NULL, NULL);

    return gst_line_to_string_range(line, 0, line->len);
}

/**
 * gst_line_to_string_range:
 * @line: a GstLine
 * @start: starting column
 * @end: ending column (exclusive)
 *
 * Converts a range of columns to a UTF-8 string.
 * Wide dummy cells are skipped.
 *
 * Returns: (transfer full): UTF-8 string representation
 */
gchar *
gst_line_to_string_range(
    const GstLine   *line,
    gint            start,
    gint            end
){
    GString *str;
    gint i;
    gchar utf8_buf[6];
    gint utf8_len;

    g_return_val_if_fail(line != NULL, NULL);

    start = CLAMP(start, 0, line->len);
    end = CLAMP(end, 0, line->len);

    if (start >= end) {
        return g_strdup("");
    }

    /* Allocate enough space for worst case (6 bytes per char) */
    str = g_string_sized_new((end - start) * 4);

    for (i = start; i < end; i++) {
        const GstGlyph *g = &line->glyphs[i];

        /* Skip wide dummy cells */
        if (g->attr & GST_GLYPH_ATTR_WDUMMY) {
            continue;
        }

        /* Convert rune to UTF-8 */
        utf8_len = g_unichar_to_utf8(g->rune, utf8_buf);
        if (utf8_len > 0) {
            g_string_append_len(str, utf8_buf, utf8_len);
        }
    }

    return g_string_free(str, FALSE);
}

/**
 * gst_line_find_last_nonspace:
 * @line: a GstLine
 *
 * Finds the column index of the last non-space character.
 * This is useful for determining the actual content length.
 *
 * Returns: column index (0-based), or -1 if line is all spaces
 */
gint
gst_line_find_last_nonspace(const GstLine *line)
{
    gint i;

    g_return_val_if_fail(line != NULL, -1);

    for (i = line->len - 1; i >= 0; i--) {
        if (line->glyphs[i].rune != ' ' &&
            line->glyphs[i].rune != '\0' &&
            !(line->glyphs[i].attr & GST_GLYPH_ATTR_WDUMMY)) {
            return i;
        }
    }

    return -1;
}
