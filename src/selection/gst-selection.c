/*
 * gst-selection.c - Text selection handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-selection.h"

/**
 * SECTION:gst-selection
 * @title: GstSelection
 * @short_description: Terminal text selection handling
 *
 * #GstSelection manages the current text selection in the terminal,
 * tracking the selected region and providing access to selected text.
 */

struct _GstSelection
{
	GObject parent_instance;

	gint start_col;
	gint start_row;
	gint end_col;
	gint end_row;
	gboolean active;

	/* TODO: Reference to terminal buffer for text extraction */
};

G_DEFINE_TYPE(GstSelection, gst_selection, G_TYPE_OBJECT)

static void
gst_selection_dispose(GObject *object)
{
	G_OBJECT_CLASS(gst_selection_parent_class)->dispose(object);
}

static void
gst_selection_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_selection_parent_class)->finalize(object);
}

static void
gst_selection_class_init(GstSelectionClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_selection_dispose;
	object_class->finalize = gst_selection_finalize;
}

static void
gst_selection_init(GstSelection *self)
{
	self->start_col = 0;
	self->start_row = 0;
	self->end_col = 0;
	self->end_row = 0;
	self->active = FALSE;
}

/**
 * gst_selection_new:
 *
 * Creates a new empty selection.
 *
 * Returns: (transfer full): A new #GstSelection
 */
GstSelection *
gst_selection_new(void)
{
	return (GstSelection *)g_object_new(GST_TYPE_SELECTION, NULL);
}

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
){
	g_return_if_fail(GST_IS_SELECTION(self));

	self->start_col = start_col;
	self->start_row = start_row;
	self->end_col = end_col;
	self->end_row = end_row;
	self->active = TRUE;
}

/**
 * gst_selection_clear:
 * @self: A #GstSelection
 *
 * Clears the current selection.
 */
void
gst_selection_clear(GstSelection *self)
{
	g_return_if_fail(GST_IS_SELECTION(self));

	self->start_col = 0;
	self->start_row = 0;
	self->end_col = 0;
	self->end_row = 0;
	self->active = FALSE;
}

/**
 * gst_selection_is_empty:
 * @self: A #GstSelection
 *
 * Checks if the selection is empty.
 *
 * Returns: %TRUE if no text is selected
 */
gboolean
gst_selection_is_empty(GstSelection *self)
{
	g_return_val_if_fail(GST_IS_SELECTION(self), TRUE);

	if (!self->active)
	{
		return TRUE;
	}

	/* Check if start equals end */
	if (self->start_row == self->end_row &&
	    self->start_col == self->end_col)
	{
		return TRUE;
	}

	return FALSE;
}

/**
 * gst_selection_get_text:
 * @self: A #GstSelection
 *
 * Gets the currently selected text.
 *
 * Returns: (transfer full) (nullable): The selected text, or %NULL
 */
gchar *
gst_selection_get_text(GstSelection *self)
{
	g_return_val_if_fail(GST_IS_SELECTION(self), NULL);

	if (gst_selection_is_empty(self))
	{
		return NULL;
	}

	/* TODO: Extract text from terminal buffer using selection coordinates */
	return g_strdup("");
}
