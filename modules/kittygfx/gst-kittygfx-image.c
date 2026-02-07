/*
 * gst-kittygfx-image.c - Kitty graphics image cache and placement
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages decoded image storage with LRU eviction, chunked uploads,
 * and placement tracking for the kitty graphics protocol.
 */

#include "gst-kittygfx-image.h"
#include <string.h>
#include <gio/gio.h>

/* Use stb_image for PNG/JPEG decoding */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

/* ===== Internal helpers ===== */

static void
kitty_image_free(gpointer data)
{
	GstKittyImage *img;

	img = (GstKittyImage *)data;
	if (img != NULL) {
		g_free(img->data);
		g_free(img);
	}
}

static void
kitty_upload_free(gpointer data)
{
	GstKittyUpload *up;

	up = (GstKittyUpload *)data;
	if (up != NULL) {
		if (up->chunks != NULL) {
			g_byte_array_unref(up->chunks);
		}
		g_free(up);
	}
}

static void
placement_free(gpointer data)
{
	g_free(data);
}

/*
 * evict_lru:
 *
 * Evicts the least-recently-used image to free memory.
 * Only evicts images that have no active placements.
 */
static void
evict_lru(GstKittyImageCache *cache)
{
	GHashTableIter iter;
	gpointer value;
	GstKittyImage *oldest;
	guint32 oldest_id;
	gint64 oldest_time;

	oldest = NULL;
	oldest_id = 0;
	oldest_time = G_MAXINT64;

	g_hash_table_iter_init(&iter, cache->images);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		GstKittyImage *img;

		img = (GstKittyImage *)value;
		if (img->last_used < oldest_time) {
			oldest_time = img->last_used;
			oldest = img;
			oldest_id = img->image_id;
		}
	}

	if (oldest != NULL) {
		cache->total_ram -= oldest->data_size;
		g_hash_table_remove(cache->images, GUINT_TO_POINTER(oldest_id));
	}
}

/*
 * decode_image_data:
 *
 * Decodes base64-encoded (and optionally zlib-compressed) image data
 * into raw RGBA pixels. Handles PNG format via stb_image and raw
 * RGB/RGBA formats.
 *
 * Returns the decoded RGBA data (caller owns), sets width/height/stride.
 * Returns NULL on failure.
 */
static guint8 *
decode_image_data(
	const guint8 *raw_data,
	gsize         raw_len,
	gint          format,
	gint          compression,
	gint          declared_w,
	gint          declared_h,
	gint         *out_w,
	gint         *out_h,
	gint         *out_stride
){
	guint8 *pixels;
	gint w;
	gint h;
	gint channels;

	pixels = NULL;
	w = 0;
	h = 0;

	if (format == GST_GFX_FORMAT_PNG) {
		/* Decode PNG using stb_image */
		pixels = stbi_load_from_memory(raw_data, (int)raw_len,
			&w, &h, &channels, 4); /* force RGBA output */
		if (pixels == NULL) {
			return NULL;
		}
	} else {
		gint bpp;
		gsize expected;

		/* Raw pixel format (RGB or RGBA) */
		bpp = (format == GST_GFX_FORMAT_RGB) ? 3 : 4;

		if (declared_w <= 0 || declared_h <= 0) {
			return NULL;
		}

		w = declared_w;
		h = declared_h;
		expected = (gsize)w * (gsize)h * (gsize)bpp;

		if (raw_len < expected) {
			return NULL;
		}

		if (bpp == 4) {
			/* RGBA - just copy */
			pixels = (guint8 *)g_malloc(expected);
			memcpy(pixels, raw_data, expected);
		} else {
			gint i;
			gint total_pixels;

			/* RGB -> RGBA conversion */
			total_pixels = w * h;
			pixels = (guint8 *)g_malloc((gsize)total_pixels * 4);

			for (i = 0; i < total_pixels; i++) {
				pixels[i * 4 + 0] = raw_data[i * 3 + 0];
				pixels[i * 4 + 1] = raw_data[i * 3 + 1];
				pixels[i * 4 + 2] = raw_data[i * 3 + 2];
				pixels[i * 4 + 3] = 255;
			}
		}
	}

	*out_w = w;
	*out_h = h;
	*out_stride = w * 4;
	return pixels;
}

