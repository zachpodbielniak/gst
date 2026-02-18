/*
 * gst-mcp-module.h - MCP (Model Context Protocol) server module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Embeds an MCP server inside the terminal emulator, exposing
 * tools for AI assistants to read screen content, inspect processes,
 * detect URLs, manage config/modules, and optionally inject input.
 * Supports unix-socket, stdio, and HTTP transports with per-tool opt-in.
 */

#ifndef GST_MCP_MODULE_H
#define GST_MCP_MODULE_H

#include <gio/gio.h>
#include <glib-object.h>
#include <gmodule.h>
#include <mcp-server.h>
#include <mcp-unix-socket-server.h>
#include "../../src/module/gst-module.h"

G_BEGIN_DECLS

#define GST_TYPE_MCP_MODULE (gst_mcp_module_get_type())

G_DECLARE_FINAL_TYPE(GstMcpModule, gst_mcp_module,
	GST, MCP_MODULE, GstModule)

/**
 * GstMcpModule:
 *
 * MCP server module. The struct is exposed so that tool registration
 * functions can check per-tool enable flags directly.
 */
struct _GstMcpModule
{
	GstModule    parent_instance;

	/* Single-server mode (http / stdio transports) */
	McpServer   *server;
	GCancellable *cancellable;

	/* Transport config */
	gchar       *transport_type;     /* "unix-socket", "stdio", or "http" */
	guint        http_port;          /* default 8808 */
	gchar       *http_host;          /* default "127.0.0.1" */

	/* Unix socket transport (via mcp-glib McpUnixSocketServer) */
	gchar               *socket_name;    /* custom name, or NULL for PID-based */
	McpUnixSocketServer *unix_server;

	/* Per-tool enable flags */
	gboolean     tool_read_screen;
	gboolean     tool_read_scrollback;
	gboolean     tool_search_scrollback;
	gboolean     tool_get_cursor_position;
	gboolean     tool_get_cell_attributes;
	gboolean     tool_get_foreground_process;
	gboolean     tool_get_working_directory;
	gboolean     tool_is_shell_idle;
	gboolean     tool_get_pty_info;
	gboolean     tool_list_detected_urls;
	gboolean     tool_get_config;
	gboolean     tool_list_modules;
	gboolean     tool_set_config;
	gboolean     tool_toggle_module;
	gboolean     tool_get_window_info;
	gboolean     tool_set_window_title;
	gboolean     tool_send_text;
	gboolean     tool_send_keys;

	/* Screenshot capture */
	gboolean     tool_screenshot;
};

/**
 * gst_mcp_module_setup_server:
 * @self: a #GstMcpModule
 * @server: a newly created #McpServer
 *
 * Configures an McpServer with instructions and registers all
 * enabled tools on it. Used for both socket sessions and
 * single-server (HTTP/stdio) paths.
 */
void
gst_mcp_module_setup_server(
	GstMcpModule *self,
	McpServer    *server
);

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_MCP_MODULE_H */
