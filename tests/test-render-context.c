/*
 * test-render-context.c - Tests for abstract render context and vtable dispatch
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests the abstract GstRenderContext vtable dispatch, GstBackendType
 * enum, and common field access. Uses mock vtable implementations
 * to verify dispatch without needing an X11 display or Wayland compositor.
 */

#include <glib.h>
#include <glib-object.h>
#include "gst-types.h"
#include "gst-enums.h"
#include "rendering/gst-render-context.h"

/* ===== Mock vtable tracking ===== */

/* Counters for mock vtable calls */
static gint mock_fill_rect_calls = 0;
static gint mock_fill_rect_rgba_calls = 0;
static gint mock_fill_rect_fg_calls = 0;
static gint mock_fill_rect_bg_calls = 0;
static gint mock_draw_glyph_calls = 0;

/* Last call parameters for verification */
static guint mock_last_color_idx = 0;
static gint mock_last_x = 0;
static gint mock_last_y = 0;
static gint mock_last_w = 0;
static gint mock_last_h = 0;
static GstRune mock_last_rune = 0;
static guint mock_last_fg_idx = 0;
static guint mock_last_bg_idx = 0;

static void
reset_mock_counters(void)
{
	mock_fill_rect_calls = 0;
	mock_fill_rect_rgba_calls = 0;
	mock_fill_rect_fg_calls = 0;
	mock_fill_rect_bg_calls = 0;
	mock_draw_glyph_calls = 0;
	mock_last_color_idx = 0;
	mock_last_x = 0;
	mock_last_y = 0;
	mock_last_w = 0;
	mock_last_h = 0;
	mock_last_rune = 0;
	mock_last_fg_idx = 0;
	mock_last_bg_idx = 0;
}

/* ===== Mock vtable implementations ===== */

static void
mock_fill_rect(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint             color_idx
){
	mock_fill_rect_calls++;
	mock_last_x = x;
	mock_last_y = y;
	mock_last_w = w;
	mock_last_h = h;
	mock_last_color_idx = color_idx;
}

static void
mock_fill_rect_rgba(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h,
	guint8            r,
	guint8            g,
	guint8            b,
	guint8            a
){
	mock_fill_rect_rgba_calls++;
	mock_last_x = x;
	mock_last_y = y;
	mock_last_w = w;
	mock_last_h = h;
}

static void
mock_fill_rect_fg(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	mock_fill_rect_fg_calls++;
	mock_last_x = x;
	mock_last_y = y;
	mock_last_w = w;
	mock_last_h = h;
}

static void
mock_fill_rect_bg(
	GstRenderContext *ctx,
	gint              x,
	gint              y,
	gint              w,
	gint              h
){
	mock_fill_rect_bg_calls++;
	mock_last_x = x;
	mock_last_y = y;
	mock_last_w = w;
	mock_last_h = h;
}

static void
mock_draw_glyph(
	GstRenderContext *ctx,
	GstRune           rune,
	GstFontStyle      style,
	gint              px,
	gint              py,
	guint             fg_idx,
	guint             bg_idx,
	guint16           attr
){
	mock_draw_glyph_calls++;
	mock_last_rune = rune;
	mock_last_x = px;
	mock_last_y = py;
	mock_last_fg_idx = fg_idx;
	mock_last_bg_idx = bg_idx;
}

static const GstRenderContextOps mock_ops = {
	mock_fill_rect,
	mock_fill_rect_rgba,
	mock_fill_rect_fg,
	mock_fill_rect_bg,
	mock_draw_glyph
};

/*
 * create_mock_context:
 *
 * Creates a stack-allocated render context with mock ops and
 * common fields populated with test values.
 */
static GstRenderContext
create_mock_context(void)
{
	GstRenderContext ctx;

	ctx.ops = &mock_ops;
	ctx.backend = GST_BACKEND_X11;
	ctx.cw = 8;
	ctx.ch = 16;
	ctx.borderpx = 2;
	ctx.win_w = 640;
	ctx.win_h = 480;
	ctx.win_mode = GST_WIN_MODE_VISIBLE | GST_WIN_MODE_FOCUSED;
	ctx.glyph_attr = 0;

	return ctx;
}

/* ===== Tests ===== */

/*
 * test_backend_type_enum:
 *
 * Verifies GstBackendType GType registration and enum values.
 */
