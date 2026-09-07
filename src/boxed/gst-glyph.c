/*
 * gst-glyph.c - GST Glyph Boxed Type Implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the GstGlyph boxed type representing
 * a single character cell in the terminal.
 */

#include "gst-glyph.h"
#include <string.h>

G_DEFINE_BOXED_TYPE(GstGlyph, gst_glyph, gst_glyph_copy, gst_glyph_free)

/**
 * gst_glyph_new:
 * @rune: Unicode code point
 * @attr: Attribute flags
 * @fg: Foreground color
 * @bg: Background color
 *
 * Creates a new glyph with the specified values.
 * The glyph must be freed with gst_glyph_free() when no longer needed.
 *
 * Returns: (transfer full): a newly allocated GstGlyph
 */
GstGlyph *
gst_glyph_new(
    GstRune         rune,
    GstGlyphAttr    attr,
    guint32         fg,
    guint32         bg
){
    GstGlyph *glyph;

    glyph = g_slice_new(GstGlyph);
    glyph->rune = rune;
    glyph->attr = attr;
    glyph->fg = fg;
    glyph->bg = bg;
	glyph->cluster = NULL;
	glyph->cluster_len = 0;
	glyph->cluster_capacity = 0;

    return glyph;
}

/**
 * gst_glyph_new_simple:
 * @rune: Unicode code point
 *
 * Creates a new glyph with default attributes and colors.
 * This is a convenience function for creating basic glyphs.
 *
 * Returns: (transfer full): a newly allocated GstGlyph
 */
GstGlyph *
gst_glyph_new_simple(GstRune rune)
{
    return gst_glyph_new(rune, GST_GLYPH_ATTR_NONE,
                         GST_COLOR_DEFAULT_FG, GST_COLOR_DEFAULT_BG);
}

/**
 * gst_glyph_copy:
 * @glyph: a GstGlyph to copy
 *
 * Creates a deep copy of the glyph.
 *
 * Returns: (transfer full): a copy of @glyph
 */
GstGlyph *
gst_glyph_copy(const GstGlyph *glyph)
{
    GstGlyph *copy;

    g_return_val_if_fail(glyph != NULL, NULL);

    copy = g_slice_new(GstGlyph);
    copy->rune = glyph->rune;
    copy->attr = glyph->attr;
    copy->fg = glyph->fg;
    copy->bg = glyph->bg;
	copy->cluster = g_strdup(glyph->cluster);
	copy->cluster_len = copy->cluster != NULL ? strlen(copy->cluster) : 0;
	copy->cluster_capacity = copy->cluster != NULL ? copy->cluster_len + 1 : 0;

    return copy;
}

/**
 * gst_glyph_free:
 * @glyph: a GstGlyph to free
 *
 * Frees the memory allocated for the glyph.
 */
void
gst_glyph_free(GstGlyph *glyph)
{
    if (glyph != NULL) {
		gst_glyph_clear(glyph);
        g_slice_free(GstGlyph, glyph);
    }
}

/**
 * gst_glyph_equal:
 * @a: first glyph
 * @b: second glyph
 *
 * Compares two glyphs for equality. Two glyphs are equal if
 * they have the same rune, attributes, and colors.
 *
 * Returns: %TRUE if the glyphs are equal
 */
gboolean
gst_glyph_equal(
    const GstGlyph *a,
    const GstGlyph *b
){
    if (a == b) {
        return TRUE;
    }

    if (a == NULL || b == NULL) {
        return FALSE;
    }

    return (a->rune == b->rune &&
            a->attr == b->attr &&
            a->fg == b->fg &&
            a->bg == b->bg &&
			g_strcmp0(a->cluster, b->cluster) == 0);
}

/**
 * gst_glyph_is_empty:
 * @glyph: a GstGlyph
 *
 * Checks if the glyph represents an empty cell.
 * A glyph is empty if it contains a space, NUL, or
 * is the dummy cell of a wide character.
 *
 * Returns: %TRUE if the glyph is empty
 */
gboolean
gst_glyph_is_empty(const GstGlyph *glyph)
{
    g_return_val_if_fail(glyph != NULL, TRUE);

    return ((glyph->cluster == NULL && (glyph->rune == ' ' ||
            glyph->rune == '\0')) ||
            (glyph->attr & GST_GLYPH_ATTR_WDUMMY));
}

/**
 * gst_glyph_is_wide:
 * @glyph: a GstGlyph
 *
 * Checks if the glyph is a wide (double-width) character.
 * Wide characters occupy two cells in the terminal.
 *
 * Returns: %TRUE if the glyph is wide
 */
