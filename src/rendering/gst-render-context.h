/*
 * gst-render-context.h - Render context for module access to X11 drawing
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Plain C struct passed as gpointer render_context to module interfaces
 * (GstGlyphTransformer, GstRenderOverlay). Stack-allocated in the
 * renderer and populated before dispatch calls.
 */

#ifndef GST_RENDER_CONTEXT_H
#define GST_RENDER_CONTEXT_H

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include "gst-font-cache.h"
#include "../gst-enums.h"

G_BEGIN_DECLS

/**
 * GstX11RenderContext:
 * @display: X11 display connection
 * @window: actual X11 window ID
 * @drawable: off-screen pixmap for double buffering
 * @gc: X11 graphics context
 * @xft_draw: Xft drawing context bound to drawable
 * @visual: X11 visual
 * @colormap: X11 colormap
 * @colors: loaded color palette array
 * @num_colors: number of entries in colors array
 * @font_cache: the font cache for glyph lookup
 * @cw: character cell width in pixels
 * @ch: character cell height in pixels
 * @borderpx: border padding in pixels
 * @win_w: window width in pixels
 * @win_h: window height in pixels
 * @win_mode: current window mode flags
 * @fg: per-glyph foreground color (set during draw_line dispatch)
 * @bg: per-glyph background color (set during draw_line dispatch)
 * @glyph_attr: per-glyph attribute flags (set during draw_line dispatch)
 *
 * Provides X11 drawing resources to modules via the opaque
 * render_context parameter in GstGlyphTransformer and GstRenderOverlay
 * interface methods.
 */
typedef struct
{
	/* X11 core */
	Display      *display;
	Window        window;
	Drawable      drawable;
	GC            gc;
	XftDraw      *xft_draw;
	Visual       *visual;
	Colormap      colormap;

	/* Colors */
	XftColor     *colors;
	gsize         num_colors;

	/* Fonts */
	GstFontCache *font_cache;

	/* Cell geometry */
	gint          cw;
	gint          ch;
	gint          borderpx;

	/* Window geometry */
	gint          win_w;
	gint          win_h;

	/* State */
	GstWinMode    win_mode;

	/* Per-glyph context (populated during draw_line dispatch only) */
	XftColor     *fg;
	XftColor     *bg;
	guint16       glyph_attr;
} GstX11RenderContext;

G_END_DECLS

#endif /* GST_RENDER_CONTEXT_H */
