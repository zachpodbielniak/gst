/*
 * gst-render-context.c - Abstract render context helpers
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Non-inline versions of render context dispatch functions.
 * These are provided for GObject Introspection and module symbol
 * resolution where inline functions may not be available.
 */

#include "gst-render-context.h"
#include "gst-x11-render-context.h"
#include "../core/gst-line.h"
#include "../boxed/gst-glyph.h"
#include "../module/gst-module-manager.h"
#include <string.h>
#ifdef GST_HAVE_WAYLAND
#include "gst-wayland-render-context.h"
#endif
#ifdef GST_HAVE_LRG_BACKEND
#include "gst-lrg-render-context.h"
#endif

gboolean
gst_render_context_draw_cluster(GstRenderContext *ctx, const gchar *text,
	GstFontStyle style, gint x, gint y, gint width)
{
	if (width <= 0 || ctx->ch <= 0)
		return FALSE;
	if (ctx->backend == GST_BACKEND_X11) {
		GstX11RenderContext *xc;
		GstFontVariant *font;
		XRectangle clip;
		gboolean drawn;

		xc = (GstX11RenderContext *)ctx;
		font = gst_font_cache_get_font(xc->font_cache, style);
		if (font == NULL)
			return FALSE;
		clip.x = (short)x;
		clip.y = (short)y;
		clip.width = (unsigned short)width;
		clip.height = (unsigned short)ctx->ch;
		XftDrawSetClipRectangles(xc->xft_draw, 0, 0, &clip, 1);
		drawn = gst_font_cache_draw_cluster(xc->font_cache, xc->xft_draw,
			xc->fg, text, style, x, y + font->ascent);
		XftDrawSetClip(xc->xft_draw, NULL);
		return drawn;
	}
#ifdef GST_HAVE_WAYLAND
	if (ctx->backend == GST_BACKEND_WAYLAND) {
		GstWaylandRenderContext *wc;
		gboolean drawn;

		wc = (GstWaylandRenderContext *)ctx;
		cairo_save(wc->cr);
		cairo_rectangle(wc->cr, x, y, width, ctx->ch);
		cairo_clip(wc->cr);
		cairo_set_source_rgb(wc->cr, GST_COLOR_R(wc->fg) / 255.0,
			GST_COLOR_G(wc->fg) / 255.0, GST_COLOR_B(wc->fg) / 255.0);
		drawn = gst_cairo_font_cache_draw_cluster(wc->font_cache, wc->cr,
			text, style, x, y + gst_cairo_font_cache_get_ascent(wc->font_cache));
		cairo_restore(wc->cr);
		return drawn;
	}
#endif
#ifdef GST_HAVE_LRG_BACKEND
	if (ctx->backend == GST_BACKEND_LRG) {
		GstLrgRenderContext *lc;
		GstCairoFontCache *cache;
		cairo_surface_t *surface;
		cairo_t *cr;
		const guint8 *data;
		g_autofree guint8 *rgba = NULL;
		gint stride, row, col;
		gboolean drawn;

		lc = (GstLrgRenderContext *)ctx;
		if (lc->font_cache == NULL || lc->frame_textures == NULL || width > G_MAXINT / 4)
			return FALSE;
		cache = gst_grl_font_cache_get_cairo_cache(lc->font_cache);
		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, ctx->ch);
		cr = cairo_create(surface);
		cairo_set_source_rgb(cr, GST_COLOR_R(lc->fg) / 255.0,
			GST_COLOR_G(lc->fg) / 255.0, GST_COLOR_B(lc->fg) / 255.0);
		drawn = gst_cairo_font_cache_draw_cluster(cache, cr, text, style,
			0, gst_cairo_font_cache_get_ascent(cache));
		if (!drawn || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
			cairo_destroy(cr);
			cairo_surface_destroy(surface);
			return FALSE;
		}
		/* Convert native premultiplied ARGB to straight RGBA, retaining emoji color. */
		cairo_surface_flush(surface);
		data = cairo_image_surface_get_data(surface);
		stride = cairo_image_surface_get_stride(surface);
		rgba = g_malloc_n((gsize)ctx->ch, (gsize)width * 4);
		for (row = 0; row < ctx->ch; row++) {
			for (col = 0; col < width; col++) {
				guint32 pixel;
				guint a;
				guint8 *out;

				memcpy(&pixel, data + (gsize)row * stride + (gsize)col * 4, 4);
				a = pixel >> 24;
				out = rgba + ((gsize)row * width + col) * 4;
				out[0] = a == 0 ? 0 : (guint8)MIN(255U, (((pixel >> 16) & 255) * 255 + a / 2) / a);
				out[1] = a == 0 ? 0 : (guint8)MIN(255U, (((pixel >> 8) & 255) * 255 + a / 2) / a);
				out[2] = a == 0 ? 0 : (guint8)MIN(255U, ((pixel & 255) * 255 + a / 2) / a);
				out[3] = (guint8)a;
			}
		}
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		gst_render_context_draw_image(ctx, rgba, width, ctx->ch, width * 4,
			x, y, width, ctx->ch);
		return TRUE;
	}
