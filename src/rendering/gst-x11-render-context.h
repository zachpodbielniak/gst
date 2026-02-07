/*
 * gst-x11-render-context.h - X11 render context extending abstract base
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Extends GstRenderContext with X11-specific drawing resources.
 * The base struct MUST be the first member so that casting between
 * GstRenderContext* and GstX11RenderContext* is safe.
 */

#ifndef GST_X11_RENDER_CONTEXT_H
#define GST_X11_RENDER_CONTEXT_H

#include "gst-render-context.h"
#include "gst-font-cache.h"
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

G_BEGIN_DECLS

/**
 * GstX11RenderContext:
 * @base: abstract base (MUST be first member)
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
 * @fg: per-glyph foreground color (set during draw_line dispatch)
 * @bg: per-glyph background color (set during draw_line dispatch)
 *
 * X11-specific render context. Extends the abstract base with
 * Xlib/Xft drawing resources.
 */
typedef struct
{
	GstRenderContext base;     /* MUST be first member */

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

	/* Per-glyph context (populated during draw_line dispatch only) */
	XftColor     *fg;
	XftColor     *bg;
} GstX11RenderContext;

/**
 * gst_x11_render_context_init_ops:
 * @ctx: an X11 render context
 *
 * Initializes the vtable ops pointer for X11 backend operations.
 * Must be called once after populating the X11-specific fields.
 */
void
gst_x11_render_context_init_ops(GstX11RenderContext *ctx);

G_END_DECLS

#endif /* GST_X11_RENDER_CONTEXT_H */
