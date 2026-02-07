/*
 * gst-glyph-transformer.c
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for transforming glyph rendering.
 */

#include "gst-glyph-transformer.h"

G_DEFINE_INTERFACE(GstGlyphTransformer, gst_glyph_transformer, G_TYPE_OBJECT)

static void
gst_glyph_transformer_default_init(GstGlyphTransformerInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

/**
 * gst_glyph_transformer_transform_glyph:
 * @self: A #GstGlyphTransformer instance.
 * @codepoint: The Unicode codepoint of the glyph.
 * @render_context: (type gpointer): An opaque render context (renderer-specific).
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
                                      gpointer             render_context,
                                      gint                 x,
                                      gint                 y,
                                      gint                 width,
                                      gint                 height)
{
	GstGlyphTransformerInterface *iface;

	g_return_val_if_fail(GST_IS_GLYPH_TRANSFORMER(self), FALSE);
	g_return_val_if_fail(render_context != NULL, FALSE);

	iface = GST_GLYPH_TRANSFORMER_GET_IFACE(self);
	g_return_val_if_fail(iface->transform_glyph != NULL, FALSE);

	return iface->transform_glyph(self, codepoint, render_context, x, y, width, height);
}
