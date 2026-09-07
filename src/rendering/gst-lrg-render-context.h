/*
 * gst-lrg-render-context.h - LRG render context extending abstract base
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Extends GstRenderContext with graylib (raylib) drawing resources for
 * the libregnum/LRG backend. The base struct MUST be the first member so
 * that casting between GstRenderContext* and GstLrgRenderContext* is safe.
 */

#ifndef GST_LRG_RENDER_CONTEXT_H
#define GST_LRG_RENDER_CONTEXT_H

#include <graylib.h>
#include "gst-render-context.h"
#include "gst-grl-font-cache.h"

G_BEGIN_DECLS

/**
 * GstLrgRenderContext:
 * @base: abstract base (MUST be first member)
 * @win: graylib window being drawn into (immediate mode)
 * @font_cache: graylib font cache for glyph lookup
 * @font_size: font size in pixels for glyph draws
 * @colors: loaded color palette as GstColor (RGBA) values
 * @num_colors: number of entries in @colors
 * @fg: per-glyph foreground color (GstColor RGBA)
 * @bg: per-glyph background color (GstColor RGBA)
 * @frame_textures: (element-type GrlTexture): borrowed renderer-owned textures retained until the frame batch is flushed
 *
 * LRG-specific render context. Drawing happens in immediate mode between
 * grl_window_begin_drawing() and grl_window_swap_buffers(), so the context
 * only needs the window plus font/colour state.
 */
typedef struct
{
	GstRenderContext     base;       /* MUST be first member */

	GrlWindow           *win;
	GstGrlFontCache     *font_cache;
	gdouble              font_size;

	GstColor            *colors;
	gsize                num_colors;

	GstColor             fg;
	GstColor             bg;
	GPtrArray           *frame_textures; /* Borrowed; retained until batch flush. */
} GstLrgRenderContext;

/**
 * gst_lrg_render_context_init_ops:
 * @ctx: an LRG render context
 *
 * Initializes the vtable ops pointer for LRG backend operations.
 * Must be called once after populating the LRG-specific fields.
 */
void
gst_lrg_render_context_init_ops(GstLrgRenderContext *ctx);

G_END_DECLS

#endif /* GST_LRG_RENDER_CONTEXT_H */
