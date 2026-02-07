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
