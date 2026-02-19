/*
 * gst-wayland-render-context.c - Wayland render context vtable implementations
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the GstRenderContextOps vtable for the Wayland/Cairo
 * backend. Each operation translates to Cairo drawing calls.
 */

#include "gst-wayland-render-context.h"

/* ===== Vtable implementations ===== */

/*
 * set_source_from_color:
 * @cr: cairo context
 * @color: GstColor RGBA value
 *
 * Sets the cairo source color from a GstColor.
 */
static void
set_source_from_color(cairo_t *cr, GstColor color)
{
	cairo_set_source_rgb(cr,
		(gdouble)GST_COLOR_R(color) / 255.0,
		(gdouble)GST_COLOR_G(color) / 255.0,
		(gdouble)GST_COLOR_B(color) / 255.0);
}

/*
 * set_source_with_opacity:
 * @cr: cairo context
 * @color: GstColor RGBA value
 * @opacity: window opacity (0.0-1.0)
 *
 * Sets the cairo source color from a GstColor with opacity as alpha.
 * Used for background fills to respect window transparency.
 */
static void
set_source_with_opacity(cairo_t *cr, GstColor color, gdouble opacity)
{
	cairo_set_source_rgba(cr,
		(gdouble)GST_COLOR_R(color) / 255.0,
		(gdouble)GST_COLOR_G(color) / 255.0,
		(gdouble)GST_COLOR_B(color) / 255.0,
		opacity);
}

/*
 * wl_fill_rect:
 *
 * Fills a rectangle with a palette color index.
 * Looks up the color from the palette array and applies
 * the render context's opacity as alpha for transparency.
 */
