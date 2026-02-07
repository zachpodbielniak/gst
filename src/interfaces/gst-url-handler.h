/*
 * gst-url-handler.h
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for detecting and handling URLs in terminal output.
 */

#ifndef GST_URL_HANDLER_H
#define GST_URL_HANDLER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_URL_HANDLER (gst_url_handler_get_type())

G_DECLARE_INTERFACE(GstUrlHandler, gst_url_handler, GST, URL_HANDLER, GObject)

/**
 * GstUrlHandlerInterface:
 * @parent_iface: The parent interface.
 * @open_url: Virtual method to open a URL.
 *
 * Interface for handling URLs in terminal output.
 */
struct _GstUrlHandlerInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	gboolean (*open_url) (GstUrlHandler *self,
	                      const gchar   *url);
};

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
                         const gchar   *url);

G_END_DECLS

#endif /* GST_URL_HANDLER_H */
