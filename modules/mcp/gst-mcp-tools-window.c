/*
 * gst-mcp-tools-window.c - Window management MCP tools
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tools: get_window_info, set_window_title
 */

#include "gst-mcp-tools.h"
#include "gst-mcp-module.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "../../src/window/gst-window.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/gst-enums.h"

/* ===== get_window_info ===== */

/*
 * handle_get_window_info:
 *
 * Returns window information: pixel width/height, title,
 * visibility state, and rendering backend (x11 or wayland).
 */
static McpToolResult *
handle_get_window_info(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstWindow *win;
	gint backend;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	mgr = gst_module_manager_get_default();
	win = (GstWindow *)gst_module_manager_get_window(mgr);
	if (win == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Window not available");
		return result;
	}

	backend = gst_module_manager_get_backend_type(mgr);

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "width");
	json_builder_add_int_value(builder, gst_window_get_width(win));
	json_builder_set_member_name(builder, "height");
	json_builder_add_int_value(builder, gst_window_get_height(win));
	json_builder_set_member_name(builder, "title");
	json_builder_add_string_value(builder, gst_window_get_title(win));
	json_builder_set_member_name(builder, "visible");
	json_builder_add_boolean_value(builder, gst_window_is_visible(win));
	json_builder_set_member_name(builder, "backend");
	json_builder_add_string_value(builder,
		backend == GST_BACKEND_X11 ? "x11" : "wayland");
	json_builder_end_object(builder);

	gen = json_generator_new();
	json_generator_set_root(gen, json_builder_get_root(builder));
	json_str = json_generator_to_data(gen, NULL);
	g_object_unref(gen);
	g_object_unref(builder);

	result = mcp_tool_result_new(FALSE);
	mcp_tool_result_add_text(result, json_str);
	g_free(json_str);

	return result;
}

/* ===== set_window_title ===== */

/*
 * handle_set_window_title:
 *
 * Updates the window title.
 */
static McpToolResult *
handle_set_window_title(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstWindow *win;
	const gchar *title;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)user_data;

	if (arguments == NULL || !json_object_has_member(arguments, "title")) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Missing required parameter: title");
		return result;
	}

	title = json_object_get_string_member(arguments, "title");

	mgr = gst_module_manager_get_default();
	win = (GstWindow *)gst_module_manager_get_window(mgr);
	if (win == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Window not available");
		return result;
	}

	gst_window_set_title(win, title);

	{
		JsonBuilder *builder;
		JsonGenerator *gen;
		gchar *json_str;

		builder = json_builder_new();
		json_builder_begin_object(builder);
		json_builder_set_member_name(builder, "success");
		json_builder_add_boolean_value(builder, TRUE);
		json_builder_set_member_name(builder, "title");
		json_builder_add_string_value(builder, title);
		json_builder_end_object(builder);

		gen = json_generator_new();
		json_generator_set_root(gen, json_builder_get_root(builder));
		json_str = json_generator_to_data(gen, NULL);
		g_object_unref(gen);
		g_object_unref(builder);

		result = mcp_tool_result_new(FALSE);
		mcp_tool_result_add_text(result, json_str);
		g_free(json_str);
	}

	return result;
}

/* ===== Tool Registration ===== */

void
gst_mcp_tools_window_register(McpServer *server, GstMcpModule *self)
{
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	if (self->tool_get_window_info) {
		tool = mcp_tool_new("get_window_info",
			"Get terminal window information: pixel dimensions, "
			"title, visibility, and rendering backend (x11/wayland).");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string("{\"type\":\"object\",\"properties\":{}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_get_window_info, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_set_window_title) {
		tool = mcp_tool_new("set_window_title",
			"Update the terminal window title.");
		mcp_tool_set_read_only_hint(tool, FALSE);
		mcp_tool_set_destructive_hint(tool, FALSE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"required\":[\"title\"],\"properties\":{"
			"\"title\":{\"type\":\"string\",\"description\":\"New window title\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_set_window_title, self, NULL);
		g_object_unref(tool);
	}
}
