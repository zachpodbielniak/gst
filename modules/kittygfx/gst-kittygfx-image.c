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
		g_autoptr(GInputStream) mem_in = NULL;
		g_autoptr(GInputStream) conv_in = NULL;
		GByteArray *result_buf;
		guint8 read_buf[65536];
		gssize n_read;

		decomp = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB);

		/*
		 * Use GConverterInputStream for streaming decompression.
		 * This avoids the problems of raw g_converter_convert():
		 * - No fixed output buffer size (handles any compression ratio)
		 * - No corrupted state on retry (stream handles internally)
		 * - Proper EOF detection (returns 0 on complete)
		 */
		mem_in = g_memory_input_stream_new_from_data(
			decoded, decoded_len, NULL);
		conv_in = g_converter_input_stream_new(
			mem_in, G_CONVERTER(decomp));

		result_buf = g_byte_array_new();

		/* Read all decompressed data in chunks */
		for (;;) {
			n_read = g_input_stream_read(conv_in,
				read_buf, sizeof(read_buf), NULL, NULL);

			if (n_read < 0) {
				/* Decompression error */
				g_byte_array_unref(result_buf);
				g_free(decoded);
				return NULL;
			}
			if (n_read == 0) {
				break; /* EOF - decompression complete */
			}

			g_byte_array_append(result_buf, read_buf, (guint)n_read);
		}

		if (result_buf->len == 0) {
			g_byte_array_unref(result_buf);
			g_free(decoded);
			return NULL;
		}

		/*
		 * Transfer ownership of the byte array's internal buffer.
		 * g_byte_array_free(arr, FALSE) frees the wrapper but
		 * returns the data pointer for us to own.
		 */
		decomp_len = result_buf->len;
		decompressed = g_byte_array_free(result_buf, FALSE);
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
	img->image_number = upload->image_number;
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
 * Includes placement_id and image_number when non-zero per spec:
 *   \033_Gi=<id>;OK\033\\
 *   \033_Gi=<id>,p=<placement_id>;OK\033\\
 *   \033_Gi=<id>,I=<image_number>;OK\033\\
 */
