/*
 * gst-escape-handler.c - Escape string handler interface implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for handling string-type escape sequences (APC, DCS, PM).
 */

#include "gst-escape-handler.h"

G_DEFINE_INTERFACE(GstEscapeHandler, gst_escape_handler, G_TYPE_OBJECT)

static void
gst_escape_handler_default_init(GstEscapeHandlerInterface *iface)
{
	(void)iface;
}

/**
 * gst_escape_handler_handle_escape_string:
 * @self: A #GstEscapeHandler instance.
 * @str_type: The escape string type character ('_' for APC, 'P' for DCS).
 * @buf: The raw string buffer.
 * @len: Length of the buffer in bytes.
 * @terminal: (type gpointer): The #GstTerminal that received the sequence.
 *
 * Handles a string-type escape sequence.
 *
 * Returns: %TRUE if the sequence was handled, %FALSE to pass through.
 */
gboolean
gst_escape_handler_handle_escape_string(GstEscapeHandler *self,
                                        gchar             str_type,
                                        const gchar      *buf,
                                        gsize             len,
                                        gpointer          terminal)
{
	GstEscapeHandlerInterface *iface;

	g_return_val_if_fail(GST_IS_ESCAPE_HANDLER(self), FALSE);

	iface = GST_ESCAPE_HANDLER_GET_IFACE(self);
	g_return_val_if_fail(iface->handle_escape_string != NULL, FALSE);

	return iface->handle_escape_string(self, str_type, buf, len, terminal);
}
