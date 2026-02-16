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
#include <gio/gunixsocketaddress.h>
#include <unistd.h>

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
	transport = yaml_get_string(mod_cfg, "transport", "unix-socket");
	g_free(self->transport_type);
	self->transport_type = g_strdup(transport);

	self->http_port = (guint)yaml_get_int(mod_cfg, "port", 8808);

	g_free(self->http_host);
	self->http_host = g_strdup(yaml_get_string(mod_cfg, "host", "127.0.0.1"));

	/* Socket name: CLI env var overrides YAML config */
	g_free(self->socket_name);
	{
		const gchar *env_name;

		env_name = g_getenv("GST_MCP_SOCKET_NAME");
		if (env_name != NULL && env_name[0] != '\0') {
			self->socket_name = g_strdup(env_name);
		} else {
			const gchar *cfg_name;

			cfg_name = yaml_get_string(mod_cfg, "socket_name", NULL);
			self->socket_name = (cfg_name != NULL) ? g_strdup(cfg_name) : NULL;
		}
	}

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
}

/* ===== Unix socket session lifecycle ===== */

/**
 * gst_mcp_session_free:
 * @session: a #GstMcpSession
 *
 * Frees a socket session and its associated server/transport.
 */
static void
gst_mcp_session_free(GstMcpSession *session)
{
	if (session == NULL)
		return;

	if (session->server != NULL) {
		mcp_server_stop(session->server);
		g_object_unref(session->server);
	}
	if (session->transport != NULL)
		g_object_unref(session->transport);
	if (session->connection != NULL)
		g_object_unref(session->connection);

	g_free(session);
}

/**
 * on_socket_server_started:
 * @source: the McpServer
 * @result: the async result
 * @user_data: the #GstMcpSession
 *
 * Callback for mcp_server_start_async on a socket session.
 * Removes the session on failure.
 */
static void
on_socket_server_started(
	GObject      *source,
	GAsyncResult *result,
	gpointer      user_data
){
	GstMcpSession *session;
	g_autoptr(GError) error = NULL;

	(void)source;

	session = (GstMcpSession *)user_data;

	if (!mcp_server_start_finish(session->server, result, &error)) {
		g_warning("mcp: socket session start failed: %s",
			error->message);
		if (session->module != NULL) {
			session->module->socket_sessions = g_list_remove(
				session->module->socket_sessions, session);
		}
		gst_mcp_session_free(session);
	} else {
		g_debug("mcp: socket session started");
	}
}

/**
 * on_socket_incoming:
 * @service: the #GSocketService
 * @connection: the incoming #GSocketConnection
 * @source: the source #GObject (nullable)
 * @user_data: the #GstMcpModule
 *
 * Called when a new client connects to the Unix domain socket.
 * Creates a new MCP server instance for this connection using
 * McpStdioTransport with the socket's GIO streams.
 *
 * Returns: %TRUE to stop further signal handlers
 */
static gboolean
on_socket_incoming(
	GSocketService    *service,
	GSocketConnection *connection,
	GObject           *source,
	gpointer           user_data
){
	GstMcpModule   *self;
	GstMcpSession  *session;
	GInputStream   *input;
	GOutputStream  *output;

	(void)service;
	(void)source;

	self = GST_MCP_MODULE(user_data);

	session = g_new0(GstMcpSession, 1);
	session->module     = self;
	session->connection = g_object_ref(connection);

	/* Wrap socket streams in McpStdioTransport (NDJSON framing) */
	input  = g_io_stream_get_input_stream(G_IO_STREAM(connection));
	output = g_io_stream_get_output_stream(G_IO_STREAM(connection));

	session->transport = mcp_stdio_transport_new_with_streams(input, output);

	{
		g_autofree gchar *version = NULL;

		version = g_strdup_printf("%d.%d.%d",
			GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO);
		session->server = mcp_server_new("gst-terminal", version);
	}

	mcp_server_set_transport(session->server,
		MCP_TRANSPORT(session->transport));
	gst_mcp_module_setup_server(self, session->server);

	/* Track this session */
	self->socket_sessions = g_list_prepend(self->socket_sessions, session);

	/* Start async */
	mcp_server_start_async(session->server, NULL,
		on_socket_server_started, session);

	g_debug("mcp: accepted socket connection");
	return TRUE;
}

