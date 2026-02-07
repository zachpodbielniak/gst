/*
 * gst-render-context.h - Abstract render context with vtable dispatch
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Plain C struct with virtual function table for backend-agnostic
 * rendering. Modules use the gst_render_context_*() inline helpers
 * instead of calling X11 or Cairo APIs directly.
 *
 * Backend-specific render contexts (GstX11RenderContext,
 * GstWaylandRenderContext) embed this as their first member and
 * provide vtable implementations for the drawing primitives.
 */

#ifndef GST_RENDER_CONTEXT_H
#define GST_RENDER_CONTEXT_H

#include <glib.h>
#include "../gst-types.h"
#include "../gst-enums.h"

G_BEGIN_DECLS

typedef struct _GstRenderContext    GstRenderContext;
typedef struct _GstRenderContextOps GstRenderContextOps;

/**
 * GstRenderContextOps:
 * @fill_rect: fill rectangle with palette color index
 * @fill_rect_rgba: fill rectangle with direct RGBA components
 * @fill_rect_fg: fill rectangle with current per-glyph foreground
 * @fill_rect_bg: fill rectangle with current per-glyph background
 * @draw_glyph: draw a single glyph (font lookup handled internally)
 *
 * Virtual function table for backend-specific drawing operations.
 * Each backend (X11, Wayland) provides its own implementations.
 */
struct _GstRenderContextOps
{
	/* Fill rectangle with palette color index */
	void (*fill_rect)(GstRenderContext *ctx, gint x, gint y,
	                  gint w, gint h, guint color_idx);

	/* Fill rectangle with direct RGBA */
	void (*fill_rect_rgba)(GstRenderContext *ctx, gint x, gint y,
	                       gint w, gint h,
	                       guint8 r, guint8 g, guint8 b, guint8 a);

	/* Fill rectangle with current per-glyph foreground */
	void (*fill_rect_fg)(GstRenderContext *ctx, gint x, gint y,
	                     gint w, gint h);

	/* Fill rectangle with current per-glyph background */
	void (*fill_rect_bg)(GstRenderContext *ctx, gint x, gint y,
	                     gint w, gint h);

	/* Draw a single glyph (font lookup handled internally) */
	void (*draw_glyph)(GstRenderContext *ctx, GstRune rune,
	                   GstFontStyle style, gint px, gint py,
	                   guint fg_idx, guint bg_idx, guint16 attr);
};

/**
 * GstRenderContext:
 * @ops: pointer to backend vtable
 * @backend: which backend this context belongs to
 * @cw: character cell width in pixels
 * @ch: character cell height in pixels
 * @borderpx: border padding in pixels
 * @win_w: window width in pixels
 * @win_h: window height in pixels
 * @win_mode: current window mode flags
 * @glyph_attr: per-glyph attributes (set during draw_line dispatch)
 *
 * Abstract base render context. Backend-specific contexts embed this
 * struct as their first member, allowing safe casting from the
 * abstract type to the concrete backend type.
 */
struct _GstRenderContext
{
	const GstRenderContextOps *ops;
	GstBackendType backend;
	gint          cw;
	gint          ch;
	gint          borderpx;
	gint          win_w;
	gint          win_h;
	GstWinMode    win_mode;
	guint16       glyph_attr;
};

/* ===== Inline dispatch helpers ===== */

/**
 * gst_render_context_fill_rect:
 * @ctx: render context
 * @x: left edge in pixels
 * @y: top edge in pixels
 * @w: width in pixels
 * @h: height in pixels
 * @color_idx: palette color index
 *
 * Fills a rectangle with a palette color.
 */
static inline void
gst_render_context_fill_rect(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint             color_idx
){
	ctx->ops->fill_rect(ctx, x, y, w, h, color_idx);
}

/**
 * gst_render_context_fill_rect_rgba:
 * @ctx: render context
 * @x: left edge in pixels
 * @y: top edge in pixels
 * @w: width in pixels
 * @h: height in pixels
 * @r: red component (0-255)
 * @g: green component (0-255)
 * @b: blue component (0-255)
 * @a: alpha component (0-255)
 *
 * Fills a rectangle with a direct RGBA color.
 */
static inline void
gst_render_context_fill_rect_rgba(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint8            r,
	guint8            g,
	guint8            b,
	guint8            a
){
	ctx->ops->fill_rect_rgba(ctx, x, y, w, h, r, g, b, a);
}

/**
 * gst_render_context_fill_rect_fg:
 * @ctx: render context
 * @x: left edge in pixels
 * @y: top edge in pixels
 * @w: width in pixels
 * @h: height in pixels
 *
 * Fills a rectangle with the current per-glyph foreground color.
 */
static inline void
gst_render_context_fill_rect_fg(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	ctx->ops->fill_rect_fg(ctx, x, y, w, h);
}

/**
 * gst_render_context_fill_rect_bg:
 * @ctx: render context
 * @x: left edge in pixels
 * @y: top edge in pixels
 * @w: width in pixels
 * @h: height in pixels
 *
 * Fills a rectangle with the current per-glyph background color.
 */
static inline void
gst_render_context_fill_rect_bg(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	ctx->ops->fill_rect_bg(ctx, x, y, w, h);
}

/**
 * gst_render_context_draw_glyph:
 * @ctx: render context
 * @rune: Unicode codepoint to render
 * @style: font style variant
 * @px: pixel x position
 * @py: pixel y position
 * @fg_idx: foreground color index
 * @bg_idx: background color index
 * @attr: glyph attribute flags
 *
 * Draws a single glyph. The backend handles font lookup internally.
 */
static inline void
gst_render_context_draw_glyph(
	GstRenderContext *ctx,
	GstRune           rune,
	GstFontStyle      style,
	gint              px,
	gint              py,
	guint             fg_idx,
	guint             bg_idx,
	guint16           attr
){
	ctx->ops->draw_glyph(ctx, rune, style, px, py, fg_idx, bg_idx, attr);
}

G_END_DECLS

#endif /* GST_RENDER_CONTEXT_H */
