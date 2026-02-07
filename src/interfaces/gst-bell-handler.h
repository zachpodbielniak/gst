/*
 * gst-bell-handler.h
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for handling terminal bell notifications.
 */

#ifndef GST_BELL_HANDLER_H
#define GST_BELL_HANDLER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_BELL_HANDLER (gst_bell_handler_get_type())

G_DECLARE_INTERFACE(GstBellHandler, gst_bell_handler, GST, BELL_HANDLER, GObject)

/**
 * GstBellHandlerInterface:
 * @parent_iface: The parent interface.
 * @handle_bell: Virtual method to handle a bell notification.
 *
 * Interface for handling terminal bell events (visual, audio, etc.).
 */
struct _GstBellHandlerInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	void (*handle_bell) (GstBellHandler *self);
};

/**
 * gst_bell_handler_handle_bell:
 * @self: A #GstBellHandler instance.
 *
 * Handles a terminal bell notification.
 */
void
gst_bell_handler_handle_bell(GstBellHandler *self);

G_END_DECLS

#endif /* GST_BELL_HANDLER_H */
