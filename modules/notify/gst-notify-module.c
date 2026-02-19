/*
 * gst-notify-module.c - Desktop notification module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Handles OSC 9 (iTerm2), OSC 777 (rxvt), and OSC 99 (kitty)
 * notification escape sequences. Dispatches desktop notifications
 * via the notify-send command.
 */

#include "gst-notify-module.h"
#include "../../src/config/gst-config.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/window/gst-window.h"
#include "../../src/core/gst-terminal.h"

struct _GstNotifyModule
{
	GstModule parent_instance;

	gboolean  show_title;       /* include terminal title */
	gchar    *urgency;          /* "low", "normal", "critical" */
	gint      timeout_secs;     /* -1 for system default */
	gboolean  suppress_focused; /* suppress when focused */

	gboolean  is_focused;       /* tracked focus state */
	gulong    focus_sig_id;     /* focus-change signal handler ID */
};

static void
gst_notify_module_escape_handler_init(GstEscapeHandlerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstNotifyModule, gst_notify_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_notify_module_escape_handler_init))

/* ===== Internal helpers ===== */

/*
 * send_notification:
 *
 * Dispatches a desktop notification via the notify-send command.
 * Escapes title and body to prevent shell injection.
 */
static void
send_notification(
	GstNotifyModule *self,
	const gchar     *title,
	const gchar     *body
){
	g_autofree gchar *cmd = NULL;
	g_autofree gchar *escaped_title = NULL;
	g_autofree gchar *escaped_body = NULL;
	GError *error = NULL;

	if (title == NULL) {
		title = "Terminal";
	}
	if (body == NULL || body[0] == '\0') {
		return;
	}

	escaped_title = g_shell_quote(title);
	escaped_body = g_shell_quote(body);

	if (self->timeout_secs >= 0) {
		cmd = g_strdup_printf("notify-send -u %s -t %d %s %s",
			self->urgency != NULL ? self->urgency : "normal",
			self->timeout_secs * 1000,
			escaped_title, escaped_body);
	} else {
		cmd = g_strdup_printf("notify-send -u %s %s %s",
			self->urgency != NULL ? self->urgency : "normal",
			escaped_title, escaped_body);
	}

	if (!g_spawn_command_line_async(cmd, &error)) {
		g_debug("notify: failed to spawn notify-send: %s",
			error->message);
		g_error_free(error);
	}
}

/*
 * on_focus_change:
 *
 * Signal handler for window focus-change events.
 * Updates the is_focused tracker.
 */
