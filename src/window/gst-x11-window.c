/*
 * gst-x11-window.c - X11 window implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-x11-window.h"

/**
 * SECTION:gst-x11-window
 * @title: GstX11Window
 * @short_description: X11-based terminal window
 *
 * #GstX11Window implements the #GstWindow interface for X11
 * display systems.
 */

struct _GstX11Window
{
	GstWindow parent_instance;

	/* TODO: Add X11-specific fields */
	/* Display *display; */
	/* Window   xwindow; */
	gulong xid;
	guint  width;
	guint  height;
	gchar *title;
	gboolean visible;
};

G_DEFINE_TYPE(GstX11Window, gst_x11_window, GST_TYPE_WINDOW)

static void
gst_x11_window_show_impl(GstWindow *window)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(window);
	self->visible = TRUE;

	/* TODO: XMapWindow(self->display, self->xwindow); */
}

static void
gst_x11_window_hide_impl(GstWindow *window)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(window);
	self->visible = FALSE;

	/* TODO: XUnmapWindow(self->display, self->xwindow); */
}

static void
gst_x11_window_resize_impl(
	GstWindow *window,
	guint      width,
	guint      height
){
	GstX11Window *self;

	self = GST_X11_WINDOW(window);
	self->width = width;
	self->height = height;

	/* TODO: XResizeWindow(self->display, self->xwindow, width, height); */
}

static void
gst_x11_window_set_title_impl(
	GstWindow   *window,
	const gchar *title
){
	GstX11Window *self;

	self = GST_X11_WINDOW(window);

	g_free(self->title);
	self->title = g_strdup(title);

	/* TODO: XStoreName(self->display, self->xwindow, title); */
}

static void
gst_x11_window_dispose(GObject *object)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(object);

	g_clear_pointer(&self->title, g_free);

	/* TODO: Destroy X11 window */

	G_OBJECT_CLASS(gst_x11_window_parent_class)->dispose(object);
}

static void
gst_x11_window_finalize(GObject *object)
{
	/* TODO: Close X11 display connection */

	G_OBJECT_CLASS(gst_x11_window_parent_class)->finalize(object);
}

static void
gst_x11_window_class_init(GstX11WindowClass *klass)
{
	GObjectClass *object_class;
	GstWindowClass *window_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_x11_window_dispose;
	object_class->finalize = gst_x11_window_finalize;

	window_class = GST_WINDOW_CLASS(klass);
	window_class->show = gst_x11_window_show_impl;
	window_class->hide = gst_x11_window_hide_impl;
	window_class->resize = gst_x11_window_resize_impl;
	window_class->set_title = gst_x11_window_set_title_impl;
}

static void
gst_x11_window_init(GstX11Window *self)
{
	self->xid = 0;
	self->width = 800;
	self->height = 600;
	self->title = g_strdup("GST Terminal");
	self->visible = FALSE;

	/* TODO: Open display and create window */
}

/**
 * gst_x11_window_new:
 *
 * Creates a new X11 window instance.
 *
 * Returns: (transfer full): A new #GstX11Window
 */
GstX11Window *
gst_x11_window_new(void)
{
	return (GstX11Window *)g_object_new(GST_TYPE_X11_WINDOW, NULL);
}

/**
 * gst_x11_window_get_xid:
 * @self: A #GstX11Window
 *
 * Gets the X11 window ID.
 *
 * Returns: The X11 window ID
 */
gulong
gst_x11_window_get_xid(GstX11Window *self)
{
	g_return_val_if_fail(GST_IS_X11_WINDOW(self), 0);

	return self->xid;
}
