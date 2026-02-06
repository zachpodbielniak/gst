/*
 * gst-externalpipe-module.c - External pipe module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Pipes visible terminal screen content to an external command
 * on keyboard shortcut. Implements GstInputHandler for key binding
 * and GstExternalPipe for the pipe_data interface.
 */

#include "gst-externalpipe-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/boxed/gst-glyph.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <string.h>
#include <unistd.h>

/**
 * SECTION:gst-externalpipe-module
 * @title: GstExternalpipeModule
 * @short_description: Pipe terminal content to external commands
 *
 * Collects visible terminal text and pipes it to a configurable
 * external command via stdin. Triggered by Ctrl+Shift+E by default.
 */

struct _GstExternalpipeModule
{
	GstModule parent_instance;
	gchar    *command;
	guint     trigger_keyval;
	guint     trigger_state;
};

/* Forward declarations */
static void
gst_externalpipe_module_input_init(GstInputHandlerInterface *iface);
static void
gst_externalpipe_module_pipe_init(GstExternalPipeInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstExternalpipeModule, gst_externalpipe_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER,
		gst_externalpipe_module_input_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_EXTERNAL_PIPE,
		gst_externalpipe_module_pipe_init))

/* ===== Internal helpers ===== */

/*
 * collect_screen_text:
 *
 * Builds a UTF-8 string from the visible terminal screen.
 * Returns newly allocated text (caller must free).
 */
static gchar *
collect_screen_text(void)
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

/*
 * spawn_pipe:
 *
 * Spawns the command and writes data to its stdin.
 */
static gboolean
spawn_pipe(const gchar *command, const gchar *data, gsize length)
{
	gint child_stdin;
	GError *error = NULL;
	gchar *argv[4];
	gboolean ok;

	argv[0] = (gchar *)"/bin/sh";
	argv[1] = (gchar *)"-c";
	argv[2] = (gchar *)command;
	argv[3] = NULL;

	ok = g_spawn_async_with_pipes(
		NULL, argv, NULL,
		G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
		NULL, NULL, NULL,
		&child_stdin, NULL, NULL,
		&error);

	if (!ok) {
		g_warning("externalpipe: spawn failed: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	/* Write data to stdin and close */
	if (data != NULL && length > 0) {
		gssize written;
		gsize total;

		total = 0;
		while (total < length) {
			written = write(child_stdin, data + total, length - total);
			if (written <= 0) {
				break;
			}
			total += (gsize)written;
		}
	}

	close(child_stdin);
	return TRUE;
}

/* ===== GstInputHandler interface ===== */

static gboolean
gst_externalpipe_module_handle_key_event(
	GstInputHandler *handler,
	guint            keyval,
	guint            keycode,
	guint            state
){
	GstExternalpipeModule *self;
	g_autofree gchar *text = NULL;

	self = GST_EXTERNALPIPE_MODULE(handler);

	/* Check if this matches our trigger key */
	if (keyval != self->trigger_keyval) {
		return FALSE;
	}

	/* Check modifier state (mask out numlock, capslock, etc.) */
	if ((state & (ShiftMask | ControlMask | Mod1Mask)) != self->trigger_state) {
		return FALSE;
	}

	if (self->command == NULL || self->command[0] == '\0') {
		g_warning("externalpipe: no command configured");
		return TRUE;
	}

	text = collect_screen_text();
	spawn_pipe(self->command, text, strlen(text));

	return TRUE;
}

static void
gst_externalpipe_module_input_init(GstInputHandlerInterface *iface)
{
	iface->handle_key_event = gst_externalpipe_module_handle_key_event;
}

/* ===== GstExternalPipe interface ===== */

static gboolean
gst_externalpipe_module_pipe_data(
	GstExternalPipe *pipe,
	const gchar     *command,
	const gchar     *data,
	gsize            length
){
	return spawn_pipe(command, data, length);
}

static void
gst_externalpipe_module_pipe_init(GstExternalPipeInterface *iface)
{
	iface->pipe_data = gst_externalpipe_module_pipe_data;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_externalpipe_module_get_name(GstModule *module)
{
	(void)module;
	return "externalpipe";
}

static const gchar *
gst_externalpipe_module_get_description(GstModule *module)
{
	(void)module;
	return "Pipe terminal content to external commands";
}

static gboolean
gst_externalpipe_module_activate(GstModule *module)
{
	g_debug("externalpipe: activated");
	return TRUE;
}

static void
gst_externalpipe_module_deactivate(GstModule *module)
{
	g_debug("externalpipe: deactivated");
}

/*
 * configure:
 *
 * Reads externalpipe configuration from the YAML config:
 *  - command: the shell command to pipe terminal content to
 */
static void
gst_externalpipe_module_configure(GstModule *module, gpointer config)
{
	GstExternalpipeModule *self;
	YamlMapping *mod_cfg;

	self = GST_EXTERNALPIPE_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "externalpipe");
	if (mod_cfg == NULL)
	{
		g_debug("externalpipe: no config section, using defaults");
		return;
	}

	if (yaml_mapping_has_member(mod_cfg, "command"))
	{
		const gchar *val;

		val = yaml_mapping_get_string_member(mod_cfg, "command");
		if (val != NULL)
		{
			g_free(self->command);
			self->command = g_strdup(val);
		}
	}

	g_debug("externalpipe: configured (command=%s)", self->command);
}

/* ===== GObject lifecycle ===== */

static void
gst_externalpipe_module_dispose(GObject *object)
{
	GstExternalpipeModule *self;

	self = GST_EXTERNALPIPE_MODULE(object);
	g_clear_pointer(&self->command, g_free);

	G_OBJECT_CLASS(gst_externalpipe_module_parent_class)->dispose(object);
}

static void
gst_externalpipe_module_class_init(GstExternalpipeModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_externalpipe_module_dispose;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_externalpipe_module_get_name;
	module_class->get_description = gst_externalpipe_module_get_description;
	module_class->activate = gst_externalpipe_module_activate;
	module_class->deactivate = gst_externalpipe_module_deactivate;
	module_class->configure = gst_externalpipe_module_configure;
}

static void
gst_externalpipe_module_init(GstExternalpipeModule *self)
{
	self->command = g_strdup("");
	/* Default trigger: Ctrl+Shift+E */
	self->trigger_keyval = XK_E;
	self->trigger_state = ShiftMask | ControlMask;
}

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_EXTERNALPIPE_MODULE;
}
