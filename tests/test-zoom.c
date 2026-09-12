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

typedef struct {
	const gchar *name;
	guint keyval;
	guint base;
	guint state;
	guint event_type;
	gdouble expected_offset;
	gboolean explicit_binding;
} ZoomShortcut;

/* Exercise real input dispatch, not just zoom(): shifted punctuation must
 * resolve through the backend's base symbol, with exact bindings preferred. */
static void
test_zoom_shortcut(
	gconstpointer data
){
	const ZoomShortcut *shortcut = (const ZoomShortcut *)data;
	GstConfig *config;
	gdouble initial_size;

	/* Isolate the default config singleton, including custom bindings. */
	if (!g_test_subprocess()) {
		g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
		g_test_trap_assert_passed();
		return;
	}
	config = gst_config_get_default();
	backend = GST_BACKEND_WAYLAND;
	window = g_object_new(GST_TYPE_WAYLAND_WINDOW, NULL);
	gst_window_resize(window, 803, 487);
	terminal = gst_terminal_new(80, 24);
	cairo_font_cache = gst_cairo_font_cache_new();
	g_assert_true(gst_cairo_font_cache_load_fonts(cairo_font_cache,
		"monospace:size=12", 0));
	cell_w = gst_cairo_font_cache_get_char_width(cairo_font_cache);
	cell_h = gst_cairo_font_cache_get_char_height(cairo_font_cache);
	initial_size = gst_cairo_font_cache_get_font_size(cairo_font_cache);

	/* Start above the default so reset and decrement have distinct results. */
	zoom(GST_ACTION_ZOOM_IN);
	zoom(GST_ACTION_ZOOM_IN);
	if (shortcut->explicit_binding)
		gst_config_add_keybind(config, "Ctrl+Shift+underscore", "zoom_in");
	g_signal_connect(window, "key-event", G_CALLBACK(on_key_event), NULL);
	gst_window_emit_key_event(window, shortcut->keyval, shortcut->base, 20,
		shortcut->state, shortcut->event_type, "", 0);
	g_assert_cmpfloat(gst_cairo_font_cache_get_font_size(cairo_font_cache),
		==, initial_size + shortcut->expected_offset);

	g_source_remove(draw_timeout_id);
	draw_timeout_id = 0;
	drawing = FALSE;
	g_clear_pointer(&forwarded_keys, g_hash_table_unref);
	g_clear_object(&cairo_font_cache);
	g_clear_object(&terminal);
	g_clear_object(&window);
}

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
#ifdef GST_HAVE_WAYLAND
	static const ZoomShortcut shortcuts[] = {
		{ "in", XK_plus, XK_equal, ControlMask | ShiftMask, 1, 3, FALSE },
		{ "out", XK_underscore, XK_minus, ControlMask | ShiftMask, 1, 1, FALSE },
		{ "reset", XK_parenright, XK_0, ControlMask | ShiftMask, 1, 0, FALSE },
		{ "out-locks", XK_underscore, XK_minus, ControlMask | ShiftMask | LockMask | Mod2Mask, 1, 1, FALSE },
		{ "reset-repeat", XK_parenright, XK_0, ControlMask | ShiftMask, 2, 0, FALSE },
		{ "reset-other-layout", XK_equal, XK_0, ControlMask | ShiftMask, 1, 0, FALSE },
		{ "exact-precedence", XK_underscore, XK_minus, ControlMask | ShiftMask, 1, 3, TRUE },
		{ "missing-control", XK_underscore, XK_minus, ShiftMask, 1, 2, FALSE },
		{ "missing-shift", XK_minus, XK_minus, ControlMask, 1, 2, FALSE },
		{ "release", XK_underscore, XK_minus, ControlMask | ShiftMask, 3, 2, FALSE }
	};
	guint i;
#endif
	g_test_init(&argc, &argv, NULL);
#ifdef GST_HAVE_WAYLAND
	for (i = 0; i < G_N_ELEMENTS(shortcuts); i++) {
		g_autofree gchar *path = g_strconcat("/zoom/shortcut/", shortcuts[i].name, NULL);
		g_test_add_data_func(path, &shortcuts[i], test_zoom_shortcut);
	}
	g_test_add_data_func("/zoom/wayland/fixed-window", GINT_TO_POINTER(803),
		test_zoom_wayland);
	g_test_add_data_func("/zoom/wayland/minimum-grid", GINT_TO_POINTER(1),
		test_zoom_wayland);
#endif
	return g_test_run();
}
