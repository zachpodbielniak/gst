/*
 * gst-urlclick-module.c - URL detection and opening module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Detects URLs in the visible terminal screen and opens them via
 * an external opener command. Triggered by Ctrl+Shift+U.
 * Implements GstInputHandler for key binding and GstUrlHandler
 * for the open_url interface.
 */

#include "gst-urlclick-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/boxed/gst-glyph.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <string.h>

/**
 * SECTION:gst-urlclick-module
 * @title: GstUrlclickModule
 * @short_description: URL detection and opening
 *
 * #GstUrlclickModule scans visible terminal text for URLs using
 * a compiled regex, and opens the first match with a configurable
 * opener command (default: xdg-open). Triggered by Ctrl+Shift+U.
 */

struct _GstUrlclickModule
{
	GstModule parent_instance;

	gchar    *opener;           /* opener command (default: "xdg-open") */
	GRegex   *url_regex;        /* compiled URL pattern */
	guint     trigger_keyval;   /* trigger key (default: XK_U) */
	guint     trigger_state;    /* trigger modifiers */
};

/* Forward declarations */
static void
gst_urlclick_module_input_init(GstInputHandlerInterface *iface);
static void
gst_urlclick_module_url_init(GstUrlHandlerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstUrlclickModule, gst_urlclick_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER,
		gst_urlclick_module_input_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_URL_HANDLER,
		gst_urlclick_module_url_init))

/* ===== Internal helpers ===== */

/*
 * collect_visible_text:
 *
 * Builds a UTF-8 string from all visible terminal lines,
 * separated by newlines. Returns newly allocated text.
 */
static gchar *
collect_visible_text(void)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	GString *buf;
	gint rows;
	gint cols;
	gint y;
	gint x;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return g_strdup("");
	}

	gst_terminal_get_size(term, &cols, &rows);
	buf = g_string_new(NULL);

	for (y = 0; y < rows; y++) {
		GstLine *line;

		line = gst_terminal_get_line(term, y);
		if (line == NULL) {
			g_string_append_c(buf, '\n');
			continue;
		}

		for (x = 0; x < cols; x++) {
			GstGlyph *g;
			gchar utf8_buf[6];
			gint utf8_len;

			g = gst_line_get_glyph(line, x);
			if (g == NULL || g->rune == 0) {
				g_string_append_c(buf, ' ');
				continue;
			}

			/* Skip wide char dummies */
			if (g->attr & GST_GLYPH_ATTR_WDUMMY) {
				continue;
			}

			utf8_len = g_unichar_to_utf8(g->rune, utf8_buf);
			g_string_append_len(buf, utf8_buf, utf8_len);
		}

		g_string_append_c(buf, '\n');
	}

	return g_string_free(buf, FALSE);
}

/* ===== GstInputHandler interface ===== */

/*
 * handle_key_event:
 *
 * On Ctrl+Shift+U, scans visible text for URLs and opens the first match.
 */
static gboolean
gst_urlclick_module_handle_key_event(
	GstInputHandler *handler,
	guint            keyval,
	guint            keycode,
	guint            state
){
	GstUrlclickModule *self;
	g_autofree gchar *text = NULL;
	GMatchInfo *match_info;

	self = GST_URLCLICK_MODULE(handler);

	/* Check trigger key */
	if (keyval != self->trigger_keyval) {
		return FALSE;
	}

	/* Check modifier state (mask out numlock, capslock, etc.) */
	if ((state & (ShiftMask | ControlMask | Mod1Mask)) != self->trigger_state) {
		return FALSE;
	}

	if (self->url_regex == NULL) {
		g_warning("urlclick: no URL regex compiled");
		return TRUE;
	}

	text = collect_visible_text();

	/* Find all URL matches */
	if (g_regex_match(self->url_regex, text, 0, &match_info)) {
		g_autofree gchar *url = NULL;

		url = g_match_info_fetch(match_info, 0);
		if (url != NULL && url[0] != '\0') {
			g_message("urlclick: opening URL: %s", url);
			gst_url_handler_open_url(
				GST_URL_HANDLER(self), url);
		}

		/* Log additional matches */
		while (g_match_info_next(match_info, NULL)) {
			g_autofree gchar *extra = NULL;

			extra = g_match_info_fetch(match_info, 0);
			if (extra != NULL && extra[0] != '\0') {
				g_message("urlclick: additional URL found: %s", extra);
			}
		}
	}

	g_match_info_free(match_info);
	return TRUE;
}

static void
gst_urlclick_module_input_init(GstInputHandlerInterface *iface)
{
	iface->handle_key_event = gst_urlclick_module_handle_key_event;
}

