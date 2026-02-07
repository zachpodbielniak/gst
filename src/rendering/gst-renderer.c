/*
 * gst-renderer.c - Abstract base renderer class
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for terminal renderers. Holds a reference
 * to the terminal (set via construct property) and dispatches
 * virtual methods to subclass implementations.
 */

#include "gst-renderer.h"
#include "../core/gst-terminal.h"

/**
 * SECTION:gst-renderer
 * @title: GstRenderer
 * @short_description: Abstract base class for terminal renderers
 *
 * #GstRenderer is an abstract base class that defines the interface
 * for terminal rendering backends. Subclasses implement the actual
 * rendering logic for specific graphics systems (X11, Wayland, etc.).
 *
 * The renderer holds a reference to the terminal it renders, set
 * at construction via the "terminal" property.
 */

enum {
	PROP_0,
	PROP_TERMINAL,
	N_PROPS
};

static GParamSpec *props[N_PROPS] = { NULL };

/* Private structure */
typedef struct
{
	GstTerminal *terminal;
	guint width;
	guint height;
} GstRendererPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(GstRenderer, gst_renderer, G_TYPE_OBJECT)

static void
gst_renderer_set_property(
	GObject         *object,
	guint           property_id,
	const GValue    *value,
	GParamSpec      *pspec
){
	GstRendererPrivate *priv;

	priv = gst_renderer_get_instance_private(GST_RENDERER(object));

	switch (property_id) {
	case PROP_TERMINAL:
		g_set_object(&priv->terminal, g_value_get_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void
gst_renderer_get_property(
	GObject         *object,
	guint           property_id,
	GValue          *value,
	GParamSpec      *pspec
){
	GstRendererPrivate *priv;

	priv = gst_renderer_get_instance_private(GST_RENDERER(object));

	switch (property_id) {
	case PROP_TERMINAL:
		g_value_set_object(value, priv->terminal);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

static void
gst_renderer_dispose(GObject *object)
{
	GstRendererPrivate *priv;

	priv = gst_renderer_get_instance_private(GST_RENDERER(object));

	g_clear_object(&priv->terminal);

	G_OBJECT_CLASS(gst_renderer_parent_class)->dispose(object);
}

static void
gst_renderer_class_init(GstRendererClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->set_property = gst_renderer_set_property;
	object_class->get_property = gst_renderer_get_property;
	object_class->dispose = gst_renderer_dispose;

	/* Virtual methods default to NULL - must be implemented by subclasses */
	klass->render = NULL;
	klass->resize = NULL;
	klass->clear = NULL;
	klass->draw_line = NULL;
	klass->draw_cursor = NULL;
	klass->start_draw = NULL;
	klass->finish_draw = NULL;

	/**
	 * GstRenderer:terminal:
	 *
	 * The terminal this renderer draws. Set at construction.
	 */
	props[PROP_TERMINAL] = g_param_spec_object(
		"terminal",
		"Terminal",
		"The terminal to render",
		GST_TYPE_TERMINAL,
		G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS
	);

	g_object_class_install_properties(object_class, N_PROPS, props);
}

static void
gst_renderer_init(GstRenderer *self)
{
	GstRendererPrivate *priv;

	priv = gst_renderer_get_instance_private(self);
	priv->terminal = NULL;
	priv->width = 0;
	priv->height = 0;
}

/**
 * gst_renderer_render:
 * @self: A #GstRenderer
 *
 * Performs a render pass. Calls the virtual render method.
 */
void
gst_renderer_render(GstRenderer *self)
{
	GstRendererClass *klass;

	g_return_if_fail(GST_IS_RENDERER(self));

	klass = GST_RENDERER_GET_CLASS(self);
	if (klass->render != NULL) {
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
	if (klass->resize != NULL) {
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
	if (klass->clear != NULL) {
		klass->clear(self);
	}
}

/**
 * gst_renderer_draw_line:
 * @self: A #GstRenderer
 * @row: Row index to draw
 * @x1: Starting column
 * @x2: Ending column (exclusive)
 *
 * Draws a single line from column x1 to x2.
 */
void
gst_renderer_draw_line(
	GstRenderer *self,
	gint         row,
	gint         x1,
	gint         x2
){
	GstRendererClass *klass;

	g_return_if_fail(GST_IS_RENDERER(self));

	klass = GST_RENDERER_GET_CLASS(self);
	if (klass->draw_line != NULL) {
		klass->draw_line(self, row, x1, x2);
	}
}

/**
 * gst_renderer_draw_cursor:
 * @self: A #GstRenderer
 * @cx: Current cursor column
 * @cy: Current cursor row
 * @ox: Previous cursor column
 * @oy: Previous cursor row
 *
 * Draws the cursor at the new position, erasing the old one.
 */
void
gst_renderer_draw_cursor(
	GstRenderer *self,
	gint         cx,
	gint         cy,
	gint         ox,
	gint         oy
){
	GstRendererClass *klass;

	g_return_if_fail(GST_IS_RENDERER(self));

	klass = GST_RENDERER_GET_CLASS(self);
	if (klass->draw_cursor != NULL) {
		klass->draw_cursor(self, cx, cy, ox, oy);
	}
}

/**
 * gst_renderer_start_draw:
 * @self: A #GstRenderer
 *
 * Begins a drawing batch. Returns FALSE if drawing should be skipped
 * (e.g., Xft not ready).
 *
 * Returns: TRUE if drawing can proceed
 */
gboolean
gst_renderer_start_draw(GstRenderer *self)
{
	GstRendererClass *klass;

	g_return_val_if_fail(GST_IS_RENDERER(self), FALSE);

	klass = GST_RENDERER_GET_CLASS(self);
	if (klass->start_draw != NULL) {
		return klass->start_draw(self);
	}

	return TRUE;
}

/**
 * gst_renderer_finish_draw:
 * @self: A #GstRenderer
 *
 * Ends a drawing batch (flush, copy buffer to window, etc.).
 */
void
gst_renderer_finish_draw(GstRenderer *self)
{
	GstRendererClass *klass;

	g_return_if_fail(GST_IS_RENDERER(self));

	klass = GST_RENDERER_GET_CLASS(self);
	if (klass->finish_draw != NULL) {
		klass->finish_draw(self);
	}
}

/**
 * gst_renderer_get_terminal:
 * @self: A #GstRenderer
 *
 * Gets the terminal associated with this renderer.
 *
 * Returns: (transfer none) (nullable): the terminal
 */
GstTerminal *
gst_renderer_get_terminal(GstRenderer *self)
{
	GstRendererPrivate *priv;

	g_return_val_if_fail(GST_IS_RENDERER(self), NULL);

	priv = gst_renderer_get_instance_private(self);
	return priv->terminal;
}