static void
wl_fill_rect(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint             color_idx
){
	GstWaylandRenderContext *wctx;

	wctx = (GstWaylandRenderContext *)ctx;

	if (wctx->cr == NULL || wctx->colors == NULL) {
		return;
	}

	if (color_idx < wctx->num_colors) {
		set_source_with_opacity(wctx->cr, wctx->colors[color_idx],
			ctx->opacity);
	} else {
		cairo_set_source_rgba(wctx->cr, 0, 0, 0, ctx->opacity);
	}

	cairo_set_operator(wctx->cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle(wctx->cr, (gdouble)x, (gdouble)y,
		(gdouble)w, (gdouble)h);
	cairo_fill(wctx->cr);
	cairo_set_operator(wctx->cr, CAIRO_OPERATOR_OVER);
}

/*
 * wl_fill_rect_rgba:
 *
 * Fills a rectangle with direct RGBA color components.
 */
static void
wl_fill_rect_rgba(
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
	GstWaylandRenderContext *wctx;

	wctx = (GstWaylandRenderContext *)ctx;

	if (wctx->cr == NULL) {
		return;
	}

	cairo_set_source_rgba(wctx->cr,
		(gdouble)r / 255.0,
		(gdouble)g / 255.0,
		(gdouble)b / 255.0,
		(gdouble)a / 255.0);

	cairo_rectangle(wctx->cr, (gdouble)x, (gdouble)y,
		(gdouble)w, (gdouble)h);
	cairo_fill(wctx->cr);
}

/*
 * wl_fill_rect_fg:
 *
 * Fills a rectangle with the current per-glyph foreground color.
 */
static void
wl_fill_rect_fg(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	GstWaylandRenderContext *wctx;

	wctx = (GstWaylandRenderContext *)ctx;

	if (wctx->cr == NULL) {
		return;
	}

	set_source_from_color(wctx->cr, wctx->fg);
	cairo_rectangle(wctx->cr, (gdouble)x, (gdouble)y,
		(gdouble)w, (gdouble)h);
	cairo_fill(wctx->cr);
}

/*
 * wl_fill_rect_bg:
 *
 * Fills a rectangle with the current per-glyph background color.
 * Applies window opacity as alpha for transparency support.
 */
static void
wl_fill_rect_bg(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	GstWaylandRenderContext *wctx;

	wctx = (GstWaylandRenderContext *)ctx;

	if (wctx->cr == NULL) {
		return;
	}

	set_source_with_opacity(wctx->cr, wctx->bg, ctx->opacity);
	cairo_set_operator(wctx->cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle(wctx->cr, (gdouble)x, (gdouble)y,
		(gdouble)w, (gdouble)h);
	cairo_fill(wctx->cr);
	cairo_set_operator(wctx->cr, CAIRO_OPERATOR_OVER);
}

/*
 * wl_draw_glyph:
 *
 * Draws a single glyph using the Cairo font cache.
 * Looks up the glyph in the font cache, then renders it using
 * cairo_show_glyphs().
 */
static void
wl_draw_glyph(
	GstRenderContext *ctx,
	GstRune           rune,
	GstFontStyle      style,
	gint              px,
	gint              py,
	guint             fg_idx,
	guint             bg_idx,
	guint16           attr
){
	GstWaylandRenderContext *wctx;
	cairo_scaled_font_t *scaled_font;
	gulong glyph_index;
	cairo_glyph_t glyph;
	GstColor fg_color;
	gint ascent;

	wctx = (GstWaylandRenderContext *)ctx;

	if (wctx->cr == NULL || wctx->font_cache == NULL) {
		return;
	}

	/* Look up the glyph in the font cache */
	if (!gst_cairo_font_cache_lookup_glyph(wctx->font_cache,
	    rune, style, &scaled_font, &glyph_index)) {
		return;
	}

	/* Get foreground color */
	if (fg_idx < wctx->num_colors) {
		fg_color = wctx->colors[fg_idx];
	} else {
		fg_color = wctx->fg;
	}

	/* Set font and color */
	cairo_set_scaled_font(wctx->cr, scaled_font);
	set_source_from_color(wctx->cr, fg_color);

	/* Position the glyph at baseline */
	ascent = gst_cairo_font_cache_get_ascent(wctx->font_cache);
	glyph.index = glyph_index;
	glyph.x = (gdouble)px;
	glyph.y = (gdouble)(py + ascent);

	cairo_show_glyphs(wctx->cr, &glyph, 1);
}

/*
 * wl_draw_image:
 *
 * Draws an RGBA image using Cairo.
 * Creates a temporary cairo image surface from the RGBA pixel data,
 * applying RGBA->ARGB32 pre-multiplied conversion (Cairo requires
 * pre-multiplied alpha in native byte order ARGB32 format).
 * Uses cairo_scale for resizing if src and dst sizes differ.
 */
static void
wl_draw_image(
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
	GstWaylandRenderContext *wctx;
	cairo_surface_t *img_surface;
	guint8 *argb_data;
	gint cairo_stride;
	gint row;
	gint col;

	wctx = (GstWaylandRenderContext *)ctx;

	if (wctx->cr == NULL || data == NULL || src_w <= 0 || src_h <= 0) {
		return;
	}

	/*
	 * Create an ARGB32 surface. Cairo ARGB32 is pre-multiplied alpha
	 * in native byte order: on little-endian that's BGRA in memory.
	 */
	cairo_stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, src_w);
	argb_data = (guint8 *)g_malloc0((gsize)cairo_stride * (gsize)src_h);

	for (row = 0; row < src_h; row++) {
		for (col = 0; col < src_w; col++) {
			guint8 r, g, b, a;
			guint16 pr, pg, pb;
			gint src_off;
			gint dst_off;

			src_off = row * src_stride + col * 4;
			dst_off = row * cairo_stride + col * 4;

			r = data[src_off + 0];
			g = data[src_off + 1];
			b = data[src_off + 2];
			a = data[src_off + 3];

			/* Pre-multiply */
			pr = (guint16)((guint16)r * a + 127) / 255;
			pg = (guint16)((guint16)g * a + 127) / 255;
			pb = (guint16)((guint16)b * a + 127) / 255;

			/* ARGB32 native byte order (little-endian: BGRA) */
			argb_data[dst_off + 0] = (guint8)pb;
			argb_data[dst_off + 1] = (guint8)pg;
			argb_data[dst_off + 2] = (guint8)pr;
			argb_data[dst_off + 3] = a;
		}
	}

	img_surface = cairo_image_surface_create_for_data(
		argb_data, CAIRO_FORMAT_ARGB32, src_w, src_h, cairo_stride);

	cairo_save(wctx->cr);

	/* Position and optionally scale */
	cairo_translate(wctx->cr, (gdouble)dst_x, (gdouble)dst_y);
	if (dst_w != src_w || dst_h != src_h) {
		cairo_scale(wctx->cr,
			(gdouble)dst_w / (gdouble)src_w,
			(gdouble)dst_h / (gdouble)src_h);
	}

	cairo_set_source_surface(wctx->cr, img_surface, 0, 0);
	cairo_paint(wctx->cr);

	cairo_restore(wctx->cr);

	cairo_surface_destroy(img_surface);
	g_free(argb_data);
}

/* ===== Static vtable ===== */

static const GstRenderContextOps wayland_ops = {
	wl_fill_rect,
	wl_fill_rect_rgba,
	wl_fill_rect_fg,
	wl_fill_rect_bg,
	wl_draw_glyph,
	wl_draw_image
};

/**
 * gst_wayland_render_context_init_ops:
 * @ctx: a Wayland render context
 *
 * Initializes the vtable ops pointer for Wayland operations.
 */
void
gst_wayland_render_context_init_ops(GstWaylandRenderContext *ctx)
{
	ctx->base.ops = &wayland_ops;
	ctx->base.backend = GST_BACKEND_WAYLAND;
}
