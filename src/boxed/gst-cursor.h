/*
 * gst-cursor.h - GST Cursor Boxed Type
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Represents the terminal cursor position and state.
 */

#ifndef GST_CURSOR_H
#define GST_CURSOR_H

#include <glib-object.h>
#include "../gst-types.h"
#include "../gst-enums.h"
#include "gst-glyph.h"

G_BEGIN_DECLS

#define GST_TYPE_CURSOR (gst_cursor_get_type())

/**
 * GstCursor:
 * @x: Column position (0-based)
 * @y: Row position (0-based)
 * @glyph: Current glyph attributes (for new characters)
 * @state: Cursor state flags (visible, blink, wrapnext)
 * @shape: Cursor shape (block, underline, bar)
 *
 * Represents the terminal cursor, including its position,
 * current character attributes, and visual state.
 */
struct _GstCursor {
    gint            x;          /* Column (0-based) */
    gint            y;          /* Row (0-based) */
    GstGlyph        glyph;      /* Current character attributes */
    GstCursorState  state;      /* State flags */
    GstCursorShape  shape;      /* Visual shape */
};

/**
 * gst_cursor_get_type:
 *
 * Gets the GType for the GstCursor boxed type.
 *
 * Returns: the GType
 */
GType gst_cursor_get_type(void) G_GNUC_CONST;

/**
 * gst_cursor_new:
 *
 * Creates a new cursor at position (0, 0) with default attributes.
 *
 * Returns: (transfer full): a new GstCursor
 */
GstCursor *gst_cursor_new(void);

/**
 * gst_cursor_new_at:
 * @x: column position
 * @y: row position
 *
 * Creates a new cursor at the specified position.
 *
 * Returns: (transfer full): a new GstCursor
 */
GstCursor *gst_cursor_new_at(gint x, gint y);

/**
 * gst_cursor_copy:
 * @cursor: a GstCursor
 *
 * Creates a copy of the cursor.
 *
 * Returns: (transfer full): a copy of @cursor
 */
GstCursor *gst_cursor_copy(const GstCursor *cursor);

/**
 * gst_cursor_free:
 * @cursor: a GstCursor
 *
 * Frees the memory allocated for the cursor.
 */
void gst_cursor_free(GstCursor *cursor);

/**
 * gst_cursor_move_to:
 * @cursor: a GstCursor
 * @x: new column position
 * @y: new row position
 *
 * Moves the cursor to the specified position.
 */
void gst_cursor_move_to(GstCursor *cursor, gint x, gint y);

/**
 * gst_cursor_move_rel:
 * @cursor: a GstCursor
 * @dx: column offset
 * @dy: row offset
 *
 * Moves the cursor by the specified offset.
 */
void gst_cursor_move_rel(GstCursor *cursor, gint dx, gint dy);

/**
 * gst_cursor_save:
 * @cursor: a GstCursor
 *
 * Creates a saved copy of the cursor state.
 * Use gst_cursor_restore() to restore from this copy.
 *
 * Returns: (transfer full): a saved cursor state
 */
GstCursor *gst_cursor_save(const GstCursor *cursor);

/**
 * gst_cursor_restore:
 * @cursor: a GstCursor to restore into
 * @saved: the saved cursor state
 *
 * Restores cursor state from a saved copy.
 */
void gst_cursor_restore(GstCursor *cursor, const GstCursor *saved);

/**
 * gst_cursor_is_visible:
 * @cursor: a GstCursor
 *
 * Checks if the cursor is visible.
 *
 * Returns: %TRUE if the cursor is visible
 */
gboolean gst_cursor_is_visible(const GstCursor *cursor);

/**
 * gst_cursor_set_visible:
 * @cursor: a GstCursor
 * @visible: whether the cursor should be visible
 *
 * Sets the cursor visibility.
 */
void gst_cursor_set_visible(GstCursor *cursor, gboolean visible);

/**
 * gst_cursor_is_blinking:
 * @cursor: a GstCursor
 *
 * Checks if the cursor is in blink mode.
 *
 * Returns: %TRUE if the cursor blinks
 */
gboolean gst_cursor_is_blinking(const GstCursor *cursor);

/**
 * gst_cursor_set_blinking:
 * @cursor: a GstCursor
 * @blinking: whether the cursor should blink
 *
 * Sets the cursor blink mode.
 */
void gst_cursor_set_blinking(GstCursor *cursor, gboolean blinking);

/**
 * gst_cursor_is_wrap_pending:
 * @cursor: a GstCursor
 *
 * Checks if the cursor has a pending wrap to next line.
 * This occurs when the cursor is at the right margin
 * and the next character should wrap.
 *
 * Returns: %TRUE if a wrap is pending
 */
gboolean gst_cursor_is_wrap_pending(const GstCursor *cursor);

/**
 * gst_cursor_set_wrap_pending:
 * @cursor: a GstCursor
 * @pending: whether a wrap is pending
 *
 * Sets the wrap pending state.
 */
void gst_cursor_set_wrap_pending(GstCursor *cursor, gboolean pending);

/**
 * gst_cursor_reset:
 * @cursor: a GstCursor
 *
 * Resets the cursor to default state at position (0, 0).
 */
void gst_cursor_reset(GstCursor *cursor);

/**
 * gst_cursor_reset_attrs:
 * @cursor: a GstCursor
 *
 * Resets only the cursor's glyph attributes to defaults.
 * Position and state are preserved.
 */
void gst_cursor_reset_attrs(GstCursor *cursor);

/**
 * GST_CURSOR_INIT:
 *
 * Static initializer for a default cursor at (0, 0).
 */
#define GST_CURSOR_INIT { 0, 0, GST_GLYPH_INIT, GST_CURSOR_STATE_VISIBLE, GST_CURSOR_SHAPE_BLOCK }

G_END_DECLS

#endif /* GST_CURSOR_H */
