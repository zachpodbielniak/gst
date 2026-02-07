/*
 * gst-x11-render-context.c - X11 vtable implementations
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the GstRenderContextOps vtable for the X11 backend.
 * Wraps Xft drawing calls (XftDrawRect, XftDrawGlyphFontSpec)
 * behind the abstract render context interface.
 */

#include "gst-x11-render-context.h"
#include <string.h>
#include <X11/extensions/Xrender.h>

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

/*
 * x11_draw_image:
 *
 * Draws an RGBA image using XRender compositing.
 * Converts RGBA row data to an XImage (BGRA byte order for
 * XRender's 32-bit visual), creates a temporary Pixmap + Picture,
 * then composites onto the drawable with PictOpOver for alpha blending.
 */
static void
x11_draw_image(
	GstRenderContext *base,
	const guint8     *data,
	gint              src_w,
	gint              src_h,
	gint              src_stride,
	gint              dst_x,
	gint              dst_y,
	gint              dst_w,
	gint              dst_h
){
	GstX11RenderContext *ctx;
	XImage *ximg;
	Pixmap pix;
	Picture pic_src;
	Picture pic_dst;
	XRenderPictFormat *fmt;
	XRenderPictureAttributes pa;
	XTransform xform;
	guint8 *bgra;
	gint row;
	gint col;
	gint idx_src;
	gint idx_dst;

	ctx = (GstX11RenderContext *)base;

	if (data == NULL || src_w <= 0 || src_h <= 0) {
		return;
	}

	/* Find a 32-bit ARGB format for XRender */
	fmt = XRenderFindStandardFormat(ctx->display, PictStandardARGB32);
	if (fmt == NULL) {
		return;
	}

	/*
	 * Convert RGBA to pre-multiplied BGRA (XRender expects pre-multiplied alpha
	 * in ARGB32 format, which is BGRA byte order on little-endian).
	 */
	bgra = (guint8 *)g_malloc((gsize)src_w * (gsize)src_h * 4);
	for (row = 0; row < src_h; row++) {
		for (col = 0; col < src_w; col++) {
			guint8 r, g, b, a;
			guint16 pr, pg, pb;

			idx_src = row * src_stride + col * 4;
			idx_dst = (row * src_w + col) * 4;

			r = data[idx_src + 0];
			g = data[idx_src + 1];
			b = data[idx_src + 2];
			a = data[idx_src + 3];

			/* Pre-multiply RGB by alpha */
			pr = (guint16)((guint16)r * a + 127) / 255;
			pg = (guint16)((guint16)g * a + 127) / 255;
			pb = (guint16)((guint16)b * a + 127) / 255;

			bgra[idx_dst + 0] = (guint8)pb;   /* B */
			bgra[idx_dst + 1] = (guint8)pg;   /* G */
			bgra[idx_dst + 2] = (guint8)pr;   /* R */
			bgra[idx_dst + 3] = a;             /* A */
		}
	}

	/* Create an XImage from the BGRA data */
	ximg = XCreateImage(ctx->display, ctx->visual, 32, ZPixmap, 0,
		(char *)bgra, (guint)src_w, (guint)src_h, 32, src_w * 4);
	if (ximg == NULL) {
		g_free(bgra);
		return;
	}

	/* Create a temporary pixmap and put the image into it */
	pix = XCreatePixmap(ctx->display, ctx->window,
		(guint)src_w, (guint)src_h, 32);

	XPutImage(ctx->display, pix, ctx->gc, ximg,
		0, 0, 0, 0, (guint)src_w, (guint)src_h);

	/* Create XRender pictures */
	memset(&pa, 0, sizeof(pa));
	pic_src = XRenderCreatePicture(ctx->display, pix, fmt, 0, &pa);

	/* Apply scaling transform if src and dst sizes differ */
	if (dst_w != src_w || dst_h != src_h) {
		memset(&xform, 0, sizeof(xform));
		xform.matrix[0][0] = XDoubleToFixed((double)src_w / (double)dst_w);
		xform.matrix[1][1] = XDoubleToFixed((double)src_h / (double)dst_h);
		xform.matrix[2][2] = XDoubleToFixed(1.0);
		XRenderSetPictureTransform(ctx->display, pic_src, &xform);
		XRenderSetPictureFilter(ctx->display, pic_src, "bilinear", NULL, 0);
	}

	/* Get the picture for the drawable */
	fmt = XRenderFindVisualFormat(ctx->display, ctx->visual);
	if (fmt != NULL) {
		pic_dst = XRenderCreatePicture(ctx->display, ctx->drawable,
			fmt, 0, &pa);

		/* Composite with alpha blending */
		XRenderComposite(ctx->display, PictOpOver,
			pic_src, None, pic_dst,
			0, 0,   /* src origin */
			0, 0,   /* mask origin */
			dst_x, dst_y,
			(guint)dst_w, (guint)dst_h);

		XRenderFreePicture(ctx->display, pic_dst);
	}

	XRenderFreePicture(ctx->display, pic_src);
	XFreePixmap(ctx->display, pix);

	/* XDestroyImage frees both the XImage struct and the data pointer */
	ximg->data = NULL; /* prevent double-free, we free bgra ourselves */
	XDestroyImage(ximg);
	g_free(bgra);
}

/* ===== Static vtable ===== */

static const GstRenderContextOps x11_ops = {
	x11_fill_rect,
	x11_fill_rect_rgba,
	x11_fill_rect_fg,
	x11_fill_rect_bg,
	x11_draw_glyph,
	x11_draw_image
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
