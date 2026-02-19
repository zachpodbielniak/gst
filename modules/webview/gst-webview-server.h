/*
 * gst-webview-server.h - HTTP/WebSocket server for webview module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Wraps a SoupServer that serves the embedded HTML page and handles
 * WebSocket connections for live terminal streaming. Manages auth,
 * terminal state serialization (with diff detection), update
 * throttling, and keyboard input relay.
 */

#ifndef GST_WEBVIEW_SERVER_H
#define GST_WEBVIEW_SERVER_H

#include <glib.h>

G_BEGIN_DECLS

/* Forward declarations */
typedef struct _GstWebviewModule GstWebviewModule;
typedef struct _GstWebviewServer GstWebviewServer;

/**
 * gst_webview_server_new:
 * @module: the owning #GstWebviewModule (for config access)
 *
 * Creates a new webview server. Does not start listening yet.
 *
 * Returns: (transfer full): a new #GstWebviewServer, free with
 *          gst_webview_server_free()
 */
GstWebviewServer *
gst_webview_server_new(GstWebviewModule *module);

/**
 * gst_webview_server_start:
 * @server: a #GstWebviewServer
 * @error: return location for a #GError, or %NULL
 *
 * Starts listening on the configured host and port.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean
gst_webview_server_start(
	GstWebviewServer *server,
	GError          **error
);

/**
 * gst_webview_server_stop:
 * @server: a #GstWebviewServer
 *
 * Stops the HTTP server and closes all WebSocket connections.
 */
void
gst_webview_server_stop(GstWebviewServer *server);

/**
 * gst_webview_server_free:
 * @server: a #GstWebviewServer
 *
 * Frees all resources associated with the server.
 */
void
gst_webview_server_free(GstWebviewServer *server);

/**
 * gst_webview_server_notify_contents_changed:
 * @server: a #GstWebviewServer
 *
 * Called when the terminal contents change. Schedules a
 * throttled diff update to all connected WebSocket clients.
 */
void
gst_webview_server_notify_contents_changed(GstWebviewServer *server);

/**
 * gst_webview_server_notify_resize:
 * @server: a #GstWebviewServer
 * @cols: new column count
 * @rows: new row count
 *
 * Called when the terminal is resized. Sends a resize event
 * followed by a full screen update to all clients.
 */
void
gst_webview_server_notify_resize(
	GstWebviewServer *server,
	gint              cols,
	gint              rows
);

/**
 * gst_webview_server_notify_title:
 * @server: a #GstWebviewServer
 * @title: the new terminal title
 *
 * Sends a title change event to all clients.
 */
void
gst_webview_server_notify_title(
	GstWebviewServer *server,
	const gchar      *title
);

/**
 * gst_webview_server_notify_bell:
 * @server: a #GstWebviewServer
 *
 * Sends a bell event to all clients.
 */
void
gst_webview_server_notify_bell(GstWebviewServer *server);

G_END_DECLS

#endif /* GST_WEBVIEW_SERVER_H */
