/*
 * test-kitty-cache.c - Regressions for kitty image replacement and cropping
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>

/* Compile the actual loadable-module implementation into this test. */
#include "../modules/kittygfx/gst-kittygfx-parser.c"
#include "../modules/kittygfx/gst-kittygfx-image.c"
#include "../modules/kittygfx/gst-kittygfx-module.c"

static void
process_command(GstKittyImageCache *cache, const gchar *sequence)
{
	GstGraphicsCommand command;
	gchar *response;

	g_assert_true(gst_gfx_command_parse(sequence, strlen(sequence), &command));
	response = NULL;
	g_assert_true(gst_kitty_image_cache_process(cache, &command,
		0, 0, &response));
	g_free(response);
}

static void
upload_image(GstKittyImageCache *cache, guint32 image_id, guint8 value)
{
	guint8 *pixels;
	gchar *payload;
	gchar *sequence;
	GstKittyImage *image;
	gsize pixel_bytes;

	pixel_bytes = 256 * 256 * 4;
	pixels = g_malloc(pixel_bytes);
	memset(pixels, value, pixel_bytes);
	payload = g_base64_encode(pixels, pixel_bytes);
	sequence = g_strdup_printf("a=t,f=32,s=256,v=256,i=%u;%s",
		image_id, payload);
	process_command(cache, sequence);

	image = gst_kitty_image_cache_get_image(cache, image_id);
	g_assert_nonnull(image);
	g_assert_cmpint(image->width, ==, 256);
	g_assert_cmpint(image->height, ==, 256);
	g_assert_cmpmem(image->data, pixel_bytes, pixels, pixel_bytes);

	g_free(sequence);
	g_free(payload);
	g_free(pixels);
}

static void
test_kitty_replacement_preserves_resident_images(void)
{
	GstKittyImageCache *cache;

	cache = gst_kitty_image_cache_new(1, 1, 16);
	upload_image(cache, 1, 0x11);
	upload_image(cache, 2, 0x22);
	upload_image(cache, 2, 0x33);
	upload_image(cache, 2, 0x44);
	upload_image(cache, 3, 0x55);

	/* Three 256 KiB images fit in 1 MiB regardless of replacement history.
	 * Checking every ID makes the result independent of LRU timestamp ties. */
	g_assert_nonnull(gst_kitty_image_cache_get_image(cache, 1));
	g_assert_nonnull(gst_kitty_image_cache_get_image(cache, 2));
	g_assert_nonnull(gst_kitty_image_cache_get_image(cache, 3));

	gst_kitty_image_cache_free(cache);
}

static gint image_draw_calls;

static void
count_image_draw(
	GstRenderContext *context,
	const guint8     *pixels,
	gint              source_width,
	gint              source_height,
	gint              stride,
	gint              x,
	gint              y,
	gint              width,
	gint              height
){
	(void)context;
	(void)pixels;
	(void)source_width;
	(void)source_height;
	(void)stride;
	(void)x;
	(void)y;
	(void)width;
	(void)height;

	/* Do not dereference pixels: the regression passes an invalid address. */
	image_draw_calls++;
}

static void
test_kitty_out_of_range_crop(gconstpointer data)
{
	static const GstRenderContextOps ops = {
		.draw_image = count_image_draw
	};
	GstKittygfxModule *module;
	GstModuleManager *manager;
	GstTerminal *terminal;
	GstRenderContext context;
	GstGraphicsCommand command;
	gchar *sequence;
	gchar *response;

	terminal = gst_terminal_new(80, 24);
	manager = gst_module_manager_get_default();
	gst_module_manager_set_terminal(manager, terminal);
	module = g_object_new(GST_TYPE_KITTYGFX_MODULE, NULL);
	g_assert_true(gst_module_activate(GST_MODULE(module)));

	memset(&context, 0, sizeof(context));
	context.ops = &ops;
	context.cw = 8;
	context.ch = 16;
	process_command(module->cache, "a=T,f=32,s=1,v=1,i=1;/wAA/w==");
	image_draw_calls = 0;
	kittygfx_render(GST_RENDER_OVERLAY(module), &context, 640, 384);
	g_assert_cmpint(image_draw_calls, ==, 1);

	/* Delete the valid placement, retaining its image for the invalid crop. */
	process_command(module->cache, "a=d,d=a");
	sequence = g_strdup_printf("a=p,i=1,%s=4294967295", (const gchar *)data);
	response = NULL;
	if (gst_gfx_command_parse(sequence, strlen(sequence), &command)) {
		gst_kitty_image_cache_process(module->cache, &command,
			0, 0, &response);
	}

	/* Rejection at either parsing, placement, or rendering is safe. */
	image_draw_calls = 0;
	kittygfx_render(GST_RENDER_OVERLAY(module), &context, 640, 384);
	g_assert_cmpint(image_draw_calls, ==, 0);

	g_free(response);
	g_free(sequence);
	gst_module_deactivate(GST_MODULE(module));
	g_object_unref(module);
	gst_module_manager_set_terminal(manager, NULL);
	g_object_unref(terminal);
}