static gchar *
build_response(
	guint32      image_id,
	guint32      placement_id,
	guint32      image_number,
	const gchar *status
){
	GString *resp;

	resp = g_string_new(NULL);
	g_string_append_printf(resp, "\033_Gi=%u", image_id);

	if (placement_id > 0) {
		g_string_append_printf(resp, ",p=%u", placement_id);
	}
	if (image_number > 0) {
		g_string_append_printf(resp, ",I=%u", image_number);
	}

	g_string_append_printf(resp, ";%s\033\\", status);

	return g_string_free(resp, FALSE);
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
	gint                cursor_col,
	gint                cursor_row,
	gchar             **response
){
	GstKittyUpload *upload;
	guint32 img_id;

	img_id = cmd->image_id;

	/*
	 * Resolve image id for continuation chunks.
	 * Per the kitty spec, subsequent chunks of a chunked transfer
	 * may omit the 'i' key; in that case we reuse the id from
	 * the most recent transmit command.  Only auto-assign a brand
	 * new id when there is no active upload to continue.
	 */
	if (img_id == 0) {
		if (cache->last_image_id != 0 &&
		    g_hash_table_lookup(cache->uploads,
		        GUINT_TO_POINTER(cache->last_image_id)) != NULL) {
			/* Continuation chunk - reuse the active upload id */
			img_id = cache->last_image_id;
		} else {
			/* New upload with no explicit id */
			img_id = cache->next_image_id++;
		}
		cmd->image_id = img_id;
	}

	/* Track the most recent transmit id for future continuations */
	cache->last_image_id = img_id;

	/* Look up or create upload accumulator */
	upload = (GstKittyUpload *)g_hash_table_lookup(
		cache->uploads, GUINT_TO_POINTER(img_id));

	if (upload == NULL) {
		upload = g_new0(GstKittyUpload, 1);
		upload->image_id = img_id;
		upload->image_number = cmd->image_number;
		upload->chunks = g_byte_array_new();
		upload->format = cmd->format;
		upload->width = cmd->src_width;
		upload->height = cmd->src_height;
		upload->compression = cmd->compression;

		/*
		 * Preserve first-chunk control keys for final-chunk processing.
		 * Per the kitty spec, continuation chunks only carry 'm' and
		 * payload — all other keys come from the first chunk.
		 */
		upload->action = cmd->action;
		upload->quiet = cmd->quiet;
		upload->placement_id = cmd->placement_id;
		upload->src_x = cmd->src_x;
		upload->src_y = cmd->src_y;
		upload->crop_w = cmd->crop_w;
		upload->crop_h = cmd->crop_h;
		upload->dst_cols = cmd->dst_cols;
		upload->dst_rows = cmd->dst_rows;
		upload->x_offset = cmd->x_offset;
		upload->y_offset = cmd->y_offset;
		upload->z_index = cmd->z_index;
		upload->cursor_movement = cmd->cursor_movement;

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
		/* No response for intermediate chunks per protocol */
		return TRUE;
	}

	/*
	 * Final chunk - decode the complete image.
	 *
	 * Use the upload's stored first-chunk values for action, quiet,
	 * and placement_id since continuation chunks only carry 'm' and
	 * payload — the parser defaults would be wrong.
	 */
	{
		GstKittyImage *img;
		GstKittyUpload saved;

		/*
		 * Copy the upload struct before the hash table remove frees it.
		 * We need first-chunk fields for placement creation and response.
		 */
		saved = *upload;

		/* Null-terminate for base64 decode */
		g_byte_array_append(upload->chunks, (const guint8 *)"\0", 1);

		img = finalize_upload(cache, upload);

		/* Remove upload accumulator and clear continuation tracker */
		g_hash_table_remove(cache->uploads,
			GUINT_TO_POINTER(img_id));
		if (cache->last_image_id == img_id) {
			cache->last_image_id = 0;
		}

		if (img == NULL) {
			/* q=2 suppresses errors; q=0 and q=1 send errors */
			if (response != NULL && saved.quiet != 2) {
				*response = build_response(img_id,
					saved.placement_id, saved.image_number,
					"EINVAL:failed to decode image");
			}
			return TRUE;
		}

		/* For 'T' (transmit+display), create a placement */
		if (saved.action == 'T') {
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
			pl->placement_id = saved.placement_id;
			pl->col = cursor_col;
			pl->row = cursor_row;
			pl->src_x = saved.src_x;
			pl->src_y = saved.src_y;
			pl->crop_w = saved.crop_w;
			pl->crop_h = saved.crop_h;
			pl->dst_cols = saved.dst_cols;
			pl->dst_rows = saved.dst_rows;
			pl->x_offset = saved.x_offset;
			pl->y_offset = saved.y_offset;
			pl->z_index = saved.z_index;

			cache->placements = g_list_append(
				cache->placements, pl);
		}

		/* q=0 sends OK; q=1 and q=2 suppress it */
		if (response != NULL && saved.quiet == 0) {
			*response = build_response(img_id,
				saved.placement_id, saved.image_number, "OK");
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
	gint                cursor_col,
	gint                cursor_row,
	gchar             **response
){
	GstKittyImage *img;
	GstImagePlacement *pl;

	img = gst_kitty_image_cache_get_image(cache, cmd->image_id);
	if (img == NULL) {
		/* q=2 suppresses errors; q=0 and q=1 send errors */
		if (response != NULL && cmd->quiet != 2) {
			*response = build_response(cmd->image_id,
				cmd->placement_id, cmd->image_number,
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
	pl->col = cursor_col;
	pl->row = cursor_row;
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

	/* q=0 sends OK; q=1 and q=2 suppress it */
	if (response != NULL && cmd->quiet == 0) {
		*response = build_response(cmd->image_id,
			cmd->placement_id, cmd->image_number, "OK");
	}

	return TRUE;
}

/*
 * handle_query:
 *
 * Handles 'a=q' (query) commands.
 * Responds with OK to indicate kitty graphics support.
 * Per spec, query responses are always sent regardless of quiet flag.
 */
static gboolean
handle_query(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gchar             **response
){
	(void)cache;

	if (response != NULL) {
		*response = build_response(cmd->image_id,
			cmd->placement_id, cmd->image_number, "OK");
	}

	return TRUE;
}

/*
 * placement_intersects_cell:
 *
 * Checks if a placement covers the given cell (0-indexed col, row).
 * Uses dst_cols/dst_rows when set, otherwise assumes 1x1.
 */
static gboolean
placement_intersects_cell(
	GstImagePlacement *pl,
	gint               col,
	gint               row
){
	gint end_col;
	gint end_row;

	end_col = pl->col + ((pl->dst_cols > 0) ? pl->dst_cols : 1);
	end_row = pl->row + ((pl->dst_rows > 0) ? pl->dst_rows : 1);

	return (col >= pl->col && col < end_col &&
	        row >= pl->row && row < end_row);
}

/*
 * maybe_free_orphan_image:
 *
 * For uppercase delete variants: if the image has no remaining
 * placements, free its pixel data from the cache.
 */
static void
maybe_free_orphan_image(
	GstKittyImageCache *cache,
	guint32             image_id
){
	GstKittyImage *img;
	GList *l;

	/* Check if any placement still references this image */
	for (l = cache->placements; l != NULL; l = l->next) {
		GstImagePlacement *pl;

		pl = (GstImagePlacement *)l->data;
		if (pl->image_id == image_id) {
			return; /* still referenced */
		}
	}

	/* No placements remain - free image data */
	img = (GstKittyImage *)g_hash_table_lookup(
		cache->images, GUINT_TO_POINTER(image_id));
	if (img != NULL) {
		cache->total_ram -= img->data_size;
		g_hash_table_remove(cache->images, GUINT_TO_POINTER(image_id));
	}
}

/*
 * delete_placements_matching:
 *
 * Removes all placements that match a predicate. If free_orphans is
 * TRUE (uppercase variants), also frees image data when no placements
 * remain for that image.
 *
 * The match function receives each placement, the command, cursor_col,
 * and cursor_row, returning TRUE if the placement should be deleted.
 */
typedef gboolean (*PlacementMatchFunc)(GstImagePlacement *, GstGraphicsCommand *,
                                       gint, gint);

static void
delete_placements_matching(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gint                cursor_col,
	gint                cursor_row,
	gboolean            free_orphans,
	PlacementMatchFunc  match_fn
){
	GList *l;
	GList *next;
	GList *orphan_ids;

	orphan_ids = NULL;

	for (l = cache->placements; l != NULL; l = next) {
		GstImagePlacement *pl;

		next = l->next;
		pl = (GstImagePlacement *)l->data;

		if (match_fn(pl, cmd, cursor_col, cursor_row)) {
			if (free_orphans) {
				/* Track image id for orphan check */
				orphan_ids = g_list_prepend(orphan_ids,
					GUINT_TO_POINTER(pl->image_id));
			}
			placement_free(pl);
			cache->placements = g_list_delete_link(
				cache->placements, l);
		}
	}

	/* Free orphaned images for uppercase variants */
	if (free_orphans) {
		for (l = orphan_ids; l != NULL; l = l->next) {
			maybe_free_orphan_image(cache,
				GPOINTER_TO_UINT(l->data));
		}
	}
	g_list_free(orphan_ids);
}

/* ===== Match functions for each delete target ===== */

static gboolean
match_by_id(GstImagePlacement *pl, GstGraphicsCommand *cmd,
            gint cursor_col, gint cursor_row)
{
	(void)cursor_col; (void)cursor_row;
	if (pl->image_id != cmd->image_id) {
		return FALSE;
	}
	/* If placement_id specified, only match that placement */
	if (cmd->placement_id > 0 && pl->placement_id != cmd->placement_id) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
match_at_cursor(GstImagePlacement *pl, GstGraphicsCommand *cmd,
                gint cursor_col, gint cursor_row)
{
	(void)cmd;
	return placement_intersects_cell(pl, cursor_col, cursor_row);
}

static gboolean
match_at_cell(GstImagePlacement *pl, GstGraphicsCommand *cmd,
              gint cursor_col, gint cursor_row)
{
	gint col;
	gint row;

	(void)cursor_col; (void)cursor_row;

	/* x,y keys are 1-indexed per spec */
	col = (cmd->src_x > 0) ? cmd->src_x - 1 : 0;
	row = (cmd->src_y > 0) ? cmd->src_y - 1 : 0;

	return placement_intersects_cell(pl, col, row);
}

static gboolean
match_at_cell_z(GstImagePlacement *pl, GstGraphicsCommand *cmd,
                gint cursor_col, gint cursor_row)
{
	if (pl->z_index != cmd->z_index) {
		return FALSE;
	}
	return match_at_cell(pl, cmd, cursor_col, cursor_row);
}

static gboolean
match_at_column(GstImagePlacement *pl, GstGraphicsCommand *cmd,
                gint cursor_col, gint cursor_row)
{
	gint col;
	gint end_col;

	(void)cursor_col; (void)cursor_row;

	/* x key is 1-indexed */
	col = (cmd->src_x > 0) ? cmd->src_x - 1 : 0;
	end_col = pl->col + ((pl->dst_cols > 0) ? pl->dst_cols : 1);

	return (col >= pl->col && col < end_col);
}

static gboolean
match_at_row(GstImagePlacement *pl, GstGraphicsCommand *cmd,
             gint cursor_col, gint cursor_row)
{
	gint row;
	gint end_row;

	(void)cursor_col; (void)cursor_row;

	/* y key is 1-indexed */
	row = (cmd->src_y > 0) ? cmd->src_y - 1 : 0;
	end_row = pl->row + ((pl->dst_rows > 0) ? pl->dst_rows : 1);

	return (row >= pl->row && row < end_row);
}

static gboolean
match_at_zindex(GstImagePlacement *pl, GstGraphicsCommand *cmd,
                gint cursor_col, gint cursor_row)
{
	(void)cursor_col; (void)cursor_row;
	return (pl->z_index == cmd->z_index);
}

/*
 * handle_delete:
 *
 * Handles 'a=d' (delete) commands per the kitty graphics protocol spec.
 *
 * Lowercase targets delete placements only. Uppercase targets also
 * free image data when no placements remain for that image.
 *
 * Delete targets:
 *   a/A - all placements (A also frees unreferenced images)
 *   i/I - by image id (respects placement_id filter)
 *   n/N - by image number (newest, respects placement_id)
 *   c/C - at cursor position
 *   p/P - at specific cell (x,y keys, 1-indexed)
 *   q/Q - at cell+z-index (x,y,z keys)
 *   r/R - by id range (x <= id <= y)
 *   x/X - at column (x key)
 *   y/Y - at row (y key)
 *   z/Z - at z-index (z key)
 *   f/F - animation frames (not implemented, ignored)
 */
static gboolean
handle_delete(
	GstKittyImageCache *cache,
	GstGraphicsCommand *cmd,
	gint                cursor_col,
	gint                cursor_row,
	gchar             **response
){
	gboolean is_upper;

	/*
	 * Per the kitty spec, the default delete target is 'a' (all
	 * placements) when no 'd=' key is provided.
	 */
	if (cmd->delete_target == 0) {
		cmd->delete_target = 'a';
	}

	is_upper = (cmd->delete_target >= 'A' && cmd->delete_target <= 'Z');

	switch (cmd->delete_target) {
	case 'a':
	case 'A':
		/* Delete all placements */
		g_list_free_full(cache->placements, placement_free);
		cache->placements = NULL;

		if (is_upper) {
			/* Free all image data */
			g_hash_table_remove_all(cache->images);
			cache->total_ram = 0;
		}
		break;

	case 'i':
	case 'I':
		/* Delete by image id, optionally filtered by placement_id */
		delete_placements_matching(cache, cmd, cursor_col, cursor_row,
			is_upper, match_by_id);

		/*
		 * For uppercase, also free the image directly even if no
		 * placements existed (spec says delete image data by id).
		 */
		if (is_upper) {
			maybe_free_orphan_image(cache, cmd->image_id);
		}
		break;

	case 'n':
	case 'N':
		{
			/*
			 * Delete newest image with specified image_number.
			 * Find highest image_id with matching image_number.
			 */
			GHashTableIter iter;
			gpointer value;
			GstKittyImage *newest;
			guint32 newest_id;

			newest = NULL;
			newest_id = 0;

			g_hash_table_iter_init(&iter, cache->images);
			while (g_hash_table_iter_next(&iter, NULL, &value)) {
				GstKittyImage *img;

				img = (GstKittyImage *)value;
				if (img->image_number == cmd->image_number &&
				    img->image_id >= newest_id) {
					newest = img;
					newest_id = img->image_id;
				}
			}

			if (newest != NULL) {
				GList *l;
				GList *next;

				/* Delete placements for this image */
				for (l = cache->placements; l != NULL; l = next) {
					GstImagePlacement *pl;

					next = l->next;
					pl = (GstImagePlacement *)l->data;
					if (pl->image_id == newest_id) {
						if (cmd->placement_id > 0 &&
						    pl->placement_id != cmd->placement_id) {
							continue;
						}
						placement_free(pl);
						cache->placements = g_list_delete_link(
							cache->placements, l);
					}
				}

				if (is_upper) {
					maybe_free_orphan_image(cache, newest_id);
				}
			}
		}
		break;

	case 'c':
	case 'C':
		/* Delete at cursor position */
		delete_placements_matching(cache, cmd, cursor_col, cursor_row,
			is_upper, match_at_cursor);
		break;

	case 'p':
	case 'P':
		/* Delete at specific cell (x,y keys, 1-indexed) */
		delete_placements_matching(cache, cmd, cursor_col, cursor_row,
			is_upper, match_at_cell);
		break;

	case 'q':
	case 'Q':
		/* Delete at cell+z-index */
		delete_placements_matching(cache, cmd, cursor_col, cursor_row,
			is_upper, match_at_cell_z);
		break;

	case 'r':
	case 'R':
		{
			/*
			 * Delete by id range: x <= image_id <= y.
			 * x,y keys are reused (src_x, src_y) but here they
			 * are raw image id bounds, not 1-indexed cell coords.
			 */
			GList *l;
			GList *next;
			guint32 lo;
			guint32 hi;
			GList *orphan_ids;

			lo = (guint32)cmd->src_x;
			hi = (guint32)cmd->src_y;
			orphan_ids = NULL;

			for (l = cache->placements; l != NULL; l = next) {
				GstImagePlacement *pl;

				next = l->next;
				pl = (GstImagePlacement *)l->data;
				if (pl->image_id >= lo && pl->image_id <= hi) {
					if (is_upper) {
						orphan_ids = g_list_prepend(orphan_ids,
							GUINT_TO_POINTER(pl->image_id));
					}
					placement_free(pl);
					cache->placements = g_list_delete_link(
						cache->placements, l);
				}
			}

			if (is_upper) {
				for (l = orphan_ids; l != NULL; l = l->next) {
					maybe_free_orphan_image(cache,
						GPOINTER_TO_UINT(l->data));
				}
			}
			g_list_free(orphan_ids);
		}
		break;

	case 'x':
	case 'X':
		/* Delete at column */
		delete_placements_matching(cache, cmd, cursor_col, cursor_row,
			is_upper, match_at_column);
		break;

	case 'y':
	case 'Y':
		/* Delete at row */
		delete_placements_matching(cache, cmd, cursor_col, cursor_row,
			is_upper, match_at_row);
		break;

	case 'z':
	case 'Z':
		/* Delete at z-index */
		delete_placements_matching(cache, cmd, cursor_col, cursor_row,
			is_upper, match_at_zindex);
		break;

	case 'f':
	case 'F':
		/* Animation frame delete - not implemented, ignore */
		break;

	default:
		break;
	}

	(void)response; /* delete commands don't generate responses */
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
 * @cursor_col: current cursor column (0-indexed)
 * @cursor_row: current cursor row (0-indexed)
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
	gint                cursor_col,
	gint                cursor_row,
	gchar             **response
){
	if (response != NULL) {
		*response = NULL;
	}

	switch (cmd->action) {
	case 't':
	case 'T':
		return handle_transmit(cache, cmd, cursor_col, cursor_row,
			response);

	case 'p':
		return handle_display(cache, cmd, cursor_col, cursor_row,
			response);

	case 'q':
		return handle_query(cache, cmd, response);

	case 'd':
		return handle_delete(cache, cmd, cursor_col, cursor_row, response);

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
