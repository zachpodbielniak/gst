/*
 * gst-selection.h - Text selection handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_SELECTION_H
#define GST_SELECTION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_SELECTION (gst_selection_get_type())

G_DECLARE_FINAL_TYPE(GstSelection, gst_selection, GST, SELECTION, GObject)

GType
gst_selection_get_type(void) G_GNUC_CONST;

/**
 * gst_selection_new:
 *
 * Creates a new empty selection.
 *
 * Returns: (transfer full): A new #GstSelection
 */
GstSelection *
gst_selection_new(void);

/**
 * gst_selection_set_range:
 * @self: A #GstSelection
 * @start_col: Starting column
 * @start_row: Starting row
 * @end_col: Ending column
 * @end_row: Ending row
 *
 * Sets the selection range.
 */
void
gst_selection_set_range(
	GstSelection *self,
	gint          start_col,
	gint          start_row,
	gint          end_col,
	gint          end_row
);

/**
 * gst_selection_clear:
 * @self: A #GstSelection
 *
 * Clears the current selection.
 */
void
gst_selection_clear(GstSelection *self);

/**
 * gst_selection_is_empty:
 * @self: A #GstSelection
 *
 * Checks if the selection is empty.
 *
 * Returns: %TRUE if no text is selected
 */
gboolean
gst_selection_is_empty(GstSelection *self);

/**
 * gst_selection_get_text:
 * @self: A #GstSelection
 *
 * Gets the currently selected text.
 *
 * Returns: (transfer full) (nullable): The selected text, or %NULL
 */
gchar *
gst_selection_get_text(GstSelection *self);

G_END_DECLS

#endif /* GST_SELECTION_H */
