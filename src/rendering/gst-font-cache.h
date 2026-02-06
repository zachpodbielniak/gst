/*
 * gst-font-cache.h - Font caching for terminal rendering
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_FONT_CACHE_H
#define GST_FONT_CACHE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_FONT_CACHE (gst_font_cache_get_type())

G_DECLARE_FINAL_TYPE(GstFontCache, gst_font_cache, GST, FONT_CACHE, GObject)

GType
gst_font_cache_get_type(void) G_GNUC_CONST;

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
 * Clears all cached font data.
 */
void
gst_font_cache_clear(GstFontCache *self);

/**
 * gst_font_cache_get_default:
 *
 * Gets the default shared font cache instance.
 *
 * Returns: (transfer none): The default #GstFontCache
 */
GstFontCache *
gst_font_cache_get_default(void);

G_END_DECLS

#endif /* GST_FONT_CACHE_H */
