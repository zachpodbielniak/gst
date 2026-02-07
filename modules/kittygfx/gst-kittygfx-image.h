/*
 * gst-kittygfx-image.h - Kitty graphics image cache and placement
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages decoded image storage, chunked upload accumulation,
 * placement tracking, and LRU eviction. Images are stored as
 * decoded RGBA pixel arrays keyed by image_id.
 */

#ifndef GST_KITTYGFX_IMAGE_H
#define GST_KITTYGFX_IMAGE_H

#include <glib.h>
#include "gst-kittygfx-parser.h"

G_BEGIN_DECLS

/*
 * GstKittyImage:
 *
 * A decoded image in the cache. Stores RGBA pixel data and metadata.
 */
typedef struct
{
	guint32  image_id;
	guint8  *data;        /* decoded RGBA pixels, owned */
	gint     width;
	gint     height;
	gint     stride;      /* bytes per row (width * 4) */
	gsize    data_size;   /* total bytes (width * height * 4) */
	gint64   last_used;   /* monotonic timestamp for LRU */
} GstKittyImage;

/*
 * GstImagePlacement:
 *
 * Tracks where an image is displayed on the terminal grid.
 * One image may have multiple placements.
 */
typedef struct
{
	guint32  image_id;
	guint32  placement_id;
	gint     col;         /* cell column */
	gint     row;         /* cell row (absolute, shifts with scroll) */
	gint     src_x;       /* source crop x */
	gint     src_y;       /* source crop y */
	gint     crop_w;      /* source crop width (0 = full) */
	gint     crop_h;      /* source crop height (0 = full) */
	gint     dst_cols;    /* display columns (0 = auto) */
	gint     dst_rows;    /* display rows (0 = auto) */
	gint     x_offset;    /* pixel offset within cell */
	gint     y_offset;    /* pixel offset within cell */
	gint32   z_index;     /* layer order */
} GstImagePlacement;

/*
 * GstKittyUpload:
 *
 * Accumulator for chunked image transfers. Collects base64 chunks
 * until m=0 signals the final chunk, then decodes the full image.
 */
typedef struct
{
	guint32     image_id;
	GByteArray *chunks;      /* accumulated base64 data */
	gint        format;      /* pixel format (GstGfxFormat) */
	gint        width;       /* declared source width */
	gint        height;      /* declared source height */
	gint        compression; /* 'o' value: 'z' for zlib */
} GstKittyUpload;

/*
 * GstKittyImageCache:
 *
 * Image cache managing decoded images, active uploads,
 * placements, and memory limits.
 */
typedef struct
{
	GHashTable *images;       /* guint32 image_id -> GstKittyImage* */
	GHashTable *uploads;      /* guint32 image_id -> GstKittyUpload* */
	GList      *placements;   /* GstImagePlacement* list */
	gsize       total_ram;    /* current total decoded bytes */
	gsize       max_ram;      /* limit in bytes */
	gsize       max_single;   /* max single image in bytes */
	gint        max_placements;
	guint32     next_image_id;  /* auto-assign if id=0 */
} GstKittyImageCache;

/**
 * gst_kitty_image_cache_new:
 * @max_ram_mb: maximum total RAM in megabytes
 * @max_single_mb: maximum single image size in megabytes
 * @max_placements: maximum number of placements
 *
 * Creates a new image cache with the specified limits.
 *
 * Returns: a new #GstKittyImageCache, free with gst_kitty_image_cache_free()
 */
GstKittyImageCache *
gst_kitty_image_cache_new(
	gint max_ram_mb,
	gint max_single_mb,
	gint max_placements
);

/**
 * gst_kitty_image_cache_free:
 * @cache: the cache to free
 *
 * Frees all images, uploads, placements, and the cache itself.
 */
void
gst_kitty_image_cache_free(GstKittyImageCache *cache);

/**
 * gst_kitty_image_cache_process:
 * @cache: the image cache
 * @cmd: parsed graphics command
 * @response: (out) (nullable): response string to send back via PTY,
 *            or %NULL if no response needed. Caller frees.
 *
 * Processes a kitty graphics command: handles transmit, display,
 * query, and delete operations. May modify the cache state
 * (add/remove images, placements).
 *
 * Returns: %TRUE if the command was handled
 */
gboolean
gst_kitty_image_cache_process(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gchar             **response
);

/**
 * gst_kitty_image_cache_get_image:
 * @cache: the image cache
 * @image_id: the image id to look up
 *
 * Looks up an image by id. Updates the LRU timestamp.
 *
 * Returns: (transfer none) (nullable): the image, or %NULL
 */
GstKittyImage *
gst_kitty_image_cache_get_image(
	GstKittyImageCache *cache,
	guint32             image_id
);

/**
 * gst_kitty_image_cache_get_visible_placements:
 * @cache: the image cache
 * @top_row: top visible row (absolute)
 * @bottom_row: bottom visible row (absolute)
 *
 * Returns a list of placements visible in the given row range,
 * sorted by z-index (lowest first).
 *
 * Returns: (transfer container) (element-type GstImagePlacement):
 *          sorted list of visible placements. Caller frees the list
 *          (but not the placement data).
 */
GList *
gst_kitty_image_cache_get_visible_placements(
	GstKittyImageCache *cache,
	gint                top_row,
	gint                bottom_row
);

/**
 * gst_kitty_image_cache_scroll:
 * @cache: the image cache
 * @amount: number of rows scrolled (positive = up)
 *
 * Adjusts all placement row positions after terminal scroll.
 * Removes placements that have scrolled entirely off-screen.
 */
void
gst_kitty_image_cache_scroll(
	GstKittyImageCache *cache,
	gint                amount
);

/**
 * gst_kitty_image_cache_clear_alt:
 * @cache: the image cache
 *
 * Clears all placements when switching to/from alternate screen.
 */
void
gst_kitty_image_cache_clear_alt(GstKittyImageCache *cache);

G_END_DECLS

#endif /* GST_KITTYGFX_IMAGE_H */
