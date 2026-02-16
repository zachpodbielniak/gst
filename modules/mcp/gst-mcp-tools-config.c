/*
 * gst-mcp-tools-config.c - Config and module management MCP tools
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tools: get_config, set_config, list_modules, toggle_module
 */

#include "gst-mcp-tools.h"
#include "gst-mcp-module.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "../../src/core/gst-terminal.h"
#include "../../src/config/gst-config.h"
#include "../../src/module/gst-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/module/gst-module-info.h"
#include "../../src/window/gst-window.h"
#include "../../src/gst-enums.h"

/* ===== get_config ===== */

/*
 * handle_get_config:
 *
 * Reads configuration values for a given section.
 * Supported sections: terminal, window, font, cursor, colors.
 * If no section specified, returns a summary of all sections.
 */
static McpToolResult *
handle_get_config(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	const gchar *section;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)user_data;

	mgr = gst_module_manager_get_default();

	section = NULL;
	if (arguments != NULL && json_object_has_member(arguments, "section")) {
		section = json_object_get_string_member(arguments, "section");
	}

	builder = json_builder_new();
	json_builder_begin_object(builder);

	if (section == NULL || g_strcmp0(section, "terminal") == 0) {
		GstTerminal *term;
		gint cols, rows;

		term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
		if (term != NULL) {
			gst_terminal_get_size(term, &cols, &rows);

			if (section != NULL) {
				json_builder_set_member_name(builder, "cols");
				json_builder_add_int_value(builder, cols);
				json_builder_set_member_name(builder, "rows");
				json_builder_add_int_value(builder, rows);
				json_builder_set_member_name(builder, "title");
				json_builder_add_string_value(builder,
					gst_terminal_get_title(term));
			} else {
				json_builder_set_member_name(builder, "terminal");
				json_builder_begin_object(builder);
				json_builder_set_member_name(builder, "cols");
				json_builder_add_int_value(builder, cols);
				json_builder_set_member_name(builder, "rows");
				json_builder_add_int_value(builder, rows);
				json_builder_end_object(builder);
			}
		}
	}

	if (section == NULL || g_strcmp0(section, "window") == 0) {
		GstWindow *win;

		win = (GstWindow *)gst_module_manager_get_window(mgr);
		if (win != NULL) {
			if (section != NULL) {
				json_builder_set_member_name(builder, "width");
				json_builder_add_int_value(builder, gst_window_get_width(win));
				json_builder_set_member_name(builder, "height");
				json_builder_add_int_value(builder, gst_window_get_height(win));
				json_builder_set_member_name(builder, "title");
				json_builder_add_string_value(builder,
					gst_window_get_title(win));
				json_builder_set_member_name(builder, "visible");
				json_builder_add_boolean_value(builder,
					gst_window_is_visible(win));
			} else {
				json_builder_set_member_name(builder, "window");
				json_builder_begin_object(builder);
				json_builder_set_member_name(builder, "width");
				json_builder_add_int_value(builder, gst_window_get_width(win));
				json_builder_set_member_name(builder, "height");
				json_builder_add_int_value(builder, gst_window_get_height(win));
				json_builder_end_object(builder);
			}
		}
	}

	if (section == NULL || g_strcmp0(section, "backend") == 0) {
		gint backend;

		backend = gst_module_manager_get_backend_type(mgr);
		if (section != NULL) {
			json_builder_set_member_name(builder, "type");
			json_builder_add_string_value(builder,
				backend == 0 ? "x11" : "wayland");
		} else {
			json_builder_set_member_name(builder, "backend");
			json_builder_add_string_value(builder,
				backend == 0 ? "x11" : "wayland");
		}
	}

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

/* ===== set_config ===== */

/*
 * handle_set_config:
 *
 * Modifies a configuration value at runtime.
 * Only a whitelist of safe keys are allowed:
 *   window.title, window.opacity
 */
static McpToolResult *
handle_set_config(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	const gchar *key;
	const gchar *value;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)user_data;

	if (arguments == NULL ||
		!json_object_has_member(arguments, "key") ||
		!json_object_has_member(arguments, "value"))
	{
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Missing required parameters: key, value");
		return result;
	}

	key = json_object_get_string_member(arguments, "key");
	value = json_object_get_string_member(arguments, "value");

	mgr = gst_module_manager_get_default();

	if (g_strcmp0(key, "window.title") == 0) {
		GstWindow *win;

		win = (GstWindow *)gst_module_manager_get_window(mgr);
		if (win != NULL) {
			gst_window_set_title(win, value);
			result = mcp_tool_result_new(FALSE);
			mcp_tool_result_add_text(result,
				"{\"success\":true,\"key\":\"window.title\"}");
			return result;
		}
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Window not available");
		return result;
	}

	if (g_strcmp0(key, "window.opacity") == 0) {
		GstWindow *win;
		gdouble opacity;

		win = (GstWindow *)gst_module_manager_get_window(mgr);
		if (win != NULL) {
			opacity = g_ascii_strtod(value, NULL);
			if (opacity < 0.0) { opacity = 0.0; }
			if (opacity > 1.0) { opacity = 1.0; }
			gst_window_set_opacity(win, opacity);
			result = mcp_tool_result_new(FALSE);
			mcp_tool_result_add_text(result,
				"{\"success\":true,\"key\":\"window.opacity\"}");
			return result;
		}
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Window not available");
		return result;
	}

	/* Key not in whitelist */
	{
		gchar *msg;

		msg = g_strdup_printf(
			"Key '%s' is not allowed for runtime modification. "
			"Allowed keys: window.title, window.opacity", key);
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, msg);
		g_free(msg);
	}

	return result;
}