static void
on_focus_change(
	GstWindow *window,
	gboolean   focused,
	gpointer   user_data
){
	GstNotifyModule *self;

	self = GST_NOTIFY_MODULE(user_data);
	self->is_focused = focused;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_notify_module_get_name(GstModule *module)
{
	(void)module;
	return "notify";
}

static const gchar *
gst_notify_module_get_description(GstModule *module)
{
	(void)module;
	return "Desktop notifications via OSC 9/777/99";
}

static void
gst_notify_module_configure(GstModule *module, gpointer config)
{
	GstNotifyModule *self;
	GstConfig *cfg;

	self = GST_NOTIFY_MODULE(module);
	cfg = (GstConfig *)config;

	self->show_title = cfg->modules.notify.show_title;
	self->suppress_focused = cfg->modules.notify.suppress_focused;
	self->timeout_secs = cfg->modules.notify.timeout;

	g_free(self->urgency);
	self->urgency = g_strdup(cfg->modules.notify.urgency);

	g_debug("notify: configured (urgency=%s, suppress_focused=%d)",
		self->urgency, self->suppress_focused);
}

static gboolean
gst_notify_module_activate(GstModule *module)
{
	GstNotifyModule *self;
	GstModuleManager *mgr;
	gpointer window;

	self = GST_NOTIFY_MODULE(module);
	mgr = gst_module_manager_get_default();
	window = gst_module_manager_get_window(mgr);

	/* Connect to focus-change signal for suppress_focused */
	if (window != NULL && GST_IS_WINDOW(window)) {
		self->focus_sig_id = g_signal_connect(
			window, "focus-change",
			G_CALLBACK(on_focus_change), self);
		self->is_focused = TRUE;
	}

	g_debug("notify: activated");
	return TRUE;
}

static void
gst_notify_module_deactivate(GstModule *module)
{
	GstNotifyModule *self;
	GstModuleManager *mgr;
	gpointer window;

	self = GST_NOTIFY_MODULE(module);
	mgr = gst_module_manager_get_default();
	window = gst_module_manager_get_window(mgr);

	if (self->focus_sig_id != 0 && window != NULL) {
		g_signal_handler_disconnect(window, self->focus_sig_id);
		self->focus_sig_id = 0;
	}

	g_debug("notify: deactivated");
}

/* ===== GstEscapeHandler interface ===== */

/*
 * handle_escape_string:
 *
 * Handles OSC 9, 777, and 99 notification sequences.
 *
 * OSC 9: ESC ] 9 ; message ST
 * OSC 777: ESC ] 777 ; notify ; title ; body ST
 * OSC 99: ESC ] 99 ; body ST (simplified kitty subset)
 *
 * The raw buffer contains the full OSC content with semicolons intact.
 */
static gboolean
gst_notify_module_handle_escape_string(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstNotifyModule *self;
	gint osc_num;
	const gchar *rest;
	gchar *endptr;

	self = GST_NOTIFY_MODULE(handler);

	/* Only handle OSC sequences */
	if (str_type != ']') {
		return FALSE;
	}

	/* Suppress if focused */
	if (self->suppress_focused && self->is_focused) {
		return FALSE;
	}

	/* Parse the OSC number from the beginning of the buffer */
	osc_num = (gint)strtol(buf, &endptr, 10);
	if (endptr == buf) {
		return FALSE;
	}

	/* Skip the semicolon after the number */
	if (*endptr == ';') {
		rest = endptr + 1;
	} else {
		return FALSE;
	}

	switch (osc_num) {
	case 9:
		/* OSC 9 ; message ST */
		{
			const gchar *title = NULL;

			if (self->show_title && terminal != NULL) {
				title = gst_terminal_get_title(
					GST_TERMINAL(terminal));
			}
			send_notification(self,
				title != NULL ? title : "Terminal",
				rest);
		}
		return TRUE;

	case 777:
		/* OSC 777 ; notify ; title ; body ST */
		{
			const gchar *semi1;
			const gchar *semi2;

			semi1 = strchr(rest, ';');
			if (semi1 == NULL) {
				return FALSE;
			}
			/* Check if the command is "notify" */
			if (g_ascii_strncasecmp(rest, "notify", 6) != 0) {
				return FALSE;
			}
			semi1++;
			semi2 = strchr(semi1, ';');
			if (semi2 != NULL) {
				g_autofree gchar *title = NULL;

				title = g_strndup(semi1,
					(gsize)(semi2 - semi1));
				send_notification(self, title, semi2 + 1);
			} else {
				send_notification(self, "Terminal", semi1);
			}
		}
		return TRUE;

	case 99:
		/* OSC 99 ; body ST (simplified) */
		{
			const gchar *title = NULL;

			if (self->show_title && terminal != NULL) {
				title = gst_terminal_get_title(
					GST_TERMINAL(terminal));
			}
			send_notification(self,
				title != NULL ? title : "Terminal",
				rest);
		}
		return TRUE;

	default:
		return FALSE;
	}
}

static void
gst_notify_module_escape_handler_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string =
		gst_notify_module_handle_escape_string;
}

/* ===== GObject lifecycle ===== */

static void
gst_notify_module_finalize(GObject *object)
{
	GstNotifyModule *self;

	self = GST_NOTIFY_MODULE(object);
	g_free(self->urgency);

	G_OBJECT_CLASS(gst_notify_module_parent_class)->finalize(object);
}

static void
gst_notify_module_class_init(GstNotifyModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_notify_module_finalize;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_notify_module_get_name;
	module_class->get_description = gst_notify_module_get_description;
	module_class->activate = gst_notify_module_activate;
	module_class->deactivate = gst_notify_module_deactivate;
	module_class->configure = gst_notify_module_configure;
}

static void
gst_notify_module_init(GstNotifyModule *self)
{
	self->show_title = TRUE;
	self->urgency = g_strdup("normal");
	self->timeout_secs = -1;
	self->suppress_focused = TRUE;
	self->is_focused = TRUE;
	self->focus_sig_id = 0;
}

/* ===== Module entry point ===== */

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_NOTIFY_MODULE;
}