gboolean
gst_glyph_is_wide(const GstGlyph *glyph)
{
    g_return_val_if_fail(glyph != NULL, FALSE);

    return (glyph->attr & GST_GLYPH_ATTR_WIDE) != 0;
}

/**
 * gst_glyph_is_dummy:
 * @glyph: a GstGlyph
 *
 * Checks if the glyph is a wide dummy cell.
 * When a wide character is placed, the second cell
 * is marked as a dummy.
 *
 * Returns: %TRUE if the glyph is a wide dummy
 */
gboolean
gst_glyph_is_dummy(const GstGlyph *glyph)
{
    g_return_val_if_fail(glyph != NULL, FALSE);

    return (glyph->attr & GST_GLYPH_ATTR_WDUMMY) != 0;
}

/**
 * gst_glyph_set_attr:
 * @glyph: a GstGlyph
 * @attr: attribute flags to set
 *
 * Sets attribute flags on the glyph. This ORs the specified
 * flags with the existing attributes.
 */
void
gst_glyph_set_attr(
    GstGlyph        *glyph,
    GstGlyphAttr    attr
){
    g_return_if_fail(glyph != NULL);

    glyph->attr |= attr;
}

/**
 * gst_glyph_clear_attr:
 * @glyph: a GstGlyph
 * @attr: attribute flags to clear
 *
 * Clears attribute flags from the glyph.
 */
void
gst_glyph_clear_attr(
    GstGlyph        *glyph,
    GstGlyphAttr    attr
){
    g_return_if_fail(glyph != NULL);

    glyph->attr &= ~attr;
}

/**
 * gst_glyph_has_attr:
 * @glyph: a GstGlyph
 * @attr: attribute flags to check
 *
 * Checks if the glyph has all the specified attributes.
 *
 * Returns: %TRUE if all specified attributes are set
 */
gboolean
gst_glyph_has_attr(
    const GstGlyph  *glyph,
    GstGlyphAttr    attr
){
    g_return_val_if_fail(glyph != NULL, FALSE);

    return (glyph->attr & attr) == attr;
}

/**
 * gst_glyph_reset:
 * @glyph: a GstGlyph
 *
 * Resets the glyph to an empty space with default attributes
 * and colors. This is equivalent to clearing the cell.
 */
void
gst_glyph_reset(GstGlyph *glyph)
{
    g_return_if_fail(glyph != NULL);

	gst_glyph_clear(glyph);
    glyph->rune = ' ';
    glyph->attr = GST_GLYPH_ATTR_NONE;
    glyph->fg = GST_COLOR_DEFAULT_FG;
    glyph->bg = GST_COLOR_DEFAULT_BG;
}

void
gst_glyph_clear(GstGlyph *glyph)
{
	g_clear_pointer(&glyph->cluster, g_free);
	glyph->cluster_len = 0;
	glyph->cluster_capacity = 0;
}

void
gst_glyph_assign(GstGlyph *dest, const GstGlyph *src)
{
	gchar *text;

	/* Duplicate first: source may be the destination itself. */
	text = g_strdup(src->cluster);
	gst_glyph_clear(dest);
	*dest = *src;
	dest->cluster = text;
	dest->cluster_len = text != NULL ? strlen(text) : 0;
	dest->cluster_capacity = text != NULL ? dest->cluster_len + 1 : 0;
}

const gchar *
gst_glyph_get_text(const GstGlyph *glyph, gchar *buffer)
{
	if (glyph->attr & GST_GLYPH_ATTR_WDUMMY) {
		return "";
	}
	if (glyph->cluster != NULL) {
		return glyph->cluster;
	}
	buffer[g_unichar_to_utf8(glyph->rune, buffer)] = '\0';
	return buffer;
}

void
gst_glyph_append(GstGlyph *glyph, GstRune rune)
{
	gchar suffix[7];
	gsize length, needed;

	/* Geometric growth keeps unbounded combining runs amortized linear. */
	length = (gsize)g_unichar_to_utf8(rune, suffix);
	if (glyph->cluster == NULL) {
		glyph->cluster_capacity = 32;
		glyph->cluster = g_malloc(glyph->cluster_capacity);
		glyph->cluster_len = (gsize)g_unichar_to_utf8(glyph->rune, glyph->cluster);
	}
	needed = glyph->cluster_len + length + 1;
	if (needed > glyph->cluster_capacity) {
		glyph->cluster_capacity = MAX(needed, glyph->cluster_capacity * 2);
		glyph->cluster = g_realloc(glyph->cluster, glyph->cluster_capacity);
	}
	memcpy(glyph->cluster + glyph->cluster_len, suffix, length);
	glyph->cluster_len += length;
	glyph->cluster[glyph->cluster_len] = '\0';
}
