/*
 * gst-cursor.c - GST Cursor Boxed Type Implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the GstCursor boxed type representing
 * the terminal cursor position and state.
 */

#include "gst-cursor.h"

G_DEFINE_BOXED_TYPE(GstCursor, gst_cursor, gst_cursor_copy, gst_cursor_free)

/**
 * gst_cursor_new:
 *
 * Creates a new cursor at position (0, 0) with default attributes.
 * The cursor is visible by default.
 *
 * Returns: (transfer full): a newly allocated GstCursor
 */
GstCursor *
gst_cursor_new(void)
{
    GstCursor *cursor;

    cursor = g_slice_new(GstCursor);
    cursor->x = 0;
    cursor->y = 0;
    cursor->glyph.rune = ' ';
    cursor->glyph.attr = GST_GLYPH_ATTR_NONE;
    cursor->glyph.fg = GST_COLOR_DEFAULT_FG;
    cursor->glyph.bg = GST_COLOR_DEFAULT_BG;
    cursor->state = GST_CURSOR_STATE_VISIBLE;
    cursor->shape = GST_CURSOR_SHAPE_BLOCK;

    return cursor;
}

/**
 * gst_cursor_new_at:
 * @x: column position
 * @y: row position
 *
 * Creates a new cursor at the specified position.
 *
 * Returns: (transfer full): a newly allocated GstCursor
 */
GstCursor *
gst_cursor_new_at(
    gint x,
    gint y
){
    GstCursor *cursor;

    cursor = gst_cursor_new();
    cursor->x = x;
    cursor->y = y;

    return cursor;
}

/**
 * gst_cursor_copy:
 * @cursor: a GstCursor to copy
 *
 * Creates a deep copy of the cursor, including all state.
 *
 * Returns: (transfer full): a copy of @cursor
 */
GstCursor *
gst_cursor_copy(const GstCursor *cursor)
{
    GstCursor *copy;

    g_return_val_if_fail(cursor != NULL, NULL);

    copy = g_slice_new(GstCursor);
    copy->x = cursor->x;
    copy->y = cursor->y;
    copy->glyph = cursor->glyph;
    copy->state = cursor->state;
    copy->shape = cursor->shape;

    return copy;
}

/**
 * gst_cursor_free:
 * @cursor: a GstCursor to free
 *
 * Frees the memory allocated for the cursor.
 */
void
gst_cursor_free(GstCursor *cursor)
{
    if (cursor != NULL) {
        g_slice_free(GstCursor, cursor);
    }
}

/**
 * gst_cursor_move_to:
 * @cursor: a GstCursor
 * @x: new column position
 * @y: new row position
 *
 * Moves the cursor to the specified absolute position.
 * This clears any pending wrap state.
 */
void
gst_cursor_move_to(
    GstCursor   *cursor,
    gint        x,
    gint        y
){
    g_return_if_fail(cursor != NULL);

    cursor->x = x;
    cursor->y = y;
    cursor->state &= ~GST_CURSOR_STATE_WRAPNEXT;
}

/**
 * gst_cursor_move_rel:
 * @cursor: a GstCursor
 * @dx: column offset (can be negative)
 * @dy: row offset (can be negative)
 *
 * Moves the cursor by the specified relative offset.
 * This clears any pending wrap state.
 */
void
gst_cursor_move_rel(
    GstCursor   *cursor,
    gint        dx,
    gint        dy
){
    g_return_if_fail(cursor != NULL);

    cursor->x += dx;
    cursor->y += dy;
    cursor->state &= ~GST_CURSOR_STATE_WRAPNEXT;
}

/**
 * gst_cursor_save:
 * @cursor: a GstCursor
 *
 * Creates a saved copy of the cursor state.
 * This is used for DECSC/DECRC escape sequences.
 *
 * Returns: (transfer full): a saved cursor state
 */
GstCursor *
gst_cursor_save(const GstCursor *cursor)
{
    g_return_val_if_fail(cursor != NULL, NULL);

    return gst_cursor_copy(cursor);
}

/**
 * gst_cursor_restore:
 * @cursor: a GstCursor to restore into
 * @saved: the saved cursor state
 *
 * Restores cursor state from a saved copy.
 * All cursor properties are restored.
 */
void
gst_cursor_restore(
    GstCursor       *cursor,
    const GstCursor *saved
){
    g_return_if_fail(cursor != NULL);
    g_return_if_fail(saved != NULL);

    cursor->x = saved->x;
    cursor->y = saved->y;
    cursor->glyph = saved->glyph;
    cursor->state = saved->state;
    cursor->shape = saved->shape;
}

