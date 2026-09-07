/*
 * test-lrg-backend.c - Tests for the libregnum (LRG) backend enums/helpers
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Covers the GstLrgRenderMode parsing helpers and the GstBackendType /
 * GstLrgRenderMode GType registrations. These are compiled unconditionally
 * (in gst-enums.c), so the test runs regardless of LRG_BACKEND. The window
 * and renderer themselves need a raylib/GL context and are exercised by the
 * manual --lrg verification, not here.
 */

#include <glib.h>
#include "gst-enums.h"
#ifdef GST_HAVE_LRG_BACKEND
#include "rendering/gst-lrg-render-context.h"
#include <string.h>

/* Capability and invalid-input paths must work without creating a GL window. */
static void
test_lrg_render_context_ops(void)
{
	GstLrgRenderContext ctx;
	guint8 pixels[4] = { 255, 0, 0, 255 };

	memset(&ctx, 0, sizeof(ctx));
	gst_lrg_render_context_init_ops(&ctx);
	g_assert_nonnull(ctx.base.ops->draw_image);
	g_assert_nonnull(ctx.base.ops->draw_glyph_id);
	ctx.frame_textures = g_ptr_array_new_with_free_func(g_object_unref);
	gst_render_context_draw_image(&ctx.base, pixels, 1, 1, 3, 0, 0, 1, 1);
	gst_render_context_draw_image(&ctx.base, pixels, G_MAXINT, 1, 4, 0, 0, 1, 1);
	gst_render_context_draw_image(&ctx.base, pixels, 1, 1, 4, 0, 0, 0, 1);
	gst_render_context_draw_glyph_id(&ctx.base, 1, GST_FONT_STYLE_NORMAL, 0, 0);
	g_assert_cmpuint(ctx.frame_textures->len, ==, 0);
	g_ptr_array_unref(ctx.frame_textures);
}

/* Opt in explicitly because raylib can terminate the process when no usable
 * display/GL context exists. Download the actual GPU upload, not a mock. */
static void
test_lrg_image_upload(void)
{
	GstLrgRenderContext ctx;
	g_autoptr(GrlWindow) window = NULL;
	g_autoptr(GrlImage) downloaded = NULL;
	g_autoptr(GrlColor) top = NULL;
	g_autoptr(GrlColor) bottom = NULL;
	g_autoptr(GstGrlFontCache) fonts = NULL;
	cairo_scaled_font_t *scaled;
	gulong glyph_id;
	const guint8 pixels[] = {
		255, 0, 0, 255, 99, 99, 99, 99,
		0, 255, 0, 128, 99, 99, 99, 99
	};

	if (g_getenv("GST_TEST_LRG_GRAPHICS") == NULL) {
		g_test_skip("Set GST_TEST_LRG_GRAPHICS=1 under a real display or Xvfb");
		return;
	}
	window = grl_window_new(64, 64, "GST graphics regression");
	g_assert_nonnull(window);
	g_assert_true(grl_window_is_ready(window));
	memset(&ctx, 0, sizeof(ctx));
	gst_lrg_render_context_init_ops(&ctx);
	ctx.frame_textures = g_ptr_array_new_with_free_func(g_object_unref);
	grl_window_begin_drawing(window);
	gst_render_context_draw_image(&ctx.base, pixels, 1, 2, 8, -2, 4, 16, 32);
	g_assert_cmpuint(ctx.frame_textures->len, ==, 1);
	downloaded = grl_texture_to_image(g_ptr_array_index(ctx.frame_textures, 0));
	g_assert_nonnull(downloaded);
	top = grl_image_get_pixel(downloaded, 0, 0);
	bottom = grl_image_get_pixel(downloaded, 0, 1);
	g_assert_cmpuint(top->r, ==, 255);
	g_assert_cmpuint(top->g, ==, 0);
	g_assert_cmpuint(bottom->g, ==, 255);
	g_assert_cmpuint(bottom->a, ==, 128);
	fonts = gst_grl_font_cache_new();
	g_assert_true(gst_grl_font_cache_load_fonts(fonts, "monospace", 16));
	g_assert_true(gst_cairo_font_cache_lookup_glyph(
		gst_grl_font_cache_get_cairo_cache(fonts), 'A', GST_FONT_STYLE_NORMAL,
		&scaled, &glyph_id));
	ctx.font_cache = fonts;
	ctx.fg = GST_COLOR_RGB(255, 255, 255);
	gst_render_context_draw_glyph_id(&ctx.base, (guint32)glyph_id,
		GST_FONT_STYLE_NORMAL, 20, 20);
	grl_window_swap_buffers(window);
	/* GPU resources must outlive the deferred batch but not the window. */
	g_ptr_array_unref(ctx.frame_textures);
	gst_grl_font_cache_unload_fonts(fonts);
}
#endif

