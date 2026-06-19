/*
 * gst-lrg-renderer.h - libregnum (LRG) renderer implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * libregnum/graylib-based terminal renderer. Draws the terminal grid in
 * immediate mode into a raylib window each frame. Implements the
 * GstRenderer abstract interface. Because raylib clears the framebuffer
 * every frame, the renderer redraws the whole grid on each pass rather
 * than using dirty-line tracking.
 */

#ifndef GST_LRG_RENDERER_H
#define GST_LRG_RENDERER_H

#include <glib-object.h>
#include <graylib.h>
#include "gst-renderer.h"
#include "gst-grl-font-cache.h"
#include "../window/gst-lrg-window.h"
#include "../gst-enums.h"
#include "../gst-types.h"
#include "../config/gst-config.h"

/* Forward declaration to avoid circular includes */
typedef struct _GstSelection GstSelection;

G_BEGIN_DECLS

#define GST_TYPE_LRG_RENDERER (gst_lrg_renderer_get_type())

G_DECLARE_FINAL_TYPE(GstLrgRenderer, gst_lrg_renderer,
	GST, LRG_RENDERER, GstRenderer)

/**
 * gst_lrg_renderer_new:
 * @terminal: the terminal to render
 * @lrg_window: LRG window (its #GrlWindow is extracted internally)
 * @font_cache: graylib font cache
 * @borderpx: border padding in pixels
 *
 * Creates a new LRG renderer that draws into @lrg_window's raylib window.
 *
 * Returns: (transfer full): A new #GstLrgRenderer
 */
GstLrgRenderer *
gst_lrg_renderer_new(
	GstTerminal         *terminal,
	GstLrgWindow        *lrg_window,
	GstGrlFontCache     *font_cache,
	gint                borderpx
);

/**
 * gst_lrg_renderer_load_colors:
 * @self: A #GstLrgRenderer
 * @config: (nullable): A #GstConfig for palette and color overrides
 *
 * Loads the full color palette (262 colors) from defaults, then applies
 * any overrides from @config.
 *
 * Returns: TRUE on success
 */
gboolean
gst_lrg_renderer_load_colors(GstLrgRenderer *self, GstConfig *config);

/**
 * gst_lrg_renderer_set_win_mode:
 * @self: A #GstLrgRenderer
 * @mode: window mode flags
 *
 * Updates the window mode flags.
 */
void
gst_lrg_renderer_set_win_mode(GstLrgRenderer *self, GstWinMode mode);

/**
 * gst_lrg_renderer_get_win_mode:
 * @self: A #GstLrgRenderer
 *
 * Returns: the current GstWinMode flags
 */
GstWinMode
gst_lrg_renderer_get_win_mode(GstLrgRenderer *self);

/**
 * gst_lrg_renderer_set_selection:
 * @self: A #GstLrgRenderer
 * @selection: (transfer none): A #GstSelection for highlight checks
 *
 * Sets the selection object used to determine which cells should be
 * rendered with reverse video.
 */
void
gst_lrg_renderer_set_selection(
	GstLrgRenderer      *self,
	GstSelection        *selection
);

G_END_DECLS

#endif /* GST_LRG_RENDERER_H */
