/*
 * gst-osc52-module.c - Remote clipboard via OSC 52
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements OSC 52 clipboard access:
 *   ESC ] 52 ; <sel> ; <base64-data> ST
 *
 * Selection characters:
 *   c = clipboard, p = primary, s = secondary, 0 = primary
 *
 * If base64 data is "?", the terminal should respond with the
 * current selection contents. This is disabled by default for
 * security (allow_read = FALSE).
 */

#include "gst-osc52-module.h"
#include "../../src/config/gst-config.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/window/gst-window.h"
#include "../../src/util/gst-base64.h"

#include <unistd.h>
#include <errno.h>

struct _GstOsc52Module
{
	GstModule parent_instance;

	gboolean  allow_read;
	gboolean  allow_write;
	gsize     max_bytes;
};

static void
gst_osc52_module_escape_handler_init(GstEscapeHandlerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstOsc52Module, gst_osc52_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_osc52_module_escape_handler_init))

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_osc52_module_get_name(GstModule *module)
{
	(void)module;
	return "osc52";
}

static const gchar *
gst_osc52_module_get_description(GstModule *module)
{
	(void)module;
	return "Remote clipboard via OSC 52";
}

static void
gst_osc52_module_configure(GstModule *module, gpointer config)
{
	GstOsc52Module *self;
	GstConfig *cfg;

	self = GST_OSC52_MODULE(module);
	cfg = (GstConfig *)config;

	self->allow_read = cfg->modules.osc52.allow_read;
	self->allow_write = cfg->modules.osc52.allow_write;
	self->max_bytes = (gsize)cfg->modules.osc52.max_bytes;

	g_debug("osc52: configured (read=%d, write=%d, max=%zu)",
		self->allow_read, self->allow_write, self->max_bytes);
}

static gboolean
gst_osc52_module_activate(GstModule *module)
{
	g_debug("osc52: activated");
	return TRUE;
}

static void
gst_osc52_module_deactivate(GstModule *module)
{
	g_debug("osc52: deactivated");
}

/* ===== Clipboard helpers ===== */

/*
 * osc52_set_clipboard_external:
 * @text: The text to place on the clipboard
 * @len: Length of @text in bytes
 * @is_clipboard: TRUE for CLIPBOARD, FALSE for PRIMARY
 *
 * Sets the system clipboard by piping text to an external tool.
 * Uses wl-copy on Wayland, xclip on X11. This bypasses the Wayland
 * input serial requirement for wl_data_device.set_selection, which
 * silently fails when OSC 52 arrives from programs (e.g., over SSH)
 * rather than direct user input — the serial from the last keypress
 * is often invalidated by keyboard repeats or subsequent events
 * before the OSC 52 response arrives.
 *
 * Returns: TRUE if the clipboard was set successfully
 */
static gboolean
osc52_set_clipboard_external(
	const gchar *text,
	gsize        len,
	gboolean     is_clipboard
){
	GPtrArray *argv;
	gint stdin_fd;
	GError *error;
	gboolean spawned;
	gssize written;
	gsize total;

	argv = g_ptr_array_new();

	if (g_getenv("WAYLAND_DISPLAY") != NULL) {
		g_ptr_array_add(argv, (gpointer)"wl-copy");
		if (!is_clipboard) {
			g_ptr_array_add(argv, (gpointer)"--primary");
		}
	} else {
		g_ptr_array_add(argv, (gpointer)"xclip");
		g_ptr_array_add(argv, (gpointer)"-selection");
		g_ptr_array_add(argv,
			(gpointer)(is_clipboard ? "clipboard" : "primary"));
	}
	g_ptr_array_add(argv, NULL);

	error = NULL;
	spawned = g_spawn_async_with_pipes(
		NULL,
		(gchar **)argv->pdata,
		NULL,
		G_SPAWN_SEARCH_PATH,
		NULL, NULL, NULL,
		&stdin_fd, NULL, NULL,
		&error);

	g_ptr_array_free(argv, TRUE);

	if (!spawned) {
		g_debug("osc52: clipboard command failed: %s",
			error->message);
		g_error_free(error);
		return FALSE;
	}

	/* Write text to stdin, handling partial writes */
	total = 0;
	while (total < len) {
		written = write(stdin_fd, text + total, len - total);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		total += (gsize)written;
	}
	close(stdin_fd);

	return (total == len);
}

/* ===== GstEscapeHandler interface ===== */

/*
 * handle_escape_string:
 *
 * Handles OSC 52 clipboard sequences.
 * Format: ESC ] 52 ; <sel> ; <base64> ST
 *
 * The raw buffer contains "52;<sel>;<base64data>" with semicolons
 * intact (dispatched before term_strparse corrupts them).
 */
