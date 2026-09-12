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
#include "window/gst-lrg-keymap.h"
#include "config/gst-keybind.h"
#include "config/gst-config.h"
#include <X11/keysym.h>
#include <X11/Xlib.h>

/* This exact helper is used by the Ctrl/Alt input path, independently of GL. */
static void
test_lrg_shifted_shortcuts(void)
{
	g_autoptr(GstConfig) config = gst_config_new();
	const GArray *bindings = gst_config_get_keybinds(config);
	const gchar *base = "abcdefghijklmnopqrstuvwxyz1234567890-=[]\\;',./`";
	const gchar *shifted = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()_+{}|:\"<>?~";
	guint i;

	for (i = 0; base[i] != '\0'; i++)
		g_assert_cmpuint(gst_lrg_shift_ascii((guint)base[i]), ==, (guint)shifted[i]);
	g_assert_cmpuint(gst_lrg_shift_ascii(' '), ==, ' ');
	g_assert_cmpuint(gst_lrg_shift_ascii(0), ==, 0);
	g_assert_cmpuint(gst_lrg_shift_ascii(XK_Escape), ==, XK_Escape);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, gst_lrg_shift_ascii('='), XK_equal,
		ControlMask | ShiftMask), ==, GST_ACTION_ZOOM_IN);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, gst_lrg_shift_ascii('-'), XK_minus,
		ControlMask | ShiftMask), ==, GST_ACTION_ZOOM_OUT);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, gst_lrg_shift_ascii('0'), XK_0,
		ControlMask | ShiftMask), ==, GST_ACTION_ZOOM_RESET);
}
#ifdef GST_HAVE_LRG_BACKEND
#include "../src/window/gst-lrg-window.c"
#include "rendering/gst-lrg-render-context.h"
#include "rendering/gst-lrg-renderer.h"
#include "core/gst-terminal.h"
#include <GL/gl.h>
#include <string.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTerminal, g_object_unref)

/* Observe the actual Ctrl/Alt event path, without creating an OpenGL window. */
static gboolean
capture_lrg_shortcut(GstWindow *win, guint keyval, guint base, guint code,
	guint state, guint event, const gchar *text, gint len, GstAction *action)
{
	(void)win;
	(void)code;
	(void)event;
	(void)text;
	(void)len;
	*action = gst_keybind_lookup_event(gst_config_get_keybinds(gst_config_get_default()),
		keyval, base, state);
	return TRUE;
}

static void
test_lrg_key_events(void)
{
	g_autoptr(GstLrgWindow) window = g_object_new(GST_TYPE_LRG_WINDOW, NULL);
	const GrlKey keys[] = { GRL_KEY_EQUAL, GRL_KEY_MINUS, GRL_KEY_ZERO, GRL_KEY_C, GRL_KEY_V };
	const GstAction actions[] = { GST_ACTION_ZOOM_IN, GST_ACTION_ZOOM_OUT,
		GST_ACTION_ZOOM_RESET, GST_ACTION_CLIPBOARD_COPY, GST_ACTION_CLIPBOARD_PASTE };
	GstAction action;
	guint i;

	g_signal_connect(window, "key-event", G_CALLBACK(capture_lrg_shortcut), &action);
	for (i = 0; i < G_N_ELEMENTS(keys); i++) {
		action = GST_ACTION_NONE;
		lrg_emit_key(window, keys[i], LRG_CONTROL_MASK | LRG_SHIFT_MASK, 1);
		g_assert_cmpint(action, ==, actions[i]);
	}
}

/* Read the presented framebuffer: the compositor must receive premultiplied
 * RGB and alpha, including opaque overlay pixels and repeated focus changes. */
static void
test_lrg_transparency(void)
{
	g_autoptr(GstLrgWindow) window = NULL;
	g_autoptr(GstTerminal) terminal = NULL;
	g_autoptr(GstGrlFontCache) fonts = NULL;
	g_autoptr(GstLrgRenderer) renderer = NULL;
	g_autoptr(GrlColor) white = NULL;
	const gdouble opacities[] = { 0.5, 0.0, 1.0, 0.25, 0.5 };
	guint i;
	gint alpha_bits;
	guint8 pixel[4];
	gint expected;

	if (g_getenv("GST_TEST_LRG_GRAPHICS") == NULL) {
		g_test_skip("Set GST_TEST_LRG_GRAPHICS=1 under a composited display");
		return;
	}
	window = gst_lrg_window_new(8, 4, 8, 16, 2);
	g_assert_nonnull(window);
	glGetIntegerv(GL_ALPHA_BITS, &alpha_bits);
	g_assert_cmpint(alpha_bits, >=, 8);
	terminal = gst_terminal_new(8, 4);
	fonts = gst_grl_font_cache_new();
	g_assert_true(gst_grl_font_cache_load_fonts(fonts, "monospace", 16));
	renderer = gst_lrg_renderer_new(terminal, window, fonts, 2);
	g_assert_true(gst_lrg_renderer_load_colors(renderer, NULL));
	gst_lrg_renderer_set_win_mode(renderer, GST_WIN_MODE_VISIBLE);
	white = grl_color_new(255, 255, 255, 255);
	for (i = 0; i < G_N_ELEMENTS(opacities); i++) {
		gst_window_set_opacity(GST_WINDOW(window), opacities[i]);
		g_assert_true(gst_renderer_start_draw(GST_RENDERER(renderer)));
		gst_renderer_render(GST_RENDERER(renderer));
		/* An opaque overlay must fade with the terminal, not cover it. */
		grl_draw_rectangle(0, 0, 32, 32, white);
		gst_renderer_finish_draw(GST_RENDERER(renderer));
		glReadBuffer(GL_FRONT);
		glReadPixels(8, grl_window_get_height(
			gst_lrg_window_get_grl_window(window)) - 8,
			1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
		g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);
		expected = (gint)(opacities[i] * 255.0 + 0.5);
		g_assert_cmpint(ABS((gint)pixel[0] - expected), <=, 1);
		g_assert_cmpint(ABS((gint)pixel[3] - expected), <=, 1);
	}
	/* Destroy GL resources while their context still exists. */
	g_clear_object(&renderer);
	gst_grl_font_cache_unload_fonts(fonts);
}

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
	g_test_add_func("/lrg/shifted-shortcuts", test_lrg_shifted_shortcuts);

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
	g_test_add_func("/lrg/key-events", test_lrg_key_events);
	g_test_add_func("/lrg/render-context/transparency", test_lrg_transparency);
	g_test_add_func("/lrg/render-context/image-upload", test_lrg_image_upload);
#endif

	return g_test_run();
}
