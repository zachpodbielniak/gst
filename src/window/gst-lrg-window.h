/*
 * gst-lrg-window.h - libregnum (LRG) window
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GstWindow implementation backed by a graylib (raylib) #GrlWindow. The
 * window owns a ~60fps frame-pump GSource on the GLib main loop: each tick
 * polls raylib input (forwarding it as GstWindow signals) and drives the
 * renderer (start_draw -> render -> finish_draw). raylib has no fd-based
 * event source, so this timeout replaces the X11/Wayland g_io_add_watch.
 */

#ifndef GST_LRG_WINDOW_H
#define GST_LRG_WINDOW_H

#include <glib-object.h>
#include <graylib.h>
#include "gst-window.h"
#include "../gst-enums.h"

G_BEGIN_DECLS

/* Forward declaration (full type from gst-renderer.h, included in the .c) */
typedef struct _GstRenderer GstRenderer;

#define GST_TYPE_LRG_WINDOW (gst_lrg_window_get_type())

G_DECLARE_FINAL_TYPE(GstLrgWindow, gst_lrg_window, GST, LRG_WINDOW, GstWindow)

/**
 * gst_lrg_window_new:
 * @cols: terminal columns
 * @rows: terminal rows
 * @cw: provisional character cell width in pixels
 * @ch: provisional character cell height in pixels
 * @borderpx: border padding in pixels
 *
 * Creates and opens a new libregnum/raylib window sized to the grid. The
 * cell size is provisional (raylib fonts need the GL context this window
 * provides); the caller resizes precisely once fonts are loaded.
 *
 * Returns: (transfer full) (nullable): A new #GstLrgWindow, or %NULL if
 *   the raylib window could not be created
 */
GstLrgWindow *
gst_lrg_window_new(
	gint    cols,
	gint    rows,
	gint    cw,
	gint    ch,
	gint    borderpx
);

/**
 * gst_lrg_window_get_grl_window:
 * @self: A #GstLrgWindow
 *
 * Returns: (transfer none) (nullable): the underlying #GrlWindow
 */
GrlWindow *
gst_lrg_window_get_grl_window(GstLrgWindow *self);

/**
 * gst_lrg_window_get_opacity:
 * @self: A #GstLrgWindow
 *
 * Returns: the current window opacity (0.0-1.0)
 */
gdouble
gst_lrg_window_get_opacity(GstLrgWindow *self);

/**
 * gst_lrg_window_set_renderer:
 * @self: A #GstLrgWindow
 * @renderer: (transfer none): the #GstRenderer the frame pump drives
 *
 * Sets the renderer driven each frame by the window's render-loop tick.
 */
void
gst_lrg_window_set_renderer(
	GstLrgWindow    *self,
	GstRenderer     *renderer
);

/**
 * gst_lrg_window_set_render_mode:
 * @self: A #GstLrgWindow
 * @mode: a #GstLrgRenderMode (only 2D is implemented)
 *
 * Selects the render mode. Only %GST_LRG_RENDER_MODE_2D is implemented;
 * other modes warn and fall back to 2D.
 */
void
gst_lrg_window_set_render_mode(
	GstLrgWindow        *self,
	GstLrgRenderMode    mode
);

/**
 * gst_lrg_window_get_render_mode:
 * @self: A #GstLrgWindow
 *
 * Returns: the current #GstLrgRenderMode
 */
GstLrgRenderMode
gst_lrg_window_get_render_mode(GstLrgWindow *self);

G_END_DECLS

#endif /* GST_LRG_WINDOW_H */
