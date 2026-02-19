/*
 * gst-webview-server.c - HTTP/WebSocket server for webview module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the HTTP server (libsoup-3.0) that serves the embedded
 * HTML page at "/", a health endpoint at "/health", and a WebSocket
 * endpoint at "/ws" for live terminal streaming.
 *
 * Screen updates are serialized as JSON and pushed over WebSocket.
 * Row-level FNV-1a hashing is used for efficient diff detection,
 * and a configurable timer throttles update frequency.
 */

#include "gst-webview-server.h"
#include "gst-webview-module.h"
#include "gst-webview-html.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <gmodule.h>
#include <string.h>

#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/core/gst-pty.h"
#include "../../src/boxed/gst-glyph.h"
#include "../../src/boxed/gst-cursor.h"
#include "../../src/config/gst-color-scheme.h"
#include "../../src/config/gst-config.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/gst-types.h"
#include "../../src/gst-enums.h"

/* ===== Webview attribute bitmask values (sent in JSON "a" field) ===== */
#define WV_ATTR_BOLD    (1)
#define WV_ATTR_FAINT   (2)
#define WV_ATTR_ITALIC  (4)
#define WV_ATTR_ULINE   (8)
#define WV_ATTR_STRUCK  (16)
#define WV_ATTR_REVERSE (32)

/* ===== Server structure ===== */

struct _GstWebviewServer
{
	SoupServer          *soup;
	GstWebviewModule    *module;        /* back-pointer, not owned */
	GPtrArray           *ws_clients;    /* SoupWebsocketConnection* */
	GstColorScheme      *color_scheme;  /* cached for color resolution */

	/* Update throttling */
	guint                update_source; /* g_timeout_add ID, 0 if none */
	gboolean             update_pending;

	/* Row hashing for diff detection */
	guint32             *row_hashes;
	gint                 prev_rows;
	gint                 prev_cols;

	/* Scrollback integration (resolved lazily at runtime) */
	gboolean             sb_resolved;
	gpointer             sb_module;       /* GstScrollbackModule*, or NULL */
	gint (*sb_get_offset)(gpointer);
	void (*sb_set_offset)(gpointer, gint);
	gint (*sb_get_count)(gpointer);
	const GstGlyph * (*sb_get_line_glyphs)(gpointer, gint, gint *);
};

/* ===== Forward declarations ===== */

static void
handle_page(
	SoupServer        *soup,
	SoupServerMessage *msg,
	const char        *path,
	GHashTable        *query,
	gpointer           user_data
);

static void
handle_health(
	SoupServer        *soup,
	SoupServerMessage *msg,
	const char        *path,
	GHashTable        *query,
	gpointer           user_data
);

static void
on_ws_opened(
	SoupServer              *soup,
	SoupServerMessage       *msg,
	const char              *path,
	SoupWebsocketConnection *conn,
	gpointer                 user_data
);

static void
on_ws_message(
	SoupWebsocketConnection *conn,
	SoupWebsocketDataType    type,
	GBytes                  *message,
	gpointer                 user_data
);

static void
on_ws_closed(
	SoupWebsocketConnection *conn,
	gpointer                 user_data
);

static gboolean
check_auth_msg(
	GstWebviewServer  *srv,
	SoupServerMessage *msg
);

static gboolean
check_auth_ws(
	GstWebviewServer  *srv,
	SoupServerMessage *msg
);

static void
broadcast_text(
	GstWebviewServer *srv,
	const gchar      *text
);

static gchar *
serialize_full_screen(GstWebviewServer *srv);

static gchar *
serialize_diff_screen(GstWebviewServer *srv);

static void
serialize_cursor_json(
	GstTerminal *term,
	GString     *json
);

static void
resolve_color(
	GstColorScheme *scheme,
	guint32         val,
	gchar          *out
);

static guint32
hash_row(
	GstTerminal *term,
	gint         row,
	gint         cols
);

static guint
glyph_to_webview_attrs(GstGlyphAttr attr);

static gboolean
on_update_tick(gpointer user_data);

static void
ensure_scrollback_api(GstWebviewServer *srv);

static guint32
hash_glyph_array(
	const GstGlyph *glyphs,
	gint            cols
);

static void
serialize_glyph_row_json(
	GString        *json,
	const GstGlyph *glyphs,
	gint            glyph_cols,
	gint            term_cols,
	GstColorScheme *scheme
);

/* ===== Key name to escape sequence (duplicated from gst-mcp-tools-input.c) ===== */

/*
 * key_name_to_escape:
 *
 * Converts a key name (e.g., "Enter", "Ctrl+c", "Up") to the
 * corresponding escape sequence. Returns NULL for unrecognized keys.
 * For Ctrl+letter, writes the result into @buf and returns it.
 */
