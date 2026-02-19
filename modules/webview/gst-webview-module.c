/*
 * gst-webview-module.c - Web view module for GST
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Module lifecycle: reads config from the "webview" YAML section,
 * creates a GstWebviewServer on activate, connects to terminal
 * signals for live content streaming, and tears everything down
 * on deactivate.
 */

#include "gst-webview-module.h"
#include "gst-webview-server.h"

#include <glib.h>

#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/module/gst-module-manager.h"
#include "../../deps/yaml-glib/src/yaml-mapping.h"
#include "../../deps/yaml-glib/src/yaml-node.h"

G_DEFINE_TYPE(GstWebviewModule, gst_webview_module, GST_TYPE_MODULE)

/* ===== YAML config helpers (same pattern as MCP module) ===== */

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
gst_webview_module_get_name(GstModule *module)
{
	(void)module;
	return "webview";
}

static const gchar *
gst_webview_module_get_description(GstModule *module)
{
	(void)module;
	return "Live HTML view of the terminal served over HTTP/WebSocket";
}

/*
 * gst_webview_module_configure:
 *
 * Reads the webview module config section from YAML. Sets all
 * fields with sane defaults when keys are missing.
 */
static void
gst_webview_module_configure(
	GstModule *module,
	gpointer   config
){
	GstWebviewModule *self;
	GstConfig *cfg;
	YamlMapping *mod_cfg;

	self = GST_WEBVIEW_MODULE(module);
	cfg = (GstConfig *)config;
	mod_cfg = gst_config_get_module_config(cfg, "webview");

	/* Host and port */
	g_free(self->host);
	self->host = g_strdup(yaml_get_string(mod_cfg, "host", "127.0.0.1"));
	self->port = (guint)yaml_get_int(mod_cfg, "port", 7681);

	/* Access mode */
	self->read_only = yaml_get_bool(mod_cfg, "read_only", TRUE);

	/* Authentication */
	g_free(self->auth_mode);
	self->auth_mode = g_strdup(yaml_get_string(mod_cfg, "auth", "none"));

	g_free(self->auth_token);
	self->auth_token = g_strdup(yaml_get_string(mod_cfg, "token", ""));

	g_free(self->auth_password);
	self->auth_password = g_strdup(yaml_get_string(mod_cfg, "password", ""));

	/* Throttling and limits */
	self->update_interval = (guint)yaml_get_int(mod_cfg, "update_interval", 50);
	self->max_clients = (guint)yaml_get_int(mod_cfg, "max_clients", 10);

	/* Clamp values to sane ranges */
	if (self->update_interval < 16) {
		self->update_interval = 16;
	}
	if (self->update_interval > 1000) {
		self->update_interval = 1000;
	}
	if (self->max_clients < 1) {
		self->max_clients = 1;
	}
	if (self->max_clients > 100) {
		self->max_clients = 100;
	}

	/* Warn if auth is configured but credentials are empty */
	if (g_strcmp0(self->auth_mode, "token") == 0 &&
		(self->auth_token == NULL || self->auth_token[0] == '\0'))
	{
		g_warning("webview: auth mode is 'token' but no token configured");
	}
	if (g_strcmp0(self->auth_mode, "password") == 0 &&
		(self->auth_password == NULL || self->auth_password[0] == '\0'))
	{
		g_warning("webview: auth mode is 'password' but no password configured");
	}
}

/* ===== Signal handlers (connected to GstTerminal) ===== */

static void
on_contents_changed(
	GstTerminal *term,
	gpointer     user_data
){
	GstWebviewModule *self;

	(void)term;

	self = GST_WEBVIEW_MODULE(user_data);
	gst_webview_server_notify_contents_changed(self->server);
}

static void
on_resize(
	GstTerminal *term,
	gint         cols,
	gint         rows,
	gpointer     user_data
){
	GstWebviewModule *self;

	(void)term;

	self = GST_WEBVIEW_MODULE(user_data);
	gst_webview_server_notify_resize(self->server, cols, rows);
}

static void
on_title_changed(
	GstTerminal *term,
	const gchar *title,
	gpointer     user_data
){
	GstWebviewModule *self;

	(void)term;

	self = GST_WEBVIEW_MODULE(user_data);
	gst_webview_server_notify_title(self->server, title);
}

