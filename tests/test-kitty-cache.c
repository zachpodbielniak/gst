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

	return g_test_run();
}
