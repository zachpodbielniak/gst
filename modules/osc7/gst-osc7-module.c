/*
 * gst-osc7-module.c - OSC 7 working-directory reports
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shells report the logical directory as:
 *   OSC 7 ; file://hostname/path ST
 *
 * The path is stored when the host is this machine and the decoded
 * path is an existing absolute directory. That stays right when
 * /proc cwd is a container, sudo, or an SSH session. The directory
 * is not fed to program resume. Until an application sets a title,
 * the window title is the directory's basename.
 */

#include "gst-osc7-module.h"

#include <string.h>

#include "../../src/core/gst-terminal.h"
#include "../../src/module/gst-module-manager.h"

struct _GstOsc7Module
{
	GstModule   parent_instance;
	GstTerminal *watched;    /* unowned, the title-changed source */
	gulong       title_sig;
	gchar       *cwd;
	gboolean     app_titled; /* an application title owns the window */
	gboolean     applying;   /* ignore the basename we just wrote */
};

static void
gst_osc7_module_escape_init(GstEscapeHandlerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstOsc7Module, gst_osc7_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_osc7_module_escape_init))

/*
 * host_is_local:
 * @host: the host part of a file:// URL, possibly empty
 *
 * Accepts an empty host, "localhost", this machine's hostname, and
 * that hostname followed by a dot (a local FQDN).
 *
 * Returns: %TRUE when the path can be used on this machine
 */
static gboolean
host_is_local(const gchar *host)
{
	const gchar *mine;
	gsize length;

	if (host[0] == '\0') {
		return TRUE;
	}
	if (g_ascii_strcasecmp(host, "localhost") == 0) {
		return TRUE;
	}
	mine = g_get_host_name();
	if (mine == NULL || mine[0] == '\0') {
		return FALSE;
	}
	if (g_ascii_strcasecmp(host, mine) == 0) {
		return TRUE;
	}
	length = strlen(mine);
	if (strlen(host) > length &&
	    g_ascii_strncasecmp(host, mine, length) == 0 &&
	    host[length] == '.')
	{
		return TRUE;
	}
	return FALSE;
}

/*
 * local_directory:
 * @rest: the OSC 7 body
 *
 * Percent-decodes a local file:// path and accepts it only when it
 * is absolute, has no C0 or DEL bytes, and names an existing directory.
 * A foreign host or a missing path is not stored.
 *
 * Returns: (transfer full) (nullable): the directory, or %NULL
 */
static gchar *
local_directory(const gchar *rest)
{
	const gchar *body;
	const gchar *slash;
	const guchar *cursor;
	gchar *host;
	gchar *decoded;

	if (rest == NULL || !g_str_has_prefix(rest, "file://")) {
		return NULL;
	}

	body = rest + 7;
	slash = strchr(body, '/');
	if (slash == NULL) {
		return NULL;
	}

	host = g_strndup(body, (gsize)(slash - body));
	if (!host_is_local(host)) {
		g_free(host);
		return NULL;
	}
	g_free(host);

	decoded = g_uri_unescape_string(slash, NULL);
	if (decoded == NULL) {
		return NULL;
	}
	for (cursor = (const guchar *)decoded; *cursor != '\0'; cursor++) {
		if (*cursor < 0x20 || *cursor == 0x7f) {
			g_free(decoded);
			return NULL;
		}
	}
	if (!g_path_is_absolute(decoded) ||
	    !g_file_test(decoded, G_FILE_TEST_IS_DIR))
	{
		g_free(decoded);
		return NULL;
	}
	return decoded;
}

static void
on_title_changed(
	GstTerminal *term,
	const gchar *title,
	gpointer     user_data
){
	GstOsc7Module *self;

	(void)term;
	(void)title;

	self = GST_OSC7_MODULE(user_data);
	if (self->applying) {
		return;
	}
	/* An application title, including a progress prefix, wins. */
	self->app_titled = TRUE;
}

static void
disconnect_title(GstOsc7Module *self)
{
	if (self->title_sig != 0 && self->watched != NULL &&
	    GST_IS_TERMINAL(self->watched))
	{
		g_signal_handler_disconnect(self->watched, self->title_sig);
	}
	self->title_sig = 0;
	self->watched = NULL;
}