static const gchar *
key_name_to_escape(const gchar *key_name, gchar *buf, gsize buflen)
{
	(void)buflen;

	if (g_ascii_strcasecmp(key_name, "Enter") == 0 ||
		g_ascii_strcasecmp(key_name, "Return") == 0)
	{
		return "\r";
	}
	if (g_ascii_strcasecmp(key_name, "Tab") == 0) {
		return "\t";
	}
	if (g_ascii_strcasecmp(key_name, "Escape") == 0 ||
		g_ascii_strcasecmp(key_name, "Esc") == 0)
	{
		return "\033";
	}
	if (g_ascii_strcasecmp(key_name, "Backspace") == 0) {
		return "\177";
	}
	if (g_ascii_strcasecmp(key_name, "Space") == 0) {
		return " ";
	}
	if (g_ascii_strcasecmp(key_name, "Up") == 0) {
		return "\033[A";
	}
	if (g_ascii_strcasecmp(key_name, "Down") == 0) {
		return "\033[B";
	}
	if (g_ascii_strcasecmp(key_name, "Right") == 0) {
		return "\033[C";
	}
	if (g_ascii_strcasecmp(key_name, "Left") == 0) {
		return "\033[D";
	}
	if (g_ascii_strcasecmp(key_name, "Home") == 0) {
		return "\033[H";
	}
	if (g_ascii_strcasecmp(key_name, "End") == 0) {
		return "\033[F";
	}
	if (g_ascii_strcasecmp(key_name, "PageUp") == 0 ||
		g_ascii_strcasecmp(key_name, "Page_Up") == 0)
	{
		return "\033[5~";
	}
	if (g_ascii_strcasecmp(key_name, "PageDown") == 0 ||
		g_ascii_strcasecmp(key_name, "Page_Down") == 0)
	{
		return "\033[6~";
	}
	if (g_ascii_strcasecmp(key_name, "Insert") == 0) {
		return "\033[2~";
	}
	if (g_ascii_strcasecmp(key_name, "Delete") == 0) {
		return "\033[3~";
	}

	/* Ctrl+letter */
	if (g_str_has_prefix(key_name, "Ctrl+") ||
		g_str_has_prefix(key_name, "ctrl+"))
	{
		const gchar *letter;
		guchar ctrl_char;

		letter = key_name + 5;
		if (strlen(letter) == 1 && g_ascii_isalpha(letter[0])) {
			ctrl_char = (guchar)(g_ascii_toupper(letter[0]) - 'A' + 1);
			buf[0] = (gchar)ctrl_char;
			buf[1] = '\0';
			return buf;
		}
	}

	return NULL;
}

/* ===== Scrollback module integration (runtime-resolved) ===== */

/*
 * ensure_scrollback_api:
 *
 * Lazily resolves the scrollback module's public API functions
 * from the process global symbol table. This avoids a compile-time
 * dependency between the webview and scrollback modules.
 * Both .so files are loaded with RTLD_GLOBAL by GModule, so
 * symbols are visible across modules at runtime.
 */
static void
ensure_scrollback_api(GstWebviewServer *srv)
{
	GstModuleManager *mgr;
	GstModule *mod;
	GModule *global;

	if (srv->sb_resolved) {
		return;
	}
	srv->sb_resolved = TRUE;
	srv->sb_module = NULL;

	mgr = gst_module_manager_get_default();
	mod = gst_module_manager_get_module(mgr, "scrollback");
	if (mod == NULL || !gst_module_is_active(mod)) {
		g_debug("webview: scrollback module not available");
		return;
	}

	/* Resolve function pointers from the process global symbol table */
	global = g_module_open(NULL, 0);
	if (global == NULL) {
		return;
	}

	if (!g_module_symbol(global, "gst_scrollback_module_get_scroll_offset",
			(gpointer *)&srv->sb_get_offset) ||
		!g_module_symbol(global, "gst_scrollback_module_set_scroll_offset",
			(gpointer *)&srv->sb_set_offset) ||
		!g_module_symbol(global, "gst_scrollback_module_get_count",
			(gpointer *)&srv->sb_get_count) ||
		!g_module_symbol(global, "gst_scrollback_module_get_line_glyphs",
			(gpointer *)&srv->sb_get_line_glyphs))
	{
		srv->sb_get_offset = NULL;
		srv->sb_set_offset = NULL;
		srv->sb_get_count = NULL;
		srv->sb_get_line_glyphs = NULL;
		g_debug("webview: scrollback API symbols not found");
		return;
	}

	srv->sb_module = mod;
	g_debug("webview: scrollback API resolved");
}

/* ===== Public API ===== */

GstWebviewServer *
gst_webview_server_new(GstWebviewModule *module)
{
	GstWebviewServer *srv;

	srv = g_new0(GstWebviewServer, 1);
	srv->module = module;
	srv->ws_clients = g_ptr_array_new();
	srv->update_source = 0;
	srv->update_pending = FALSE;
	srv->row_hashes = NULL;
	srv->prev_rows = 0;
	srv->prev_cols = 0;
	srv->sb_resolved = FALSE;
	srv->sb_module = NULL;
	srv->sb_get_offset = NULL;
	srv->sb_set_offset = NULL;
	srv->sb_get_count = NULL;
	srv->sb_get_line_glyphs = NULL;

	/* Load color scheme from config */
	{
		GstConfig *config;

		srv->color_scheme = gst_color_scheme_new("webview");
		config = gst_config_get_default();
		if (config != NULL) {
			gst_color_scheme_load_from_config(srv->color_scheme, config);
		}
	}

	return srv;
}

gboolean
gst_webview_server_start(
	GstWebviewServer *srv,
	GError          **error
){
	gboolean listen_all;
	GError *local_error;

	srv->soup = soup_server_new(NULL, NULL);

	/* Register HTTP handlers */
	soup_server_add_handler(srv->soup, "/health",
		handle_health, srv, NULL);
	soup_server_add_handler(srv->soup, "/",
		handle_page, srv, NULL);

	/* Register WebSocket handler */
	soup_server_add_websocket_handler(srv->soup, "/ws",
		NULL, NULL,
		on_ws_opened, srv, NULL);

	/* Start listening */
	listen_all = (g_strcmp0(srv->module->host, "0.0.0.0") == 0);
	local_error = NULL;

	if (listen_all) {
		soup_server_listen_all(srv->soup, srv->module->port,
			(SoupServerListenOptions)0, &local_error);
	} else {
		/*
		 * soup_server_listen_local binds to loopback.
		 * For custom bind addresses, use listen_all with firewall
		 * or accept that "127.0.0.1" maps to local-only.
		 */
		soup_server_listen_local(srv->soup, srv->module->port,
			(SoupServerListenOptions)0, &local_error);
	}

	if (local_error != NULL) {
		g_propagate_error(error, local_error);
		g_object_unref(srv->soup);
		srv->soup = NULL;
		return FALSE;
	}

	if (g_strcmp0(srv->module->auth_mode, "token") == 0 &&
		srv->module->auth_token != NULL &&
		srv->module->auth_token[0] != '\0')
	{
		g_message("webview: serving at http://%s:%u/?token=%s",
			srv->module->host, srv->module->port,
			srv->module->auth_token);
	} else {
		g_message("webview: serving at http://%s:%u/",
			srv->module->host, srv->module->port);
	}

	return TRUE;
}

