/*
 * gst-mcp-module.c - MCP server module for GST
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Module lifecycle: creates an McpServer with the configured transport
 * (unix-socket, stdio, or HTTP), registers enabled tools, and starts
 * the server. The unix-socket transport creates per-connection sessions,
 * each with its own McpServer. Tool handlers access terminal state via
 * the GstModuleManager singleton.
 */

#include "gst-mcp-module.h"
#include "gst-mcp-tools.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <unistd.h>

#include "../../src/config/gst-config.h"

G_DEFINE_TYPE(GstMcpModule, gst_mcp_module, GST_TYPE_MODULE)

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

	self = GST_MCP_MODULE(module);
	cfg = (GstConfig *)config;

	/* Transport settings */
	g_free(self->transport_type);
	self->transport_type = g_strdup(cfg->modules.mcp.transport);

	self->http_port = (guint)cfg->modules.mcp.port;

	g_free(self->http_host);
	self->http_host = g_strdup(cfg->modules.mcp.host);

	/* Socket name: CLI env var overrides config struct */
	g_free(self->socket_name);
	{
		const gchar *env_name;

		env_name = g_getenv("GST_MCP_SOCKET_NAME");
		if (env_name != NULL && env_name[0] != '\0') {
			self->socket_name = g_strdup(env_name);
		} else {
			self->socket_name = g_strdup(cfg->modules.mcp.socket_name);
		}
	}

	/* Screen reading tools */
	self->tool_read_screen = cfg->modules.mcp.tools.read_screen;
	self->tool_read_scrollback = cfg->modules.mcp.tools.read_scrollback;
	self->tool_search_scrollback = cfg->modules.mcp.tools.search_scrollback;
	self->tool_get_cursor_position = cfg->modules.mcp.tools.get_cursor_position;
	self->tool_get_cell_attributes = cfg->modules.mcp.tools.get_cell_attributes;

	/* Process awareness tools */
	self->tool_get_foreground_process = cfg->modules.mcp.tools.get_foreground_process;
	self->tool_get_working_directory = cfg->modules.mcp.tools.get_working_directory;
	self->tool_is_shell_idle = cfg->modules.mcp.tools.is_shell_idle;
	self->tool_get_pty_info = cfg->modules.mcp.tools.get_pty_info;

	/* URL detection */
	self->tool_list_detected_urls = cfg->modules.mcp.tools.list_detected_urls;

	/* Config / module management */
	self->tool_get_config = cfg->modules.mcp.tools.get_config;
	self->tool_list_modules = cfg->modules.mcp.tools.list_modules;
	self->tool_set_config = cfg->modules.mcp.tools.set_config;
	self->tool_toggle_module = cfg->modules.mcp.tools.toggle_module;

	/* Window management */
	self->tool_get_window_info = cfg->modules.mcp.tools.get_window_info;
	self->tool_set_window_title = cfg->modules.mcp.tools.set_window_title;

	/* Input injection */
	self->tool_send_text = cfg->modules.mcp.tools.send_text;
	self->tool_send_keys = cfg->modules.mcp.tools.send_keys;

	/* Screenshot capture */
	self->tool_screenshot = cfg->modules.mcp.tools.screenshot;
}

/* ===== Server setup (shared across all transport paths) ===== */

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
){
	mcp_server_set_instructions(server,
		"MCP server embedded in the GST terminal emulator. "
		"Provides tools to read terminal screen content, inspect "
		"running processes, detect URLs, manage configuration and "
		"modules, and optionally send input to the terminal.");

	gst_mcp_tools_screen_register(server, self);
	gst_mcp_tools_process_register(server, self);
	gst_mcp_tools_url_register(server, self);
	gst_mcp_tools_config_register(server, self);
	gst_mcp_tools_input_register(server, self);
	gst_mcp_tools_window_register(server, self);
	gst_mcp_tools_screenshot_register(server, self);
}

/* ===== Unix socket session-created handler ===== */

/*
 * on_unix_session_created:
 *
 * Called by McpUnixSocketServer when a new client connects.
 * Registers all enabled tools on the per-connection McpServer.
 */
