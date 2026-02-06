/*
 * gst-glyph-transformer.h
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for transforming glyph rendering.
 */

#ifndef GST_GLYPH_TRANSFORMER_H
#define GST_GLYPH_TRANSFORMER_H

#include <glib-object.h>
#include <pango/pango.h>
#include <cairo.h>

G_BEGIN_DECLS

#define GST_TYPE_GLYPH_TRANSFORMER (gst_glyph_transformer_get_type())

G_DECLARE_INTERFACE(GstGlyphTransformer, gst_glyph_transformer, GST, GLYPH_TRANSFORMER, GObject)

/**
 * GstGlyphTransformerInterface:
 * @parent_iface: The parent interface.
 * @transform_glyph: Virtual method to transform a glyph during rendering.
 *
 * Interface for modifying glyph rendering (e.g., box drawing, ligatures).
 */
struct _GstGlyphTransformerInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	gboolean (*transform_glyph) (GstGlyphTransformer *self,
	                             gunichar             codepoint,
	                             cairo_t             *cr,
	                             gint                 x,
	                             gint                 y,
	                             gint                 width,
	                             gint                 height);
};

/**
 * gst_glyph_transformer_transform_glyph:
 * @self: A #GstGlyphTransformer instance.
 * @codepoint: The Unicode codepoint of the glyph.
 * @cr: The Cairo context to render to.
 * @x: The x position for rendering.
 * @y: The y position for rendering.
 * @width: The cell width.
 * @height: The cell height.
 *
 * Transforms and renders a glyph at the specified position.
 *
 * Returns: %TRUE if the glyph was handled, %FALSE to use default rendering.
 */
gboolean
gst_glyph_transformer_transform_glyph(GstGlyphTransformer *self,
                                      gunichar             codepoint,
                                      cairo_t             *cr,
                                      gint                 x,
                                      gint                 y,
                                      gint                 width,
                                      gint                 height);

G_END_DECLS

#endif /* GST_GLYPH_TRANSFORMER_H */
