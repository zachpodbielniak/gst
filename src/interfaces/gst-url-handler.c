/*
 * gst-url-handler.c
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for detecting and handling URLs in terminal output.
 */

#include "gst-url-handler.h"

G_DEFINE_INTERFACE(GstUrlHandler, gst_url_handler, G_TYPE_OBJECT)

static void
gst_url_handler_default_init(GstUrlHandlerInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

/**
 * gst_url_handler_open_url:
 * @self: A #GstUrlHandler instance.
 * @url: The URL to open.
 *
 * Opens the specified URL using the handler's configured method.
 *
 * Returns: %TRUE if the URL was opened successfully, %FALSE otherwise.
 */
gboolean
gst_url_handler_open_url(GstUrlHandler *self,
                         const gchar   *url)
{
	GstUrlHandlerInterface *iface;

	g_return_val_if_fail(GST_IS_URL_HANDLER(self), FALSE);
	g_return_val_if_fail(url != NULL, FALSE);

	iface = GST_URL_HANDLER_GET_IFACE(self);
	g_return_val_if_fail(iface->open_url != NULL, FALSE);

	return iface->open_url(self, url);
}
