/*
 * test-renderer.c - Tests for rendering enums, macros, and abstract renderer
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests what's testable without an X11 display:
 * - GST_TRUECOLOR macro correctness
 * - GstWinMode flag manipulation
 * - GstFontStyle enum values
 * - GstFontCache object lifecycle
 * - Renderer abstract class (mock subclass)
 * - Coordinate conversion (pixel <-> col/row)
 */

#include <glib.h>
#include <glib-object.h>
#include "gst-types.h"
#include "gst-enums.h"
#include "rendering/gst-renderer.h"
#include "rendering/gst-font-cache.h"
#include "core/gst-terminal.h"

/* ===== TRUECOLOR Macro Tests ===== */

static void
test_truecolor_encode(void)
{
	guint32 c;

	/* Pure red */
	c = GST_TRUECOLOR(0xFF, 0x00, 0x00);
	g_assert_true(GST_IS_TRUECOLOR(c));
	g_assert_cmpuint(c & 0xFFFFFF, ==, 0xFF0000);

	/* Pure green */
	c = GST_TRUECOLOR(0x00, 0xFF, 0x00);
	g_assert_true(GST_IS_TRUECOLOR(c));
	g_assert_cmpuint(c & 0xFFFFFF, ==, 0x00FF00);

	/* Pure blue */
	c = GST_TRUECOLOR(0x00, 0x00, 0xFF);
	g_assert_true(GST_IS_TRUECOLOR(c));
	g_assert_cmpuint(c & 0xFFFFFF, ==, 0x0000FF);

	/* White */
	c = GST_TRUECOLOR(0xFF, 0xFF, 0xFF);
	g_assert_true(GST_IS_TRUECOLOR(c));
	g_assert_cmpuint(c & 0xFFFFFF, ==, 0xFFFFFF);

	/* Black */
	c = GST_TRUECOLOR(0x00, 0x00, 0x00);
	g_assert_true(GST_IS_TRUECOLOR(c));
	g_assert_cmpuint(c & 0xFFFFFF, ==, 0x000000);
}

static void
test_truecolor_flag(void)
{
	guint32 tc;
	guint32 idx;

	/* True color value should have flag set */
	tc = GST_TRUECOLOR(0x12, 0x34, 0x56);
	g_assert_true(GST_IS_TRUECOLOR(tc));
	g_assert_true((tc & GST_TRUECOLOR_FLAG) != 0);

	/* Plain indexed color should NOT have flag */
	idx = 7;
	g_assert_false(GST_IS_TRUECOLOR(idx));

	idx = 255;
	g_assert_false(GST_IS_TRUECOLOR(idx));

	idx = 0;
	g_assert_false(GST_IS_TRUECOLOR(idx));
}

static void
test_truecolor_extract(void)
{
	guint32 c;

	/*
	 * GST_TRUERED/GREEN/BLUE extract 16-bit XRenderColor values.
	 * Red: bits 23..16 shifted to produce a 16-bit value.
	 * Green: bits 15..8, already in place for 16-bit.
	 * Blue: bits 7..0, shifted left 8 to produce 16-bit.
	 */
	c = GST_TRUECOLOR(0xAB, 0xCD, 0xEF);

	/* TRUERED: ((c & 0xff0000) >> 8) = 0xAB00 */
	g_assert_cmpuint(GST_TRUERED(c), ==, 0xAB00);

	/* TRUEGREEN: (c & 0xff00) = 0xCD00 */
	g_assert_cmpuint(GST_TRUEGREEN(c), ==, 0xCD00);

	/* TRUEBLUE: ((c & 0xff) << 8) = 0xEF00 */
	g_assert_cmpuint(GST_TRUEBLUE(c), ==, 0xEF00);
}

static void
test_truecolor_roundtrip(void)
{
	guint32 c;
	guint r, g, b;

	c = GST_TRUECOLOR(0x42, 0x87, 0xBE);

	/* Extract 8-bit components back from the encoded value */
	r = (c >> 16) & 0xFF;
	g = (c >> 8) & 0xFF;
	b = c & 0xFF;

	g_assert_cmpuint(r, ==, 0x42);
	g_assert_cmpuint(g, ==, 0x87);
	g_assert_cmpuint(b, ==, 0xBE);
}

/* ===== GstWinMode Flag Tests ===== */