void
gst_webview_server_stop(GstWebviewServer *srv)
{
	guint i;

	if (srv == NULL) {
		return;
	}

	/* Remove update timer */
	if (srv->update_source != 0) {
		g_source_remove(srv->update_source);
		srv->update_source = 0;
	}

	/* Close all WebSocket connections */
	for (i = 0; i < srv->ws_clients->len; i++) {
		SoupWebsocketConnection *conn;

		conn = g_ptr_array_index(srv->ws_clients, i);
		if (soup_websocket_connection_get_state(conn) ==
			SOUP_WEBSOCKET_STATE_OPEN)
		{
			soup_websocket_connection_close(conn, 1001, "Server shutting down");
		}
	}

	/* Disconnect and stop soup server */
	if (srv->soup != NULL) {
		soup_server_disconnect(srv->soup);
		g_object_unref(srv->soup);
		srv->soup = NULL;
	}
}

void
gst_webview_server_free(GstWebviewServer *srv)
{
	if (srv == NULL) {
		return;
	}

	gst_webview_server_stop(srv);

	g_ptr_array_unref(srv->ws_clients);

	if (srv->color_scheme != NULL) {
		g_object_unref(srv->color_scheme);
	}

	g_free(srv->row_hashes);
	g_free(srv);
}

void
gst_webview_server_notify_contents_changed(GstWebviewServer *srv)
{
	if (srv == NULL || srv->ws_clients->len == 0) {
		return;
	}

	srv->update_pending = TRUE;

	/* Schedule throttled update if not already scheduled */
	if (srv->update_source == 0) {
		srv->update_source = g_timeout_add(
			srv->module->update_interval,
			on_update_tick, srv);
	}
}

void
gst_webview_server_notify_resize(
	GstWebviewServer *srv,
	gint              cols,
	gint              rows
){
	gchar *msg;
	gchar *full;

	if (srv == NULL || srv->ws_clients->len == 0) {
		return;
	}

	/* Reset row hashes for new dimensions */
	g_free(srv->row_hashes);
	srv->row_hashes = NULL;
	srv->prev_rows = 0;
	srv->prev_cols = 0;

	/* Send resize event */
	msg = g_strdup_printf(
		"{\"type\":\"resize\",\"cols\":%d,\"rows\":%d}",
		cols, rows);
	broadcast_text(srv, msg);
	g_free(msg);

	/* Follow with full screen update */
	full = serialize_full_screen(srv);
	if (full != NULL) {
		broadcast_text(srv, full);
		g_free(full);
	}
}

void
gst_webview_server_notify_title(
	GstWebviewServer *srv,
	const gchar      *title
){
	g_autofree gchar *escaped = NULL;
	g_autofree gchar *msg = NULL;

	if (srv == NULL || srv->ws_clients->len == 0) {
		return;
	}

	escaped = g_strescape(title != NULL ? title : "", NULL);
	msg = g_strdup_printf("{\"type\":\"title\",\"title\":\"%s\"}", escaped);
	broadcast_text(srv, msg);
}

void
gst_webview_server_notify_bell(GstWebviewServer *srv)
{
	if (srv == NULL || srv->ws_clients->len == 0) {
		return;
	}

	broadcast_text(srv, "{\"type\":\"bell\"}");
}

/* ===== HTTP Handlers ===== */

/*
 * handle_page:
 *
 * Serves the embedded HTML page. Checks auth for both token and
 * password modes. Token mode requires ?token=<value> query param
 * (or Authorization header). Password mode uses HTTP Basic auth.
 */
static void
handle_page(
	SoupServer        *soup,
	SoupServerMessage *msg,
	const char        *path,
	GHashTable        *query,
	gpointer           user_data
){
	GstWebviewServer *srv;

	(void)soup;
	(void)path;
	(void)query;

	srv = (GstWebviewServer *)user_data;

	if (g_strcmp0(srv->module->auth_mode, "none") != 0) {
		if (!check_auth_msg(srv, msg)) {
			if (g_strcmp0(srv->module->auth_mode, "password") == 0) {
				SoupMessageHeaders *resp_headers;

				soup_server_message_set_status(msg, 401, "Unauthorized");
				resp_headers = soup_server_message_get_response_headers(msg);
				soup_message_headers_replace(resp_headers,
					"WWW-Authenticate", "Basic realm=\"gst\"");
				soup_server_message_set_response(msg,
					"text/plain", SOUP_MEMORY_STATIC,
					"Unauthorized", 12);
			} else {
				soup_server_message_set_status(msg, 403, "Forbidden");
				soup_server_message_set_response(msg,
					"text/plain", SOUP_MEMORY_STATIC,
					"Forbidden: invalid or missing token", 35);
			}
			return;
		}
	}

	soup_server_message_set_status(msg, 200, "OK");
	soup_server_message_set_response(msg,
		"text/html; charset=utf-8", SOUP_MEMORY_STATIC,
		GST_WEBVIEW_HTML, strlen(GST_WEBVIEW_HTML));
}

/*
 * handle_health:
 *
 * Returns a JSON health check response. No auth required.
 */
