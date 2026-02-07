/*
 * gst-x11-window.h - X11 window implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * X11-based terminal window with GMainLoop integration.
 * Uses g_io_add_watch() on the X11 connection fd to process
 * events inside GLib's main loop instead of st's pselect().
 */

#ifndef GST_X11_WINDOW_H
#define GST_X11_WINDOW_H

#include <glib-object.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include "gst-window.h"

G_BEGIN_DECLS

#define GST_TYPE_X11_WINDOW (gst_x11_window_get_type())

G_DECLARE_FINAL_TYPE(GstX11Window, gst_x11_window, GST, X11_WINDOW, GstWindow)

GType
gst_x11_window_get_type(void) G_GNUC_CONST;

/**
 * gst_x11_window_new:
 * @cols: initial terminal columns
 * @rows: initial terminal rows
 * @cw: character cell width in pixels
 * @ch: character cell height in pixels
 * @borderpx: border padding in pixels
 * @embed_id: parent window ID for embedding (0 for root)
 *
 * Creates a new X11 window sized to fit the given terminal
 * dimensions plus border padding. Ports st's xinit().
 *
 * Returns: (transfer full) (nullable): A new #GstX11Window, or NULL on failure
 */
GstX11Window *
gst_x11_window_new(
	gint    cols,
	gint    rows,
	gint    cw,
	gint    ch,
	gint    borderpx,
	gulong  embed_id
);

/**
 * gst_x11_window_get_display:
 * @self: A #GstX11Window
 *
 * Gets the X11 display connection.
 *
 * Returns: (transfer none): the Display pointer
 */
Display *
gst_x11_window_get_display(GstX11Window *self);

/**
 * gst_x11_window_get_xid:
 * @self: A #GstX11Window
 *
 * Gets the X11 window ID.
 *
 * Returns: the X11 window ID
 */
Window
gst_x11_window_get_xid(GstX11Window *self);

/**
 * gst_x11_window_get_visual:
 * @self: A #GstX11Window
 *
 * Gets the X11 visual.
 *
 * Returns: (transfer none): the Visual pointer
 */
Visual *
gst_x11_window_get_visual(GstX11Window *self);

/**
 * gst_x11_window_get_colormap:
 * @self: A #GstX11Window
 *
 * Gets the X11 colormap.
 *
 * Returns: the Colormap
 */
Colormap
gst_x11_window_get_colormap(GstX11Window *self);

/**
 * gst_x11_window_get_screen:
 * @self: A #GstX11Window
 *
 * Gets the X11 screen number.
 *
 * Returns: the screen number
 */
gint
gst_x11_window_get_screen(GstX11Window *self);

G_END_DECLS

#endif /* GST_X11_WINDOW_H */
