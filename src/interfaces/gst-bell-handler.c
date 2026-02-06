/*
 * gst-bell-handler.c
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for handling terminal bell notifications.
 */

#include "gst-bell-handler.h"

G_DEFINE_INTERFACE(GstBellHandler, gst_bell_handler, G_TYPE_OBJECT)

static void
gst_bell_handler_default_init(GstBellHandlerInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

/**
 * gst_bell_handler_handle_bell:
 * @self: A #GstBellHandler instance.
 *
 * Handles a terminal bell notification.
 */
void
gst_bell_handler_handle_bell(GstBellHandler *self)
{
	GstBellHandlerInterface *iface;

	g_return_if_fail(GST_IS_BELL_HANDLER(self));

	iface = GST_BELL_HANDLER_GET_IFACE(self);
	g_return_if_fail(iface->handle_bell != NULL);

	iface->handle_bell(self);
}
