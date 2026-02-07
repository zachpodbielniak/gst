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
 * wl_fill_rect:
 *
 * Fills a rectangle with a palette color index.
 * Looks up the color from the palette array.
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
		set_source_from_color(wctx->cr, wctx->colors[color_idx]);
	} else {
		cairo_set_source_rgb(wctx->cr, 0, 0, 0);
	}

	cairo_rectangle(wctx->cr, (gdouble)x, (gdouble)y,
		(gdouble)w, (gdouble)h);
	cairo_fill(wctx->cr);
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

	set_source_from_color(wctx->cr, wctx->bg);
	cairo_rectangle(wctx->cr, (gdouble)x, (gdouble)y,
		(gdouble)w, (gdouble)h);
	cairo_fill(wctx->cr);
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

/* ===== Static vtable ===== */

static const GstRenderContextOps wayland_ops = {
	wl_fill_rect,
	wl_fill_rect_rgba,
	wl_fill_rect_fg,
	wl_fill_rect_bg,
	wl_draw_glyph
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