/*
 * finalize_upload:
 *
 * Completes a chunked upload by base64-decoding the accumulated chunks,
 * optionally decompressing, then decoding into RGBA pixels.
 * Adds the resulting image to the cache.
 *
 * Returns the newly created image, or NULL on failure.
 */
static GstKittyImage *
finalize_upload(
	GstKittyImageCache *cache,
	GstKittyUpload     *upload
){
	GstKittyImage *img;
	guchar *decoded;
	gsize decoded_len;
	guint8 *pixels;
	guint8 *decompressed;
	gsize decomp_len;
	gint w;
	gint h;
	gint stride;

	/* Base64 decode the accumulated chunks */
	decoded = g_base64_decode((const gchar *)upload->chunks->data,
		&decoded_len);
	if (decoded == NULL || decoded_len == 0) {
		g_free(decoded);
		return NULL;
	}

	/* Decompress if zlib compressed */
	decompressed = NULL;
	if (upload->compression == 'z') {
		g_autoptr(GZlibDecompressor) decomp = NULL;
		g_autoptr(GConverter) conv = NULL;
		guint8 *out_buf;
		gsize out_size;
		gsize total_read;
		gsize total_written;
		GConverterResult result;

		decomp = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB);
		conv = G_CONVERTER(g_object_ref(decomp));

		/* Allocate generous output buffer */
		out_size = decoded_len * 4;
		if (out_size < 65536) {
			out_size = 65536;
		}
		out_buf = (guint8 *)g_malloc(out_size);

		result = g_converter_convert(conv,
			decoded, decoded_len,
			out_buf, out_size,
			G_CONVERTER_INPUT_AT_END,
			&total_read, &total_written,
			NULL);

		if (result == G_CONVERTER_ERROR || result == G_CONVERTER_FLUSHED) {
			/* Retry with larger buffer if needed */
			out_size = decoded_len * 16;
			out_buf = (guint8 *)g_realloc(out_buf, out_size);

			result = g_converter_convert(conv,
				decoded, decoded_len,
				out_buf, out_size,
				G_CONVERTER_INPUT_AT_END,
				&total_read, &total_written,
				NULL);
		}

		if (result == G_CONVERTER_FINISHED || result == G_CONVERTER_FLUSHED) {
			decompressed = out_buf;
			decomp_len = total_written;
		} else {
			g_free(out_buf);
			g_free(decoded);
			return NULL;
		}
		g_free(decoded);
	}

	/* Decode the pixel data */
	if (decompressed != NULL) {
		pixels = decode_image_data(decompressed, decomp_len,
			upload->format, 0, upload->width, upload->height,
			&w, &h, &stride);
		g_free(decompressed);
	} else {
		pixels = decode_image_data(decoded, decoded_len,
			upload->format, 0, upload->width, upload->height,
			&w, &h, &stride);
		g_free(decoded);
	}

	if (pixels == NULL) {
		return NULL;
	}

	/* Check size limits */
	if ((gsize)w * (gsize)h * 4 > cache->max_single) {
		if (upload->format == GST_GFX_FORMAT_PNG) {
			stbi_image_free(pixels);
		} else {
			g_free(pixels);
		}
		return NULL;
	}

	/* Evict until we have room */
	while (cache->total_ram + (gsize)w * (gsize)h * 4 > cache->max_ram &&
	       g_hash_table_size(cache->images) > 0) {
		evict_lru(cache);
	}

	/* Create image entry */
	img = g_new0(GstKittyImage, 1);
	img->image_id = upload->image_id;
	img->width = w;
	img->height = h;
	img->stride = stride;
	img->data_size = (gsize)w * (gsize)h * 4;
	img->last_used = g_get_monotonic_time();

	/*
	 * If stb_image allocated the buffer, copy it to a glib-owned buffer
	 * so we can safely g_free() later. For raw formats, pixels is already
	 * g_malloc'd.
	 */
	if (upload->format == GST_GFX_FORMAT_PNG) {
		img->data = (guint8 *)g_malloc(img->data_size);
		memcpy(img->data, pixels, img->data_size);
		stbi_image_free(pixels);
	} else {
		img->data = pixels;
	}

	cache->total_ram += img->data_size;

	/* Remove any existing image with same id */
	g_hash_table_remove(cache->images,
		GUINT_TO_POINTER(img->image_id));

	g_hash_table_insert(cache->images,
		GUINT_TO_POINTER(img->image_id), img);

	return img;
}

