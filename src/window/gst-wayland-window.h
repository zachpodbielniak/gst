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

G_END_DECLS

#endif /* GST_WAYLAND_WINDOW_H */
