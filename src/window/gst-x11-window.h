/*
 * gst-x11-window.h - X11 window implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_X11_WINDOW_H
#define GST_X11_WINDOW_H

#include <glib-object.h>
#include "gst-window.h"

G_BEGIN_DECLS

#define GST_TYPE_X11_WINDOW (gst_x11_window_get_type())

G_DECLARE_FINAL_TYPE(GstX11Window, gst_x11_window, GST, X11_WINDOW, GstWindow)

GType
gst_x11_window_get_type(void) G_GNUC_CONST;

/**
 * gst_x11_window_new:
 *
 * Creates a new X11 window instance.
 *
 * Returns: (transfer full): A new #GstX11Window
 */
GstX11Window *
gst_x11_window_new(void);

/**
 * gst_x11_window_get_xid:
 * @self: A #GstX11Window
 *
 * Gets the X11 window ID.
 *
 * Returns: The X11 window ID
 */
gulong
gst_x11_window_get_xid(GstX11Window *self);

G_END_DECLS

#endif /* GST_X11_WINDOW_H */
