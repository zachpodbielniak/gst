/*
 * gst-syncupdate-module.c - Synchronized update (mode 2026) module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the synchronized update protocol (DEC private mode 2026).
 * When a program begins a synchronized update (CSI ? 2026 h), this
 * module suppresses rendering by marking lines clean. When the update
 * ends (CSI ? 2026 l) or a safety timeout expires, all lines are
 * marked dirty and a redraw is triggered via the "expose" signal on
 * the window.
 *
 * The module connects to the terminal's "mode-changed" signal to
 * detect transitions of the GST_MODE_SYNC_UPDATE flag. A configurable
 * timeout (default 150ms) prevents indefinite rendering stalls if the
 * end-sync sequence is never received.
 */

#include "gst-syncupdate-module.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/window/gst-window.h"
#include "../../src/module/gst-module-manager.h"

/**
 * SECTION:gst-syncupdate-module
 * @title: GstSyncupdateModule
 * @short_description: Synchronized update (mode 2026) module
 *
 * #GstSyncupdateModule eliminates flicker by suppressing rendering
 * while a program is performing a synchronized screen update. The
 * module listens for mode 2026 transitions on the terminal and
 * defers redraws until the update is complete or a safety timeout
 * fires.
 */

/* Default safety timeout in milliseconds */
#define GST_SYNCUPDATE_DEFAULT_TIMEOUT (150)

/* Minimum and maximum clamp values for the timeout */
#define GST_SYNCUPDATE_MIN_TIMEOUT (10)
#define GST_SYNCUPDATE_MAX_TIMEOUT (5000)

/* Private data for the sync update module */
struct _GstSyncupdateModule
{
	GstModule parent_instance;

	guint     timeout_ms;       /* safety timeout in milliseconds */
	guint     timeout_id;       /* GSource id for the safety timer */
	gboolean  sync_active;      /* TRUE while rendering is suppressed */
	gulong    mode_handler_id;  /* signal handler id for mode-changed */
};

G_DEFINE_TYPE(GstSyncupdateModule, gst_syncupdate_module, GST_TYPE_MODULE)

/* ===== Internal helpers ===== */

/*
 * force_full_redraw:
 * @self: the sync update module
 *
 * Marks all terminal lines dirty and emits "expose" on the window
 * to trigger a full redraw. Called when the sync update ends or
 * the safety timeout fires.
 */
static void
force_full_redraw(GstSyncupdateModule *self)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	GstWindow *win;
	gint rows;
	gint y;

	mgr = gst_module_manager_get_default();

	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term != NULL) {
		rows = gst_terminal_get_rows(term);
		for (y = 0; y < rows; y++) {
			gst_terminal_mark_dirty(term, y);
		}
	}

	win = (GstWindow *)gst_module_manager_get_window(mgr);
	if (win != NULL) {
		g_signal_emit_by_name(win, "expose");
	}
}

/*
 * cancel_timeout:
 * @self: the sync update module
 *
 * Cancels the safety timeout timer if it is running.
 */
static void
cancel_timeout(GstSyncupdateModule *self)
{
	if (self->timeout_id != 0) {
		g_source_remove(self->timeout_id);
		self->timeout_id = 0;
	}
}

/*
 * on_timeout:
 * @user_data: pointer to the GstSyncupdateModule
 *
 * Safety timeout callback. If the end-sync sequence was never
 * received, this fires to resume normal rendering. Clears the
 * sync_active flag and triggers a full redraw.
 *
 * Returns: %G_SOURCE_REMOVE (single-shot timer)
 */
static gboolean
on_timeout(gpointer user_data)
{
	GstSyncupdateModule *self;

	self = GST_SYNCUPDATE_MODULE(user_data);
	self->timeout_id = 0;
	self->sync_active = FALSE;

	g_debug("sync_update: safety timeout expired, forcing redraw");
	force_full_redraw(self);

	return G_SOURCE_REMOVE;
}

/*
 * on_mode_changed:
 * @term: the terminal that emitted the signal
 * @mode: the mode flag(s) that changed
 * @enabled: whether the mode was enabled or disabled
 * @user_data: pointer to the GstSyncupdateModule
 *
 * Signal handler for the terminal's "mode-changed" signal. Detects
 * transitions of GST_MODE_SYNC_UPDATE and starts/stops the sync
 * accordingly.
 */
