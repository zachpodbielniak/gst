/*
 * gst-render-context.h - Abstract render context with vtable dispatch
 *
 * Copyright (C) 2026 Zach Podbielniak
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

	/* Draw an RGBA image at the given pixel position.
	 * @data: pixel data in row-major RGBA format (4 bytes per pixel)
	 * @src_w: source image width in pixels
	 * @src_h: source image height in pixels
	 * @src_stride: bytes per row in source data (usually src_w * 4)
	 * @dst_x: destination x position in pixels
	 * @dst_y: destination y position in pixels
	 * @dst_w: destination width in pixels (may differ from src_w for scaling)
	 * @dst_h: destination height in pixels (may differ from src_h for scaling)
	 *
	 * May be NULL if the backend does not support image drawing.
	 */
	void (*draw_image)(GstRenderContext *ctx,
	                   const guint8 *data,
	                   gint src_w, gint src_h, gint src_stride,
	                   gint dst_x, gint dst_y,
	                   gint dst_w, gint dst_h);

	/* Draw a glyph by its font-internal glyph ID (for HarfBuzz ligatures).
	 * @glyph_id: font-internal glyph index (from hb_shape output)
	 * @style: font style variant
	 * @px: pixel x position (with x_offset applied by caller)
	 * @py: pixel y position (with y_offset applied by caller)
	 *
	 * May be NULL if the backend does not support glyph-ID rendering.
	 */
	void (*draw_glyph_id)(GstRenderContext *ctx, guint32 glyph_id,
	                      GstFontStyle style, gint px, gint py);
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
	gdouble       opacity;     /* window opacity (0.0-1.0) for bg alpha */
	gpointer      current_line;  /* pointer to current GstLine being drawn */
	gint          current_col;   /* column index of glyph being rendered */
	gint          current_cols;  /* total columns in the terminal */
	gboolean      has_wallpaper;   /* TRUE when a background provider is active */
	gdouble       wallpaper_bg_alpha; /* cell bg alpha for default-bg cells */
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

/**
 * gst_render_context_draw_image:
 * @ctx: render context
 * @data: RGBA pixel data (4 bytes per pixel, row-major)
 * @src_w: source image width in pixels
 * @src_h: source image height in pixels
 * @src_stride: bytes per row in source data
 * @dst_x: destination x in pixels
 * @dst_y: destination y in pixels
 * @dst_w: destination width in pixels
 * @dst_h: destination height in pixels
 *
 * Draws an RGBA image. Returns silently if the backend does not
 * support image drawing (draw_image is %NULL).
 */
static inline void
gst_render_context_draw_image(
	GstRenderContext *ctx,
	const guint8     *data,
	gint              src_w,
	gint              src_h,
	gint              src_stride,
	gint              dst_x,
	gint              dst_y,
	gint              dst_w,
	gint              dst_h
){
	if (ctx->ops->draw_image != NULL) {
		ctx->ops->draw_image(ctx, data, src_w, src_h, src_stride,
			dst_x, dst_y, dst_w, dst_h);
	}
}

/**
 * gst_render_context_draw_glyph_id:
 * @ctx: render context
 * @glyph_id: font-internal glyph index (from HarfBuzz shaping)
 * @style: font style variant
 * @px: pixel x position
 * @py: pixel y position
 *
 * Draws a glyph by its font-internal glyph ID rather than by Unicode
 * codepoint. Used by the ligatures module to render shaped output.
 * Returns silently if the backend does not support glyph-ID rendering
 * (draw_glyph_id is %NULL).
 */
static inline void
gst_render_context_draw_glyph_id(
	GstRenderContext *ctx,
	guint32           glyph_id,
	GstFontStyle      style,
	gint              px,
	gint              py
){
	if (ctx->ops->draw_glyph_id != NULL) {
		ctx->ops->draw_glyph_id(ctx, glyph_id, style, px, py);
	}
}

G_END_DECLS

#endif /* GST_RENDER_CONTEXT_H */
