/*
 * gst-window.c - Abstract base window class
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-window.h"

/**
 * SECTION:gst-window
 * @title: GstWindow
 * @short_description: Abstract base class for terminal windows
 *
 * #GstWindow is an abstract base class that defines the interface
 * for terminal windows. Subclasses implement the actual window
 * management for specific display systems (X11, Wayland, etc.).
 */

/* Private structure */
typedef struct
{
	gchar *title;
	guint  width;
	guint  height;
	gboolean visible;
} GstWindowPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(GstWindow, gst_window, G_TYPE_OBJECT)

static void
gst_window_dispose(GObject *object)
{
	GstWindow *self;
	GstWindowPrivate *priv;

	self = GST_WINDOW(object);
	priv = gst_window_get_instance_private(self);

	g_clear_pointer(&priv->title, g_free);

	G_OBJECT_CLASS(gst_window_parent_class)->dispose(object);
}

static void
gst_window_class_init(GstWindowClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_window_dispose;

	/* Virtual methods default to NULL - must be implemented by subclasses */
	klass->show = NULL;
	klass->hide = NULL;
	klass->resize = NULL;
	klass->set_title = NULL;
}

static void
gst_window_init(GstWindow *self)
{
	GstWindowPrivate *priv;

	priv = gst_window_get_instance_private(self);
	priv->title = g_strdup("GST Terminal");
	priv->width = 800;
	priv->height = 600;
	priv->visible = FALSE;
}

/**
 * gst_window_show:
 * @self: A #GstWindow
 *
 * Shows the window on screen.
 */
void
gst_window_show(GstWindow *self)
{
	GstWindowClass *klass;

	g_return_if_fail(GST_IS_WINDOW(self));

	klass = GST_WINDOW_GET_CLASS(self);
	if (klass->show != NULL)
	{
		klass->show(self);
	}
}

/**
 * gst_window_hide:
 * @self: A #GstWindow
 *
 * Hides the window.
 */
void
gst_window_hide(GstWindow *self)
{
	GstWindowClass *klass;

	g_return_if_fail(GST_IS_WINDOW(self));

	klass = GST_WINDOW_GET_CLASS(self);
	if (klass->hide != NULL)
	{
		klass->hide(self);
	}
}

/**
 * gst_window_resize:
 * @self: A #GstWindow
 * @width: New width in pixels
 * @height: New height in pixels
 *
 * Resizes the window to the specified dimensions.
 */
void
gst_window_resize(
	GstWindow *self,
	guint      width,
	guint      height
){
	GstWindowClass *klass;

	g_return_if_fail(GST_IS_WINDOW(self));

	klass = GST_WINDOW_GET_CLASS(self);
	if (klass->resize != NULL)
	{
		klass->resize(self, width, height);
	}
}

/**
 * gst_window_set_title:
 * @self: A #GstWindow
 * @title: The new window title
 *
 * Sets the window title.
 */
void
gst_window_set_title(
	GstWindow   *self,
	const gchar *title
){
	GstWindowClass *klass;

	g_return_if_fail(GST_IS_WINDOW(self));

	klass = GST_WINDOW_GET_CLASS(self);
	if (klass->set_title != NULL)
	{
		klass->set_title(self, title);
	}
}