/* Virtual prototypes have no physical location and survive screen erasure. */
static void
test_kitty_virtual_lifetime(void)
{
	GstKittyImageCache *cache;
	GstImagePlacement *pl;
	GList *visible;

	cache = gst_kitty_image_cache_new(1, 1, 16);
	process_command(cache, "a=T,U=1,c=2,r=2,i=42,p=7,f=32,s=1,v=1,m=1;/wAA");
	process_command(cache, "m=0;/w==");
	g_assert_cmpuint(g_list_length(cache->placements), ==, 1);
	pl = (GstImagePlacement *)cache->placements->data;
	g_assert_true(pl->virtual_placement);
	g_assert_cmpuint(pl->placement_id, ==, 7);
	visible = gst_kitty_image_cache_get_visible_placements(cache, 0, 24);
	g_assert_null(visible);
	process_command(cache, "a=d,d=A");
	process_command(cache, "a=d,d=Z,z=0");
	process_command(cache, "a=d,d=C");
	gst_kitty_image_cache_clear_alt(cache);
	gst_kitty_image_cache_scroll(cache, 5);
	g_assert_cmpuint(g_list_length(cache->placements), ==, 1);
	g_assert_cmpint(pl->row, ==, 0);
	g_assert_nonnull(gst_kitty_image_cache_get_image(cache, 42));
	process_command(cache, "a=d,d=I,i=42,p=7");
	g_assert_null(cache->placements);
	g_assert_null(gst_kitty_image_cache_get_image(cache, 42));
	gst_kitty_image_cache_free(cache);
}

static void
test_kitty_placement_replacement(void)
{
	GstKittyImageCache *cache;
	GstImagePlacement *pl;

	cache = gst_kitty_image_cache_new(1, 1, 16);
	process_command(cache, "a=T,i=1,p=8,c=2,r=2,f=32,s=1,v=1;/wAA/w==");
	process_command(cache, "a=p,i=1,p=8,c=4,r=3");
	g_assert_cmpuint(g_list_length(cache->placements), ==, 1);
	pl = (GstImagePlacement *)cache->placements->data;
	g_assert_cmpint(pl->dst_cols, ==, 4);
	process_command(cache, "a=p,i=1");
	process_command(cache, "a=p,i=1");
	g_assert_cmpuint(g_list_length(cache->placements), ==, 3);
	/* Re-transmission must discard all old placements, not repaint them. */
	process_command(cache, "a=t,i=1,f=32,s=1,v=1;AP8A/w==");
	g_assert_null(cache->placements);
	gst_kitty_image_cache_free(cache);
}