static void
handle_health(
	SoupServer        *soup,
	SoupServerMessage *msg,
	const char        *path,
	GHashTable        *query,
	gpointer           user_data
){
	GstWebviewServer *srv;
	gchar *json;

	(void)soup;
	(void)path;
	(void)query;

	srv = (GstWebviewServer *)user_data;

	json = g_strdup_printf(
		"{\"status\":\"ok\",\"clients\":%u}",
		srv->ws_clients->len);

	soup_server_message_set_status(msg, 200, "OK");
	soup_server_message_set_response(msg,
		"application/json", SOUP_MEMORY_TAKE,
		json, strlen(json));
}

/* ===== WebSocket Handlers ===== */

/*
 * on_ws_opened:
 *
 * Called when a WebSocket connection is established. Checks auth,
 * enforces max_clients, adds to client list, and sends initial
 * full screen update.
 */
static void
on_ws_opened(
	SoupServer              *soup,
	SoupServerMessage       *msg,
	const char              *path,
	SoupWebsocketConnection *conn,
	gpointer                 user_data
){
	GstWebviewServer *srv;
	gchar *full;

	(void)soup;
	(void)path;

	srv = (GstWebviewServer *)user_data;

	/* Check auth */
	if (!check_auth_ws(srv, msg)) {
		soup_websocket_connection_close(conn, 1008, "Authentication failed");
		return;
	}

	/* Enforce max_clients */
	if (srv->ws_clients->len >= srv->module->max_clients) {
		soup_websocket_connection_close(conn, 1013,
			"Maximum clients reached");
		return;
	}

	/* Add to client list */
	g_object_ref(conn);
	g_ptr_array_add(srv->ws_clients, conn);

	/* Connect signals */
	g_signal_connect(conn, "message", G_CALLBACK(on_ws_message), srv);
	g_signal_connect(conn, "closed", G_CALLBACK(on_ws_closed), srv);

	g_debug("webview: client connected (%u total)",
		srv->ws_clients->len);

	/* Send initial full screen update */
	full = serialize_full_screen(srv);
	if (full != NULL) {
		soup_websocket_connection_send_text(conn, full);
		g_free(full);
	}
}

/*
 * on_ws_message:
 *
 * Handles incoming WebSocket messages from clients.
 * Scroll events are processed in both read-only and read-write modes.
 * Key and text events are only processed in read-write mode.
 */
static void
on_ws_message(
	SoupWebsocketConnection *conn,
	SoupWebsocketDataType    type,
	GBytes                  *message,
	gpointer                 user_data
){
	GstWebviewServer *srv;
	JsonParser *parser;
	JsonNode *root;
	JsonObject *obj;
	const gchar *msg_type;
	gconstpointer data;
	gsize len;

	(void)conn;

	srv = (GstWebviewServer *)user_data;

	if (type != SOUP_WEBSOCKET_DATA_TEXT) {
		return;
	}

	data = g_bytes_get_data(message, &len);
	parser = json_parser_new();

	if (!json_parser_load_from_data(parser, (const gchar *)data, (gssize)len, NULL)) {
		g_object_unref(parser);
		return;
	}

	root = json_parser_get_root(parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT(root)) {
		g_object_unref(parser);
		return;
	}

	obj = json_node_get_object(root);
	if (!json_object_has_member(obj, "type")) {
		g_object_unref(parser);
		return;
	}

	msg_type = json_object_get_string_member(obj, "type");

	/* Scroll events work in both read-only and read-write modes */
	if (g_strcmp0(msg_type, "scroll") == 0) {
		gint delta;
		gint current;
		gchar *full;

		if (!json_object_has_member(obj, "delta")) {
			g_object_unref(parser);
			return;
		}

		ensure_scrollback_api(srv);
		if (srv->sb_module == NULL) {
			g_object_unref(parser);
			return;
		}

		delta = (gint)json_object_get_int_member(obj, "delta");
		current = srv->sb_get_offset(srv->sb_module);
		srv->sb_set_offset(srv->sb_module, current + delta);

		/* Force immediate full screen update with reset hashes */
		g_free(srv->row_hashes);
		srv->row_hashes = NULL;
		srv->prev_rows = 0;
		srv->prev_cols = 0;

		full = serialize_full_screen(srv);
		if (full != NULL) {
			broadcast_text(srv, full);
			g_free(full);
		}

		g_object_unref(parser);
		return;
	}

	/* Read-only mode: silently discard all other input */
	if (srv->module->read_only) {
		g_object_unref(parser);
		return;
	}

	if (g_strcmp0(msg_type, "key") == 0) {
		/* Key event: convert name to escape sequence and write to PTY */
		const gchar *key_name;
		const gchar *seq;
		gchar buf[8];
		GstModuleManager *mgr;
		GstPty *pty;

		if (!json_object_has_member(obj, "key")) {
			g_object_unref(parser);
			return;
		}

		key_name = json_object_get_string_member(obj, "key");
		seq = key_name_to_escape(key_name, buf, sizeof(buf));

		if (seq != NULL) {
			mgr = gst_module_manager_get_default();
			pty = (GstPty *)gst_module_manager_get_pty(mgr);
			if (pty != NULL && gst_pty_is_running(pty)) {
				gst_pty_write(pty, seq, (gssize)strlen(seq));
			}
		}
	} else if (g_strcmp0(msg_type, "text") == 0) {
		/* Raw text input */
		const gchar *text;
		GstModuleManager *mgr;
		GstPty *pty;

		if (!json_object_has_member(obj, "text")) {
			g_object_unref(parser);
			return;
		}

		text = json_object_get_string_member(obj, "text");
		if (text != NULL && text[0] != '\0') {
			mgr = gst_module_manager_get_default();
			pty = (GstPty *)gst_module_manager_get_pty(mgr);
			if (pty != NULL && gst_pty_is_running(pty)) {
				gst_pty_write(pty, text, (gssize)strlen(text));
			}
		}
	}

	g_object_unref(parser);
}