static void
test_win_mode_flags(void)
{
	GstWinMode mode;

	/* Start with no flags */
	mode = 0;
	g_assert_false(mode & GST_WIN_MODE_VISIBLE);
	g_assert_false(mode & GST_WIN_MODE_FOCUSED);

	/* Set visible */
	mode |= GST_WIN_MODE_VISIBLE;
	g_assert_true(mode & GST_WIN_MODE_VISIBLE);
	g_assert_false(mode & GST_WIN_MODE_FOCUSED);

	/* Set focused too */
	mode |= GST_WIN_MODE_FOCUSED;
	g_assert_true(mode & GST_WIN_MODE_VISIBLE);
	g_assert_true(mode & GST_WIN_MODE_FOCUSED);

	/* Clear visible, focused should remain */
	mode &= ~GST_WIN_MODE_VISIBLE;
	g_assert_false(mode & GST_WIN_MODE_VISIBLE);
	g_assert_true(mode & GST_WIN_MODE_FOCUSED);
}

static void
test_win_mode_values(void)
{
	/* Verify each flag is a distinct power of 2 */
	g_assert_cmpuint(GST_WIN_MODE_VISIBLE, ==, 1 << 0);
	g_assert_cmpuint(GST_WIN_MODE_FOCUSED, ==, 1 << 1);
	g_assert_cmpuint(GST_WIN_MODE_BLINK, ==, 1 << 2);
	g_assert_cmpuint(GST_WIN_MODE_NUMLOCK, ==, 1 << 3);

	/* No overlaps */
	g_assert_cmpuint(GST_WIN_MODE_VISIBLE & GST_WIN_MODE_FOCUSED, ==, 0);
	g_assert_cmpuint(GST_WIN_MODE_FOCUSED & GST_WIN_MODE_BLINK, ==, 0);
	g_assert_cmpuint(GST_WIN_MODE_BLINK & GST_WIN_MODE_NUMLOCK, ==, 0);
}

static void
test_win_mode_gtype(void)
{
	GType type;

	type = gst_win_mode_get_type();
	g_assert_true(type != G_TYPE_INVALID);
	g_assert_cmpstr(g_type_name(type), ==, "GstWinMode");
}

/* ===== GstFontStyle Enum Tests ===== */

static void
test_font_style_values(void)
{
	/* Verify sequential values */
	g_assert_cmpint(GST_FONT_STYLE_NORMAL, ==, 0);
	g_assert_cmpint(GST_FONT_STYLE_ITALIC, ==, 1);
	g_assert_cmpint(GST_FONT_STYLE_BOLD, ==, 2);
	g_assert_cmpint(GST_FONT_STYLE_BOLD_ITALIC, ==, 3);
}

static void
test_font_style_gtype(void)
{
	GType type;

	type = gst_font_style_get_type();
	g_assert_true(type != G_TYPE_INVALID);
	g_assert_cmpstr(g_type_name(type), ==, "GstFontStyle");
}

/* ===== GstFontCache Lifecycle Tests ===== */

static void
test_font_cache_new(void)
{
	GstFontCache *cache;

	cache = gst_font_cache_new();
	g_assert_nonnull(cache);
	g_assert_true(G_IS_OBJECT(cache));

	/* Without loading fonts, metrics should be 0 */
	g_assert_cmpint(gst_font_cache_get_char_width(cache), ==, 0);
	g_assert_cmpint(gst_font_cache_get_char_height(cache), ==, 0);

	g_object_unref(cache);
}

static void
test_font_cache_gtype(void)
{
	GType type;

	type = gst_font_cache_get_type();
	g_assert_true(type != G_TYPE_INVALID);
	g_assert_cmpstr(g_type_name(type), ==, "GstFontCache");
}

static void
test_font_cache_default_font_size(void)
{
	GstFontCache *cache;

	cache = gst_font_cache_new();

	/*
	 * Before loading fonts, default font size should be 0.
	 * After loading, it would reflect the parsed fontspec size.
	 */
	g_assert_cmpfloat(gst_font_cache_get_default_font_size(cache), ==, 0.0);
	g_assert_cmpfloat(gst_font_cache_get_font_size(cache), ==, 0.0);

	g_object_unref(cache);
}

/* ===== Mock Renderer Subclass Tests ===== */

/*
 * Create a minimal concrete subclass of GstRenderer for testing
 * the abstract base class behavior.
 */

#define TEST_TYPE_MOCK_RENDERER (test_mock_renderer_get_type())

typedef struct {
	GstRenderer parent_instance;
	gint render_count;
	gint clear_count;
	gint start_draw_count;
	gint finish_draw_count;
	guint last_resize_w;
	guint last_resize_h;
} TestMockRenderer;

typedef struct {
	GstRendererClass parent_class;
} TestMockRendererClass;

static void test_mock_renderer_render(GstRenderer *self)
{
	((TestMockRenderer *)self)->render_count++;
}

static void test_mock_renderer_clear(GstRenderer *self)
{
	((TestMockRenderer *)self)->clear_count++;
}