static void
test_backend_type_enum(void)
{
	GType type;
	GEnumClass *klass;
	GEnumValue *val;

	type = gst_backend_type_get_type();
	g_assert_true(type != G_TYPE_INVALID);
	g_assert_true(g_type_is_a(type, G_TYPE_ENUM));

	klass = (GEnumClass *)g_type_class_ref(type);
	g_assert_nonnull(klass);

	/* Check X11 value */
	val = g_enum_get_value(klass, GST_BACKEND_X11);
	g_assert_nonnull(val);
	g_assert_cmpstr(val->value_nick, ==, "x11");

	/* Check Wayland value */
	val = g_enum_get_value(klass, GST_BACKEND_WAYLAND);
	g_assert_nonnull(val);
	g_assert_cmpstr(val->value_nick, ==, "wayland");

	g_type_class_unref(klass);
}

/*
 * test_render_context_common_fields:
 *
 * Verifies that common fields are accessible on the base struct.
 */
static void
test_render_context_common_fields(void)
{
	GstRenderContext ctx;

	ctx = create_mock_context();

	g_assert_cmpint(ctx.cw, ==, 8);
	g_assert_cmpint(ctx.ch, ==, 16);
	g_assert_cmpint(ctx.borderpx, ==, 2);
	g_assert_cmpint(ctx.win_w, ==, 640);
	g_assert_cmpint(ctx.win_h, ==, 480);
	g_assert_true(ctx.backend == GST_BACKEND_X11);
	g_assert_nonnull(ctx.ops);
}

/*
 * test_fill_rect_dispatch:
 *
 * Verifies fill_rect dispatches through the vtable with correct args.
 */
static void
test_fill_rect_dispatch(void)
{
	GstRenderContext ctx;

	ctx = create_mock_context();
	reset_mock_counters();

	gst_render_context_fill_rect(&ctx, 10, 20, 100, 50, 42);

	g_assert_cmpint(mock_fill_rect_calls, ==, 1);
	g_assert_cmpint(mock_last_x, ==, 10);
	g_assert_cmpint(mock_last_y, ==, 20);
	g_assert_cmpint(mock_last_w, ==, 100);
	g_assert_cmpint(mock_last_h, ==, 50);
	g_assert_cmpuint(mock_last_color_idx, ==, 42);
}

/*
 * test_fill_rect_rgba_dispatch:
 *
 * Verifies fill_rect_rgba dispatches through the vtable.
 */
static void
test_fill_rect_rgba_dispatch(void)
{
	GstRenderContext ctx;

	ctx = create_mock_context();
	reset_mock_counters();

	gst_render_context_fill_rect_rgba(&ctx, 5, 10, 80, 40,
		0xFF, 0x80, 0x40, 0xFF);

	g_assert_cmpint(mock_fill_rect_rgba_calls, ==, 1);
	g_assert_cmpint(mock_last_x, ==, 5);
	g_assert_cmpint(mock_last_y, ==, 10);
	g_assert_cmpint(mock_last_w, ==, 80);
	g_assert_cmpint(mock_last_h, ==, 40);
}

/*
 * test_fill_rect_fg_dispatch:
 *
 * Verifies fill_rect_fg dispatches through the vtable.
 */
static void
test_fill_rect_fg_dispatch(void)
{
	GstRenderContext ctx;

	ctx = create_mock_context();
	reset_mock_counters();

	gst_render_context_fill_rect_fg(&ctx, 0, 0, 200, 100);

	g_assert_cmpint(mock_fill_rect_fg_calls, ==, 1);
	g_assert_cmpint(mock_last_w, ==, 200);
	g_assert_cmpint(mock_last_h, ==, 100);
}

/*
 * test_fill_rect_bg_dispatch:
 *
 * Verifies fill_rect_bg dispatches through the vtable.
 */
static void
test_fill_rect_bg_dispatch(void)
{
	GstRenderContext ctx;

	ctx = create_mock_context();
	reset_mock_counters();

	gst_render_context_fill_rect_bg(&ctx, 3, 7, 50, 25);

	g_assert_cmpint(mock_fill_rect_bg_calls, ==, 1);
	g_assert_cmpint(mock_last_x, ==, 3);
	g_assert_cmpint(mock_last_y, ==, 7);
	g_assert_cmpint(mock_last_w, ==, 50);
	g_assert_cmpint(mock_last_h, ==, 25);
}

