/*
 * gst-cairo-font-cache.h - Cairo-based font caching for Wayland rendering
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages font loading and glyph lookup using Cairo, cairo-ft, and
 * fontconfig. This is the Wayland counterpart of GstFontCache (Xft-based).
 * Provides the same four font variants (regular, bold, italic, bold+italic)
 * and a dynamic ring cache for fallback fonts.
 */

#ifndef GST_CAIRO_FONT_CACHE_H
#define GST_CAIRO_FONT_CACHE_H

#include <glib-object.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include "../gst-enums.h"
#include "../gst-types.h"

G_BEGIN_DECLS

#define GST_TYPE_CAIRO_FONT_CACHE (gst_cairo_font_cache_get_type())

G_DECLARE_FINAL_TYPE(GstCairoFontCache, gst_cairo_font_cache,
	GST, CAIRO_FONT_CACHE, GObject)

/**
 * gst_cairo_font_cache_new:
 *
 * Creates a new Cairo font cache instance.
 *
 * Returns: (transfer full): A new #GstCairoFontCache
 */
GstCairoFontCache *
gst_cairo_font_cache_new(void);

/**
 * gst_cairo_font_cache_load_fonts:
 * @self: A #GstCairoFontCache
 * @fontstr: fontconfig font specification string
 * @fontsize: desired font size in pixels (0 for pattern default)
 *
 * Loads all four font variants (regular, bold, italic, bold+italic)
 * from the given font specification. Determines character cell
 * dimensions from the regular font metrics.
 *
 * Returns: TRUE on success, FALSE on failure
 */
gboolean
gst_cairo_font_cache_load_fonts(
	GstCairoFontCache   *self,
	const gchar         *fontstr,
	gdouble             fontsize
);

/**
 * gst_cairo_font_cache_unload_fonts:
 * @self: A #GstCairoFontCache
 *
 * Frees all loaded fonts and the fallback ring cache.
 */
void
gst_cairo_font_cache_unload_fonts(GstCairoFontCache *self);

/**
 * gst_cairo_font_cache_clear:
 * @self: A #GstCairoFontCache
 *
 * Clears the fallback font ring cache, freeing all cached
 * fallback fonts. Does not unload the main fonts.
 */
void
gst_cairo_font_cache_clear(GstCairoFontCache *self);

/**
 * gst_cairo_font_cache_get_char_width:
 * @self: A #GstCairoFontCache
 *
 * Gets the character cell width in pixels.
 *
 * Returns: character width
 */
gint
gst_cairo_font_cache_get_char_width(GstCairoFontCache *self);

/**
 * gst_cairo_font_cache_get_char_height:
 * @self: A #GstCairoFontCache
 *
 * Gets the character cell height in pixels.
 *
 * Returns: character height
 */
gint
gst_cairo_font_cache_get_char_height(GstCairoFontCache *self);

/**
 * gst_cairo_font_cache_get_ascent:
 * @self: A #GstCairoFontCache
 *
 * Gets the font ascent in pixels.
 *
 * Returns: font ascent
 */
gint
gst_cairo_font_cache_get_ascent(GstCairoFontCache *self);

/**
 * gst_cairo_font_cache_lookup_glyph:
 * @self: A #GstCairoFontCache
 * @rune: Unicode code point to look up
 * @style: desired font style
 * @font_out: (out) (transfer none): the cairo_scaled_font_t containing the glyph
 * @glyph_out: (out): the glyph index within the font
 *
 * Looks up a glyph in the main font for the given style.
 * If not found, searches the fallback ring cache, then
 * fontconfig for a system font containing the character.
 *
 * Returns: TRUE if the glyph was found
 */
gboolean
gst_cairo_font_cache_lookup_glyph(
	GstCairoFontCache       *self,
	GstRune                 rune,
	GstFontStyle            style,
	cairo_scaled_font_t     **font_out,
	gulong                  *glyph_out
);

/**
 * gst_cairo_font_cache_get_used_font:
 * @self: A #GstCairoFontCache
 *
 * Gets the current font name string.
 *
 * Returns: (transfer none) (nullable): the font name
 */
const gchar *
gst_cairo_font_cache_get_used_font(GstCairoFontCache *self);

/**
 * gst_cairo_font_cache_get_font_size:
 * @self: A #GstCairoFontCache
 *
 * Gets the current font size in pixels.
 *
 * Returns: font size
 */
gdouble
gst_cairo_font_cache_get_font_size(GstCairoFontCache *self);

/**
 * gst_cairo_font_cache_get_default_font_size:
 * @self: A #GstCairoFontCache
 *
 * Gets the default font size (before any zoom).
 *
 * Returns: default font size
 */
gdouble
gst_cairo_font_cache_get_default_font_size(GstCairoFontCache *self);

G_END_DECLS

#endif /* GST_CAIRO_FONT_CACHE_H */
