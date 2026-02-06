/*
 * gst-font-cache.h - Font caching for terminal rendering
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages Xft font loading and a fallback ring cache.
 * Ports st's Font struct, xloadfont(), xloadfonts(),
 * xunloadfont(), and the frc[] (font ring cache) array.
 */

#ifndef GST_FONT_CACHE_H
#define GST_FONT_CACHE_H

#include <glib-object.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>
#include "../gst-enums.h"
#include "../gst-types.h"

G_BEGIN_DECLS

#define GST_TYPE_FONT_CACHE (gst_font_cache_get_type())

G_DECLARE_FINAL_TYPE(GstFontCache, gst_font_cache, GST, FONT_CACHE, GObject)

GType
gst_font_cache_get_type(void) G_GNUC_CONST;

/**
 * GstFontVariant:
 * @height: total font height (ascent + descent)
 * @width: character cell width
 * @ascent: ascent above baseline
 * @descent: descent below baseline
 * @badslant: TRUE if font could not match requested slant
 * @badweight: TRUE if font could not match requested weight
 * @lbearing: left bearing (always 0)
 * @rbearing: right bearing (max_advance_width)
 * @match: the matched XftFont
 * @set: fontconfig font set for fallback searching
 * @pattern: fontconfig pattern used for matching
 *
 * Holds a single font variant (regular, bold, italic, etc.)
 * along with its metrics. Ports st's Font struct.
 */
typedef struct {
	gint height;
	gint width;
	gint ascent;
	gint descent;
	gint badslant;
	gint badweight;
	gshort lbearing;
	gshort rbearing;
	XftFont *match;
	FcFontSet *set;
	FcPattern *pattern;
} GstFontVariant;

/**
 * gst_font_cache_new:
 *
 * Creates a new font cache instance.
 *
 * Returns: (transfer full): A new #GstFontCache
 */
GstFontCache *
gst_font_cache_new(void);

/**
 * gst_font_cache_clear:
 * @self: A #GstFontCache
 *
 * Clears the fallback font ring cache, freeing all
 * cached fallback fonts. Does not unload the main fonts.
 */
void
gst_font_cache_clear(GstFontCache *self);

/**
 * gst_font_cache_load_fonts:
 * @self: A #GstFontCache
 * @display: (not nullable): X11 display connection
 * @screen: X11 screen number
 * @fontstr: fontconfig font specification string
 * @fontsize: desired font size in pixels (0 for default)
 *
 * Loads all four font variants (regular, bold, italic, bold+italic)
 * from the given font specification. Sets character cell dimensions.
 * Ports st's xloadfonts().
 *
 * Returns: TRUE on success, FALSE on failure
 */
gboolean
gst_font_cache_load_fonts(
	GstFontCache    *self,
	Display         *display,
	gint            screen,
	const gchar     *fontstr,
	gdouble         fontsize
);

/**
 * gst_font_cache_unload_fonts:
 * @self: A #GstFontCache
 *
 * Frees all loaded fonts and the fallback ring cache.
 * Ports st's xunloadfonts().
 */
void
gst_font_cache_unload_fonts(GstFontCache *self);

/**
 * gst_font_cache_get_font:
 * @self: A #GstFontCache
 * @style: the font style variant to retrieve
 *
 * Gets a font variant by style.
 *
 * Returns: (transfer none) (nullable): the #GstFontVariant, or NULL
 */
GstFontVariant *
gst_font_cache_get_font(
	GstFontCache    *self,
	GstFontStyle    style
);

/**
 * gst_font_cache_get_char_width:
 * @self: A #GstFontCache
 *
 * Gets the character cell width in pixels.
 *
 * Returns: character width
 */
gint
gst_font_cache_get_char_width(GstFontCache *self);

/**
 * gst_font_cache_get_char_height:
 * @self: A #GstFontCache
 *
 * Gets the character cell height in pixels.
 *
 * Returns: character height
 */
gint
gst_font_cache_get_char_height(GstFontCache *self);

/**
 * gst_font_cache_lookup_glyph:
 * @self: A #GstFontCache
 * @rune: Unicode code point to look up
 * @style: desired font style
 * @font_out: (out) (transfer none): the XftFont containing the glyph
 * @glyph_out: (out): the glyph index within the font
 *
 * Looks up a glyph in the main font for the given style.
 * If not found, searches the fallback ring cache, then
 * fontconfig for a system font containing the character.
 * Ports st's frc[] lookup logic from xmakeglyphfontspecs().
 *
 * Returns: TRUE if the glyph was found
 */
gboolean
gst_font_cache_lookup_glyph(
	GstFontCache    *self,
	GstRune         rune,
	GstFontStyle    style,
	XftFont         **font_out,
	FT_UInt         *glyph_out
);

/**
 * gst_font_cache_get_used_font:
 * @self: A #GstFontCache
 *
 * Gets the current font name string.
 *
 * Returns: (transfer none) (nullable): the font name
 */
const gchar *
gst_font_cache_get_used_font(GstFontCache *self);

/**
 * gst_font_cache_get_font_size:
 * @self: A #GstFontCache
 *
 * Gets the current font size in pixels.
 *
 * Returns: font size
 */
gdouble
gst_font_cache_get_font_size(GstFontCache *self);

/**
 * gst_font_cache_get_default_font_size:
 * @self: A #GstFontCache
 *
 * Gets the default font size (before any zoom).
 *
 * Returns: default font size
 */
gdouble
gst_font_cache_get_default_font_size(GstFontCache *self);

G_END_DECLS

#endif /* GST_FONT_CACHE_H */