static void
on_mode_changed(
	GstTerminal *term,
	GstTermMode  mode,
	gboolean     enabled,
	gpointer     user_data
){
	GstSyncupdateModule *self;

	self = GST_SYNCUPDATE_MODULE(user_data);

	/* Only care about sync update mode transitions */
	if (!(mode & GST_MODE_SYNC_UPDATE)) {
		return;
	}

	if (enabled) {
		/* Begin synchronized update: suppress rendering */
		if (!self->sync_active) {
			self->sync_active = TRUE;
			g_debug("sync_update: begin (timeout=%u ms)", self->timeout_ms);

			/* Start safety timeout */
			cancel_timeout(self);
			self->timeout_id = g_timeout_add(
				self->timeout_ms, on_timeout, self);
		}
	} else {
		/* End synchronized update: trigger full redraw */
		if (self->sync_active) {
			cancel_timeout(self);
			self->sync_active = FALSE;
			g_debug("sync_update: end, triggering redraw");
			force_full_redraw(self);
		}
	}
}

/* ===== GstModule vfuncs ===== */

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 */
static const gchar *
gst_syncupdate_module_get_name(GstModule *module)
{
	(void)module;
	return "sync_update";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
gst_syncupdate_module_get_description(GstModule *module)
{
	(void)module;
	return "Synchronized update (mode 2026) - eliminates flicker";
}

/*
 * activate:
 *
 * Activates the sync update module. Connects to the terminal's
 * "mode-changed" signal to detect mode 2026 transitions.
 *
 * Returns: %TRUE on success, %FALSE if the terminal is unavailable
 */
static gboolean
gst_syncupdate_module_activate(GstModule *module)
{
	GstSyncupdateModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;

	self = GST_SYNCUPDATE_MODULE(module);
	mgr = gst_module_manager_get_default();

	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		g_warning("sync_update: no terminal available, cannot activate");
		return FALSE;
	}

	/* Connect to mode-changed to detect sync update transitions */
	self->mode_handler_id = g_signal_connect(
		term, "mode-changed",
		G_CALLBACK(on_mode_changed), self);

	g_debug("sync_update: activated (timeout=%u ms)", self->timeout_ms);
	return TRUE;
}

/*
 * deactivate:
 *
 * Deactivates the sync update module. Disconnects the signal
 * handler and cancels any pending timeout.
 */
static void
gst_syncupdate_module_deactivate(GstModule *module)
{
	GstSyncupdateModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;

	self = GST_SYNCUPDATE_MODULE(module);

	/* Cancel any pending safety timeout */
	cancel_timeout(self);
	self->sync_active = FALSE;

	/* Disconnect signal handler */
	if (self->mode_handler_id != 0) {
		mgr = gst_module_manager_get_default();
		term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
		if (term != NULL) {
			g_signal_handler_disconnect(term, self->mode_handler_id);
		}
		self->mode_handler_id = 0;
	}

	g_debug("sync_update: deactivated");
}

/*
 * configure:
 *
 * Reads sync update configuration from the YAML config:
 *  - timeout: safety timeout in milliseconds (clamped to 10-5000)
 */
static void
gst_syncupdate_module_configure(GstModule *module, gpointer config)
{
	GstSyncupdateModule *self;
	YamlMapping *mod_cfg;

	self = GST_SYNCUPDATE_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "sync_update");
	if (mod_cfg == NULL) {
		g_debug("sync_update: no config section, using defaults");
		return;
	}

	if (yaml_mapping_has_member(mod_cfg, "timeout")) {
		gint64 val;

		val = yaml_mapping_get_int_member(mod_cfg, "timeout");
		if (val < GST_SYNCUPDATE_MIN_TIMEOUT) {
			val = GST_SYNCUPDATE_MIN_TIMEOUT;
		}
		if (val > GST_SYNCUPDATE_MAX_TIMEOUT) {
			val = GST_SYNCUPDATE_MAX_TIMEOUT;
		}
		self->timeout_ms = (guint)val;
	}

	g_debug("sync_update: configured (timeout=%u ms)", self->timeout_ms);
}

/* ===== GObject class/instance init ===== */

static void
gst_syncupdate_module_class_init(GstSyncupdateModuleClass *klass)
{
	GstModuleClass *module_class;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_syncupdate_module_get_name;
	module_class->get_description = gst_syncupdate_module_get_description;
	module_class->activate = gst_syncupdate_module_activate;
	module_class->deactivate = gst_syncupdate_module_deactivate;
	module_class->configure = gst_syncupdate_module_configure;
}

static void
gst_syncupdate_module_init(GstSyncupdateModule *self)
{
	self->timeout_ms = GST_SYNCUPDATE_DEFAULT_TIMEOUT;
	self->timeout_id = 0;
	self->sync_active = FALSE;
	self->mode_handler_id = 0;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 * Returns the GType so the manager can instantiate the module.
 *
 * Returns: The #GType for #GstSyncupdateModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_SYNCUPDATE_MODULE;
}
