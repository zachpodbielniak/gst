/*
 * gst-selection.h - GST Text Selection
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Port of st.c's selection system. Manages text selection state
 * including regular and rectangular selection modes, word/line
 * snapping, scrolling adjustments, and text extraction.
 */

#ifndef GST_SELECTION_H
#define GST_SELECTION_H

#include <glib-object.h>
#include "../gst-types.h"
#include "../gst-enums.h"

G_BEGIN_DECLS

#define GST_TYPE_SELECTION (gst_selection_get_type())

G_DECLARE_FINAL_TYPE(GstSelection, gst_selection, GST, SELECTION, GObject)

GType gst_selection_get_type(void) G_GNUC_CONST;

/**
 * gst_selection_new:
 * @term: a #GstTerminal that owns this selection
 *
 * Creates a new selection bound to the given terminal.
 * The selection accesses the terminal's screen buffer
 * for snapping and text extraction.
 *
 * Returns: (transfer full): a new #GstSelection
 */
GstSelection *gst_selection_new(GstTerminal *term);

/**
 * gst_selection_start:
 * @sel: a #GstSelection
 * @col: starting column (0-based)
 * @row: starting row (0-based)
 * @snap: snap mode (0=none, %GST_SELECTION_SNAP_WORD, %GST_SELECTION_SNAP_LINE)
 *
 * Begins a new selection at the given position. Clears any
 * existing selection first. If snap is non-zero, the selection
 * immediately snaps to the specified boundary.
 */
void gst_selection_start(GstSelection *sel, gint col, gint row,
                         GstSelectionSnap snap);

/**
 * gst_selection_extend:
 * @sel: a #GstSelection
 * @col: current column (0-based)
 * @row: current row (0-based)
 * @type: selection type (%GST_SELECTION_TYPE_REGULAR or %GST_SELECTION_TYPE_RECTANGULAR)
 * @done: %TRUE if the selection is finalized (mouse released)
 *
 * Extends the selection to the given position. If @done is %TRUE,
 * the selection transitions to idle state and can be queried for text.
 */
void gst_selection_extend(GstSelection *sel, gint col, gint row,
                          GstSelectionType type, gboolean done);

/**
 * gst_selection_clear:
 * @sel: a #GstSelection
 *
 * Clears the current selection, resetting to idle state.
 */
void gst_selection_clear(GstSelection *sel);

/**
 * gst_selection_scroll:
 * @sel: a #GstSelection
 * @orig: origin row of the scroll operation
 * @n: number of lines scrolled (positive=up, negative=down)
 *
 * Adjusts the selection coordinates when the terminal scrolls.
 * If the scroll region partially overlaps the selection, the
 * selection is cleared. Otherwise coordinates are shifted.
 */
void gst_selection_scroll(GstSelection *sel, gint orig, gint n);

/**
 * gst_selection_selected:
 * @sel: a #GstSelection
 * @col: column to check
 * @row: row to check
 *
 * Checks if the given cell is within the current selection.
 * Takes into account whether the selection was made on the
 * alternate screen vs primary screen.
 *
 * Returns: %TRUE if the cell is selected
 */
gboolean gst_selection_selected(GstSelection *sel, gint col, gint row);

/**
 * gst_selection_is_empty:
 * @sel: a #GstSelection
 *
 * Checks if the selection is empty (no text selected).
 *
 * Returns: %TRUE if no text is selected
 */
gboolean gst_selection_is_empty(GstSelection *sel);

/**
 * gst_selection_get_text:
 * @sel: a #GstSelection
 *
 * Extracts the selected text from the terminal buffer as
 * a UTF-8 string. Trailing spaces on each line are trimmed.
 * Lines are separated by newlines.
 *
 * Returns: (transfer full) (nullable): selected text, or %NULL if empty
 */
gchar *gst_selection_get_text(GstSelection *sel);

/**
 * gst_selection_get_mode:
 * @sel: a #GstSelection
 *
 * Gets the current selection mode (idle, empty, ready).
 *
 * Returns: the current #GstSelectionMode
 */
GstSelectionMode gst_selection_get_mode(GstSelection *sel);

/**
 * gst_selection_set_range:
 * @sel: a #GstSelection
 * @start_col: starting column
 * @start_row: starting row
 * @end_col: ending column
 * @end_row: ending row
 *
 * Sets the selection range directly without snapping.
 * Used for programmatic selection.
 */
void gst_selection_set_range(GstSelection *sel, gint start_col,
                             gint start_row, gint end_col, gint end_row);

G_END_DECLS

#endif /* GST_SELECTION_H */