static gboolean
gst_osc52_module_handle_escape_string(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstOsc52Module *self;
	GstModuleManager *mgr;
	gpointer window;
	gint osc_num;
	gchar *endptr;
	const gchar *sel_start;
	const gchar *data_start;
	gchar sel_char;
	gboolean is_clipboard;

	self = GST_OSC52_MODULE(handler);

	/* Only handle OSC sequences */
	if (str_type != ']') {
		return FALSE;
	}

	/* Parse the OSC number */
	osc_num = (gint)strtol(buf, &endptr, 10);
	if (osc_num != 52 || endptr == buf) {
		return FALSE;
	}

	/* Skip semicolon after "52" */
	if (*endptr != ';') {
		return FALSE;
	}
	sel_start = endptr + 1;

	/* Parse selection character (c, p, s, 0, etc.) */
	data_start = strchr(sel_start, ';');
	if (data_start == NULL) {
		return FALSE;
	}

	/* First character of the selection specifier */
	sel_char = sel_start[0];
	data_start++; /* skip the semicolon */

	/* Determine target clipboard */
	is_clipboard = (sel_char == 'c' || sel_char == 's');

	/* Check for query ("?") */
	if (data_start[0] == '?' && (data_start[1] == '\0' ||
	    (gsize)(data_start - buf + 1) >= len))
	{
		/* Clipboard read request -- disabled by default */
		if (!self->allow_read) {
			g_debug("osc52: read query rejected "
				"(allow_read=false)");
		}
		return TRUE;
	}

	/* Write operation */
	if (!self->allow_write) {
		g_debug("osc52: write rejected (allow_write=false)");
		return TRUE;
	}

	/* Decode base64 data */
	{
		gsize data_len;
		guchar *decoded;
		gsize decoded_len;

		data_len = len - (gsize)(data_start - buf);

		decoded = g_base64_decode_inplace(
			g_strndup(data_start, data_len),
			&decoded_len);

		if (decoded == NULL || decoded_len == 0) {
			g_free(decoded);
			return TRUE;
		}

		/* Enforce size limit */
		if (decoded_len > self->max_bytes) {
			g_debug("osc52: payload too large "
				"(%zu > %zu), rejected",
				decoded_len, self->max_bytes);
			g_free(decoded);
			return TRUE;
		}

		/*
		 * Set the system clipboard. Use an external tool
		 * (wl-copy on Wayland, xclip on X11) to bypass the
		 * Wayland serial requirement for set_selection, which
		 * silently fails for programmatic clipboard sets (e.g.,
		 * OSC 52 over SSH). Also set via the window API so the
		 * terminal has the content cached for self-paste.
		 */
		{
			g_autofree gchar *text = NULL;
			gboolean ext_ok;

			/* Ensure null-termination */
			text = g_strndup((const gchar *)decoded,
				decoded_len);

			ext_ok = osc52_set_clipboard_external(text,
				decoded_len, is_clipboard);

			/*
			 * Always set via window API as well. This gives
			 * the terminal a cached copy for self-paste (the
			 * Wayland deadlock avoidance path) and acts as a
			 * fallback if the external tool is missing.
			 */
			mgr = gst_module_manager_get_default();
			window = gst_module_manager_get_window(mgr);
			if (window != NULL && GST_IS_WINDOW(window)) {
				gst_window_set_selection(
					GST_WINDOW(window),
					text, is_clipboard);
			}

			g_debug("osc52: set %s (%zu bytes, %s)",
				is_clipboard ? "clipboard" : "primary",
				decoded_len,
				ext_ok ? "external" : "window-api");
		}

		g_free(decoded);
	}

	return TRUE;
}

static void
gst_osc52_module_escape_handler_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string =
		gst_osc52_module_handle_escape_string;
}

/* ===== GObject lifecycle ===== */

static void
gst_osc52_module_class_init(GstOsc52ModuleClass *klass)
{
	GstModuleClass *module_class;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_osc52_module_get_name;
	module_class->get_description = gst_osc52_module_get_description;
	module_class->activate = gst_osc52_module_activate;
	module_class->deactivate = gst_osc52_module_deactivate;
	module_class->configure = gst_osc52_module_configure;
}

static void
gst_osc52_module_init(GstOsc52Module *self)
{
	self->allow_read = FALSE;
	self->allow_write = TRUE;
	self->max_bytes = 100000;
}

/* ===== Module entry point ===== */

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_OSC52_MODULE;
}