/**
 * setup_socket_transport:
 * @self: the MCP module
 *
 * Creates the Unix domain socket listener. If socket_name is set
 * (via config or $GST_MCP_SOCKET_NAME), uses gst-mcp-NAME.sock;
 * otherwise falls back to gst-mcp-PID.sock.
 *
 * Returns: %TRUE on success
 */
static gboolean
setup_socket_transport(GstMcpModule *self)
{
	const gchar           *runtime_dir;
	g_autoptr(GError)      error = NULL;
	g_autoptr(GSocketAddress) address = NULL;

	runtime_dir = g_get_user_runtime_dir();

	if (self->socket_name != NULL) {
		self->socket_path = g_strdup_printf("%s/gst-mcp-%s.sock",
			runtime_dir, self->socket_name);
	} else {
		self->socket_path = g_strdup_printf("%s/gst-mcp-%d.sock",
			runtime_dir, (gint)getpid());
	}

	/* Remove stale socket file */
	unlink(self->socket_path);

	self->socket_service = g_socket_service_new();

	address = g_unix_socket_address_new(self->socket_path);
	if (!g_socket_listener_add_address(
		G_SOCKET_LISTENER(self->socket_service),
		address,
		G_SOCKET_TYPE_STREAM,
		G_SOCKET_PROTOCOL_DEFAULT,
		NULL,   /* source_object */
		NULL,   /* effective_address */
		&error))
	{
		g_warning("mcp: failed to listen on %s: %s",
			self->socket_path, error->message);
		g_clear_object(&self->socket_service);
		return FALSE;
	}

	g_signal_connect(self->socket_service, "incoming",
		G_CALLBACK(on_socket_incoming), self);

	g_socket_service_start(self->socket_service);

	g_message("mcp: unix-socket transport listening on %s",
		self->socket_path);
	return TRUE;
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
 * - "unix-socket": creates GSocketService, sessions spawned per-connection
 * - "http": creates single McpServer with McpHttpServerTransport
 * - "stdio": creates single McpServer with McpStdioTransport
 */
static gboolean
gst_mcp_module_activate(GstModule *module)
{
	GstMcpModule *self;

	self = GST_MCP_MODULE(module);

	if (g_strcmp0(self->transport_type, "unix-socket") == 0) {
		/* Unix socket transport -- servers created per-connection */
		if (!setup_socket_transport(self)) {
			g_warning("mcp: failed to set up unix-socket transport");
			return FALSE;
		}
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
	GList *l;

	self = GST_MCP_MODULE(module);

	/* Cancel single-server mode (http/stdio) */
	if (self->cancellable != NULL) {
		g_cancellable_cancel(self->cancellable);
		g_clear_object(&self->cancellable);
	}
	g_clear_object(&self->server);

	/* Clean up socket sessions */
	for (l = self->socket_sessions; l != NULL; l = l->next)
		gst_mcp_session_free((GstMcpSession *)l->data);
	g_list_free(self->socket_sessions);
	self->socket_sessions = NULL;

	/* Clean up socket service */
	if (self->socket_service != NULL) {
		g_socket_service_stop(self->socket_service);
		g_clear_object(&self->socket_service);
	}

	/* Remove socket file */
	if (self->socket_path != NULL) {
		unlink(self->socket_path);
		g_free(self->socket_path);
		self->socket_path = NULL;
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
	g_free(self->socket_path);

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
	self->socket_service  = NULL;
	self->socket_path     = NULL;
	self->socket_sessions = NULL;

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