/* ===== render mode: from_string ===== */

static void
test_render_mode_from_string(void)
{
	GstLrgRenderMode mode = GST_LRG_RENDER_MODE_3DVR;

	/* Bare --lrg (NULL / empty) defaults to 2D and succeeds. */
	g_assert_true(gst_lrg_render_mode_from_string(NULL, &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);

	mode = GST_LRG_RENDER_MODE_3DVR;
	g_assert_true(gst_lrg_render_mode_from_string("", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);

	/* Explicit modes. */
	g_assert_true(gst_lrg_render_mode_from_string("2d", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);

	g_assert_true(gst_lrg_render_mode_from_string("3d", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_3D);

	g_assert_true(gst_lrg_render_mode_from_string("3dvr", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_3DVR);

	/* Case-insensitive. */
	g_assert_true(gst_lrg_render_mode_from_string("2D", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);
}

static void
test_render_mode_from_string_invalid(void)
{
	GstLrgRenderMode mode = GST_LRG_RENDER_MODE_3D;

	/* Unknown modes fail and leave the out parameter at 2D. */
	g_assert_false(gst_lrg_render_mode_from_string("bogus", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);

	/* A NULL out parameter is tolerated. */
	g_assert_true(gst_lrg_render_mode_from_string("3d", NULL));
	g_assert_false(gst_lrg_render_mode_from_string("nope", NULL));
}

/* ===== render mode: to_string ===== */

static void
test_render_mode_to_string(void)
{
	g_assert_cmpstr(gst_lrg_render_mode_to_string(GST_LRG_RENDER_MODE_2D),
		==, "2d");
	g_assert_cmpstr(gst_lrg_render_mode_to_string(GST_LRG_RENDER_MODE_3D),
		==, "3d");
	g_assert_cmpstr(gst_lrg_render_mode_to_string(GST_LRG_RENDER_MODE_3DVR),
		==, "3dvr");
}

/* ===== render mode: is_implemented ===== */

static void
test_render_mode_is_implemented(void)
{
	g_assert_true(gst_lrg_render_mode_is_implemented(GST_LRG_RENDER_MODE_2D));
	g_assert_false(gst_lrg_render_mode_is_implemented(GST_LRG_RENDER_MODE_3D));
	g_assert_false(gst_lrg_render_mode_is_implemented(GST_LRG_RENDER_MODE_3DVR));
}

/* ===== GType registrations ===== */

static void
test_backend_type_registered(void)
{
	GEnumClass *klass;
	GEnumValue *value;

	g_assert_true(G_TYPE_IS_ENUM(GST_TYPE_BACKEND_TYPE));

	klass = g_type_class_ref(GST_TYPE_BACKEND_TYPE);

	value = g_enum_get_value(klass, GST_BACKEND_LRG);
	g_assert_nonnull(value);
	g_assert_cmpstr(value->value_nick, ==, "lrg");

	/* The existing backends remain registered. */
	g_assert_nonnull(g_enum_get_value(klass, GST_BACKEND_X11));
	g_assert_nonnull(g_enum_get_value(klass, GST_BACKEND_WAYLAND));

	g_type_class_unref(klass);
}

static void
test_render_mode_type_registered(void)
{
	GEnumClass *klass;
	GEnumValue *value;

	g_assert_true(G_TYPE_IS_ENUM(GST_TYPE_LRG_RENDER_MODE));

	klass = g_type_class_ref(GST_TYPE_LRG_RENDER_MODE);

	value = g_enum_get_value_by_nick(klass, "2d");
	g_assert_nonnull(value);
	g_assert_cmpint(value->value, ==, GST_LRG_RENDER_MODE_2D);

	g_type_class_unref(klass);
}

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/lrg/render-mode/from-string",
		test_render_mode_from_string);
	g_test_add_func("/lrg/render-mode/from-string-invalid",
		test_render_mode_from_string_invalid);
	g_test_add_func("/lrg/render-mode/to-string",
		test_render_mode_to_string);
	g_test_add_func("/lrg/render-mode/is-implemented",
		test_render_mode_is_implemented);
	g_test_add_func("/lrg/backend-type/registered",
		test_backend_type_registered);
	g_test_add_func("/lrg/render-mode/type-registered",
		test_render_mode_type_registered);
#ifdef GST_HAVE_LRG_BACKEND
	g_test_add_func("/lrg/render-context/ops", test_lrg_render_context_ops);
	g_test_add_func("/lrg/render-context/image-upload", test_lrg_image_upload);
#endif

	return g_test_run();
}