static gboolean test_mock_renderer_start_draw(GstRenderer *self)
{
	((TestMockRenderer *)self)->start_draw_count++;
	return TRUE;
}

static void test_mock_renderer_finish_draw(GstRenderer *self)
{
	((TestMockRenderer *)self)->finish_draw_count++;
}

static void test_mock_renderer_resize(GstRenderer *self, guint w, guint h)
{
	((TestMockRenderer *)self)->last_resize_w = w;
	((TestMockRenderer *)self)->last_resize_h = h;
}

static void
test_mock_renderer_class_init(TestMockRendererClass *klass)
{
	GstRendererClass *renderer_class;

	renderer_class = GST_RENDERER_CLASS(klass);
	renderer_class->render = test_mock_renderer_render;
	renderer_class->clear = test_mock_renderer_clear;
	renderer_class->start_draw = test_mock_renderer_start_draw;
	renderer_class->finish_draw = test_mock_renderer_finish_draw;
	renderer_class->resize = test_mock_renderer_resize;
}

static void
test_mock_renderer_init(TestMockRenderer *self)
{
	self->render_count = 0;
	self->clear_count = 0;
	self->start_draw_count = 0;
	self->finish_draw_count = 0;
	self->last_resize_w = 0;
	self->last_resize_h = 0;
}

G_DEFINE_TYPE(TestMockRenderer, test_mock_renderer, GST_TYPE_RENDERER)

static void
test_renderer_gtype(void)
{
	GType type;

	type = gst_renderer_get_type();
	g_assert_true(type != G_TYPE_INVALID);
	g_assert_cmpstr(g_type_name(type), ==, "GstRenderer");
}

static void
test_renderer_mock_subclass(void)
{
	TestMockRenderer *mock;

	mock = g_object_new(TEST_TYPE_MOCK_RENDERER, NULL);
	g_assert_nonnull(mock);
	g_assert_true(GST_IS_RENDERER(mock));

	/* Render dispatches to our override */
	gst_renderer_render(GST_RENDERER(mock));
	g_assert_cmpint(mock->render_count, ==, 1);

	gst_renderer_render(GST_RENDERER(mock));
	g_assert_cmpint(mock->render_count, ==, 2);

	/* Clear dispatches */
	gst_renderer_clear(GST_RENDERER(mock));
	g_assert_cmpint(mock->clear_count, ==, 1);

	/* Start/finish draw */
	g_assert_true(gst_renderer_start_draw(GST_RENDERER(mock)));
	g_assert_cmpint(mock->start_draw_count, ==, 1);

	gst_renderer_finish_draw(GST_RENDERER(mock));
	g_assert_cmpint(mock->finish_draw_count, ==, 1);

	/* Resize dispatches with correct args */
	gst_renderer_resize(GST_RENDERER(mock), 800, 600);
	g_assert_cmpuint(mock->last_resize_w, ==, 800);
	g_assert_cmpuint(mock->last_resize_h, ==, 600);

	g_object_unref(mock);
}

static void
test_renderer_terminal_property(void)
{
	TestMockRenderer *mock;
	GstTerminal *terminal;
	GstTerminal *got;

	terminal = gst_terminal_new(80, 24);
	g_assert_nonnull(terminal);

	mock = g_object_new(TEST_TYPE_MOCK_RENDERER,
		"terminal", terminal,
		NULL);

	got = gst_renderer_get_terminal(GST_RENDERER(mock));
	g_assert_true(got == terminal);

	g_object_unref(mock);
	g_object_unref(terminal);
}

static void
test_renderer_null_terminal(void)
{
	TestMockRenderer *mock;
	GstTerminal *got;

	/* Without setting terminal, should be NULL */
	mock = g_object_new(TEST_TYPE_MOCK_RENDERER, NULL);

	got = gst_renderer_get_terminal(GST_RENDERER(mock));
	g_assert_null(got);

	g_object_unref(mock);
}

/* ===== Coordinate Conversion Tests ===== */

/*
 * Test the pixel-to-column/row conversion logic that lives in main.c.
 * We replicate the math here since those are static functions:
 *   col = (px - borderpx) / cw
 *   row = (py - borderpx) / ch
 * clamped to [0, cols-1] and [0, rows-1].
 */

static gint
test_pixel_to_col(
	gint px,
	gint cw,
	gint borderpx,
	gint cols
){
	gint col;

	col = (px - borderpx) / cw;
	if (col < 0) {
		col = 0;
	}
	if (col >= cols) {
		col = cols - 1;
	}
	return col;
}

