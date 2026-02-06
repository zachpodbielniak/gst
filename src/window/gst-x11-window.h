/*
 * gst-x11-window.h - X11 window implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
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

/**
 * gst_x11_window_set_wm_hints:
 * @self: A #GstX11Window
 * @cw: character cell width
 * @ch: character cell height
 * @borderpx: border padding
 *
 * Sets window manager hints for size increments
 * so the WM snaps to character boundaries.
 */
void
gst_x11_window_set_wm_hints(
	GstX11Window    *self,
	gint            cw,
	gint            ch,
	gint            borderpx
);

/**
 * gst_x11_window_set_title_x11:
 * @self: A #GstX11Window
 * @title: the title string
 *
 * Sets the X11 window title using _NET_WM_NAME.
 */
void
gst_x11_window_set_title_x11(
	GstX11Window    *self,
	const gchar     *title
);

/**
 * gst_x11_window_bell:
 * @self: A #GstX11Window
 *
 * Triggers an X11 bell (urgency hint toggle).
 */
void
gst_x11_window_bell(GstX11Window *self);

/**
 * gst_x11_window_set_pointer_motion:
 * @self: A #GstX11Window
 * @enable: TRUE to enable pointer motion events
 *
 * Enables or disables pointer motion event reporting.
 */
void
gst_x11_window_set_pointer_motion(
	GstX11Window    *self,
	gboolean        enable
);

/**
 * gst_x11_window_set_selection:
 * @self: A #GstX11Window
 * @text: selection text
 * @is_clipboard: TRUE for CLIPBOARD, FALSE for PRIMARY
 *
 * Sets the X11 selection (PRIMARY or CLIPBOARD).
 */
void
gst_x11_window_set_selection(
	GstX11Window    *self,
	const gchar     *text,
	gboolean        is_clipboard
);

/**
 * gst_x11_window_paste_primary:
 * @self: A #GstX11Window
 *
 * Requests the PRIMARY selection contents.
 */
void
gst_x11_window_paste_primary(GstX11Window *self);

/**
 * gst_x11_window_paste_clipboard:
 * @self: A #GstX11Window
 *
 * Requests the CLIPBOARD contents.
 */
void
gst_x11_window_paste_clipboard(GstX11Window *self);

/**
 * gst_x11_window_copy_to_clipboard:
 * @self: A #GstX11Window
 *
 * Copies the primary selection to clipboard.
 */
void
gst_x11_window_copy_to_clipboard(GstX11Window *self);

/**
 * gst_x11_window_start_event_watch:
 * @self: A #GstX11Window
 *
 * Starts watching for X11 events via GLib main loop.
 * Must be called after the window is shown and the
 * main loop is about to run.
 */
void
gst_x11_window_start_event_watch(GstX11Window *self);

G_END_DECLS

#endif /* GST_X11_WINDOW_H */