#endif
	return FALSE;
}

void
gst_render_context_draw_cell(GstRenderContext *ctx, GstLine *line,
	gint col, gint cols, gint x, gint y)
{
	union {
		GstRenderContext base;
		GstX11RenderContext x11;
#ifdef GST_HAVE_WAYLAND
		GstWaylandRenderContext wl;
#endif
#ifdef GST_HAVE_LRG_BACKEND
		GstLrgRenderContext lrg;
#endif
	} local;
	const GstGlyph *g;
	GstColor colors[2];
	guint32 indices[2];
	XftColor xcolors[2];
	gboolean allocated[2] = { FALSE, FALSE };
	GstFontStyle style;
	gint i, width;
	gboolean handled;

	g = gst_line_get_glyph_const(line, col);
	if (g == NULL || col >= cols || gst_glyph_is_dummy(g))
		return;
	indices[0] = g->fg;
	indices[1] = g->bg;
	if ((g->attr & GST_GLYPH_ATTR_BOLD) && !(g->attr & GST_GLYPH_ATTR_FAINT) && indices[0] < 8)
		indices[0] += 8;
	/* Resolve palette/truecolor before transformers; raw IDs remain in the line. */
	for (i = 0; i < 2; i++) {
		guint32 idx = indices[i];

		colors[i] = i == 0 ? GST_COLOR_RGB(255, 255, 255) : GST_COLOR_RGB(0, 0, 0);
		switch (ctx->backend) {
		case GST_BACKEND_X11:
			local.x11 = *(GstX11RenderContext *)ctx;
			if (idx < local.x11.num_colors) {
				XRenderColor c = local.x11.colors[idx].color;
				colors[i] = GST_COLOR_RGB(c.red >> 8, c.green >> 8, c.blue >> 8);
			}
			break;
#ifdef GST_HAVE_WAYLAND
		case GST_BACKEND_WAYLAND:
			local.wl = *(GstWaylandRenderContext *)ctx;
			if (idx < local.wl.num_colors)
				colors[i] = local.wl.colors[idx];
			break;
#endif
#ifdef GST_HAVE_LRG_BACKEND
		case GST_BACKEND_LRG:
			local.lrg = *(GstLrgRenderContext *)ctx;
			if (idx < local.lrg.num_colors)
				colors[i] = local.lrg.colors[idx];
			break;
#endif
		default:
			return;
		}
		if (GST_IS_TRUECOLOR(idx))
			colors[i] = GST_COLOR_RGB(GST_TRUERED(idx) >> 8,
				GST_TRUEGREEN(idx) >> 8, GST_TRUEBLUE(idx) >> 8);
	}
	if ((g->attr & GST_GLYPH_ATTR_FAINT) && !(g->attr & GST_GLYPH_ATTR_BOLD))
		colors[0] = GST_COLOR_RGB(GST_COLOR_R(colors[0]) / 2,
			GST_COLOR_G(colors[0]) / 2, GST_COLOR_B(colors[0]) / 2);
	if (g->attr & GST_GLYPH_ATTR_REVERSE) {
		GstColor tmp = colors[0];
		colors[0] = colors[1];
		colors[1] = tmp;
	}
	if ((g->attr & GST_GLYPH_ATTR_INVISIBLE) ||
	    ((g->attr & GST_GLYPH_ATTR_BLINK) && (ctx->win_mode & GST_WIN_MODE_BLINK)))
		colors[0] = colors[1];
	if (ctx->backend == GST_BACKEND_X11) {
		for (i = 0; i < 2; i++) {
			XRenderColor c;

			c.red = GST_COLOR_R(colors[i]) * 257;
			c.green = GST_COLOR_G(colors[i]) * 257;
			c.blue = GST_COLOR_B(colors[i]) * 257;
			c.alpha = 65535;
			allocated[i] = XftColorAllocValue(local.x11.display, local.x11.visual,
				local.x11.colormap, &c, &xcolors[i]);
			if (!allocated[i])
				goto cleanup;
		}
		local.x11.fg = &xcolors[0];
		local.x11.bg = &xcolors[1];
	}
#ifdef GST_HAVE_WAYLAND
	if (ctx->backend == GST_BACKEND_WAYLAND) {
		local.wl.fg = colors[0];
		local.wl.bg = colors[1];
	}
#endif
#ifdef GST_HAVE_LRG_BACKEND
	if (ctx->backend == GST_BACKEND_LRG) {
		local.lrg.fg = colors[0];
		local.lrg.bg = colors[1];
	}
#endif
	local.base.current_line = line;
	local.base.current_col = col;
	local.base.current_cols = MIN(cols, line->len);
	local.base.glyph_attr = (guint16)g->attr;
	width = ctx->cw * MIN(gst_glyph_is_wide(g) ? 2 : 1, cols - col);
	style = (g->attr & GST_GLYPH_ATTR_BOLD)
		? ((g->attr & GST_GLYPH_ATTR_ITALIC) ? GST_FONT_STYLE_BOLD_ITALIC : GST_FONT_STYLE_BOLD)
		: ((g->attr & GST_GLYPH_ATTR_ITALIC) ? GST_FONT_STYLE_ITALIC : GST_FONT_STYLE_NORMAL);
	handled = FALSE;
	if (g->rune >= 0x20 && (g->cluster == NULL || g->rune == 0x10EEEE))
		handled = gst_module_manager_dispatch_glyph_transform(gst_module_manager_get_default(),
			g->rune, &local.base, x, y, ctx->cw, ctx->ch);
	if (!handled) {
		gst_render_context_fill_rect_bg(&local.base, x, y, width, ctx->ch);
		if (g->cluster == NULL || !gst_render_context_draw_cluster(&local.base,
		    g->cluster, style, x, y, width)) {
			/* X11's scalar op uses palette colors rather than its context fg. */
			if (ctx->backend == GST_BACKEND_X11) {
				local.x11.colors = xcolors;
				local.x11.num_colors = 2;
			}
			gst_render_context_draw_glyph(&local.base, g->rune, style, x, y,
				ctx->backend == GST_BACKEND_X11 ? 0 : G_MAXUINT,
				G_MAXUINT, (guint16)g->attr);
		}
	}
cleanup:
	for (i = 0; i < 2; i++) {
		if (allocated[i])
			XftColorFree(local.x11.display, local.x11.visual, local.x11.colormap, &xcolors[i]);
	}
}