static gint
test_pixel_to_row(
	gint py,
	gint ch,
	gint borderpx,
	gint rows
){
	gint row;

	row = (py - borderpx) / ch;
	if (row < 0) {
		row = 0;
	}
	if (row >= rows) {
		row = rows - 1;
	}
	return row;
}

static void
test_coord_pixel_to_col(void)
{
	gint cw;
	gint borderpx;
	gint cols;

	cw = 8;
	borderpx = 2;
	cols = 80;

	/* At the border itself → column 0 */
	g_assert_cmpint(test_pixel_to_col(2, cw, borderpx, cols), ==, 0);

	/* First pixel of column 1 */
	g_assert_cmpint(test_pixel_to_col(10, cw, borderpx, cols), ==, 1);

	/* Last pixel of column 0 */
	g_assert_cmpint(test_pixel_to_col(9, cw, borderpx, cols), ==, 0);

	/* Negative offset clamps to 0 */
	g_assert_cmpint(test_pixel_to_col(0, cw, borderpx, cols), ==, 0);

	/* Beyond last column clamps to cols-1 */
	g_assert_cmpint(test_pixel_to_col(9999, cw, borderpx, cols), ==, 79);
}

static void
test_coord_pixel_to_row(void)
{
	gint ch;
	gint borderpx;
	gint rows;

	ch = 16;
	borderpx = 2;
	rows = 24;

	/* At border → row 0 */
	g_assert_cmpint(test_pixel_to_row(2, ch, borderpx, rows), ==, 0);

	/* First pixel of row 1 */
	g_assert_cmpint(test_pixel_to_row(18, ch, borderpx, rows), ==, 1);

	/* Last pixel of row 0 */
	g_assert_cmpint(test_pixel_to_row(17, ch, borderpx, rows), ==, 0);

	/* Negative offset clamps to 0 */
	g_assert_cmpint(test_pixel_to_row(0, ch, borderpx, rows), ==, 0);

	/* Beyond last row clamps to rows-1 */
	g_assert_cmpint(test_pixel_to_row(9999, ch, borderpx, rows), ==, 23);
}

static void
test_coord_zero_border(void)
{
	/* With no border, column starts at pixel 0 */
	g_assert_cmpint(test_pixel_to_col(0, 8, 0, 80), ==, 0);
	g_assert_cmpint(test_pixel_to_col(7, 8, 0, 80), ==, 0);
	g_assert_cmpint(test_pixel_to_col(8, 8, 0, 80), ==, 1);

	g_assert_cmpint(test_pixel_to_row(0, 16, 0, 24), ==, 0);
	g_assert_cmpint(test_pixel_to_row(15, 16, 0, 24), ==, 0);
	g_assert_cmpint(test_pixel_to_row(16, 16, 0, 24), ==, 1);
}

/* ===== Main ===== */

int
main(
	int	argc,
	char	**argv
){
	g_test_init(&argc, &argv, NULL);

	/* TRUECOLOR macro tests */
	g_test_add_func("/renderer/truecolor/encode", test_truecolor_encode);
	g_test_add_func("/renderer/truecolor/flag", test_truecolor_flag);
	g_test_add_func("/renderer/truecolor/extract", test_truecolor_extract);
	g_test_add_func("/renderer/truecolor/roundtrip", test_truecolor_roundtrip);

	/* GstWinMode tests */
	g_test_add_func("/renderer/win-mode/flags", test_win_mode_flags);
	g_test_add_func("/renderer/win-mode/values", test_win_mode_values);
	g_test_add_func("/renderer/win-mode/gtype", test_win_mode_gtype);

	/* GstFontStyle tests */
	g_test_add_func("/renderer/font-style/values", test_font_style_values);
	g_test_add_func("/renderer/font-style/gtype", test_font_style_gtype);

	/* GstFontCache tests */
	g_test_add_func("/renderer/font-cache/new", test_font_cache_new);
	g_test_add_func("/renderer/font-cache/gtype", test_font_cache_gtype);
	g_test_add_func("/renderer/font-cache/default-font-size", test_font_cache_default_font_size);

	/* Abstract renderer tests */
	g_test_add_func("/renderer/abstract/gtype", test_renderer_gtype);
	g_test_add_func("/renderer/mock/subclass", test_renderer_mock_subclass);
	g_test_add_func("/renderer/mock/terminal-property", test_renderer_terminal_property);
	g_test_add_func("/renderer/mock/null-terminal", test_renderer_null_terminal);

	/* Coordinate conversion tests */
	g_test_add_func("/renderer/coord/pixel-to-col", test_coord_pixel_to_col);
	g_test_add_func("/renderer/coord/pixel-to-row", test_coord_pixel_to_row);
	g_test_add_func("/renderer/coord/zero-border", test_coord_zero_border);

	return g_test_run();
}