static void
on_bell(
	GstTerminal *term,
	gpointer     user_data
){
	GstWebviewModule *self;

	(void)term;

	self = GST_WEBVIEW_MODULE(user_data);
	gst_webview_server_notify_bell(self->server);
}

/*
 * gst_webview_module_activate:
 *
 * Creates the webview server, connects to terminal signals,
 * and starts listening.
 */
static gboolean
gst_webview_module_activate(GstModule *module)
{
	GstWebviewModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;
	GError *error;

	self = GST_WEBVIEW_MODULE(module);

	/* Create and start the server */
	self->server = gst_webview_server_new(self);

	error = NULL;
	if (!gst_webview_server_start(self->server, &error)) {
		g_warning("webview: failed to start server: %s",
			error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		gst_webview_server_free(self->server);
		self->server = NULL;
		return FALSE;
	}

	/* Connect to terminal signals */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);

	if (term != NULL) {
		self->sig_contents_changed = g_signal_connect(
			term, "contents-changed",
			G_CALLBACK(on_contents_changed), self);

		self->sig_resize = g_signal_connect(
			term, "resize",
			G_CALLBACK(on_resize), self);

		self->sig_title_changed = g_signal_connect(
			term, "title-changed",
			G_CALLBACK(on_title_changed), self);

		self->sig_bell = g_signal_connect(
			term, "bell",
			G_CALLBACK(on_bell), self);
	}

	g_debug("webview: activated (http://%s:%u/)", self->host, self->port);
	return TRUE;
}

/*
 * gst_webview_module_deactivate:
 *
 * Disconnects terminal signals, stops the server, and cleans up.
 */
static void
gst_webview_module_deactivate(GstModule *module)
{
	GstWebviewModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;

	self = GST_WEBVIEW_MODULE(module);

	/* Disconnect signals */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);

	if (term != NULL) {
		if (self->sig_contents_changed != 0) {
			g_signal_handler_disconnect(term, self->sig_contents_changed);
			self->sig_contents_changed = 0;
		}
		if (self->sig_resize != 0) {
			g_signal_handler_disconnect(term, self->sig_resize);
			self->sig_resize = 0;
		}
		if (self->sig_title_changed != 0) {
			g_signal_handler_disconnect(term, self->sig_title_changed);
			self->sig_title_changed = 0;
		}
		if (self->sig_bell != 0) {
			g_signal_handler_disconnect(term, self->sig_bell);
			self->sig_bell = 0;
		}
	}

	/* Stop and free the server */
	if (self->server != NULL) {
		gst_webview_server_free(self->server);
		self->server = NULL;
	}

	g_debug("webview: deactivated");
}

/* ===== GObject boilerplate ===== */

static void
gst_webview_module_finalize(GObject *object)
{
	GstWebviewModule *self;

	self = GST_WEBVIEW_MODULE(object);

	g_free(self->host);
	g_free(self->auth_mode);
	g_free(self->auth_token);
	g_free(self->auth_password);

	G_OBJECT_CLASS(gst_webview_module_parent_class)->finalize(object);
}

static void
gst_webview_module_class_init(GstWebviewModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_webview_module_finalize;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_webview_module_get_name;
	module_class->get_description = gst_webview_module_get_description;
	module_class->configure = gst_webview_module_configure;
	module_class->activate = gst_webview_module_activate;
	module_class->deactivate = gst_webview_module_deactivate;
}

static void
gst_webview_module_init(GstWebviewModule *self)
{
	self->server = NULL;
	self->host = g_strdup("127.0.0.1");
	self->port = 7681;
	self->read_only = TRUE;
	self->auth_mode = g_strdup("none");
	self->auth_token = g_strdup("");
	self->auth_password = g_strdup("");
	self->update_interval = 50;
	self->max_clients = 10;
	self->sig_contents_changed = 0;
	self->sig_resize = 0;
	self->sig_title_changed = 0;
	self->sig_bell = 0;
}

/* ===== Module entry point ===== */

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return gst_webview_module_get_type();
}