/*
 * on_ws_closed:
 *
 * Removes the connection from the client list.
 */
static void
on_ws_closed(
	SoupWebsocketConnection *conn,
	gpointer                 user_data
){
	GstWebviewServer *srv;

	srv = (GstWebviewServer *)user_data;

	g_ptr_array_remove(srv->ws_clients, conn);
	g_object_unref(conn);

	g_debug("webview: client disconnected (%u remaining)",
		srv->ws_clients->len);
}

/* ===== Authentication ===== */

/*
 * extract_query_param:
 *
 * Extracts a query parameter from a SoupServerMessage URI.
 * Returns NULL if not found.
 */
static const gchar *
extract_query_param(SoupServerMessage *msg, const gchar *param)
{
	GUri *uri;
	const gchar *query;
	g_autofree gchar *search = NULL;
	const gchar *pos;
	const gchar *val_start;
	const gchar *val_end;
	static gchar param_buf[256];

	uri = soup_server_message_get_uri(msg);
	query = g_uri_get_query(uri);
	if (query == NULL) {
		return NULL;
	}

	search = g_strdup_printf("%s=", param);
	pos = strstr(query, search);
	if (pos == NULL) {
		return NULL;
	}

	val_start = pos + strlen(search);
	val_end = strchr(val_start, '&');
	if (val_end == NULL) {
		val_end = val_start + strlen(val_start);
	}

	{
		gsize copy_len;

		copy_len = (gsize)(val_end - val_start);
		if (copy_len >= sizeof(param_buf)) {
			copy_len = sizeof(param_buf) - 1;
		}
		memcpy(param_buf, val_start, copy_len);
		param_buf[copy_len] = '\0';
	}

	return param_buf;
}

/*
 * check_auth_msg:
 *
 * Validates authentication for an HTTP request.
 * Returns TRUE if auth passes (or auth mode is "none").
 */
static gboolean
check_auth_msg(
	GstWebviewServer  *srv,
	SoupServerMessage *msg
){
	SoupMessageHeaders *headers;
	const gchar *auth_header;

	if (g_strcmp0(srv->module->auth_mode, "none") == 0) {
		return TRUE;
	}

	headers = soup_server_message_get_request_headers(msg);
	auth_header = soup_message_headers_get_one(headers, "Authorization");

	if (g_strcmp0(srv->module->auth_mode, "token") == 0) {
		/* Check Bearer token header */
		if (auth_header != NULL &&
			g_str_has_prefix(auth_header, "Bearer "))
		{
			return g_strcmp0(auth_header + 7,
				srv->module->auth_token) == 0;
		}

		/* Fall back to query param */
		{
			const gchar *qtoken;

			qtoken = extract_query_param(msg, "token");
			if (qtoken != NULL) {
				return g_strcmp0(qtoken, srv->module->auth_token) == 0;
			}
		}

		return FALSE;
	}

	if (g_strcmp0(srv->module->auth_mode, "password") == 0) {
		/* Check Basic auth header */
		if (auth_header != NULL &&
			g_str_has_prefix(auth_header, "Basic "))
		{
			g_autofree guchar *decoded = NULL;
			gsize decoded_len;
			gchar *colon;

			decoded = g_base64_decode(auth_header + 6, &decoded_len);
			if (decoded != NULL) {
				/* Format: user:password, we only check the password */
				colon = strchr((gchar *)decoded, ':');
				if (colon != NULL) {
					return g_strcmp0(colon + 1,
						srv->module->auth_password) == 0;
				}
			}
		}

		/* Fall back to query param */
		{
			const gchar *qpass;

			qpass = extract_query_param(msg, "password");
			if (qpass != NULL) {
				return g_strcmp0(qpass,
					srv->module->auth_password) == 0;
			}
		}

		return FALSE;
	}

	return TRUE;
}

/*
 * check_auth_ws:
 *
 * Validates authentication for a WebSocket upgrade request.
 * Uses the same logic as check_auth_msg.
 */
static gboolean
check_auth_ws(
	GstWebviewServer  *srv,
	SoupServerMessage *msg
){
	return check_auth_msg(srv, msg);
}

/* ===== Broadcasting ===== */

/*
 * broadcast_text:
 *
 * Sends a text message to all connected WebSocket clients.
 */
static void
broadcast_text(
	GstWebviewServer *srv,
	const gchar      *text
){
	guint i;

	for (i = 0; i < srv->ws_clients->len; i++) {
		SoupWebsocketConnection *conn;

		conn = g_ptr_array_index(srv->ws_clients, i);
		if (soup_websocket_connection_get_state(conn) ==
			SOUP_WEBSOCKET_STATE_OPEN)
		{
			soup_websocket_connection_send_text(conn, text);
		}
	}
}

/* ===== Color Resolution ===== */

/*
 * resolve_color:
 *
 * Resolves a glyph color value (palette index or truecolor) to
 * a "#RRGGBB" hex string. Output buffer must be at least 8 bytes.
 */
static void
resolve_color(
	GstColorScheme *scheme,
	guint32         val,
	gchar          *out
){
	guint8 r, g, b;

	if (GST_IS_TRUECOLOR(val)) {
		r = (guint8)((val >> 16) & 0xFF);
		g = (guint8)((val >> 8) & 0xFF);
		b = (guint8)(val & 0xFF);
	} else {
		guint32 argb;

		/*
		 * Palette indices 0-255, plus special indices for
		 * default fg/bg (256/257). gst_color_scheme_get_color
		 * handles all of these.
		 */
		if (val == GST_COLOR_DEFAULT_FG) {
			argb = gst_color_scheme_get_foreground(scheme);
		} else if (val == GST_COLOR_DEFAULT_BG) {
			argb = gst_color_scheme_get_background(scheme);
		} else {
			argb = gst_color_scheme_get_color(scheme, val);
		}

		r = (guint8)((argb >> 16) & 0xFF);
		g = (guint8)((argb >> 8) & 0xFF);
		b = (guint8)(argb & 0xFF);
	}

	g_snprintf(out, 8, "#%02x%02x%02x", r, g, b);
}

