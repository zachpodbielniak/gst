/*
 * gst-wayland-window.h - Wayland window implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Wayland-based terminal window using libdecor for universal window
 * decorations (CSD on GNOME, SSD on wlroots), wl_keyboard + xkbcommon
 * for input, and wl_data_device for clipboard. Integrates with GLib
 * main loop via g_io_add_watch() on the Wayland fd.
 */

#ifndef GST_WAYLAND_WINDOW_H
#define GST_WAYLAND_WINDOW_H

#include <glib-object.h>
#include <wayland-client.h>
#include "gst-window.h"

G_BEGIN_DECLS

#define GST_TYPE_WAYLAND_WINDOW (gst_wayland_window_get_type())

G_DECLARE_FINAL_TYPE(GstWaylandWindow, gst_wayland_window,
	GST, WAYLAND_WINDOW, GstWindow)

/**
 * gst_wayland_window_new:
 * @cols: initial terminal columns
 * @rows: initial terminal rows
 * @cw: character cell width in pixels
 * @ch: character cell height in pixels
 * @borderpx: border padding in pixels
 *
 * Creates a new Wayland window sized to fit the given terminal
 * dimensions plus border padding. Connects to the Wayland
 * compositor and creates a libdecor-managed decorated surface.
 *
 * Returns: (transfer full) (nullable): A new #GstWaylandWindow,
 *   or NULL on failure
 */
GstWaylandWindow *
gst_wayland_window_new(
	gint    cols,
	gint    rows,
	gint    cw,
	gint    ch,
	gint    borderpx
);

/**
 * gst_wayland_window_get_display:
 * @self: A #GstWaylandWindow
 *
 * Gets the Wayland display connection.
 *
 * Returns: (transfer none): the wl_display pointer
 */
struct wl_display *
gst_wayland_window_get_display(GstWaylandWindow *self);

/**
 * gst_wayland_window_get_surface:
 * @self: A #GstWaylandWindow
 *
 * Gets the Wayland surface.
 *
 * Returns: (transfer none): the wl_surface pointer
 */
struct wl_surface *
gst_wayland_window_get_surface(GstWaylandWindow *self);

/**
 * gst_wayland_window_get_shm:
 * @self: A #GstWaylandWindow
 *
 * Gets the Wayland shared memory interface.
 *
 * Returns: (transfer none): the wl_shm pointer
 */
struct wl_shm *
gst_wayland_window_get_shm(GstWaylandWindow *self);

/**
 * gst_wayland_window_get_opacity:
 * @self: A #GstWaylandWindow
 *
 * Gets the current rendering opacity. The Wayland renderer
 * uses this to paint backgrounds with alpha transparency,
 * since Wayland has no compositor-level opacity protocol.
 *
 * Returns: opacity between 0.0 (fully transparent) and 1.0 (opaque)
 */
gdouble
gst_wayland_window_get_opacity(GstWaylandWindow *self);

/**
 * gst_wayland_scaled_size:
 * @logical: positive logical extent
 * @scale: scale in 120ths
 *
 * Returns: rounded-up buffer extent, or zero on invalid input/overflow
 */
gint
gst_wayland_scaled_size(gint logical, guint scale);

/**
 * gst_wayland_window_get_scale:
 * @self: a window
 *
 * Returns: effective buffer scale in 120ths
 */
guint
gst_wayland_window_get_scale(GstWaylandWindow *self);

/**
 * gst_wayland_window_get_logical_size:
 * @self: a window
 * @width: (out) (optional): configured logical width
 * @height: (out) (optional): configured logical height
 *
 * Retrieves libdecor content geometry, not backing-buffer pixels.
 */
void
gst_wayland_window_get_logical_size(GstWaylandWindow *self, gint *width, gint *height);

/**
 * gst_wayland_window_prepare_surface:
 * @self: a window
 * @width: logical width
 * @height: logical height
 *
 * Sets pending scale/viewport state immediately before attaching a buffer.
 */
void
gst_wayland_window_prepare_surface(GstWaylandWindow *self, gint width, gint height);

/**
 * gst_wayland_window_set_text_input_enabled:
 * @self: a window
 * @enabled: whether terminal text input is allowed
 *
 * Opt in only after connecting text-commit and preedit-changed handlers.
 * Focus enter/leave then enables/disables text-input-v3 automatically.
 * No editable surrounding text is advertised: a PTY cannot implement deletion.
 */
void
gst_wayland_window_set_text_input_enabled(GstWaylandWindow *self, gboolean enabled);

/**
 * gst_wayland_window_set_text_cursor:
 * @self: a window
 * @x: logical cursor x
 * @y: logical cursor y
 * @width: logical cursor width
 * @height: logical cursor height
 *
 * Updates the IME candidate rectangle in surface coordinates.
 */
void
gst_wayland_window_set_text_cursor(GstWaylandWindow *self,
	gint x, gint y, gint width, gint height);

/**
 * gst_wayland_window_get_preedit:
 * @self: a window
 * @begin: (out) (optional): cursor start byte offset, or -1
 * @end: (out) (optional): cursor end byte offset, or -1
 *
 * Returns: (transfer none): current UTF-8 preedit, never NULL
 */
const gchar *
gst_wayland_window_get_preedit(GstWaylandWindow *self, gint *begin, gint *end);

G_END_DECLS

#endif /* GST_WAYLAND_WINDOW_H */