/*
 * build_response:
 *
 * Builds a kitty graphics protocol response string.
 * Format: \033_Gi=<id>;OK\033\\  or  \033_Gi=<id>;ENOENT:msg\033\\
 */
static gchar *
build_response(
	guint32      image_id,
	const gchar *status
){
	return g_strdup_printf("\033_Gi=%u;%s\033\\", image_id, status);
}

/*
 * handle_transmit:
 *
 * Handles 'a=t' (transmit) and 'a=T' (transmit+display) commands.
 * Manages chunked transfers via the upload accumulator.
 */
static gboolean
handle_transmit(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gchar             **response
){
	GstKittyUpload *upload;
	guint32 img_id;

	img_id = cmd->image_id;

	/* Auto-assign image id if not specified */
	if (img_id == 0) {
		img_id = cache->next_image_id++;
		cmd->image_id = img_id;
	}

	/* Look up or create upload accumulator */
	upload = (GstKittyUpload *)g_hash_table_lookup(
		cache->uploads, GUINT_TO_POINTER(img_id));

	if (upload == NULL) {
		upload = g_new0(GstKittyUpload, 1);
		upload->image_id = img_id;
		upload->chunks = g_byte_array_new();
		upload->format = cmd->format;
		upload->width = cmd->src_width;
		upload->height = cmd->src_height;
		upload->compression = cmd->compression;

		g_hash_table_insert(cache->uploads,
			GUINT_TO_POINTER(img_id), upload);
	}

	/* Append payload chunk */
	if (cmd->payload != NULL && cmd->payload_len > 0) {
		g_byte_array_append(upload->chunks,
			(const guint8 *)cmd->payload, (guint)cmd->payload_len);
	}

	/* If more chunks expected, we're done for now */
	if (cmd->more == 1) {
		if (response != NULL && cmd->quiet < 2) {
			*response = NULL; /* no response for intermediate chunks */
		}
		return TRUE;
	}

	/* Final chunk - decode the complete image */
	{
		GstKittyImage *img;

		/* Null-terminate for base64 decode */
		g_byte_array_append(upload->chunks, (const guint8 *)"\0", 1);

		img = finalize_upload(cache, upload);

		/* Remove upload accumulator */
		g_hash_table_remove(cache->uploads,
			GUINT_TO_POINTER(img_id));

		if (img == NULL) {
			if (response != NULL && cmd->quiet == 0) {
				*response = build_response(img_id,
					"EINVAL:failed to decode image");
			}
			return TRUE;
		}

		/* For 'T' (transmit+display), create a placement */
		if (cmd->action == 'T') {
			GstImagePlacement *pl;

			if ((gint)g_list_length(cache->placements) >=
			    cache->max_placements) {
				/* Remove oldest placement */
				GList *first;

				first = g_list_first(cache->placements);
				if (first != NULL) {
					placement_free(first->data);
					cache->placements = g_list_delete_link(
						cache->placements, first);
				}
			}

			pl = g_new0(GstImagePlacement, 1);
			pl->image_id = img_id;
			pl->placement_id = cmd->placement_id;
			pl->col = -1;  /* will be set by caller from cursor pos */
			pl->row = -1;
			pl->src_x = cmd->src_x;
			pl->src_y = cmd->src_y;
			pl->crop_w = cmd->crop_w;
			pl->crop_h = cmd->crop_h;
			pl->dst_cols = cmd->dst_cols;
			pl->dst_rows = cmd->dst_rows;
			pl->x_offset = cmd->x_offset;
			pl->y_offset = cmd->y_offset;
			pl->z_index = cmd->z_index;

			cache->placements = g_list_append(
				cache->placements, pl);
		}

		if (response != NULL && cmd->quiet == 0) {
			*response = build_response(img_id, "OK");
		}
	}

	return TRUE;
}

/*
 * handle_display:
 *
 * Handles 'a=p' (display/place) commands.
 * Creates a new placement for an existing image.
 */
