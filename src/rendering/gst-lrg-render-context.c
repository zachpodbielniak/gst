/*
 * gst-lrg-render-context.c - LRG render context vtable implementations
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the GstRenderContextOps vtable for the libregnum/LRG
 * (graylib/raylib) backend. Each operation translates to an immediate-mode
 * graylib draw call against the current frame.
 */

#include "gst-lrg-render-context.h"

/* ===== Helpers ===== */

/*
 * grl_from_gst:
 * @c: a GstColor (0xRRGGBBAA)
 * @a: alpha override (0-255)
 *
 * Returns: (transfer full): a new #GrlColor with @c's RGB and alpha @a
 */
static GrlColor *
grl_from_gst(GstColor c, guint8 a)
{
	return grl_color_new((guint8)GST_COLOR_R(c), (guint8)GST_COLOR_G(c),
		(guint8)GST_COLOR_B(c), a);
}

/* ===== Vtable implementations ===== */

/*
 * lrg_fill_rect:
 *
 * Fills a rectangle with a palette color index, applying the context's
 * opacity as alpha for transparency support.
 */
static void
lrg_fill_rect(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint             color_idx
){
	GstLrgRenderContext *lctx;
	guint8 a;
	g_autoptr(GrlColor) col = NULL;

	lctx = (GstLrgRenderContext *)ctx;
	if (lctx->colors == NULL) {
		return;
	}

	a = (guint8)(ctx->opacity * 255.0 + 0.5);
	if (color_idx < lctx->num_colors) {
		col = grl_from_gst(lctx->colors[color_idx], a);
	} else {
		col = grl_color_new(0, 0, 0, a);
	}
	grl_draw_rectangle(x, y, w, h, col);
}

/*
 * lrg_fill_rect_rgba:
 *
 * Fills a rectangle with direct RGBA color components.
 */
static void
lrg_fill_rect_rgba(
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
	g_autoptr(GrlColor) col = NULL;

	(void)ctx;
	col = grl_color_new(r, g, b, a);
	grl_draw_rectangle(x, y, w, h, col);
}

/*
 * lrg_fill_rect_fg:
 *
 * Fills a rectangle with the current per-glyph foreground color.
 */
static void
lrg_fill_rect_fg(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	GstLrgRenderContext *lctx;
	g_autoptr(GrlColor) col = NULL;

	lctx = (GstLrgRenderContext *)ctx;
	col = grl_from_gst(lctx->fg, 255);
	grl_draw_rectangle(x, y, w, h, col);
}

/*
 * lrg_fill_rect_bg:
 *
 * Fills a rectangle with the current per-glyph background color,
 * applying window opacity as alpha.
 */
static void
lrg_fill_rect_bg(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	GstLrgRenderContext *lctx;
	guint8 a;
	g_autoptr(GrlColor) col = NULL;

	lctx = (GstLrgRenderContext *)ctx;
	a = (guint8)(ctx->opacity * 255.0 + 0.5);
	col = grl_from_gst(lctx->bg, a);
	grl_draw_rectangle(x, y, w, h, col);
}

/*
 * lrg_draw_glyph:
 *
 * Draws a single glyph via the cairo-ft glyph atlas (identical rasterization
 * to the Wayland/X11 backends), tinted with the foreground color.
 */
static void
lrg_draw_glyph(
	GstRenderContext *ctx,
	GstRune           rune,
	GstFontStyle      style,
	gint              px,
	gint              py,
	guint             fg_idx,
	guint             bg_idx,
	guint16           attr
){
	GstLrgRenderContext *lctx;
	GstColor fg_color;

	(void)bg_idx;
	(void)attr;

	lctx = (GstLrgRenderContext *)ctx;
	if (lctx->font_cache == NULL) {
		return;
	}

	if (fg_idx < lctx->num_colors) {
		fg_color = lctx->colors[fg_idx];
	} else {
		fg_color = lctx->fg;
	}

	gst_grl_font_cache_draw_glyph(lctx->font_cache, rune, style,
		px, py, fg_color);
}

/* ===== Static vtable ===== */

static const GstRenderContextOps lrg_ops = {
	lrg_fill_rect,
	lrg_fill_rect_rgba,
	lrg_fill_rect_fg,
	lrg_fill_rect_bg,
	lrg_draw_glyph,
	NULL,   /* draw_image: not supported in the 2D LRG backend yet */
	NULL    /* draw_glyph_id: graylib has no glyph-id draw */
};

/**
 * gst_lrg_render_context_init_ops:
 * @ctx: an LRG render context
 *
 * Initializes the vtable ops pointer for LRG operations.
 */
void
gst_lrg_render_context_init_ops(GstLrgRenderContext *ctx)
{
	ctx->base.ops = &lrg_ops;
	ctx->base.backend = GST_BACKEND_LRG;
}
