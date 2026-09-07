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
#include <string.h>

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

/* Upload packed RGBA without assuming that cropped input rows are contiguous.
 * Retain textures until swap_buffers flushes raylib's deferred draw batch. */
static void
lrg_draw_image(
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
	GstLrgRenderContext *lctx;
	g_autofree guint8 *packed = NULL;
	g_autoptr(GrlImage) image = NULL;
	g_autoptr(GrlTexture) texture = NULL;
	g_autoptr(GrlRectangle) source = NULL;
	g_autoptr(GrlRectangle) dest = NULL;
	g_autoptr(GrlVector2) origin = NULL;
	g_autoptr(GrlColor) tint = NULL;
	gint row;

	lctx = (GstLrgRenderContext *)ctx;
	if (data == NULL || lctx->frame_textures == NULL ||
	    src_w <= 0 || src_h <= 0 || src_w > G_MAXINT / 4 ||
	    src_stride < src_w * 4 || dst_w <= 0 || dst_h <= 0 ||
	    (gsize)src_h > G_MAXSIZE / (gsize)src_stride) {
		return;
	}
	if (src_stride != src_w * 4) {
		packed = g_try_malloc_n((gsize)src_h, (gsize)src_w * 4);
		if (packed == NULL) {
			return;
		}
		for (row = 0; row < src_h; row++) {
			memcpy(packed + (gsize)row * (gsize)src_w * 4,
				data + (gsize)row * (gsize)src_stride, (gsize)src_w * 4);
		}
		data = packed;
	}
	image = grl_image_new_from_pixels(src_w, src_h,
		GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, data);
	if (image == NULL) {
		return;
	}
	texture = grl_texture_new_from_image(image);
	if (texture == NULL) {
		return;
	}
	grl_texture_set_filter(texture, GRL_TEXTURE_FILTER_POINT);
	source = grl_rectangle_new(0, 0, (gfloat)src_w, (gfloat)src_h);
	dest = grl_rectangle_new((gfloat)dst_x, (gfloat)dst_y,
		(gfloat)dst_w, (gfloat)dst_h);
	origin = grl_vector2_new(0, 0);
	tint = grl_color_new(255, 255, 255, 255);
	grl_draw_texture_pro(texture, source, dest, origin, 0, tint);
	g_ptr_array_add(lctx->frame_textures, g_steal_pointer(&texture));
}

/* The legacy glyph-ID op uses the primary style font, matching Wayland.
 * Call draw_glyph_index directly when shaping selected a fallback font. */
static void
lrg_draw_glyph_id(
	GstRenderContext *ctx,
	guint32           glyph_id,
	GstFontStyle      style,
	gint              px,
	gint              py
){
	GstLrgRenderContext *lctx;
	cairo_scaled_font_t *scaled;
	gulong unused;

	lctx = (GstLrgRenderContext *)ctx;
	if (lctx->font_cache != NULL && gst_cairo_font_cache_lookup_glyph(
	    gst_grl_font_cache_get_cairo_cache(lctx->font_cache), ' ', style,
	    &scaled, &unused)) {
		gst_grl_font_cache_draw_glyph_index(lctx->font_cache, scaled,
			(gulong)glyph_id, px, py, lctx->fg);
	}
}

/* ===== Static vtable ===== */

static const GstRenderContextOps lrg_ops = {
	lrg_fill_rect,
	lrg_fill_rect_rgba,
	lrg_fill_rect_fg,
	lrg_fill_rect_bg,
	lrg_draw_glyph,
	lrg_draw_image,
	lrg_draw_glyph_id
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
