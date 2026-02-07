/*
 * gst-render-overlay.h
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for rendering overlays on the terminal.
 */

#ifndef GST_RENDER_OVERLAY_H
#define GST_RENDER_OVERLAY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_RENDER_OVERLAY (gst_render_overlay_get_type())

G_DECLARE_INTERFACE(GstRenderOverlay, gst_render_overlay, GST, RENDER_OVERLAY, GObject)

/**
 * GstRenderOverlayInterface:
 * @parent_iface: The parent interface.
 * @render: Virtual method to render an overlay.
 *
 * Interface for rendering overlays on the terminal surface.
 */
struct _GstRenderOverlayInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	void (*render) (GstRenderOverlay *self,
	                gpointer          render_context,
	                gint              width,
	                gint              height);
};

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
                          gint              height);

G_END_DECLS

#endif /* GST_RENDER_OVERLAY_H */
