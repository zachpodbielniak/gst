/*
 * gst-wayland-render-context.h - Wayland render context extending abstract base
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Extends GstRenderContext with Cairo drawing resources for the
 * Wayland backend. The base struct MUST be the first member so that
 * casting between GstRenderContext* and GstWaylandRenderContext* is safe.
 */

#ifndef GST_WAYLAND_RENDER_CONTEXT_H
#define GST_WAYLAND_RENDER_CONTEXT_H

#include "gst-render-context.h"
#include "gst-cairo-font-cache.h"
#include <cairo.h>

G_BEGIN_DECLS

/**
 * GstWaylandRenderContext:
 * @base: abstract base (MUST be first member)
 * @cr: cairo drawing context
 * @surface: cairo image surface (backed by shared memory)
 * @font_cache: cairo font cache for glyph lookup
 * @colors: loaded color palette as GstColor (RGBA) values
 * @num_colors: number of entries in colors array
 * @fg: per-glyph foreground color (GstColor RGBA)
 * @bg: per-glyph background color (GstColor RGBA)
 *
 * Wayland-specific render context. Extends the abstract base
 * with Cairo drawing resources.
 */
typedef struct
{
	GstRenderContext     base;       /* MUST be first member */

	/* Cairo drawing */
	cairo_t             *cr;
	cairo_surface_t     *surface;

	/* Fonts */
	GstCairoFontCache   *font_cache;

	/* Colors */
	GstColor            *colors;
	gsize                num_colors;

	/* Per-glyph context (populated during draw_line dispatch only) */
	GstColor             fg;
	GstColor             bg;
} GstWaylandRenderContext;

/**
 * gst_wayland_render_context_init_ops:
 * @ctx: a Wayland render context
 *
 * Initializes the vtable ops pointer for Wayland backend operations.
 * Must be called once after populating the Wayland-specific fields.
 */
void
gst_wayland_render_context_init_ops(GstWaylandRenderContext *ctx);

G_END_DECLS

#endif /* GST_WAYLAND_RENDER_CONTEXT_H */
