/*
 * gst-transparency-module.c - Window transparency module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Controls window opacity via _NET_WM_WINDOW_OPACITY X11 property.
 * Tracks focus state and adjusts opacity for focused/unfocused windows.
 * Implements GstRenderOverlay to hook into the render cycle for
 * focus-change detection.
 */

#include "gst-transparency-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/rendering/gst-render-context.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>

/**
 * SECTION:gst-transparency-module
 * @title: GstTransparencyModule
 * @short_description: Window opacity control with focus tracking
 *
 * #GstTransparencyModule sets window opacity using the
 * _NET_WM_WINDOW_OPACITY X11 property. Supports different
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
 * set_window_opacity:
 *
 * Sets the _NET_WM_WINDOW_OPACITY property on the given X11 window.
 * opacity is a value from 0.0 (fully transparent) to 1.0 (opaque).
 */
static void
set_window_opacity(Display *display, Window window, gdouble opacity)
{
	guint32 val;
	Atom atom;

	/* Clamp opacity */
	if (opacity < 0.0) {
		opacity = 0.0;
	}
	if (opacity > 1.0) {
		opacity = 1.0;
	}

	val = (guint32)(0xFFFFFFFFU * opacity);
	atom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);

	XChangeProperty(display, window, atom,
		XA_CARDINAL, 32, PropModeReplace,
		(guchar *)&val, 1);
	XFlush(display);
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
	GstX11RenderContext *ctx;
	gboolean focused;

	(void)width;
	(void)height;

	self = GST_TRANSPARENCY_MODULE(overlay);
	ctx = (GstX11RenderContext *)render_context;

	focused = (ctx->win_mode & GST_WIN_MODE_FOCUSED) != 0;

	/* Set initial opacity on first render */
	if (!self->initial_set) {
		self->initial_set = TRUE;
		self->was_focused = focused;
		set_window_opacity(ctx->display, ctx->window,
			focused ? self->focus_opacity : self->unfocus_opacity);
		return;
	}

	/* Only update when focus state changes */
	if (focused != self->was_focused) {
		gdouble target;

		self->was_focused = focused;
		target = focused ? self->focus_opacity : self->unfocus_opacity;
		set_window_opacity(ctx->display, ctx->window, target);
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

static void
gst_transparency_module_configure(GstModule *module, gpointer config)
{
	(void)config;
	g_debug("transparency: configured");
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