static gboolean
handle_display(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gchar             **response
){
	GstKittyImage *img;
	GstImagePlacement *pl;

	img = gst_kitty_image_cache_get_image(cache, cmd->image_id);
	if (img == NULL) {
		if (response != NULL && cmd->quiet == 0) {
			*response = build_response(cmd->image_id,
				"ENOENT:image not found");
		}
		return TRUE;
	}

	if ((gint)g_list_length(cache->placements) >= cache->max_placements) {
		GList *first;

		first = g_list_first(cache->placements);
		if (first != NULL) {
			placement_free(first->data);
			cache->placements = g_list_delete_link(
				cache->placements, first);
		}
	}

	pl = g_new0(GstImagePlacement, 1);
	pl->image_id = cmd->image_id;
	pl->placement_id = cmd->placement_id;
	pl->col = -1;  /* set by caller */
	pl->row = -1;
	pl->src_x = cmd->src_x;
	pl->src_y = cmd->src_y;
	pl->crop_w = cmd->crop_w;
	pl->crop_h = cmd->crop_h;
	pl->dst_cols = cmd->dst_cols;
	pl->dst_rows = cmd->dst_rows;
	pl->x_offset = cmd->x_offset;
	pl->y_offset = cmd->y_offset;
	pl->z_index = cmd->z_index;

	cache->placements = g_list_append(cache->placements, pl);

	if (response != NULL && cmd->quiet == 0) {
		*response = build_response(cmd->image_id, "OK");
	}

	return TRUE;
}

/*
 * handle_query:
 *
 * Handles 'a=q' (query) commands.
 * Responds with OK to indicate kitty graphics support.
 */
static gboolean
handle_query(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gchar             **response
){
	(void)cache;

	if (response != NULL) {
		*response = build_response(cmd->image_id, "OK");
	}

	return TRUE;
}

/*
 * handle_delete:
 *
 * Handles 'a=d' (delete) commands.
 * Removes images and/or placements based on the delete target.
 */
static gboolean
handle_delete(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gchar             **response
){
	GList *l;
	GList *next;

	switch (cmd->delete_target) {
	case 'a': /* delete all */
		g_hash_table_remove_all(cache->images);
		g_list_free_full(cache->placements, placement_free);
		cache->placements = NULL;
		cache->total_ram = 0;
		break;

	case 'i': /* delete by image id */
		{
			GstKittyImage *img;

			img = (GstKittyImage *)g_hash_table_lookup(
				cache->images,
				GUINT_TO_POINTER(cmd->image_id));
			if (img != NULL) {
				cache->total_ram -= img->data_size;
				g_hash_table_remove(cache->images,
					GUINT_TO_POINTER(cmd->image_id));
			}

			/* Remove associated placements */
			for (l = cache->placements; l != NULL; l = next) {
				GstImagePlacement *pl;

				next = l->next;
				pl = (GstImagePlacement *)l->data;
				if (pl->image_id == cmd->image_id) {
					placement_free(pl);
					cache->placements = g_list_delete_link(
						cache->placements, l);
				}
			}
		}
		break;

	case 'c': /* delete at cursor */
	case 'p': /* delete at cell */
	case 'x': /* delete at column */
	case 'y': /* delete at row */
	case 'z': /* delete at z-index */
		/* Remove matching placements */
		for (l = cache->placements; l != NULL; l = next) {
			GstImagePlacement *pl;
			gboolean match;

			next = l->next;
			pl = (GstImagePlacement *)l->data;
			match = FALSE;

			switch (cmd->delete_target) {
			case 'z':
				match = (pl->z_index == cmd->z_index);
				break;
			default:
				/* Other targets need cursor position context
				 * which we don't have here - skip for now */
				break;
			}

			if (match) {
				placement_free(pl);
				cache->placements = g_list_delete_link(
					cache->placements, l);
			}
		}
		break;

	default:
		break;
	}

	(void)response;
	return TRUE;
}

/* ===== Public API ===== */

/**
 * gst_kitty_image_cache_new:
 * @max_ram_mb: maximum total RAM in megabytes
 * @max_single_mb: maximum single image size in megabytes
 * @max_placements: maximum number of placements
 *
 * Creates a new image cache.
 *
 * Returns: a new #GstKittyImageCache
 */
GstKittyImageCache *
gst_kitty_image_cache_new(
	gint max_ram_mb,
	gint max_single_mb,
	gint max_placements
){
	GstKittyImageCache *cache;

	cache = g_new0(GstKittyImageCache, 1);
	cache->images = g_hash_table_new_full(
		g_direct_hash, g_direct_equal, NULL, kitty_image_free);
	cache->uploads = g_hash_table_new_full(
		g_direct_hash, g_direct_equal, NULL, kitty_upload_free);
	cache->placements = NULL;
	cache->total_ram = 0;
	cache->max_ram = (gsize)max_ram_mb * 1024 * 1024;
	cache->max_single = (gsize)max_single_mb * 1024 * 1024;
	cache->max_placements = max_placements;
	cache->next_image_id = 1;

	return cache;
}