static void
on_unix_session_created(
	McpUnixSocketServer *unix_server,
	McpServer           *server,
	gpointer             user_data
){
	GstMcpModule *self;

	(void)unix_server;

	self = GST_MCP_MODULE(user_data);
	gst_mcp_tools_screen_register(server, self);
	gst_mcp_tools_process_register(server, self);
	gst_mcp_tools_url_register(server, self);
	gst_mcp_tools_config_register(server, self);
	gst_mcp_tools_input_register(server, self);
	gst_mcp_tools_window_register(server, self);
	gst_mcp_tools_screenshot_register(server, self);
}

/* ===== Single-server callback (HTTP / stdio) ===== */

/**
 * on_server_started:
 *
 * Callback for mcp_server_start_async() on the single-server path
 * (HTTP or stdio transports). Logs success or failure.
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

/* ===== Module activate / deactivate ===== */

/**
 * gst_mcp_module_activate:
 *
 * Activates the MCP server with the configured transport:
 * - "unix-socket": uses McpUnixSocketServer, sessions spawned per-connection
 * - "http": creates single McpServer with McpHttpServerTransport
 * - "stdio": creates single McpServer with McpStdioTransport
 */
static gboolean
gst_mcp_module_activate(GstModule *module)
{
	GstMcpModule *self;

	self = GST_MCP_MODULE(module);

	if (g_strcmp0(self->transport_type, "unix-socket") == 0) {
		/* Unix socket transport via McpUnixSocketServer */
		const gchar *runtime_dir;
		g_autofree gchar *socket_path = NULL;
		g_autofree gchar *version = NULL;
		g_autoptr(GError) error = NULL;

		runtime_dir = g_get_user_runtime_dir();
		if (self->socket_name != NULL) {
			socket_path = g_strdup_printf("%s/gst-mcp-%s.sock",
				runtime_dir, self->socket_name);
		} else {
			socket_path = g_strdup_printf("%s/gst-mcp-%d.sock",
				runtime_dir, (gint)getpid());
		}

		version = g_strdup_printf("%d.%d.%d",
			GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO);

		self->unix_server = mcp_unix_socket_server_new(
			"gst-terminal", version, socket_path);

		mcp_unix_socket_server_set_instructions(self->unix_server,
			"MCP server embedded in the GST terminal emulator. "
			"Provides tools to read terminal screen content, inspect "
			"running processes, detect URLs, manage configuration and "
			"modules, and optionally send input to the terminal.");

		g_signal_connect(self->unix_server, "session-created",
			G_CALLBACK(on_unix_session_created), self);

		if (!mcp_unix_socket_server_start(self->unix_server, &error)) {
			g_warning("mcp: failed to start unix-socket server: %s",
				error->message);
			g_clear_object(&self->unix_server);
			return FALSE;
		}

		g_message("mcp: unix-socket server listening on %s",
			socket_path);
	} else {
		/* Single-server path for HTTP and stdio */
		McpTransport *transport;

		{
			g_autofree gchar *version = NULL;

			version = g_strdup_printf("%d.%d.%d",
				GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO);
			self->server = mcp_server_new("gst-terminal", version);
		}

		gst_mcp_module_setup_server(self, self->server);

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

		self->cancellable = g_cancellable_new();
		mcp_server_start_async(self->server, self->cancellable,
			on_server_started, self);
	}

	g_debug("mcp: module activated (transport=%s)", self->transport_type);
	return TRUE;
}

/**
 * gst_mcp_module_deactivate:
 *
 * Stops the MCP server(s) and cleans up all resources.
 */
static void
gst_mcp_module_deactivate(GstModule *module)
{
	GstMcpModule *self;

	self = GST_MCP_MODULE(module);

	/* Cancel single-server mode (http/stdio) */
	if (self->cancellable != NULL) {
		g_cancellable_cancel(self->cancellable);
		g_clear_object(&self->cancellable);
	}
	g_clear_object(&self->server);

	/* Stop unix socket server (handles session cleanup internally) */
	if (self->unix_server != NULL) {
		mcp_unix_socket_server_stop(self->unix_server);
		g_clear_object(&self->unix_server);
	}

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
	g_free(self->socket_name);

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
	/* Single-server mode */
	self->server = NULL;
	self->cancellable = NULL;

	/* Transport config */
	self->transport_type = g_strdup("unix-socket");
	self->http_port = 8808;
	self->http_host = g_strdup("127.0.0.1");

	/* Unix socket transport */
	self->socket_name     = NULL;
	self->unix_server     = NULL;

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
	self->tool_screenshot = FALSE;
}

/* ===== Module entry point ===== */

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return gst_mcp_module_get_type();
}