/* ===== Attribute conversion ===== */

/*
 * glyph_to_webview_attrs:
 *
 * Converts GstGlyphAttr bitmask to the compact webview attribute
 * integer sent in the JSON "a" field.
 */
static guint
glyph_to_webview_attrs(GstGlyphAttr attr)
{
	guint a;

	a = 0;
	if (attr & GST_GLYPH_ATTR_BOLD)      a |= WV_ATTR_BOLD;
	if (attr & GST_GLYPH_ATTR_FAINT)     a |= WV_ATTR_FAINT;
	if (attr & GST_GLYPH_ATTR_ITALIC)    a |= WV_ATTR_ITALIC;
	if (attr & GST_GLYPH_ATTR_UNDERLINE) a |= WV_ATTR_ULINE;
	if (attr & GST_GLYPH_ATTR_STRUCK)    a |= WV_ATTR_STRUCK;
	if (attr & GST_GLYPH_ATTR_REVERSE)   a |= WV_ATTR_REVERSE;

	return a;
}

/* ===== Row Hashing ===== */

/*
 * hash_row:
 *
 * Computes an FNV-1a hash of a terminal row's glyph data.
 * Used for efficient diff detection between updates.
 */
static guint32
hash_row(
	GstTerminal *term,
	gint         row,
	gint         cols
){
	GstLine *line;
	guint32 hash;
	gint x;

	hash = 2166136261u; /* FNV-1a offset basis */

	line = gst_terminal_get_line(term, row);
	if (line == NULL) {
		return hash;
	}

	for (x = 0; x < cols; x++) {
		const GstGlyph *g;

		g = gst_line_get_glyph_const(line, x);
		if (g == NULL) {
			break;
		}

		hash ^= g->rune;  hash *= 16777619u;
		hash ^= g->fg;    hash *= 16777619u;
		hash ^= g->bg;    hash *= 16777619u;
		hash ^= g->attr;  hash *= 16777619u;
	}

	return hash;
}

/*
 * hash_glyph_array:
 *
 * Computes an FNV-1a hash of a glyph array (for scrollback rows).
 * Used for diff detection when viewing scrollback content.
 */
static guint32
hash_glyph_array(
	const GstGlyph *glyphs,
	gint            cols
){
	guint32 hash;
	gint x;

	hash = 2166136261u; /* FNV-1a offset basis */

	if (glyphs == NULL) {
		return hash;
	}

	for (x = 0; x < cols; x++) {
		hash ^= glyphs[x].rune;  hash *= 16777619u;
		hash ^= glyphs[x].fg;    hash *= 16777619u;
		hash ^= glyphs[x].bg;    hash *= 16777619u;
		hash ^= glyphs[x].attr;  hash *= 16777619u;
	}

	return hash;
}

/* ===== JSON Serialization ===== */

/*
 * append_cell_json:
 *
 * Appends a single cell's JSON representation to the GString.
 * Format: {"c":"X","fg":"#RRGGBB","bg":"#RRGGBB","a":N}
 * Wide characters get an additional "w":1 field.
 */
static void
append_cell_json(
	GString        *json,
	const GstGlyph *glyph,
	GstColorScheme *scheme,
	gboolean        first
){
	gchar fg_hex[8];
	gchar bg_hex[8];
	gchar utf8[7];
	gint utf8_len;
	guint wv_attrs;

	if (!first) {
		g_string_append_c(json, ',');
	}

	resolve_color(scheme, glyph->fg, fg_hex);
	resolve_color(scheme, glyph->bg, bg_hex);

	/* Convert codepoint to UTF-8 */
	if (glyph->rune == 0 || glyph->rune == ' ') {
		utf8[0] = ' ';
		utf8[1] = '\0';
	} else {
		utf8_len = g_unichar_to_utf8((gunichar)glyph->rune, utf8);
		utf8[utf8_len] = '\0';
	}

	wv_attrs = glyph_to_webview_attrs(glyph->attr);

	/* Build cell JSON */
	g_string_append(json, "{\"c\":\"");

	/* Escape special JSON characters in the UTF-8 string */
	{
		const gchar *p;

		for (p = utf8; *p != '\0'; p++) {
			switch (*p) {
			case '"':  g_string_append(json, "\\\""); break;
			case '\\': g_string_append(json, "\\\\"); break;
			case '\n': g_string_append(json, "\\n");  break;
			case '\r': g_string_append(json, "\\r");  break;
			case '\t': g_string_append(json, "\\t");  break;
			default:
				if ((guchar)*p < 0x20) {
					g_string_append_printf(json, "\\u%04x",
						(guint)(guchar)*p);
				} else {
					g_string_append_c(json, *p);
				}
				break;
			}
		}
	}

	g_string_append_printf(json,
		"\",\"fg\":\"%s\",\"bg\":\"%s\",\"a\":%u",
		fg_hex, bg_hex, wv_attrs);

	if (glyph->attr & GST_GLYPH_ATTR_WIDE) {
		g_string_append(json, ",\"w\":1");
	}

	g_string_append_c(json, '}');
}

/*
 * serialize_row_json:
 *
 * Serializes a single terminal row as a JSON array of cells.
 * Skips WDUMMY cells (second cell of wide characters).
 */
