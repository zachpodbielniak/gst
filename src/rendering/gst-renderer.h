/*
 * gst-renderer.h - Abstract base renderer class
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_RENDERER_H
#define GST_RENDERER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_RENDERER (gst_renderer_get_type())

G_DECLARE_DERIVABLE_TYPE(GstRenderer, gst_renderer, GST, RENDERER, GObject)

/**
 * GstRendererClass:
 * @parent_class: The parent class
 * @render: Virtual method to perform rendering
 * @resize: Virtual method to handle resize events
 * @clear: Virtual method to clear the render surface
 *
 * The class structure for #GstRenderer.
 */
struct _GstRendererClass
{
	GObjectClass parent_class;

	/* Virtual methods */
	void (*render) (GstRenderer *self);
	void (*resize) (GstRenderer *self,
	                guint        width,
	                guint        height);
	void (*clear)  (GstRenderer *self);

	/* Padding for future expansion */
	gpointer padding[8];
};

GType
gst_renderer_get_type(void) G_GNUC_CONST;

void
gst_renderer_render(GstRenderer *self);

void
gst_renderer_resize(
	GstRenderer *self,
	guint        width,
	guint        height
);

void
gst_renderer_clear(GstRenderer *self);

G_END_DECLS

#endif /* GST_RENDERER_H */
