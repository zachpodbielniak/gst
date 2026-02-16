/*
 * gst-selection-handler.c
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for handling selection completion events.
 */

#include "gst-selection-handler.h"

G_DEFINE_INTERFACE(GstSelectionHandler, gst_selection_handler, G_TYPE_OBJECT)

static void
gst_selection_handler_default_init(GstSelectionHandlerInterface *iface)
{
	(void)iface;
}

/**
 * gst_selection_handler_handle_selection_done:
 * @self: A #GstSelectionHandler instance.
 * @text: The selected text as a UTF-8 string.
 * @len: Length of @text in bytes.
 *
 * Notifies the handler that a text selection has been finalized.
 */
void
gst_selection_handler_handle_selection_done(
	GstSelectionHandler *self,
	const gchar         *text,
	gint                 len
){
	GstSelectionHandlerInterface *iface;

	g_return_if_fail(GST_IS_SELECTION_HANDLER(self));

	iface = GST_SELECTION_HANDLER_GET_IFACE(self);
	g_return_if_fail(iface->handle_selection_done != NULL);

	iface->handle_selection_done(self, text, len);
}