static void
watch_terminal(
	GstOsc7Module *self,
	gpointer       terminal
){
	if (self->title_sig != 0) {
		return;
	}
	if (terminal == NULL || !GST_IS_TERMINAL(terminal)) {
		return;
	}
	self->watched = GST_TERMINAL(terminal);
	self->title_sig = g_signal_connect(self->watched, "title-changed",
		G_CALLBACK(on_title_changed), self);
}

/*
 * show_basename:
 *
 * Use the directory name as the window title only while no
 * application has set one. "/" stays "/" rather than becoming empty.
 */
static void
show_basename(
	GstOsc7Module *self,
	GstTerminal   *term
){
	gchar *base;

	if (self->app_titled || term == NULL || self->cwd == NULL) {
		return;
	}

	base = g_path_get_basename(self->cwd);
	if (base == NULL || base[0] == '\0') {
		g_free(base);
		base = g_strdup("/");
	}
	self->applying = TRUE;
	gst_terminal_set_title(term, base);
	self->applying = FALSE;
	g_free(base);
}

static const gchar *
gst_osc7_module_get_name(GstModule *module)
{
	(void)module;
	return "osc7";
}

static const gchar *
gst_osc7_module_get_description(GstModule *module)
{
	(void)module;
	return "Shell working directory via OSC 7";
}

static gboolean
gst_osc7_module_activate(GstModule *module)
{
	GstOsc7Module *self;
	GstModuleManager *mgr;

	self = GST_OSC7_MODULE(module);
	mgr = gst_module_manager_get_default();
	watch_terminal(self, gst_module_manager_get_terminal(mgr));
	g_debug("osc7: activated");
	return TRUE;
}

static void
gst_osc7_module_deactivate(GstModule *module)
{
	GstOsc7Module *self;

	self = GST_OSC7_MODULE(module);
	disconnect_title(self);
	g_debug("osc7: deactivated");
}

static gboolean
gst_osc7_module_handle_escape_string(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstOsc7Module *self;
	GstTerminal *term;
	gint osc_num;
	gchar *endptr;
	const gchar *rest;
	gchar *path;

	(void)len;

	self = GST_OSC7_MODULE(handler);
	if (str_type != ']') {
		return FALSE;
	}

	osc_num = (gint)strtol(buf, &endptr, 10);
	if (endptr == buf) {
		return FALSE;
	}
	if (osc_num != 7) {
		return FALSE;
	}
	if (*endptr == ';') {
		rest = endptr + 1;
	} else {
		rest = endptr;
	}

	/*
	 * OSC 7 is consumed even when the URL is ignored, so it does
	 * not fall through to another module.
	 */
	path = local_directory(rest);
	if (path == NULL) {
		return TRUE;
	}

	g_free(self->cwd);
	self->cwd = path;
	term = NULL;
	if (terminal != NULL && GST_IS_TERMINAL(terminal)) {
		term = GST_TERMINAL(terminal);
	}
	watch_terminal(self, term);
	show_basename(self, term);
	return TRUE;
}

static void
gst_osc7_module_escape_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string = gst_osc7_module_handle_escape_string;
}

static void
gst_osc7_module_finalize(GObject *object)
{
	GstOsc7Module *self;

	self = GST_OSC7_MODULE(object);
	disconnect_title(self);
	g_clear_pointer(&self->cwd, g_free);
	G_OBJECT_CLASS(gst_osc7_module_parent_class)->finalize(object);
}

static void
gst_osc7_module_class_init(GstOsc7ModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	module_class = GST_MODULE_CLASS(klass);
	object_class->finalize = gst_osc7_module_finalize;
	module_class->get_name = gst_osc7_module_get_name;
	module_class->get_description = gst_osc7_module_get_description;
	module_class->activate = gst_osc7_module_activate;
	module_class->deactivate = gst_osc7_module_deactivate;
}

static void
gst_osc7_module_init(GstOsc7Module *self)
{
	self->watched = NULL;
	self->title_sig = 0;
	self->cwd = NULL;
	self->app_titled = FALSE;
	self->applying = FALSE;
}

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_OSC7_MODULE;
}