static void
test_kitty_overwrite_and_scroll(void)
{
	GstKittyImageCache *cache;
	GstImagePlacement *pl;
	GList *visible;

	cache = gst_kitty_image_cache_new(1, 1, 16);
	process_command(cache, "a=T,i=1,c=4,r=3,f=32,s=1,v=1;/wAA/w==");
	pl = (GstImagePlacement *)cache->placements->data;
	pl->row = 3;
	gst_kitty_image_cache_scroll_region(cache, 0, 2, 1);
	g_assert_cmpint(pl->row, ==, 3);
	gst_kitty_image_cache_scroll_region(cache, 0, 10, 4);
	g_assert_cmpint(pl->row, ==, -1);
	visible = gst_kitty_image_cache_get_visible_placements(cache, 0, 10);
	g_assert_cmpuint(g_list_length(visible), ==, 1);
	g_list_free(visible);
	gst_kitty_image_cache_erase(cache, 4, 0, 4, 0);
	g_assert_nonnull(cache->placements);
	gst_kitty_image_cache_erase(cache, 3, 1, 3, 1);
	g_assert_null(cache->placements);
	g_assert_nonnull(gst_kitty_image_cache_get_image(cache, 1));
	gst_kitty_image_cache_free(cache);
}

static void
test_kitty_placeholder_coordinates(void)
{
	GstLine *line;
	GstGlyph *g;
	guint32 image_id;
	gint row;
	gint col;
	gint i;

	line = gst_line_new(5);
	for (i = 0; i < 5; i++) {
		g = gst_line_get_glyph(line, i);
		g->rune = 0x10EEEE;
		g->fg = 42;
	}
	g = gst_line_get_glyph(line, 0);
	gst_glyph_append(g, 0x030D);
	gst_glyph_append(g, 0x0305);
	gst_glyph_append(g, 0x030E);
	g_assert_true(placeholder_coordinates(line, 1, &image_id, &row, &col));
	g_assert_cmpuint(image_id, ==, 42U + (2U << 24));
	g_assert_cmpint(row, ==, 1);
	g_assert_cmpint(col, ==, 1);
	/* Explicit row/column still inherit the high byte when contiguous. */
	g = gst_line_get_glyph(line, 2);
	gst_glyph_append(g, 0x030D);
	gst_glyph_append(g, 0x030E);
	g_assert_true(placeholder_coordinates(line, 2, &image_id, &row, &col));
	g_assert_cmpuint(image_id, ==, 42U + (2U << 24));
	g_assert_cmpint(col, ==, 2);
	/* A color change starts a new image rather than leaking coordinates. */
	g = gst_line_get_glyph(line, 3);
	g->fg = GST_TRUECOLOR(1, 2, 3);
	g_assert_true(placeholder_coordinates(line, 3, &image_id, &row, &col));
	g_assert_cmpuint(image_id, ==, 0x010203);
	g_assert_cmpint(row, ==, 0);
	g_assert_cmpint(col, ==, 0);
	/* Invalid combining marks must not accidentally address another image. */
	gst_glyph_append(g, 0x0300);
	g_assert_false(placeholder_coordinates(line, 3, &image_id, &row, &col));
	gst_line_free(line);
}

static void
ignore_cell_background(GstRenderContext *ctx, gint x, gint y, gint w, gint h)
{
	(void)ctx; (void)x; (void)y; (void)w; (void)h;
}

static guint8 placeholder_pixel[4];

static void
capture_placeholder_pixel(GstRenderContext *ctx, const guint8 *pixels,
	gint sw, gint sh, gint stride, gint x, gint y, gint w, gint h)
{
	(void)ctx; (void)x; (void)y;
	g_assert_cmpint(sw, ==, w);
	g_assert_cmpint(sh, ==, h);
	g_assert_cmpint(stride, ==, w * 4);
	memcpy(placeholder_pixel, pixels, 4);
	image_draw_calls++;
}

