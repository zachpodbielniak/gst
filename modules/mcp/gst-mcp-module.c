/*
 * gst-mcp-module.c - MCP server module for GST
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Module lifecycle: creates an McpServer with the configured transport
 * (stdio or HTTP), registers enabled tools, and starts the server.
 * Tool handlers access terminal state via the GstModuleManager singleton.
 */

#include "gst-mcp-module.h"
#include "gst-mcp-tools.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "../../src/config/gst-config.h"
#include "../../src/module/gst-module-manager.h"
#include "../../deps/yaml-glib/src/yaml-mapping.h"
#include "../../deps/yaml-glib/src/yaml-node.h"

G_DEFINE_TYPE(GstMcpModule, gst_mcp_module, GST_TYPE_MODULE)

/* ===== Helper: read a boolean from YAML mapping with default ===== */

static gboolean
yaml_get_bool(
	YamlMapping *map,
	const gchar *key,
	gboolean     def
){
	if (map == NULL || !yaml_mapping_has_member(map, key)) {
		return def;
	}
	return yaml_mapping_get_boolean_member(map, key);
}

static const gchar *
yaml_get_string(
	YamlMapping *map,
	const gchar *key,
	const gchar *def
){
	if (map == NULL || !yaml_mapping_has_member(map, key)) {
		return def;
	}
	return yaml_mapping_get_string_member(map, key);
}

