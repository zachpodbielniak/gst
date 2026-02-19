/*
 * gst-webview-module.h - Web view module for GST
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Serves a live HTML view of the terminal over HTTP/WebSocket.
 * Supports read-only and read-write modes with optional
 * token/password authentication.
 */

#ifndef GST_WEBVIEW_MODULE_H
#define GST_WEBVIEW_MODULE_H

#include <glib-object.h>
#include <gmodule.h>

#include "../../src/module/gst-module.h"
#include "gst-webview-server.h"

G_BEGIN_DECLS

#define GST_TYPE_WEBVIEW_MODULE (gst_webview_module_get_type())

G_DECLARE_FINAL_TYPE(GstWebviewModule, gst_webview_module,
	GST, WEBVIEW_MODULE, GstModule)

/**
 * GstWebviewModule:
 *
 * A terminal module that serves a live HTML view of the terminal
 * content over HTTP with WebSocket streaming. Configurable with
 * read-only/read-write modes and token/password authentication.
 */
struct _GstWebviewModule
{
	GstModule          parent_instance;

	/* Server instance (created on activate) */
	GstWebviewServer  *server;

	/* Configuration */
	gchar             *host;            /* bind address, default "127.0.0.1" */
	guint              port;            /* HTTP port, default 7681 */
	gboolean           read_only;       /* default TRUE */
	gchar             *auth_mode;       /* "none", "token", or "password" */
	gchar             *auth_token;      /* token value */
	gchar             *auth_password;   /* password value */
	guint              update_interval; /* min ms between updates, default 50 */
	guint              max_clients;     /* max WebSocket clients, default 10 */

	/* Signal handler IDs for clean disconnection */
	gulong             sig_contents_changed;
	gulong             sig_resize;
	gulong             sig_title_changed;
	gulong             sig_bell;
};

/**
 * gst_module_register:
 *
 * Module entry point called by GModule loader.
 * Returns the GType of #GstWebviewModule.
 *
 * Returns: the #GType
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_WEBVIEW_MODULE_H */