/**
 * SECTION:gst-render-context
 * @title: GstRenderContext
 * @short_description: Abstract render context with vtable dispatch
 *
 * #GstRenderContext is a plain C struct with a virtual function table
 * for backend-agnostic drawing. Modules use the
 * gst_render_context_*() helpers instead of X11 or Cairo APIs.
 *
 * Backend-specific contexts (GstX11RenderContext, GstWaylandRenderContext)
 * embed this struct as their first member and provide vtable implementations.
 */

/*
 * Non-inline wrappers for GIR / module symbol resolution.
 * These simply call through to the inline versions in the header.
 * They are compiled into the shared library so that modules linked
 * against libgst.so can resolve these symbols even if the compiler
 * did not inline them.
 */

void
gst_render_context_fill_rect_non_inline(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint             color_idx
){
	gst_render_context_fill_rect(ctx, x, y, w, h, color_idx);
}

void
gst_render_context_fill_rect_rgba_non_inline(
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
	gst_render_context_fill_rect_rgba(ctx, x, y, w, h, r, g, b, a);
}

void
gst_render_context_fill_rect_fg_non_inline(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	gst_render_context_fill_rect_fg(ctx, x, y, w, h);
}

void
gst_render_context_fill_rect_bg_non_inline(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	gst_render_context_fill_rect_bg(ctx, x, y, w, h);
}

void
gst_render_context_draw_glyph_non_inline(
	GstRenderContext *ctx,
	GstRune           rune,
	GstFontStyle      style,
	gint              px,
	gint              py,
	guint             fg_idx,
	guint             bg_idx,
	guint16           attr
){
	gst_render_context_draw_glyph(ctx, rune, style, px, py,
		fg_idx, bg_idx, attr);
}
