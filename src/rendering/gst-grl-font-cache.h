/*
 * gst-grl-font-cache.h - graylib glyph atlas for the LRG backend
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Font handling for the libregnum/LRG backend. To render glyphs identically
 * to the X11 (Xft) and Wayland (Cairo) backends, this wraps GstCairoFontCache
 * (the same cairo-ft path used by the Wayland renderer) for font matching,
 * glyph lookup and cell metrics, and maintains a per-glyph atlas of graylib
 * textures: each glyph is rasterized once with cairo (white + alpha coverage)
 * and blitted tinted with the foreground color. This mirrors cmacs's lrgterm
 * (cmacs-lrgfont.c), so on-screen text matches the non-LRG backends exactly.
 *
 * NOTE: graylib textures need an active GL context, so glyph atlas entries are
 * created lazily during drawing (inside a frame), not at load time.
 */

#ifndef GST_GRL_FONT_CACHE_H
#define GST_GRL_FONT_CACHE_H

#include <glib-object.h>
#include <graylib.h>
#include "gst-cairo-font-cache.h"
#include "../gst-enums.h"
#include "../gst-types.h"

G_BEGIN_DECLS

#define GST_TYPE_GRL_FONT_CACHE (gst_grl_font_cache_get_type())

G_DECLARE_FINAL_TYPE(GstGrlFontCache, gst_grl_font_cache,
	GST, GRL_FONT_CACHE, GObject)

/**
 * gst_grl_font_cache_new:
 *
 * Creates a new LRG glyph atlas (wrapping a #GstCairoFontCache).
 *
 * Returns: (transfer full): A new #GstGrlFontCache
 */
GstGrlFontCache *
gst_grl_font_cache_new(void);

/**
 * gst_grl_font_cache_load_fonts:
 * @self: A #GstGrlFontCache
 * @fontstr: fontconfig font specification string
 * @fontsize: desired font size in pixels (0 for pattern default)
 *
 * Loads the font via the wrapped cairo-ft cache and clears the glyph atlas.
 *
 * Returns: TRUE on success, FALSE on failure
 */
gboolean
gst_grl_font_cache_load_fonts(
	GstGrlFontCache     *self,
	const gchar         *fontstr,
	gdouble             fontsize
);

/**
 * gst_grl_font_cache_unload_fonts:
 * @self: A #GstGrlFontCache
 *
 * Clears the glyph atlas (freeing textures) and unloads the cairo fonts.
 * Must be called while the GL context is still alive.
 */
void
gst_grl_font_cache_unload_fonts(GstGrlFontCache *self);

/**
 * gst_grl_font_cache_draw_glyph:
 * @self: A #GstGrlFontCache
 * @rune: Unicode codepoint to draw
 * @style: font style variant
 * @cell_x: cell left edge in pixels
 * @cell_y: cell top edge in pixels
 * @fg: foreground color (GstColor RGBA) to tint the glyph
 *
 * Draws a single glyph at the given cell, rasterizing and caching it as a
 * graylib texture on first use. Must be called inside a frame (between
 * grl_window_begin_drawing() and grl_window_swap_buffers()).
 */
void
gst_grl_font_cache_draw_glyph(
	GstGrlFontCache     *self,
	GstRune             rune,
	GstFontStyle        style,
	gint                cell_x,
	gint                cell_y,
	GstColor            fg
);

/**
 * gst_grl_font_cache_draw_glyph_index:
 * @self: a #GstGrlFontCache
 * @scaled: (transfer none): the exact font used for shaping
 * @glyph_index: font-internal glyph index
 * @cell_x: horizontal pen position in pixels
 * @cell_y: cell top in pixels, including shaping offset
 * @fg: foreground color
 *
 * Draws a shaped glyph through the same atlas as scalar glyphs. Requires
 * an active GL frame. The atlas retains the font until it is cleared.
 */
void
gst_grl_font_cache_draw_glyph_index(
	GstGrlFontCache     *self,
	cairo_scaled_font_t *scaled,
	gulong              glyph_index,
	gint                cell_x,
	gint                cell_y,
	GstColor            fg
);

/**
 * gst_grl_font_cache_get_char_width:
 * @self: A #GstGrlFontCache
 *
 * Returns: character cell width in pixels
 */
gint
gst_grl_font_cache_get_char_width(GstGrlFontCache *self);

/**
 * gst_grl_font_cache_get_char_height:
 * @self: A #GstGrlFontCache
 *
 * Returns: character cell height in pixels
 */
gint
gst_grl_font_cache_get_char_height(GstGrlFontCache *self);

/**
 * gst_grl_font_cache_get_ascent:
 * @self: A #GstGrlFontCache
 *
 * Returns: font ascent in pixels (glyph baseline offset within the cell)
 */
gint
gst_grl_font_cache_get_ascent(GstGrlFontCache *self);

/**
 * gst_grl_font_cache_get_font_size:
 * @self: A #GstGrlFontCache
 *
 * Returns: the current font size in pixels
 */
gdouble
gst_grl_font_cache_get_font_size(GstGrlFontCache *self);

/**
 * gst_grl_font_cache_get_default_font_size:
 * @self: A #GstGrlFontCache
 *
 * Returns: the default (un-zoomed) font size in pixels
 */
gdouble
gst_grl_font_cache_get_default_font_size(GstGrlFontCache *self);

/**
 * gst_grl_font_cache_get_used_font:
 * @self: A #GstGrlFontCache
 *
 * Returns: (transfer none) (nullable): the font specification string
 */
const gchar *
gst_grl_font_cache_get_used_font(GstGrlFontCache *self);

/**
 * gst_grl_font_cache_get_cairo_cache:
 * @self: A #GstGrlFontCache
 *
 * Returns: (transfer none): the wrapped #GstCairoFontCache (e.g. for the
 *   module font cache hook)
 */
GstCairoFontCache *
gst_grl_font_cache_get_cairo_cache(GstGrlFontCache *self);

G_END_DECLS

#endif /* GST_GRL_FONT_CACHE_H */
