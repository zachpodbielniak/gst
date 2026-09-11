/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Exercise the real zoom driver and font reload without a compositor.
 * Capture renderer/PTY boundaries so no display or child process is needed.
 */
#include <glib.h>

#ifdef GST_HAVE_WAYLAND
#include "gst.h"

static guint resize_calls;
static guint resize_width;
static guint resize_height;
static gint pty_cols;
static gint pty_rows;

/* Record the geometry handed to the renderer by the application driver. */
static void
zoom_capture_renderer(
	GstRenderer *unused,
	guint width,
	guint height
){
	(void)unused;
	resize_calls++;
	resize_width = width;
	resize_height = height;
}

/* Record the grid handed to the PTY independently of terminal state. */
static void
zoom_capture_pty(
	GstPty *unused,
	gint cols,
	gint rows
){
	(void)unused;
	pty_cols = cols;
	pty_rows = rows;
}

#define gst_renderer_resize zoom_capture_renderer
#define gst_pty_resize zoom_capture_pty
#define main zoom_application_main
#include "../src/main.c"
#undef main
#undef gst_pty_resize
#undef gst_renderer_resize

/* Zoom must update all geometry and request drawing without a configure.
 * Non-cell-aligned dimensions catch accidental window snapping; a tiny
 * surface verifies the one-cell minimum after borders consume its area. */
static void
test_zoom_wayland(
	gconstpointer data
){
	const GstAction actions[] = {
		GST_ACTION_ZOOM_IN, GST_ACTION_ZOOM_IN,
		GST_ACTION_ZOOM_OUT, GST_ACTION_ZOOM_RESET
	};
	gint width;
	gint height;
	gint actual_width;
	gint actual_height;
	gint initial_cw;
	gint initial_ch;
	guint i;
	gdouble initial_size;

	width = GPOINTER_TO_INT(data);
	height = width == 1 ? 1 : 487;
	backend = GST_BACKEND_WAYLAND;
	window = g_object_new(GST_TYPE_WAYLAND_WINDOW, NULL);
	gst_window_resize(window, (guint)width, (guint)height);
	terminal = gst_terminal_new(80, 24);
	cairo_font_cache = gst_cairo_font_cache_new();
	g_assert_true(gst_cairo_font_cache_load_fonts(cairo_font_cache,
		"monospace:size=12", 0));
	cell_w = gst_cairo_font_cache_get_char_width(cairo_font_cache);
	cell_h = gst_cairo_font_cache_get_char_height(cairo_font_cache);
	initial_cw = cell_w;
	initial_ch = cell_h;
	initial_size = gst_cairo_font_cache_get_font_size(cairo_font_cache);
	resize_calls = 0;

	for (i = 0; i < G_N_ELEMENTS(actions); i++) {
		zoom(actions[i]);
		g_assert_cmpuint(resize_calls, ==, i + 1);
		g_assert_cmpuint(resize_width, ==, (guint)width);
		g_assert_cmpuint(resize_height, ==, (guint)height);
		g_assert_cmpint(pty_cols, ==,
			MAX(1, (width - 2 * (gint)cfg_border_px) / cell_w));
		g_assert_cmpint(pty_rows, ==,
			MAX(1, (height - 2 * (gint)cfg_border_px) / cell_h));
		g_assert_cmpint(gst_terminal_get_cols(terminal), ==, pty_cols);
		g_assert_cmpint(gst_terminal_get_rows(terminal), ==, pty_rows);
		gst_wayland_window_get_logical_size(GST_WAYLAND_WINDOW(window),
			&actual_width, &actual_height);
		g_assert_cmpint(actual_width, ==, width);
		g_assert_cmpint(actual_height, ==, height);
		g_assert_true(drawing);
		g_assert_cmpuint(draw_timeout_id, !=, 0);
		g_source_remove(draw_timeout_id);
		draw_timeout_id = 0;
		drawing = FALSE;
	}
	g_assert_cmpfloat(gst_cairo_font_cache_get_font_size(cairo_font_cache),
		==, initial_size);
	g_assert_cmpint(cell_w, ==, initial_cw);
	g_assert_cmpint(cell_h, ==, initial_ch);
	/* Reset at the default size is a no-op. */
	zoom(GST_ACTION_ZOOM_RESET);
	g_assert_cmpuint(resize_calls, ==, G_N_ELEMENTS(actions));
	g_assert_cmpuint(draw_timeout_id, ==, 0);
	g_clear_object(&cairo_font_cache);
	g_clear_object(&terminal);
	g_clear_object(&window);
}
#endif

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
#ifdef GST_HAVE_WAYLAND
	g_test_add_data_func("/zoom/wayland/fixed-window", GINT_TO_POINTER(803),
		test_zoom_wayland);
	g_test_add_data_func("/zoom/wayland/minimum-grid", GINT_TO_POINTER(1),
		test_zoom_wayland);
#endif
	return g_test_run();
}