/*
 * test_draw_glyph_dispatch:
 *
 * Verifies draw_glyph dispatches through the vtable with correct args.
 */
static void
test_draw_glyph_dispatch(void)
{
	GstRenderContext ctx;

	ctx = create_mock_context();
	reset_mock_counters();

	gst_render_context_draw_glyph(&ctx, (GstRune)'A',
		GST_FONT_STYLE_NORMAL, 16, 32, 256, 257, 0);

	g_assert_cmpint(mock_draw_glyph_calls, ==, 1);
	g_assert_cmpuint(mock_last_rune, ==, (GstRune)'A');
	g_assert_cmpint(mock_last_x, ==, 16);
	g_assert_cmpint(mock_last_y, ==, 32);
	g_assert_cmpuint(mock_last_fg_idx, ==, 256);
	g_assert_cmpuint(mock_last_bg_idx, ==, 257);
}

/*
 * test_multiple_dispatch_calls:
 *
 * Verifies that call counters accumulate correctly across
 * multiple vtable dispatches.
 */
static void
test_multiple_dispatch_calls(void)
{
	GstRenderContext ctx;

	ctx = create_mock_context();
	reset_mock_counters();

	gst_render_context_fill_rect(&ctx, 0, 0, 10, 10, 0);
	gst_render_context_fill_rect(&ctx, 0, 0, 10, 10, 1);
	gst_render_context_fill_rect(&ctx, 0, 0, 10, 10, 2);
	gst_render_context_fill_rect_fg(&ctx, 0, 0, 10, 10);
	gst_render_context_fill_rect_bg(&ctx, 0, 0, 10, 10);
	gst_render_context_draw_glyph(&ctx, (GstRune)'X',
		GST_FONT_STYLE_BOLD, 0, 0, 256, 257, 0);

	g_assert_cmpint(mock_fill_rect_calls, ==, 3);
	g_assert_cmpint(mock_fill_rect_fg_calls, ==, 1);
	g_assert_cmpint(mock_fill_rect_bg_calls, ==, 1);
	g_assert_cmpint(mock_draw_glyph_calls, ==, 1);

	/* Verify last fill_rect got color_idx=2 */
	g_assert_cmpuint(mock_last_color_idx, ==, 2);

	/* Verify last glyph was 'X' */
	g_assert_cmpuint(mock_last_rune, ==, (GstRune)'X');
}

/*
 * test_win_mode_in_context:
 *
 * Verifies win_mode flags can be tested on the abstract context.
 */
static void
test_win_mode_in_context(void)
{
	GstRenderContext ctx;

	ctx = create_mock_context();

	g_assert_true((ctx.win_mode & GST_WIN_MODE_VISIBLE) != 0);
	g_assert_true((ctx.win_mode & GST_WIN_MODE_FOCUSED) != 0);
	g_assert_false((ctx.win_mode & GST_WIN_MODE_NUMLOCK) != 0);

	/* Toggle focus off */
	ctx.win_mode &= ~GST_WIN_MODE_FOCUSED;
	g_assert_false((ctx.win_mode & GST_WIN_MODE_FOCUSED) != 0);
	g_assert_true((ctx.win_mode & GST_WIN_MODE_VISIBLE) != 0);
}

/* ===== Main ===== */

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/render-context/backend-type-enum",
		test_backend_type_enum);
	g_test_add_func("/render-context/common-fields",
		test_render_context_common_fields);
	g_test_add_func("/render-context/fill-rect-dispatch",
		test_fill_rect_dispatch);
	g_test_add_func("/render-context/fill-rect-rgba-dispatch",
		test_fill_rect_rgba_dispatch);
	g_test_add_func("/render-context/fill-rect-fg-dispatch",
		test_fill_rect_fg_dispatch);
	g_test_add_func("/render-context/fill-rect-bg-dispatch",
		test_fill_rect_bg_dispatch);
	g_test_add_func("/render-context/draw-glyph-dispatch",
		test_draw_glyph_dispatch);
	g_test_add_func("/render-context/multiple-dispatch-calls",
		test_multiple_dispatch_calls);
	g_test_add_func("/render-context/win-mode-in-context",
		test_win_mode_in_context);

	return g_test_run();
}
