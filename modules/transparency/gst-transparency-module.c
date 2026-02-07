/*
 * gst-transparency-module.c - Window transparency module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Controls window opacity via the abstract GstWindow set_opacity vfunc.
 * Tracks focus state and adjusts opacity for focused/unfocused windows.
 * Implements GstRenderOverlay to hook into the render cycle for
 * focus-change detection.
 */

#include "gst-transparency-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/rendering/gst-render-context.h"
#include "../../src/window/gst-window.h"

/**
 * SECTION:gst-transparency-module
 * @title: GstTransparencyModule
 * @short_description: Window opacity control with focus tracking
 *
 * #GstTransparencyModule sets window opacity using the abstract
 * GstWindow set_opacity virtual method. Supports different
 * opacity values for focused and unfocused states.
 */

struct _GstTransparencyModule
{
	GstModule parent_instance;

	gdouble   opacity;           /* static opacity (default 0.95) */
	gdouble   focus_opacity;     /* opacity when focused (default 1.0) */
	gdouble   unfocus_opacity;   /* opacity when unfocused (default 0.85) */
	gboolean  was_focused;       /* cached focus state */
	gboolean  initial_set;       /* whether initial opacity has been set */
};

/* Forward declaration */
static void
gst_transparency_module_overlay_init(GstRenderOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstTransparencyModule, gst_transparency_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_transparency_module_overlay_init))

/* ===== Internal helpers ===== */

/*
 * apply_opacity:
 *
 * Sets window opacity via the abstract GstWindow vfunc.
 * Gets the window from the module manager.
 */
static void
apply_opacity(gdouble opacity)
{
	GstModuleManager *mgr;
	GstWindow *win;

	mgr = gst_module_manager_get_default();
	win = (GstWindow *)gst_module_manager_get_window(mgr);
	if (win != NULL) {
		gst_window_set_opacity(win, opacity);
	}
}

/* ===== GstRenderOverlay interface ===== */

/*
 * render:
 *
 * Called each render cycle. Checks if focus state has changed
 * and updates window opacity accordingly.
 */
static void
gst_transparency_module_render(
	GstRenderOverlay *overlay,
	gpointer          render_context,
	gint              width,
	gint              height
){
	GstTransparencyModule *self;
	GstRenderContext *ctx;
	gboolean focused;

	(void)width;
	(void)height;

	self = GST_TRANSPARENCY_MODULE(overlay);
	ctx = (GstRenderContext *)render_context;

	focused = (ctx->win_mode & GST_WIN_MODE_FOCUSED) != 0;

	/* Set initial opacity on first render */
	if (!self->initial_set) {
		self->initial_set = TRUE;
		self->was_focused = focused;
		apply_opacity(focused ? self->focus_opacity
			: self->unfocus_opacity);
		return;
	}

	/* Only update when focus state changes */
	if (focused != self->was_focused) {
		gdouble target;

		self->was_focused = focused;
		target = focused ? self->focus_opacity : self->unfocus_opacity;
		apply_opacity(target);
	}
}

static void
gst_transparency_module_overlay_init(GstRenderOverlayInterface *iface)
{
	iface->render = gst_transparency_module_render;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_transparency_module_get_name(GstModule *module)
{
	(void)module;
	return "transparency";
}

static const gchar *
gst_transparency_module_get_description(GstModule *module)
{
	(void)module;
	return "Window opacity with focus tracking";
}

static gboolean
gst_transparency_module_activate(GstModule *module)
{
	g_debug("transparency: activated");
	return TRUE;
}

static void
gst_transparency_module_deactivate(GstModule *module)
{
	g_debug("transparency: deactivated");
}

/*
 * configure:
 *
 * Reads transparency configuration from the YAML config:
 *  - opacity: static opacity value (clamped to 0.0-1.0)
 *  - focus_opacity: opacity when window is focused (clamped to 0.0-1.0)
 *  - unfocus_opacity: opacity when window loses focus (clamped to 0.0-1.0)
 */
static void
gst_transparency_module_configure(GstModule *module, gpointer config)
{
	GstTransparencyModule *self;
	YamlMapping *mod_cfg;

	self = GST_TRANSPARENCY_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "transparency");
	if (mod_cfg == NULL)
	{
		g_debug("transparency: no config section, using defaults");
		return;
	}

	if (yaml_mapping_has_member(mod_cfg, "opacity"))
	{
		gdouble val;

		val = yaml_mapping_get_double_member(mod_cfg, "opacity");
		if (val < 0.0) val = 0.0;
		if (val > 1.0) val = 1.0;
		self->opacity = val;
	}

	if (yaml_mapping_has_member(mod_cfg, "focus_opacity"))
	{
		gdouble val;

		val = yaml_mapping_get_double_member(mod_cfg, "focus_opacity");
		if (val < 0.0) val = 0.0;
		if (val > 1.0) val = 1.0;
		self->focus_opacity = val;
	}

	if (yaml_mapping_has_member(mod_cfg, "unfocus_opacity"))
	{
		gdouble val;

		val = yaml_mapping_get_double_member(mod_cfg, "unfocus_opacity");
		if (val < 0.0) val = 0.0;
		if (val > 1.0) val = 1.0;
		self->unfocus_opacity = val;
	}

	g_debug("transparency: configured (opacity=%.2f, focus=%.2f, unfocus=%.2f)",
		self->opacity, self->focus_opacity, self->unfocus_opacity);
}

/* ===== GObject lifecycle ===== */

static void
gst_transparency_module_class_init(GstTransparencyModuleClass *klass)
{
	GstModuleClass *module_class;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_transparency_module_get_name;
	module_class->get_description = gst_transparency_module_get_description;
	module_class->activate = gst_transparency_module_activate;
	module_class->deactivate = gst_transparency_module_deactivate;
	module_class->configure = gst_transparency_module_configure;
}

static void
gst_transparency_module_init(GstTransparencyModule *self)
{
	self->opacity = 0.95;
	self->focus_opacity = 1.0;
	self->unfocus_opacity = 0.85;
	self->was_focused = TRUE;
	self->initial_set = FALSE;
}

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_TRANSPARENCY_MODULE;
}
