/*
 * gst-x11-render-context.c - X11 vtable implementations
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the GstRenderContextOps vtable for the X11 backend.
 * Wraps Xft drawing calls (XftDrawRect, XftDrawGlyphFontSpec)
 * behind the abstract render context interface.
 */

#include "gst-x11-render-context.h"
#include <string.h>

/**
 * SECTION:gst-x11-render-context
 * @title: GstX11RenderContext
 * @short_description: X11 vtable for abstract render context
 *
 * Provides X11/Xft implementations of the drawing primitives
 * defined in #GstRenderContextOps.
 */

/* ===== Vtable implementations ===== */

/*
 * x11_fill_rect:
 *
 * Fills a rectangle using a palette color index.
 * Looks up the XftColor from the palette array.
 */
static void
x11_fill_rect(
	GstRenderContext *base,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint             color_idx
){
	GstX11RenderContext *ctx;

	ctx = (GstX11RenderContext *)base;

	if (color_idx < ctx->num_colors) {
		XftDrawRect(ctx->xft_draw, &ctx->colors[color_idx],
			x, y, (guint)w, (guint)h);
	}
}

/*
 * x11_fill_rect_rgba:
 *
 * Fills a rectangle with a direct RGBA color.
 * Allocates a temporary XftColor, draws, then frees it.
 */
static void
x11_fill_rect_rgba(
	GstRenderContext *base,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint8            r,
	guint8            g,
	guint8            b,
	guint8            a
){
	GstX11RenderContext *ctx;
	XRenderColor color;
	XftColor xft_color;

	ctx = (GstX11RenderContext *)base;

	color.red   = (guint16)((guint16)r << 8 | r);
	color.green = (guint16)((guint16)g << 8 | g);
	color.blue  = (guint16)((guint16)b << 8 | b);
	color.alpha = (guint16)((guint16)a << 8 | a);

	if (XftColorAllocValue(ctx->display, ctx->visual, ctx->colormap,
	    &color, &xft_color))
	{
		XftDrawRect(ctx->xft_draw, &xft_color, x, y, (guint)w, (guint)h);
		XftColorFree(ctx->display, ctx->visual, ctx->colormap, &xft_color);
	}
}

/*
 * x11_fill_rect_fg:
 *
 * Fills a rectangle with the current per-glyph foreground color.
 */
static void
x11_fill_rect_fg(
	GstRenderContext *base,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	GstX11RenderContext *ctx;

	ctx = (GstX11RenderContext *)base;

	if (ctx->fg != NULL) {
		XftDrawRect(ctx->xft_draw, ctx->fg, x, y, (guint)w, (guint)h);
	}
}

/*
 * x11_fill_rect_bg:
 *
 * Fills a rectangle with the current per-glyph background color.
 */
static void
x11_fill_rect_bg(
	GstRenderContext *base,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	GstX11RenderContext *ctx;

	ctx = (GstX11RenderContext *)base;

	if (ctx->bg != NULL) {
		XftDrawRect(ctx->xft_draw, ctx->bg, x, y, (guint)w, (guint)h);
	}
}

/*
 * x11_draw_glyph:
 *
 * Draws a single glyph using the font cache for lookup
 * and XftDrawGlyphFontSpec for rendering.
 */
static void
x11_draw_glyph(
	GstRenderContext *base,
	GstRune           rune,
	GstFontStyle      style,
	gint              px,
	gint              py,
	guint             fg_idx,
	guint             bg_idx,
	guint16           attr
){
	GstX11RenderContext *ctx;
	XftFont *font_out;
	FT_UInt glyph_out;
	XftGlyphFontSpec spec;
	GstFontVariant *fv;
	XftColor *fg_color;

	ctx = (GstX11RenderContext *)base;

	/* Look up glyph in font cache */
	gst_font_cache_lookup_glyph(ctx->font_cache, rune, style,
		&font_out, &glyph_out);

	/* Get ascent for vertical positioning */
	fv = gst_font_cache_get_font(ctx->font_cache, style);

	/* Build spec */
	spec.font = font_out;
	spec.glyph = glyph_out;
	spec.x = (gshort)px;
	spec.y = (gshort)(py + fv->ascent);

	/* Determine foreground color */
	fg_color = (fg_idx < ctx->num_colors)
		? &ctx->colors[fg_idx]
		: &ctx->colors[256]; /* default fg fallback */

	/* Draw */
	XftDrawGlyphFontSpec(ctx->xft_draw, fg_color, &spec, 1);
}

/* ===== Static vtable ===== */

static const GstRenderContextOps x11_ops = {
	x11_fill_rect,
	x11_fill_rect_rgba,
	x11_fill_rect_fg,
	x11_fill_rect_bg,
	x11_draw_glyph
};

/* ===== Public API ===== */

/**
 * gst_x11_render_context_init_ops:
 * @ctx: an X11 render context
 *
 * Initializes the vtable ops pointer and backend type for X11.
 */
void
gst_x11_render_context_init_ops(GstX11RenderContext *ctx)
{
	ctx->base.ops = &x11_ops;
	ctx->base.backend = GST_BACKEND_X11;
}
