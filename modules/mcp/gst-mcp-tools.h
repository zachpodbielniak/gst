/*
 * gst-mcp-tools.h - MCP tool handler declarations
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Each category file registers its tools with the McpServer.
 * Only tools enabled in config are registered (invisible if disabled).
 */

#ifndef GST_MCP_TOOLS_H
#define GST_MCP_TOOLS_H

#include <mcp-server.h>
#include "gst-mcp-module.h"

G_BEGIN_DECLS

/*
 * gst_mcp_tools_screen_register:
 * @server: The MCP server instance
 * @self: The MCP module (checked for per-tool enable flags)
 *
 * Registers screen reading tools: read_screen, read_scrollback,
 * search_scrollback, get_cursor_position, get_cell_attributes.
 */
void
gst_mcp_tools_screen_register(McpServer *server, GstMcpModule *self);

/*
 * gst_mcp_tools_process_register:
 * @server: The MCP server instance
 * @self: The MCP module
 *
 * Registers process awareness tools: get_foreground_process,
 * get_working_directory, is_shell_idle, get_pty_info.
 */
void
gst_mcp_tools_process_register(McpServer *server, GstMcpModule *self);

/*
 * gst_mcp_tools_url_register:
 * @server: The MCP server instance
 * @self: The MCP module
 *
 * Registers URL detection tool: list_detected_urls.
 */
void
gst_mcp_tools_url_register(McpServer *server, GstMcpModule *self);

/*
 * gst_mcp_tools_config_register:
 * @server: The MCP server instance
 * @self: The MCP module
 *
 * Registers config/module management tools: get_config,
 * set_config, list_modules, toggle_module.
 */
void
gst_mcp_tools_config_register(McpServer *server, GstMcpModule *self);

/*
 * gst_mcp_tools_input_register:
 * @server: The MCP server instance
 * @self: The MCP module
 *
 * Registers input injection tools: send_text, send_keys.
 */
void
gst_mcp_tools_input_register(McpServer *server, GstMcpModule *self);

/*
 * gst_mcp_tools_window_register:
 * @server: The MCP server instance
 * @self: The MCP module
 *
 * Registers window management tools: get_window_info, set_window_title.
 */
void
gst_mcp_tools_window_register(McpServer *server, GstMcpModule *self);

/*
 * gst_mcp_tools_screenshot_register:
 * @server: The MCP server instance
 * @self: The MCP module
 *
 * Registers screenshot capture tool: screenshot.
 */
void
gst_mcp_tools_screenshot_register(McpServer *server, GstMcpModule *self);

G_END_DECLS

#endif /* GST_MCP_TOOLS_H */