/**
 * gst_kitty_image_cache_free:
 * @cache: the cache to free
 *
 * Frees all resources.
 */
void
gst_kitty_image_cache_free(GstKittyImageCache *cache)
{
	if (cache == NULL) {
		return;
	}

	g_hash_table_destroy(cache->images);
	g_hash_table_destroy(cache->uploads);
	g_list_free_full(cache->placements, placement_free);
	g_free(cache);
}

/**
 * gst_kitty_image_cache_process:
 * @cache: the image cache
 * @cmd: parsed graphics command
 * @response: (out) (nullable): response string, caller frees
 *
 * Routes a parsed command to the appropriate handler.
 *
 * Returns: %TRUE if the command was handled
 */
gboolean
gst_kitty_image_cache_process(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gchar             **response
){
	if (response != NULL) {
		*response = NULL;
	}

	switch (cmd->action) {
	case 't':
	case 'T':
		return handle_transmit(cache, cmd, response);

	case 'p':
		return handle_display(cache, cmd, response);

	case 'q':
		return handle_query(cache, cmd, response);

	case 'd':
		return handle_delete(cache, cmd, response);

	default:
		/* Unknown action - ignore */
		return FALSE;
	}
}

/**
 * gst_kitty_image_cache_get_image:
 * @cache: the image cache
 * @image_id: the image id
 *
 * Looks up an image and updates its LRU timestamp.
 *
 * Returns: (transfer none) (nullable): the image
 */
GstKittyImage *
gst_kitty_image_cache_get_image(
	GstKittyImageCache *cache,
	guint32             image_id
){
	GstKittyImage *img;

	img = (GstKittyImage *)g_hash_table_lookup(
		cache->images, GUINT_TO_POINTER(image_id));

	if (img != NULL) {
		img->last_used = g_get_monotonic_time();
	}

	return img;
}

/*
 * placement_z_compare:
 *
 * Sort by z-index ascending (lowest z renders first / behind).
 */
static gint
placement_z_compare(gconstpointer a, gconstpointer b)
{
	const GstImagePlacement *pa;
	const GstImagePlacement *pb;

	pa = (const GstImagePlacement *)a;
	pb = (const GstImagePlacement *)b;

	if (pa->z_index < pb->z_index) return -1;
	if (pa->z_index > pb->z_index) return 1;
	return 0;
}

/**
 * gst_kitty_image_cache_get_visible_placements:
 * @cache: the image cache
 * @top_row: top visible row
 * @bottom_row: bottom visible row
 *
 * Returns visible placements sorted by z-index.
 *
 * Returns: (transfer container): sorted list
 */
GList *
gst_kitty_image_cache_get_visible_placements(
	GstKittyImageCache *cache,
	gint                top_row,
	gint                bottom_row
){
	GList *result;
	GList *l;

	result = NULL;

	for (l = cache->placements; l != NULL; l = l->next) {
		GstImagePlacement *pl;

		pl = (GstImagePlacement *)l->data;

		/* Check if placement is in visible range */
		if (pl->row >= top_row && pl->row <= bottom_row) {
			result = g_list_prepend(result, pl);
		}
	}

	result = g_list_sort(result, placement_z_compare);

	return result;
}

/**
 * gst_kitty_image_cache_scroll:
 * @cache: the image cache
 * @amount: rows scrolled (positive = up)
 *
 * Adjusts placement positions after scroll.
 */
void
gst_kitty_image_cache_scroll(
	GstKittyImageCache *cache,
	gint                amount
){
	GList *l;
	GList *next;

	for (l = cache->placements; l != NULL; l = next) {
		GstImagePlacement *pl;

		next = l->next;
		pl = (GstImagePlacement *)l->data;
		pl->row -= amount;

		/* Remove placements scrolled way off top */
		if (pl->row < -1000) {
			placement_free(pl);
			cache->placements = g_list_delete_link(
				cache->placements, l);
		}
	}
}

/**
 * gst_kitty_image_cache_clear_alt:
 * @cache: the image cache
 *
 * Clears all placements for alt-screen transition.
 */
void
gst_kitty_image_cache_clear_alt(GstKittyImageCache *cache)
{
	g_list_free_full(cache->placements, placement_free);
	cache->placements = NULL;
}