static void
serialize_row_json(
	GString        *json,
	GstTerminal    *term,
	gint            row,
	gint            cols,
	GstColorScheme *scheme
){
	GstLine *line;
	gint x;
	gboolean first;

	g_string_append_c(json, '[');

	line = gst_terminal_get_line(term, row);
	first = TRUE;

	for (x = 0; x < cols; x++) {
		const GstGlyph *glyph;

		if (line == NULL) {
			/* Empty line: emit space cells */
			GstGlyph empty = {' ', GST_GLYPH_ATTR_NONE,
				GST_COLOR_DEFAULT_FG, GST_COLOR_DEFAULT_BG};
			append_cell_json(json, &empty, scheme, first);
			first = FALSE;
			continue;
		}

		glyph = gst_line_get_glyph_const(line, x);
		if (glyph == NULL) {
			GstGlyph empty = {' ', GST_GLYPH_ATTR_NONE,
				GST_COLOR_DEFAULT_FG, GST_COLOR_DEFAULT_BG};
			append_cell_json(json, &empty, scheme, first);
			first = FALSE;
			continue;
		}

		/* Skip dummy cells (second cell of wide characters) */
		if (glyph->attr & GST_GLYPH_ATTR_WDUMMY) {
			continue;
		}

		append_cell_json(json, glyph, scheme, first);
		first = FALSE;
	}

	g_string_append_c(json, ']');
}

/*
 * serialize_glyph_row_json:
 *
 * Serializes a row from a glyph array (used for scrollback lines).
 * Pads to term_cols with empty cells if the glyph array is shorter.
 */
static void
serialize_glyph_row_json(
	GString        *json,
	const GstGlyph *glyphs,
	gint            glyph_cols,
	gint            term_cols,
	GstColorScheme *scheme
){
	gint x;
	gboolean first;
	GstGlyph empty = {' ', GST_GLYPH_ATTR_NONE,
		GST_COLOR_DEFAULT_FG, GST_COLOR_DEFAULT_BG};

	g_string_append_c(json, '[');
	first = TRUE;

	for (x = 0; x < term_cols; x++) {
		const GstGlyph *glyph;

		if (glyphs != NULL && x < glyph_cols) {
			glyph = &glyphs[x];
		} else {
			glyph = &empty;
		}

		/* Skip dummy cells (second cell of wide characters) */
		if (glyph->attr & GST_GLYPH_ATTR_WDUMMY) {
			continue;
		}

		append_cell_json(json, glyph, scheme, first);
		first = FALSE;
	}

	g_string_append_c(json, ']');
}

/*
 * serialize_cursor_json:
 *
 * Appends cursor state as a JSON object to the GString.
 */
static void
serialize_cursor_json(
	GstTerminal *term,
	GString     *json
){
	GstCursor *cur;
	const gchar *shape;

	cur = gst_terminal_get_cursor(term);
	if (cur == NULL) {
		g_string_append(json, "{\"x\":0,\"y\":0,\"shape\":\"block\",\"visible\":false}");
		return;
	}

	switch (cur->shape) {
	case GST_CURSOR_SHAPE_UNDERLINE:
		shape = "underline";
		break;
	case GST_CURSOR_SHAPE_BAR:
		shape = "bar";
		break;
	default:
		shape = "block";
		break;
	}

	g_string_append_printf(json,
		"{\"x\":%d,\"y\":%d,\"shape\":\"%s\",\"visible\":%s}",
		cur->x, cur->y, shape,
		(cur->state & GST_CURSOR_STATE_VISIBLE) ? "true" : "false");
}

/*
 * serialize_full_screen:
 *
 * Serializes the entire terminal screen as a JSON "full" message.
 * When scrollback is active (scroll_offset > 0), reads from the
 * scrollback buffer instead of the live terminal. Updates row
 * hashes for subsequent diff detection.
 *
 * Returns: (transfer full): JSON string, or NULL on error
 */
static gchar *
serialize_full_screen(GstWebviewServer *srv)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	GString *json;
	gint cols, rows, y;
	const gchar *title;
	gint scroll_offset;
	gint scroll_count;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return NULL;
	}

	gst_terminal_get_size(term, &cols, &rows);
	title = gst_terminal_get_title(term);

	/* Query scrollback state */
	ensure_scrollback_api(srv);
	scroll_offset = 0;
	scroll_count = 0;
	if (srv->sb_module != NULL) {
		scroll_offset = srv->sb_get_offset(srv->sb_module);
		scroll_count = srv->sb_get_count(srv->sb_module);
	}

	/* Allocate/reallocate row hash array */
	if (srv->prev_rows != rows || srv->prev_cols != cols) {
		g_free(srv->row_hashes);
		srv->row_hashes = g_new0(guint32, rows);
		srv->prev_rows = rows;
		srv->prev_cols = cols;
	}

	/* Pre-size for ~30 bytes per cell */
	json = g_string_sized_new((gsize)(cols * rows * 30 + 256));

	g_string_append(json, "{\"type\":\"full\"");
	g_string_append_printf(json, ",\"cols\":%d,\"rows\":%d", cols, rows);

	/* Title */
	g_string_append(json, ",\"title\":\"");
	if (title != NULL) {
		g_autofree gchar *escaped = g_strescape(title, NULL);
		g_string_append(json, escaped);
	}
	g_string_append_c(json, '"');

	/* Read-only flag */
	g_string_append_printf(json, ",\"read_only\":%s",
		srv->module->read_only ? "true" : "false");

	/* Scrollback state */
	g_string_append_printf(json, ",\"scroll_offset\":%d,\"scroll_count\":%d",
		scroll_offset, scroll_count);

	/* Cursor: hide when viewing scrollback */
	g_string_append(json, ",\"cursor\":");
	if (scroll_offset > 0) {
		g_string_append(json,
			"{\"x\":0,\"y\":0,\"shape\":\"block\",\"visible\":false}");
	} else {
		serialize_cursor_json(term, json);
	}

	/* Lines */
	g_string_append(json, ",\"lines\":[");
	for (y = 0; y < rows; y++) {
		if (y > 0) {
			g_string_append_c(json, ',');
		}

		if (scroll_offset > 0 && y < scroll_offset) {
			/*
			 * This row comes from the scrollback buffer.
			 * Row 0 = oldest visible, row (offset-1) = most recent.
			 * Public API: index 0 = most recent scrollback line.
			 */
			gint line_index;
			gint sb_cols;
			const GstGlyph *glyphs;

			line_index = scroll_offset - 1 - y;
			sb_cols = 0;
			glyphs = srv->sb_get_line_glyphs(
				srv->sb_module, line_index, &sb_cols);
			serialize_glyph_row_json(json, glyphs, sb_cols, cols,
				srv->color_scheme);
			srv->row_hashes[y] = hash_glyph_array(glyphs, sb_cols);
		} else if (scroll_offset > 0) {
			/* Empty row below scrollback content */
			serialize_glyph_row_json(json, NULL, 0, cols,
				srv->color_scheme);
			srv->row_hashes[y] = 2166136261u;
		} else {
			/* Live terminal row */
			serialize_row_json(json, term, y, cols, srv->color_scheme);
			srv->row_hashes[y] = hash_row(term, y, cols);
		}
	}
	g_string_append(json, "]}");

	return g_string_free(json, FALSE);
}

