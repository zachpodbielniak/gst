/*
 * gst-window.h - Abstract base window class
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_WINDOW_H
#define GST_WINDOW_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_WINDOW (gst_window_get_type())

G_DECLARE_DERIVABLE_TYPE(GstWindow, gst_window, GST, WINDOW, GObject)

/**
 * GstWindowClass:
 * @parent_class: The parent class
 * @show: Virtual method to show the window
 * @hide: Virtual method to hide the window
 * @resize: Virtual method to resize the window
 * @set_title: Virtual method to set the window title
 *
 * The class structure for #GstWindow.
 */
struct _GstWindowClass
{
	GObjectClass parent_class;

	/* Virtual methods */
	void (*show)      (GstWindow   *self);
	void (*hide)      (GstWindow   *self);
	void (*resize)    (GstWindow   *self,
	                   guint        width,
	                   guint        height);
	void (*set_title) (GstWindow   *self,
	                   const gchar *title);

	/* Padding for future expansion */
	gpointer padding[8];
};

GType
gst_window_get_type(void) G_GNUC_CONST;

void
gst_window_show(GstWindow *self);

void
gst_window_hide(GstWindow *self);

void
gst_window_resize(
	GstWindow *self,
	guint      width,
	guint      height
);

void
gst_window_set_title(
	GstWindow   *self,
	const gchar *title
);

G_END_DECLS

#endif /* GST_WINDOW_H */