static gint64
yaml_get_int(
	YamlMapping *map,
	const gchar *key,
	gint64       def
){
	if (map == NULL || !yaml_mapping_has_member(map, key)) {
		return def;
	}
	return yaml_mapping_get_int_member(map, key);
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_mcp_module_get_name(GstModule *module)
{
	(void)module;
	return "mcp";
}

static const gchar *
gst_mcp_module_get_description(GstModule *module)
{
	(void)module;
	return "MCP (Model Context Protocol) server for AI assistant integration";
}

/*
 * gst_mcp_module_configure:
 *
 * Parses the mcp module config section from YAML. Reads transport
 * settings and per-tool enable flags from the tools: sub-mapping.
 */
static void
gst_mcp_module_configure(
	GstModule *module,
	gpointer   config
){
	GstMcpModule *self;
	GstConfig *cfg;
	YamlMapping *mod_cfg;
	YamlMapping *tools_cfg;
	const gchar *transport;

	self = GST_MCP_MODULE(module);
	cfg = (GstConfig *)config;
	mod_cfg = gst_config_get_module_config(cfg, "mcp");

	/* Transport settings */
	transport = yaml_get_string(mod_cfg, "transport", "http");
	g_free(self->transport_type);
	self->transport_type = g_strdup(transport);

	self->http_port = (guint)yaml_get_int(mod_cfg, "port", 8808);

	g_free(self->http_host);
	self->http_host = g_strdup(yaml_get_string(mod_cfg, "host", "127.0.0.1"));

	/* Per-tool enable flags from tools: sub-mapping */
	tools_cfg = NULL;
	if (mod_cfg != NULL && yaml_mapping_has_member(mod_cfg, "tools")) {
		YamlNode *tools_node;

		tools_node = yaml_mapping_get_member(mod_cfg, "tools");
		if (tools_node != NULL &&
			yaml_node_get_node_type(tools_node) == YAML_NODE_MAPPING)
		{
			tools_cfg = yaml_node_get_mapping(tools_node);
		}
	}

	/* Screen reading tools */
	self->tool_read_screen = yaml_get_bool(tools_cfg, "read_screen", FALSE);
	self->tool_read_scrollback = yaml_get_bool(tools_cfg, "read_scrollback", FALSE);
	self->tool_search_scrollback = yaml_get_bool(tools_cfg, "search_scrollback", FALSE);
	self->tool_get_cursor_position = yaml_get_bool(tools_cfg, "get_cursor_position", FALSE);
	self->tool_get_cell_attributes = yaml_get_bool(tools_cfg, "get_cell_attributes", FALSE);

	/* Process awareness tools */
	self->tool_get_foreground_process = yaml_get_bool(tools_cfg, "get_foreground_process", FALSE);
	self->tool_get_working_directory = yaml_get_bool(tools_cfg, "get_working_directory", FALSE);
	self->tool_is_shell_idle = yaml_get_bool(tools_cfg, "is_shell_idle", FALSE);
	self->tool_get_pty_info = yaml_get_bool(tools_cfg, "get_pty_info", FALSE);

	/* URL detection */
	self->tool_list_detected_urls = yaml_get_bool(tools_cfg, "list_detected_urls", FALSE);

	/* Config / module management */
	self->tool_get_config = yaml_get_bool(tools_cfg, "get_config", FALSE);
	self->tool_list_modules = yaml_get_bool(tools_cfg, "list_modules", FALSE);
	self->tool_set_config = yaml_get_bool(tools_cfg, "set_config", FALSE);
	self->tool_toggle_module = yaml_get_bool(tools_cfg, "toggle_module", FALSE);

	/* Window management */
	self->tool_get_window_info = yaml_get_bool(tools_cfg, "get_window_info", FALSE);
	self->tool_set_window_title = yaml_get_bool(tools_cfg, "set_window_title", FALSE);

	/* Input injection */
	self->tool_send_text = yaml_get_bool(tools_cfg, "send_text", FALSE);
	self->tool_send_keys = yaml_get_bool(tools_cfg, "send_keys", FALSE);
}

/*
 * on_server_started:
 *
 * Callback for mcp_server_start_async(). Logs success or failure.
 */
static void
on_server_started(
	GObject      *source,
	GAsyncResult *result,
	gpointer      user_data
){
	GstMcpModule *self;
	g_autoptr(GError) error = NULL;

	(void)source;

	self = GST_MCP_MODULE(user_data);

	if (!mcp_server_start_finish(self->server, result, &error)) {
		g_warning("mcp: failed to start server: %s", error->message);
		return;
	}

	if (g_strcmp0(self->transport_type, "http") == 0) {
		g_message("mcp: HTTP server listening on %s:%u",
			self->http_host, self->http_port);
	} else {
		g_message("mcp: stdio server started on stdin/stdout");
	}
}

/*
 * gst_mcp_module_activate:
 *
 * Creates the McpServer, registers all enabled tools, sets up the
 * configured transport, and starts the server asynchronously.
 */
static gboolean
gst_mcp_module_activate(GstModule *module)
{
	GstMcpModule *self;
	McpTransport *transport;

	self = GST_MCP_MODULE(module);

	/* Create server -- construct version string from integer components
	 * because GST_VERSION string quoting is lost through shell expansion
	 * when the parent Makefile invokes sub-make for modules */
	{
		g_autofree gchar *version = NULL;

		version = g_strdup_printf("%d.%d.%d",
			GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO);
		self->server = mcp_server_new("gst-terminal", version);
	}
	mcp_server_set_instructions(self->server,
		"MCP server embedded in the GST terminal emulator. "
		"Provides tools to read terminal screen content, inspect "
		"running processes, detect URLs, manage configuration and "
		"modules, and optionally send input to the terminal.");

	/* Register enabled tools */
	gst_mcp_tools_screen_register(self->server, self);
	gst_mcp_tools_process_register(self->server, self);
	gst_mcp_tools_url_register(self->server, self);
	gst_mcp_tools_config_register(self->server, self);
	gst_mcp_tools_input_register(self->server, self);
	gst_mcp_tools_window_register(self->server, self);

	/* Create transport */
	transport = NULL;
	if (g_strcmp0(self->transport_type, "stdio") == 0) {
		McpStdioTransport *stdio_transport;

		stdio_transport = mcp_stdio_transport_new();
		transport = MCP_TRANSPORT(stdio_transport);
		g_debug("mcp: using stdio transport");
	} else {
		McpHttpServerTransport *http_transport;

		http_transport = mcp_http_server_transport_new_full(
			self->http_host, self->http_port);
		transport = MCP_TRANSPORT(http_transport);
		g_debug("mcp: using HTTP transport on %s:%u",
			self->http_host, self->http_port);
	}

	mcp_server_set_transport(self->server, transport);
	g_object_unref(transport);

	/* Start async */
	self->cancellable = g_cancellable_new();
	mcp_server_start_async(self->server, self->cancellable,
		on_server_started, self);

	g_debug("mcp: module activated");
	return TRUE;
}

/*
 * gst_mcp_module_deactivate:
 *
 * Stops the MCP server and cleans up resources.
 */
static void
gst_mcp_module_deactivate(GstModule *module)
{
	GstMcpModule *self;

	self = GST_MCP_MODULE(module);

	if (self->cancellable != NULL) {
		g_cancellable_cancel(self->cancellable);
		g_clear_object(&self->cancellable);
	}

	g_clear_object(&self->server);

	g_debug("mcp: module deactivated");
}

/* ===== GObject boilerplate ===== */

static void
gst_mcp_module_finalize(GObject *object)
{
	GstMcpModule *self;

	self = GST_MCP_MODULE(object);

	g_free(self->transport_type);
	g_free(self->http_host);

	G_OBJECT_CLASS(gst_mcp_module_parent_class)->finalize(object);
}

static void
gst_mcp_module_class_init(GstMcpModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_mcp_module_finalize;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_mcp_module_get_name;
	module_class->get_description = gst_mcp_module_get_description;
	module_class->configure = gst_mcp_module_configure;
	module_class->activate = gst_mcp_module_activate;
	module_class->deactivate = gst_mcp_module_deactivate;
}

static void
gst_mcp_module_init(GstMcpModule *self)
{
	self->server = NULL;
	self->cancellable = NULL;
	self->transport_type = g_strdup("http");
	self->http_port = 8808;
	self->http_host = g_strdup("127.0.0.1");

	/* All tools default to disabled */
	self->tool_read_screen = FALSE;
	self->tool_read_scrollback = FALSE;
	self->tool_search_scrollback = FALSE;
	self->tool_get_cursor_position = FALSE;
	self->tool_get_cell_attributes = FALSE;
	self->tool_get_foreground_process = FALSE;
	self->tool_get_working_directory = FALSE;
	self->tool_is_shell_idle = FALSE;
	self->tool_get_pty_info = FALSE;
	self->tool_list_detected_urls = FALSE;
	self->tool_get_config = FALSE;
	self->tool_list_modules = FALSE;
	self->tool_set_config = FALSE;
	self->tool_toggle_module = FALSE;
	self->tool_get_window_info = FALSE;
	self->tool_set_window_title = FALSE;
	self->tool_send_text = FALSE;
	self->tool_send_keys = FALSE;
}

/* ===== Module entry point ===== */

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return gst_mcp_module_get_type();
}