/**
 * gst_cursor_is_visible:
 * @cursor: a GstCursor
 *
 * Checks if the cursor is visible.
 *
 * Returns: %TRUE if the cursor is visible
 */
gboolean
gst_cursor_is_visible(const GstCursor *cursor)
{
    g_return_val_if_fail(cursor != NULL, FALSE);

    return (cursor->state & GST_CURSOR_STATE_VISIBLE) != 0;
}

/**
 * gst_cursor_set_visible:
 * @cursor: a GstCursor
 * @visible: whether the cursor should be visible
 *
 * Sets the cursor visibility.
 */
void
gst_cursor_set_visible(
    GstCursor   *cursor,
    gboolean    visible
){
    g_return_if_fail(cursor != NULL);

    if (visible) {
        cursor->state |= GST_CURSOR_STATE_VISIBLE;
    } else {
        cursor->state &= ~GST_CURSOR_STATE_VISIBLE;
    }
}

/**
 * gst_cursor_is_blinking:
 * @cursor: a GstCursor
 *
 * Checks if the cursor is in blink mode.
 *
 * Returns: %TRUE if the cursor blinks
 */
gboolean
gst_cursor_is_blinking(const GstCursor *cursor)
{
    g_return_val_if_fail(cursor != NULL, FALSE);

    return (cursor->state & GST_CURSOR_STATE_BLINK) != 0;
}

/**
 * gst_cursor_set_blinking:
 * @cursor: a GstCursor
 * @blinking: whether the cursor should blink
 *
 * Sets the cursor blink mode.
 */
void
gst_cursor_set_blinking(
    GstCursor   *cursor,
    gboolean    blinking
){
    g_return_if_fail(cursor != NULL);

    if (blinking) {
        cursor->state |= GST_CURSOR_STATE_BLINK;
    } else {
        cursor->state &= ~GST_CURSOR_STATE_BLINK;
    }
}

/**
 * gst_cursor_is_wrap_pending:
 * @cursor: a GstCursor
 *
 * Checks if the cursor has a pending wrap to next line.
 * This state is set when the cursor reaches the right
 * margin and the next character should cause a wrap.
 *
 * Returns: %TRUE if a wrap is pending
 */
gboolean
gst_cursor_is_wrap_pending(const GstCursor *cursor)
{
    g_return_val_if_fail(cursor != NULL, FALSE);

    return (cursor->state & GST_CURSOR_STATE_WRAPNEXT) != 0;
}

/**
 * gst_cursor_set_wrap_pending:
 * @cursor: a GstCursor
 * @pending: whether a wrap is pending
 *
 * Sets the wrap pending state.
 */
void
gst_cursor_set_wrap_pending(
    GstCursor   *cursor,
    gboolean    pending
){
    g_return_if_fail(cursor != NULL);

    if (pending) {
        cursor->state |= GST_CURSOR_STATE_WRAPNEXT;
    } else {
        cursor->state &= ~GST_CURSOR_STATE_WRAPNEXT;
    }
}

/**
 * gst_cursor_reset:
 * @cursor: a GstCursor
 *
 * Resets the cursor to default state at position (0, 0).
 * All attributes are reset to defaults.
 */
void
gst_cursor_reset(GstCursor *cursor)
{
    g_return_if_fail(cursor != NULL);

    cursor->x = 0;
    cursor->y = 0;
    cursor->glyph.rune = ' ';
    cursor->glyph.attr = GST_GLYPH_ATTR_NONE;
    cursor->glyph.fg = GST_COLOR_DEFAULT_FG;
    cursor->glyph.bg = GST_COLOR_DEFAULT_BG;
    cursor->state = GST_CURSOR_STATE_VISIBLE;
    cursor->shape = GST_CURSOR_SHAPE_BLOCK;
}

/**
 * gst_cursor_reset_attrs:
 * @cursor: a GstCursor
 *
 * Resets only the cursor's glyph attributes to defaults.
 * Position, state, and shape are preserved.
 */
void
gst_cursor_reset_attrs(GstCursor *cursor)
{
    g_return_if_fail(cursor != NULL);

    cursor->glyph.rune = ' ';
    cursor->glyph.attr = GST_GLYPH_ATTR_NONE;
    cursor->glyph.fg = GST_COLOR_DEFAULT_FG;
    cursor->glyph.bg = GST_COLOR_DEFAULT_BG;
}
