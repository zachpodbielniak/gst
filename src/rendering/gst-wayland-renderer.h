/*
 * gst-wayland-renderer.h - Wayland renderer implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Wayland-based terminal renderer using Cairo for drawing and
 * wl_shm for shared-memory double-buffered surface rendering.
 * Implements the GstRenderer abstract interface.
 */

#ifndef GST_WAYLAND_RENDERER_H
#define GST_WAYLAND_RENDERER_H

#include <glib-object.h>
#include <cairo.h>
#include <wayland-client.h>
#include "gst-renderer.h"
#include "gst-cairo-font-cache.h"
#include "../gst-enums.h"
#include "../gst-types.h"

G_BEGIN_DECLS

#define GST_TYPE_WAYLAND_RENDERER (gst_wayland_renderer_get_type())

G_DECLARE_FINAL_TYPE(GstWaylandRenderer, gst_wayland_renderer,
	GST, WAYLAND_RENDERER, GstRenderer)

/**
 * gst_wayland_renderer_new:
 * @terminal: the terminal to render
 * @display: Wayland display connection
 * @surface: Wayland surface to render to
 * @shm: Wayland shared memory interface
 * @font_cache: Cairo font cache
 * @borderpx: border padding in pixels
 *
 * Creates a new Wayland renderer with Cairo drawing and
 * wl_shm double-buffered rendering.
 *
 * Returns: (transfer full): A new #GstWaylandRenderer
 */
GstWaylandRenderer *
gst_wayland_renderer_new(
	GstTerminal         *terminal,
	struct wl_display   *display,
	struct wl_surface   *surface,
	struct wl_shm       *shm,
	GstCairoFontCache   *font_cache,
	gint                borderpx
);

/**
 * gst_wayland_renderer_load_colors:
 * @self: A #GstWaylandRenderer
 *
 * Loads the 262-entry color palette.
 *
 * Returns: TRUE on success
 */
gboolean
gst_wayland_renderer_load_colors(GstWaylandRenderer *self);

/**
 * gst_wayland_renderer_set_win_mode:
 * @self: A #GstWaylandRenderer
 * @mode: window mode flags
 *
 * Updates the window mode flags.
 */
void
gst_wayland_renderer_set_win_mode(
	GstWaylandRenderer  *self,
	GstWinMode          mode
);

/**
 * gst_wayland_renderer_get_win_mode:
 * @self: A #GstWaylandRenderer
 *
 * Gets the current window mode flags.
 *
 * Returns: the current GstWinMode flags
 */
GstWinMode
gst_wayland_renderer_get_win_mode(GstWaylandRenderer *self);

G_END_DECLS

#endif /* GST_WAYLAND_RENDERER_H */
