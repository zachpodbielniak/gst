/*
 * gst-render-overlay.c
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for rendering overlays on the terminal.
 */

#include "gst-render-overlay.h"

G_DEFINE_INTERFACE(GstRenderOverlay, gst_render_overlay, G_TYPE_OBJECT)

static void
gst_render_overlay_default_init(GstRenderOverlayInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

/**
 * gst_render_overlay_render:
 * @self: A #GstRenderOverlay instance.
 * @render_context: Opaque rendering context.
 * @width: The width of the render area.
 * @height: The height of the render area.
 *
 * Renders an overlay on the terminal surface.
 */
void
gst_render_overlay_render(GstRenderOverlay *self,
                          gpointer          render_context,
                          gint              width,
                          gint              height)
{
	GstRenderOverlayInterface *iface;

	g_return_if_fail(GST_IS_RENDER_OVERLAY(self));
	g_return_if_fail(render_context != NULL);

	iface = GST_RENDER_OVERLAY_GET_IFACE(self);
	g_return_if_fail(iface->render != NULL);

	iface->render(self, render_context, width, height);
}
