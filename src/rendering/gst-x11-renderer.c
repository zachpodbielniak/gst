/*
 * gst-x11-renderer.c - X11 renderer implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-x11-renderer.h"

/**
 * SECTION:gst-x11-renderer
 * @title: GstX11Renderer
 * @short_description: X11-based terminal renderer
 *
 * #GstX11Renderer implements the #GstRenderer interface for X11
 * display systems, using Xlib and XRender for drawing operations.
 */

struct _GstX11Renderer
{
	GstRenderer parent_instance;

	/* TODO: Add X11-specific fields */
	/* Display *display; */
	/* Window   window; */
	/* GC       gc; */
	guint width;
	guint height;
};

G_DEFINE_TYPE(GstX11Renderer, gst_x11_renderer, GST_TYPE_RENDERER)

static void
gst_x11_renderer_render_impl(GstRenderer *renderer)
{
	GstX11Renderer *self;

	self = GST_X11_RENDERER(renderer);

	/* TODO: Implement X11 rendering */
	(void)self;
}

static void
gst_x11_renderer_resize_impl(
	GstRenderer *renderer,
	guint        width,
	guint        height
){
	GstX11Renderer *self;

	self = GST_X11_RENDERER(renderer);

	self->width = width;
	self->height = height;

	/* TODO: Handle X11 resize */
}

static void
gst_x11_renderer_clear_impl(GstRenderer *renderer)
{
	GstX11Renderer *self;

	self = GST_X11_RENDERER(renderer);

	/* TODO: Clear X11 surface */
	(void)self;
}

static void
gst_x11_renderer_dispose(GObject *object)
{
	/* TODO: Clean up X11 resources */

	G_OBJECT_CLASS(gst_x11_renderer_parent_class)->dispose(object);
}

static void
gst_x11_renderer_finalize(GObject *object)
{
	/* TODO: Final cleanup */

	G_OBJECT_CLASS(gst_x11_renderer_parent_class)->finalize(object);
}

static void
gst_x11_renderer_class_init(GstX11RendererClass *klass)
{
	GObjectClass *object_class;
	GstRendererClass *renderer_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_x11_renderer_dispose;
	object_class->finalize = gst_x11_renderer_finalize;

	renderer_class = GST_RENDERER_CLASS(klass);
	renderer_class->render = gst_x11_renderer_render_impl;
	renderer_class->resize = gst_x11_renderer_resize_impl;
	renderer_class->clear = gst_x11_renderer_clear_impl;
}

static void
gst_x11_renderer_init(GstX11Renderer *self)
{
	self->width = 0;
	self->height = 0;

	/* TODO: Initialize X11 connection */
}

/**
 * gst_x11_renderer_new:
 *
 * Creates a new X11 renderer instance.
 *
 * Returns: (transfer full): A new #GstX11Renderer
 */
GstX11Renderer *
gst_x11_renderer_new(void)
{
	return (GstX11Renderer *)g_object_new(GST_TYPE_X11_RENDERER, NULL);
}
