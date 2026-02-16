/*
 * gst-selection-handler.h
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for handling selection completion events.
 * Modules implementing this interface are notified when the user
 * finalizes a text selection (e.g. mouse button release after drag).
 */

#ifndef GST_SELECTION_HANDLER_H
#define GST_SELECTION_HANDLER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_SELECTION_HANDLER (gst_selection_handler_get_type())

G_DECLARE_INTERFACE(GstSelectionHandler, gst_selection_handler, GST, SELECTION_HANDLER, GObject)

/**
 * GstSelectionHandlerInterface:
 * @parent_iface: The parent interface.
 * @handle_selection_done: Virtual method called when a selection is finalized.
 *
 * Interface for reacting to completed text selections. The selected
 * text is provided as a UTF-8 string with its length.
 */
struct _GstSelectionHandlerInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	void (*handle_selection_done) (GstSelectionHandler *self,
	                               const gchar         *text,
	                               gint                 len);
};

/**
 * gst_selection_handler_handle_selection_done:
 * @self: A #GstSelectionHandler instance.
 * @text: The selected text as a UTF-8 string.
 * @len: Length of @text in bytes.
 *
 * Notifies the handler that a text selection has been finalized.
 */
void
gst_selection_handler_handle_selection_done(GstSelectionHandler *self,
                                            const gchar         *text,
                                            gint                 len);

G_END_DECLS

#endif /* GST_SELECTION_HANDLER_H */
