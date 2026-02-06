/*
 * gst-renderer.c - Abstract base renderer class
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-renderer.h"

/**
 * SECTION:gst-renderer
 * @title: GstRenderer
 * @short_description: Abstract base class for terminal renderers
 *
 * #GstRenderer is an abstract base class that defines the interface
 * for terminal rendering backends. Subclasses implement the actual
 * rendering logic for specific graphics systems (X11, Wayland, etc.).
 */

/* Private structure */
typedef struct
{
	guint width;
	guint height;
} GstRendererPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(GstRenderer, gst_renderer, G_TYPE_OBJECT)

static void
gst_renderer_class_init(GstRendererClass *klass)
{
	/* Virtual methods default to NULL - must be implemented by subclasses */
	klass->render = NULL;
	klass->resize = NULL;
	klass->clear = NULL;
}

static void
gst_renderer_init(GstRenderer *self)
{
	GstRendererPrivate *priv;

	priv = gst_renderer_get_instance_private(self);
	priv->width = 0;
	priv->height = 0;
}

/**
 * gst_renderer_render:
 * @self: A #GstRenderer
 *
 * Performs a render pass. This calls the virtual render method
 * which must be implemented by subclasses.
 */
void
gst_renderer_render(GstRenderer *self)
{
	GstRendererClass *klass;

	g_return_if_fail(GST_IS_RENDERER(self));

	klass = GST_RENDERER_GET_CLASS(self);
	if (klass->render != NULL)
	{
		klass->render(self);
	}
}

/**
 * gst_renderer_resize:
 * @self: A #GstRenderer
 * @width: New width in pixels
 * @height: New height in pixels
 *
 * Notifies the renderer of a size change.
 */
void
gst_renderer_resize(
	GstRenderer *self,
	guint        width,
	guint        height
){
	GstRendererClass *klass;

	g_return_if_fail(GST_IS_RENDERER(self));

	klass = GST_RENDERER_GET_CLASS(self);
	if (klass->resize != NULL)
	{
		klass->resize(self, width, height);
	}
}

/**
 * gst_renderer_clear:
 * @self: A #GstRenderer
 *
 * Clears the render surface.
 */
void
gst_renderer_clear(GstRenderer *self)
{
	GstRendererClass *klass;

	g_return_if_fail(GST_IS_RENDERER(self));

	klass = GST_RENDERER_GET_CLASS(self);
	if (klass->clear != NULL)
	{
		klass->clear(self);
	}
}