/* ===== list_modules ===== */

/*
 * handle_list_modules:
 *
 * Lists all registered modules with their name, description,
 * and active status.
 */
static McpToolResult *
handle_list_modules(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GList *modules, *l;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	mgr = gst_module_manager_get_default();
	modules = gst_module_manager_list_modules(mgr);

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "modules");
	json_builder_begin_array(builder);

	for (l = modules; l != NULL; l = l->next) {
		GstModuleInfo *info;
		const gchar *mod_name;
		GstModule *mod;
		gboolean active;

		info = (GstModuleInfo *)l->data;
		mod_name = gst_module_info_get_name(info);

		/* Get actual module to check active state */
		mod = gst_module_manager_get_module(mgr, mod_name);
		active = (mod != NULL) ? gst_module_is_active(mod) : FALSE;

		json_builder_begin_object(builder);
		json_builder_set_member_name(builder, "name");
		json_builder_add_string_value(builder, mod_name);
		json_builder_set_member_name(builder, "description");
		json_builder_add_string_value(builder,
			gst_module_info_get_description(info));
		json_builder_set_member_name(builder, "active");
		json_builder_add_boolean_value(builder, active);
		json_builder_end_object(builder);
	}

	json_builder_end_array(builder);
	json_builder_end_object(builder);

	g_list_free_full(modules, (GDestroyNotify)gst_module_info_free);

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

/* ===== toggle_module ===== */

/*
 * handle_toggle_module:
 *
 * Enables or disables a module by name at runtime.
 * Refuses to toggle the MCP module itself.
 */
static McpToolResult *
handle_toggle_module(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstModule *mod;
	const gchar *mod_name;
	gboolean enabled;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)user_data;

	if (arguments == NULL ||
		!json_object_has_member(arguments, "name") ||
		!json_object_has_member(arguments, "enabled"))
	{
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result,
			"Missing required parameters: name, enabled");
		return result;
	}

	mod_name = json_object_get_string_member(arguments, "name");
	enabled = json_object_get_boolean_member(arguments, "enabled");

	/* Refuse to toggle ourselves */
	if (g_strcmp0(mod_name, "mcp") == 0) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result,
			"Cannot toggle the MCP module itself");
		return result;
	}

	mgr = gst_module_manager_get_default();
	mod = gst_module_manager_get_module(mgr, mod_name);
	if (mod == NULL) {
		gchar *msg;

		msg = g_strdup_printf("Module '%s' not found", mod_name);
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, msg);
		g_free(msg);
		return result;
	}

	if (enabled && !gst_module_is_active(mod)) {
		gst_module_activate(mod);
	} else if (!enabled && gst_module_is_active(mod)) {
		gst_module_deactivate(mod);
	}

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "name");
	json_builder_add_string_value(builder, mod_name);
	json_builder_set_member_name(builder, "active");
	json_builder_add_boolean_value(builder, gst_module_is_active(mod));
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

/* ===== Tool Registration ===== */

void
gst_mcp_tools_config_register(McpServer *server, GstMcpModule *self)
{
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	if (self->tool_get_config) {
		tool = mcp_tool_new("get_config",
			"Read terminal configuration values. "
			"Specify a section (terminal, window, backend) or omit for summary.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"properties\":{"
			"\"section\":{\"type\":\"string\",\"description\":"
			"\"Config section: terminal, window, backend\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_get_config, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_set_config) {
		tool = mcp_tool_new("set_config",
			"Modify a configuration value at runtime. "
			"Only whitelisted keys allowed: window.title, window.opacity.");
		mcp_tool_set_read_only_hint(tool, FALSE);
		mcp_tool_set_destructive_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"required\":[\"key\",\"value\"],\"properties\":{"
			"\"key\":{\"type\":\"string\",\"description\":"
			"\"Config key (e.g. window.title, window.opacity)\"},"
			"\"value\":{\"type\":\"string\",\"description\":"
			"\"New value to set\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_set_config, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_list_modules) {
		tool = mcp_tool_new("list_modules",
			"List all registered terminal modules with their "
			"name, description, and active status.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string("{\"type\":\"object\",\"properties\":{}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_list_modules, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_toggle_module) {
		tool = mcp_tool_new("toggle_module",
			"Enable or disable a terminal module at runtime. "
			"Cannot toggle the MCP module itself.");
		mcp_tool_set_read_only_hint(tool, FALSE);
		mcp_tool_set_destructive_hint(tool, FALSE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"required\":[\"name\",\"enabled\"],\"properties\":{"
			"\"name\":{\"type\":\"string\",\"description\":\"Module name\"},"
			"\"enabled\":{\"type\":\"boolean\",\"description\":"
			"\"true to activate, false to deactivate\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_toggle_module, self, NULL);
		g_object_unref(tool);
	}
}