static void
test_kitty_placeholder_overwrite(void)
{
	static const GstRenderContextOps ops = {
		.fill_rect_bg = ignore_cell_background,
		.draw_image = capture_placeholder_pixel
	};
	GstKittygfxModule *module;
	GstLine *line;
	GstGlyph *g;
	GstRenderContext ctx;

	module = g_object_new(GST_TYPE_KITTYGFX_MODULE, NULL);
	g_assert_true(gst_module_activate(GST_MODULE(module)));
	process_command(module->cache, "a=T,U=1,c=2,r=2,i=42,f=32,s=1,v=1;/wAA/w==");
	line = gst_line_new(4);
	g = gst_line_get_glyph(line, 2);
	g->rune = 0x10EEEE;
	g->fg = 42;
	gst_glyph_append(g, 0x0305);
	gst_glyph_append(g, 0x0305);
	memset(&ctx, 0, sizeof(ctx));
	ctx.ops = &ops;
	ctx.current_line = line;
	ctx.current_col = 2;
	image_draw_calls = 0;
	g_assert_true(kittygfx_transform_glyph(GST_GLYPH_TRANSFORMER(module),
		g->rune, &ctx, 16, 0, 8, 16));
	g_assert_cmpint(image_draw_calls, ==, 1);
	g_assert_cmpuint(placeholder_pixel[0], ==, 255);
	g_assert_cmpuint(placeholder_pixel[3], ==, 255);
	/* The square source fits a 16x32 placement without vertical distortion;
	 * cells in its lower half are transparent rather than stretched. */
	gst_glyph_clear(g);
	gst_glyph_append(g, 0x030D);
	gst_glyph_append(g, 0x0305);
	g_assert_true(kittygfx_transform_glyph(GST_GLYPH_TRANSFORMER(module),
		g->rune, &ctx, 16, 16, 8, 16));
	g_assert_cmpint(image_draw_calls, ==, 2);
	g_assert_cmpuint(placeholder_pixel[3], ==, 0);
	gst_glyph_reset(g);
	g_assert_false(kittygfx_transform_glyph(GST_GLYPH_TRANSFORMER(module),
		g->rune, &ctx, 16, 0, 8, 16));
	g_assert_cmpint(image_draw_calls, ==, 2);
	g_assert_cmpuint(g_list_length(module->cache->placements), ==, 1);
	gst_line_free(line);
	g_object_unref(module);
}

static void
test_kitty_animation_rejected(void)
{
	GstKittyImageCache *cache;
	GstGraphicsCommand cmd;
	gchar *response;

	cache = gst_kitty_image_cache_new(1, 1, 16);
	g_assert_true(gst_gfx_command_parse("a=f,i=42", 8, &cmd));
	response = NULL;
	g_assert_true(gst_kitty_image_cache_process(cache, &cmd, 0, 0, &response));
	g_assert_nonnull(strstr(response, "ENOTSUP:"));
	g_free(response);
	cmd.quiet = 2;
	g_assert_true(gst_kitty_image_cache_process(cache, &cmd, 0, 0, &response));
	g_assert_null(response);
	gst_kitty_image_cache_free(cache);
}

static void
test_kitty_delete_aborts_upload(void)
{
	GstKittyImageCache *cache;

	cache = gst_kitty_image_cache_new(1, 1, 16);
	process_command(cache, "a=t,i=42,f=32,s=1,v=1,m=1;/wAA");
	g_assert_cmpuint(g_hash_table_size(cache->uploads), ==, 1);
	process_command(cache, "a=d,d=i,i=99");
	g_assert_cmpuint(g_hash_table_size(cache->uploads), ==, 0);
	g_assert_cmpuint(cache->last_image_id, ==, 0);
	process_command(cache, "m=0;/w==");
	g_assert_null(gst_kitty_image_cache_get_image(cache, 42));
	gst_kitty_image_cache_free(cache);
}

int
main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/kitty/cache/replacement-preserves-resident-images",
		test_kitty_replacement_preserves_resident_images);
	g_test_add_data_func("/kitty/render/out-of-range-crop-x", "x",
		test_kitty_out_of_range_crop);
	g_test_add_data_func("/kitty/render/out-of-range-crop-y", "y",
		test_kitty_out_of_range_crop);
	g_test_add_func("/kitty/cache/virtual-lifetime", test_kitty_virtual_lifetime);
	g_test_add_func("/kitty/cache/placement-replacement", test_kitty_placement_replacement);
	g_test_add_func("/kitty/cache/overwrite-scroll", test_kitty_overwrite_and_scroll);
	g_test_add_func("/kitty/placeholder/coordinates", test_kitty_placeholder_coordinates);
	g_test_add_func("/kitty/placeholder/overwrite", test_kitty_placeholder_overwrite);
	g_test_add_func("/kitty/animation/rejected", test_kitty_animation_rejected);
	g_test_add_func("/kitty/cache/delete-aborts-upload", test_kitty_delete_aborts_upload);

	return g_test_run();
}
