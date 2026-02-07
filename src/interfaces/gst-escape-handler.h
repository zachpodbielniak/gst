/*
 * gst-escape-handler.h - Interface for handling escape string sequences
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for modules that need to intercept APC, DCS, or other
 * string-type escape sequences before the terminal handles them.
 */

#ifndef GST_ESCAPE_HANDLER_H
#define GST_ESCAPE_HANDLER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_ESCAPE_HANDLER (gst_escape_handler_get_type())

G_DECLARE_INTERFACE(GstEscapeHandler, gst_escape_handler, GST, ESCAPE_HANDLER, GObject)

/**
 * GstEscapeHandlerInterface:
 * @parent_iface: The parent interface.
 * @handle_escape_string: Virtual method to handle a string escape sequence.
 *
 * Interface for handling string-type escape sequences (APC, DCS, PM).
 * The str_type character indicates the sequence type ('_' for APC,
 * 'P' for DCS, '^' for PM).
 */
struct _GstEscapeHandlerInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	gboolean (*handle_escape_string) (GstEscapeHandler *self,
	                                  gchar             str_type,
	                                  const gchar      *buf,
	                                  gsize             len,
	                                  gpointer          terminal);
};

/**
 * gst_escape_handler_handle_escape_string:
 * @self: A #GstEscapeHandler instance.
 * @str_type: The escape string type character ('_' for APC, 'P' for DCS).
 * @buf: The raw string buffer (not NUL-terminated by the caller, but
 *       the terminal null-terminates before dispatch).
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
                                        gpointer          terminal);

G_END_DECLS

#endif /* GST_ESCAPE_HANDLER_H */
