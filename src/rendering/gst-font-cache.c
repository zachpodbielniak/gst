/*
 * gst-font-cache.c - Font caching for terminal rendering
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-font-cache.h"

/**
 * SECTION:gst-font-cache
 * @title: GstFontCache
 * @short_description: Caches font glyphs for efficient rendering
 *
 * #GstFontCache provides efficient caching of rendered font glyphs
 * to avoid repeated rasterization of the same characters.
 */

struct _GstFontCache
{
	GObject parent_instance;

	/* TODO: Add font cache fields */
	GHashTable *glyph_cache;
};

G_DEFINE_TYPE(GstFontCache, gst_font_cache, G_TYPE_OBJECT)

/* Singleton instance */
static GstFontCache *default_font_cache = NULL;

static void
gst_font_cache_dispose(GObject *object)
{
	GstFontCache *self;

	self = GST_FONT_CACHE(object);

	g_clear_pointer(&self->glyph_cache, g_hash_table_unref);

	G_OBJECT_CLASS(gst_font_cache_parent_class)->dispose(object);
}

static void
gst_font_cache_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_font_cache_parent_class)->finalize(object);
}

static void
gst_font_cache_class_init(GstFontCacheClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_font_cache_dispose;
	object_class->finalize = gst_font_cache_finalize;
}

static void
gst_font_cache_init(GstFontCache *self)
{
	/* TODO: Initialize with proper hash/equal functions for glyphs */
	self->glyph_cache = g_hash_table_new_full(
		g_direct_hash,
		g_direct_equal,
		NULL,
		NULL
	);
}

/**
 * gst_font_cache_new:
 *
 * Creates a new font cache instance.
 *
 * Returns: (transfer full): A new #GstFontCache
 */
GstFontCache *
gst_font_cache_new(void)
{
	return (GstFontCache *)g_object_new(GST_TYPE_FONT_CACHE, NULL);
}

/**
 * gst_font_cache_clear:
 * @self: A #GstFontCache
 *
 * Clears all cached font data.
 */
void
gst_font_cache_clear(GstFontCache *self)
{
	g_return_if_fail(GST_IS_FONT_CACHE(self));

	if (self->glyph_cache != NULL)
	{
		g_hash_table_remove_all(self->glyph_cache);
	}
}

/**
 * gst_font_cache_get_default:
 *
 * Gets the default shared font cache instance.
 *
 * Returns: (transfer none): The default #GstFontCache
 */
GstFontCache *
gst_font_cache_get_default(void)
{
	if (default_font_cache == NULL)
	{
		default_font_cache = gst_font_cache_new();
	}

	return default_font_cache;
}
