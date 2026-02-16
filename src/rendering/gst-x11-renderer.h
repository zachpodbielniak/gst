/*
 * gst-x11-renderer.h - X11 renderer implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * X11-based terminal renderer using Xlib, Xft, and XRender.
 * Ports st's dc (drawing context), xdrawline, xdrawglyphfontspecs,
 * xdrawcursor, color loading, and double-buffered pixmap rendering.
 */

#ifndef GST_X11_RENDERER_H
#define GST_X11_RENDERER_H

#include <glib-object.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include "gst-renderer.h"
#include "gst-font-cache.h"
#include "../gst-enums.h"
#include "../config/gst-config.h"
#include "../gst-types.h"

/* Forward declaration to avoid circular includes */
typedef struct _GstSelection GstSelection;

G_BEGIN_DECLS

#define GST_TYPE_X11_RENDERER (gst_x11_renderer_get_type())

G_DECLARE_FINAL_TYPE(GstX11Renderer, gst_x11_renderer, GST, X11_RENDERER, GstRenderer)

GType
gst_x11_renderer_get_type(void) G_GNUC_CONST;

/**
 * gst_x11_renderer_new:
 * @terminal: the terminal to render
 * @display: X11 display connection
 * @xwindow: X11 window to draw on
 * @visual: X11 visual
 * @colormap: X11 colormap
 * @screen: X11 screen number
 * @font_cache: the font cache to use
 * @borderpx: border padding in pixels
 *
 * Creates a new X11 renderer. Sets up the drawing context,
 * pixmap double buffer, and loads the color palette.
 *
 * Returns: (transfer full): A new #GstX11Renderer
 */
GstX11Renderer *
gst_x11_renderer_new(
	GstTerminal     *terminal,
	Display         *display,
	Window          xwindow,
	Visual          *visual,
	Colormap        colormap,
	gint            screen,
	GstFontCache    *font_cache,
	gint            borderpx
);

/**
 * gst_x11_renderer_load_colors:
 * @self: A #GstX11Renderer
 * @config: (nullable): A #GstConfig for palette and color overrides
 *
 * Loads the full color palette (262 colors) from defaults,
 * then applies any overrides from @config (palette hex strings
 * and direct foreground/background/cursor hex colors).
 *
 * Returns: TRUE on success
 */
gboolean
gst_x11_renderer_load_colors(GstX11Renderer *self, GstConfig *config);

/**
 * gst_x11_renderer_set_color:
 * @self: A #GstX11Renderer
 * @index: color index (0-261)
 * @name: X11 color name or hex string
 *
 * Sets a single color by name. Used for dynamic color changes
 * via OSC escape sequences.
 *
 * Returns: TRUE on success
 */
gboolean
gst_x11_renderer_set_color(
	GstX11Renderer  *self,
	gint            index,
	const gchar     *name
);

/**
 * gst_x11_renderer_get_font_cache:
 * @self: A #GstX11Renderer
 *
 * Gets the font cache used by this renderer.
 *
 * Returns: (transfer none): the font cache
 */
GstFontCache *
gst_x11_renderer_get_font_cache(GstX11Renderer *self);

/**
 * gst_x11_renderer_set_win_mode:
 * @self: A #GstX11Renderer
 * @mode: window mode flags to set
 *
 * Updates the window mode flags (visible, focused, blink state).
 */
void
gst_x11_renderer_set_win_mode(
	GstX11Renderer  *self,
	GstWinMode      mode
);

/**
 * gst_x11_renderer_get_win_mode:
 * @self: A #GstX11Renderer
 *
 * Gets the current window mode flags.
 *
 * Returns: the current GstWinMode flags
 */
GstWinMode
gst_x11_renderer_get_win_mode(GstX11Renderer *self);

/**
 * gst_x11_renderer_set_selection:
 * @self: A #GstX11Renderer
 * @selection: (transfer none): A #GstSelection to use for highlight checks
 *
 * Sets the selection object used to determine which cells should
 * be rendered with selection highlighting (reverse video).
 */
void
gst_x11_renderer_set_selection(
	GstX11Renderer  *self,
	GstSelection    *selection
);

G_END_DECLS

#endif /* GST_X11_RENDERER_H */