/*
 * serialize_diff_screen:
 *
 * Serializes only the changed rows as a JSON "diff" message.
 * Compares current row hashes against cached values. When
 * scrollback is active, hashes and reads from the scrollback
 * buffer instead of the live terminal.
 *
 * Returns: (transfer full): JSON string, or NULL if no changes
 */
static gchar *
serialize_diff_screen(GstWebviewServer *srv)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	GString *json;
	gint cols, rows, y;
	gboolean any_changed;
	gint scroll_offset;
	gint scroll_count;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return NULL;
	}

	gst_terminal_get_size(term, &cols, &rows);

	/* If dimensions changed, do a full update instead */
	if (rows != srv->prev_rows || cols != srv->prev_cols) {
		return serialize_full_screen(srv);
	}

	/* Query scrollback state */
	ensure_scrollback_api(srv);
	scroll_offset = 0;
	scroll_count = 0;
	if (srv->sb_module != NULL) {
		scroll_offset = srv->sb_get_offset(srv->sb_module);
		scroll_count = srv->sb_get_count(srv->sb_module);
	}

	json = g_string_sized_new(4096);
	g_string_append(json, "{\"type\":\"diff\"");

	/* Scrollback state */
	g_string_append_printf(json, ",\"scroll_offset\":%d,\"scroll_count\":%d",
		scroll_offset, scroll_count);

	/* Cursor: hide when viewing scrollback */
	g_string_append(json, ",\"cursor\":");
	if (scroll_offset > 0) {
		g_string_append(json,
			"{\"x\":0,\"y\":0,\"shape\":\"block\",\"visible\":false}");
	} else {
		serialize_cursor_json(term, json);
	}

	g_string_append(json, ",\"rows\":{");

	any_changed = FALSE;
	for (y = 0; y < rows; y++) {
		guint32 new_hash;

		if (scroll_offset > 0 && y < scroll_offset) {
			/* Row from scrollback buffer */
			gint line_index;
			gint sb_cols;
			const GstGlyph *glyphs;

			line_index = scroll_offset - 1 - y;
			sb_cols = 0;
			glyphs = srv->sb_get_line_glyphs(
				srv->sb_module, line_index, &sb_cols);
			new_hash = hash_glyph_array(glyphs, sb_cols);
			if (new_hash != srv->row_hashes[y]) {
				if (any_changed) {
					g_string_append_c(json, ',');
				}
				g_string_append_printf(json, "\"%d\":", y);
				serialize_glyph_row_json(json, glyphs, sb_cols, cols,
					srv->color_scheme);
				srv->row_hashes[y] = new_hash;
				any_changed = TRUE;
			}
		} else if (scroll_offset > 0) {
			/* Empty row below scrollback */
			new_hash = 2166136261u;
			if (new_hash != srv->row_hashes[y]) {
				if (any_changed) {
					g_string_append_c(json, ',');
				}
				g_string_append_printf(json, "\"%d\":", y);
				serialize_glyph_row_json(json, NULL, 0, cols,
					srv->color_scheme);
				srv->row_hashes[y] = new_hash;
				any_changed = TRUE;
			}
		} else {
			/* Live terminal row */
			new_hash = hash_row(term, y, cols);
			if (new_hash != srv->row_hashes[y]) {
				if (any_changed) {
					g_string_append_c(json, ',');
				}
				g_string_append_printf(json, "\"%d\":", y);
				serialize_row_json(json, term, y, cols, srv->color_scheme);
				srv->row_hashes[y] = new_hash;
				any_changed = TRUE;
			}
		}
	}

	g_string_append(json, "}}");

	if (!any_changed) {
		/* Only cursor may have moved; still send the diff for cursor update */
	}

	return g_string_free(json, FALSE);
}

/* ===== Update Throttling ===== */

/*
 * on_update_tick:
 *
 * Timer callback for throttled screen updates. If an update
 * is pending, serializes dirty rows and broadcasts to clients.
 */
static gboolean
on_update_tick(gpointer user_data)
{
	GstWebviewServer *srv;
	gchar *msg;

	srv = (GstWebviewServer *)user_data;

	if (!srv->update_pending) {
		srv->update_source = 0;
		return G_SOURCE_REMOVE;
	}

	srv->update_pending = FALSE;

	msg = serialize_diff_screen(srv);
	if (msg != NULL) {
		broadcast_text(srv, msg);
		g_free(msg);
	}

	return G_SOURCE_CONTINUE;
}
