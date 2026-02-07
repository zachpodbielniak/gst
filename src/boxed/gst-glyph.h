/*
 * gst-glyph.h - GST Glyph Boxed Type
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A glyph represents a single character cell in the terminal,
 * containing the Unicode code point, attributes, and colors.
 */

#ifndef GST_GLYPH_H
#define GST_GLYPH_H

#include <glib-object.h>
#include "../gst-types.h"
#include "../gst-enums.h"

G_BEGIN_DECLS

#define GST_TYPE_GLYPH (gst_glyph_get_type())

/**
 * GstGlyph:
 * @rune: Unicode code point (UTF-32)
 * @attr: Attribute flags (bold, italic, etc.)
 * @fg: Foreground color index or RGB value
 * @bg: Background color index or RGB value
 *
 * Represents a single character cell in the terminal.
 * This structure contains all information needed to render
 * a single cell: the character, its attributes, and colors.
 */
struct _GstGlyph {
    GstRune     rune;   /* Unicode code point */
    GstGlyphAttr attr;  /* Attribute flags */
    guint32     fg;     /* Foreground color */
    guint32     bg;     /* Background color */
};

/**
 * gst_glyph_get_type:
 *
 * Gets the GType for the GstGlyph boxed type.
 *
 * Returns: the GType
 */
GType gst_glyph_get_type(void) G_GNUC_CONST;

/**
 * gst_glyph_new:
 * @rune: Unicode code point
 * @attr: Attribute flags
 * @fg: Foreground color
 * @bg: Background color
 *
 * Creates a new glyph with the specified values.
 *
 * Returns: (transfer full): a new GstGlyph
 */
GstGlyph *gst_glyph_new(GstRune rune, GstGlyphAttr attr, guint32 fg, guint32 bg);

/**
 * gst_glyph_new_simple:
 * @rune: Unicode code point
 *
 * Creates a new glyph with default attributes and colors.
 *
 * Returns: (transfer full): a new GstGlyph
 */
GstGlyph *gst_glyph_new_simple(GstRune rune);

/**
 * gst_glyph_copy:
 * @glyph: a GstGlyph
 *
 * Creates a copy of the glyph.
 *
 * Returns: (transfer full): a copy of @glyph
 */
GstGlyph *gst_glyph_copy(const GstGlyph *glyph);

/**
 * gst_glyph_free:
 * @glyph: a GstGlyph
 *
 * Frees the memory allocated for the glyph.
 */
void gst_glyph_free(GstGlyph *glyph);

/**
 * gst_glyph_equal:
 * @a: first glyph
 * @b: second glyph
 *
 * Compares two glyphs for equality.
 *
 * Returns: %TRUE if the glyphs are equal
 */
gboolean gst_glyph_equal(const GstGlyph *a, const GstGlyph *b);

/**
 * gst_glyph_is_empty:
 * @glyph: a GstGlyph
 *
 * Checks if the glyph represents an empty/space cell.
 *
 * Returns: %TRUE if the glyph is empty or a space
 */
gboolean gst_glyph_is_empty(const GstGlyph *glyph);

/**
 * gst_glyph_is_wide:
 * @glyph: a GstGlyph
 *
 * Checks if the glyph is wide (double-width character).
 *
 * Returns: %TRUE if the glyph is wide
 */
gboolean gst_glyph_is_wide(const GstGlyph *glyph);

/**
 * gst_glyph_is_dummy:
 * @glyph: a GstGlyph
 *
 * Checks if the glyph is a wide dummy (second cell of wide char).
 *
 * Returns: %TRUE if the glyph is a wide dummy
 */
gboolean gst_glyph_is_dummy(const GstGlyph *glyph);

/**
 * gst_glyph_set_attr:
 * @glyph: a GstGlyph
 * @attr: attribute flags to set
 *
 * Sets attribute flags on the glyph (ORs with existing).
 */
void gst_glyph_set_attr(GstGlyph *glyph, GstGlyphAttr attr);

/**
 * gst_glyph_clear_attr:
 * @glyph: a GstGlyph
 * @attr: attribute flags to clear
 *
 * Clears attribute flags from the glyph.
 */
void gst_glyph_clear_attr(GstGlyph *glyph, GstGlyphAttr attr);

/**
 * gst_glyph_has_attr:
 * @glyph: a GstGlyph
 * @attr: attribute flags to check
 *
 * Checks if the glyph has all the specified attributes.
 *
 * Returns: %TRUE if all specified attributes are set
 */
gboolean gst_glyph_has_attr(const GstGlyph *glyph, GstGlyphAttr attr);

/**
 * gst_glyph_reset:
 * @glyph: a GstGlyph
 *
 * Resets the glyph to an empty space with default attributes.
 */
void gst_glyph_reset(GstGlyph *glyph);

/**
 * GST_GLYPH_INIT:
 *
 * Static initializer for a default (empty) glyph.
 */
#define GST_GLYPH_INIT { ' ', GST_GLYPH_ATTR_NONE, GST_COLOR_DEFAULT_FG, GST_COLOR_DEFAULT_BG }

G_END_DECLS

#endif /* GST_GLYPH_H */