/* ===== GstUrlHandler interface ===== */

/*
 * open_url:
 *
 * Opens the given URL using the configured opener command.
 */
static gboolean
gst_urlclick_module_open_url(
	GstUrlHandler *url_handler,
	const gchar   *url
){
	GstUrlclickModule *self;
	g_autofree gchar *cmd = NULL;
	GError *error = NULL;

	self = GST_URLCLICK_MODULE(url_handler);

	if (self->opener == NULL || self->opener[0] == '\0') {
		g_warning("urlclick: no opener command configured");
		return FALSE;
	}

	/* Build command with shell quoting for the URL */
	cmd = g_strdup_printf("%s '%s'", self->opener, url);

	if (!g_spawn_command_line_async(cmd, &error)) {
		g_warning("urlclick: failed to open URL: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	return TRUE;
}

static void
gst_urlclick_module_url_init(GstUrlHandlerInterface *iface)
{
	iface->open_url = gst_urlclick_module_open_url;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_urlclick_module_get_name(GstModule *module)
{
	(void)module;
	return "urlclick";
}

static const gchar *
gst_urlclick_module_get_description(GstModule *module)
{
	(void)module;
	return "URL detection and opening";
}

static gboolean
gst_urlclick_module_activate(GstModule *module)
{
	g_debug("urlclick: activated");
	return TRUE;
}

static void
gst_urlclick_module_deactivate(GstModule *module)
{
	g_debug("urlclick: deactivated");
}

/*
 * configure:
 *
 * Reads urlclick configuration from the YAML config:
 *  - opener: command to open URLs (e.g. "xdg-open")
 *  - regex: URL detection regex pattern (recompiles on change)
 */
static void
gst_urlclick_module_configure(GstModule *module, gpointer config)
{
	GstUrlclickModule *self;
	YamlMapping *mod_cfg;

	self = GST_URLCLICK_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "urlclick");
	if (mod_cfg == NULL)
	{
		g_debug("urlclick: no config section, using defaults");
		return;
	}

	if (yaml_mapping_has_member(mod_cfg, "opener"))
	{
		const gchar *val;

		val = yaml_mapping_get_string_member(mod_cfg, "opener");
		if (val != NULL && val[0] != '\0')
		{
			g_free(self->opener);
			self->opener = g_strdup(val);
		}
	}

	if (yaml_mapping_has_member(mod_cfg, "regex"))
	{
		const gchar *val;

		val = yaml_mapping_get_string_member(mod_cfg, "regex");
		if (val != NULL && val[0] != '\0')
		{
			GRegex *new_regex;
			GError *error = NULL;

			new_regex = g_regex_new(val,
				G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
				0, &error);
			if (new_regex != NULL)
			{
				g_clear_pointer(&self->url_regex, g_regex_unref);
				self->url_regex = new_regex;
			}
			else
			{
				g_warning("urlclick: invalid regex '%s': %s",
					val, error->message);
				g_error_free(error);
			}
		}
	}

	g_debug("urlclick: configured (opener=%s)", self->opener);
}

/* ===== GObject lifecycle ===== */

static void
gst_urlclick_module_dispose(GObject *object)
{
	GstUrlclickModule *self;

	self = GST_URLCLICK_MODULE(object);

	g_clear_pointer(&self->opener, g_free);
	g_clear_pointer(&self->url_regex, g_regex_unref);

	G_OBJECT_CLASS(gst_urlclick_module_parent_class)->dispose(object);
}

static void
gst_urlclick_module_class_init(GstUrlclickModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_urlclick_module_dispose;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_urlclick_module_get_name;
	module_class->get_description = gst_urlclick_module_get_description;
	module_class->activate = gst_urlclick_module_activate;
	module_class->deactivate = gst_urlclick_module_deactivate;
	module_class->configure = gst_urlclick_module_configure;
}

static void
gst_urlclick_module_init(GstUrlclickModule *self)
{
	GError *error = NULL;

	self->opener = g_strdup("xdg-open");

	/* Default trigger: Ctrl+Shift+U */
	self->trigger_keyval = XK_U;
	self->trigger_state = ShiftMask | ControlMask;

	/* Compile default URL regex */
	self->url_regex = g_regex_new(
		"(https?|ftp|file)://[\\w\\-_.~:/?#\\[\\]@!$&'()*+,;=%]+",
		G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
		0, &error);

	if (error != NULL) {
		g_warning("urlclick: failed to compile URL regex: %s",
			error->message);
		g_error_free(error);
		self->url_regex = NULL;
	}
}

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_URLCLICK_MODULE;
}
