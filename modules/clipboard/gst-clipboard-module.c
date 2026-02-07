/*
 * gst-clipboard-module.c - Automatic clipboard sync module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * When text is selected in the terminal (button-release on button 1),
 * this module automatically copies the PRIMARY selection to CLIPBOARD.
 * This mirrors the st-clipboard patch behavior: select text once and
 * it's available in both PRIMARY (middle-click) and CLIPBOARD (Ctrl+V).
 *
 * The module uses the abstract GstWindow API so it works with both
 * X11 and Wayland backends.
 */

#include "gst-clipboard-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/window/gst-window.h"
#include "../../src/config/gst-config.h"

/**
 * SECTION:gst-clipboard-module
 * @title: GstClipboardModule
 * @short_description: Sync PRIMARY selection to CLIPBOARD
 *
 * #GstClipboardModule listens for button-release events on the window.
 * When button 1 is released (end of text selection), it calls
 * gst_window_copy_to_clipboard() to sync the PRIMARY selection into
 * the CLIPBOARD buffer.
 */

struct _GstClipboardModule
{
	GstModule parent_instance;
	gulong    sig_id;  /* signal handler ID for disconnection */
};

G_DEFINE_TYPE(GstClipboardModule, gst_clipboard_module, GST_TYPE_MODULE)

/* ===== Signal callback ===== */

/*
 * on_button_release:
 * @win: the window that emitted the signal
 * @button: which button was released
 * @state: modifier state
 * @px: pixel x coordinate
 * @py: pixel y coordinate
 * @time: event timestamp
 * @user_data: pointer to our module instance
 *
 * When the left mouse button (button 1) is released, the selection
 * is finalized. We copy the PRIMARY selection to CLIPBOARD so that
 * clipboard managers and Ctrl+V paste work automatically.
 */
static void
on_button_release(
	GstWindow   *win,
	guint        button,
	guint        state,
	gint         px,
	gint         py,
	gulong       time,
	gpointer     user_data
){
	(void)state;
	(void)px;
	(void)py;
	(void)time;
	(void)user_data;

	/* Only sync on left-button release (end of selection) */
	if (button != 1)
	{
		return;
	}

	gst_window_copy_to_clipboard(win);
}

/* ===== GstModule vfuncs ===== */

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 * This must match the config key under modules: { clipboard: ... }.
 */
static const gchar *
gst_clipboard_module_get_name(GstModule *module)
{
	(void)module;
	return "clipboard";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
gst_clipboard_module_get_description(GstModule *module)
{
	(void)module;
	return "Sync PRIMARY selection to CLIPBOARD on select";
}

/*
 * activate:
 *
 * Connects to the window's "button-release" signal. The signal
 * ordering guarantees this handler runs after main.c's selection
 * handler (modules activate after main.c connects signals).
 */
static gboolean
gst_clipboard_module_activate(GstModule *module)
{
	GstClipboardModule *self;
	GstModuleManager *mgr;
	GstWindow *win;

	self = GST_CLIPBOARD_MODULE(module);

	mgr = gst_module_manager_get_default();
	win = (GstWindow *)gst_module_manager_get_window(mgr);
	if (win == NULL)
	{
		g_warning("clipboard: no window available");
		return FALSE;
	}

	self->sig_id = g_signal_connect(win, "button-release",
		G_CALLBACK(on_button_release), self);

	g_debug("clipboard: activated");
	return TRUE;
}

/*
 * deactivate:
 *
 * Disconnects from the window's "button-release" signal.
 */
static void
gst_clipboard_module_deactivate(GstModule *module)
{
	GstClipboardModule *self;
	GstModuleManager *mgr;
	GstWindow *win;

	self = GST_CLIPBOARD_MODULE(module);

	if (self->sig_id != 0)
	{
		mgr = gst_module_manager_get_default();
		win = (GstWindow *)gst_module_manager_get_window(mgr);
		if (win != NULL)
		{
			g_signal_handler_disconnect(win, self->sig_id);
		}
		self->sig_id = 0;
	}

	g_debug("clipboard: deactivated");
}

/*
 * configure:
 *
 * Reads clipboard configuration from YAML config.
 * Currently no configurable options beyond enabled/disabled.
 */
static void
gst_clipboard_module_configure(GstModule *module, gpointer config)
{
	(void)module;
	(void)config;

	g_debug("clipboard: configured");
}

/* ===== GObject lifecycle ===== */

static void
gst_clipboard_module_class_init(GstClipboardModuleClass *klass)
{
	GstModuleClass *module_class;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_clipboard_module_get_name;
	module_class->get_description = gst_clipboard_module_get_description;
	module_class->activate = gst_clipboard_module_activate;
	module_class->deactivate = gst_clipboard_module_deactivate;
	module_class->configure = gst_clipboard_module_configure;
}

static void
gst_clipboard_module_init(GstClipboardModule *self)
{
	self->sig_id = 0;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 * Returns the GType so the manager can instantiate the module.
 *
 * Returns: The #GType for #GstClipboardModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_CLIPBOARD_MODULE;
}
