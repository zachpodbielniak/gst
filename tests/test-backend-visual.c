/*
 * Real backend visual regressions; deliberately opt-in.
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <glib.h>
#include <string.h>
#include "core/gst-terminal.h"
#include "selection/gst-selection.h"
#include "rendering/gst-x11-renderer.h"
#include "window/gst-x11-window.h"
#include "rendering/gst-render-context.h"
#include "interfaces/gst-render-overlay.h"
#include "module/gst-module-manager.h"
#ifdef GST_HAVE_WAYLAND
#include "rendering/gst-wayland-renderer.h"
#endif

/* A deterministic overlay producer, not a mocked rendering backend. */
typedef struct { GstModule parent; guint calls; } BackendOverlay;
typedef struct { GstModuleClass parent; } BackendOverlayClass;
static void backend_overlay_iface_init(GstRenderOverlayInterface *iface);
GType backend_overlay_get_type(void);
G_DEFINE_TYPE_WITH_CODE(BackendOverlay, backend_overlay, GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY, backend_overlay_iface_init))

/* Draw an opaque, font-independent marker through the native context. */
static void
backend_overlay_render(GstRenderOverlay *overlay, gpointer context,
	gint width, gint height)
{
	BackendOverlay *self = (BackendOverlay *)overlay;
	self->calls++;
	gst_render_context_fill_rect_rgba((GstRenderContext *)context,
		width / 2, height / 2, 16, 16, 255, 0, 0, 255);
}

/* Install the real renderer's overlay hook. */
static void
backend_overlay_iface_init(GstRenderOverlayInterface *iface)
{
	iface->render = backend_overlay_render;
}

/* Supply a unique registration name. */
static const gchar *
backend_overlay_name(GstModule *module)
{
	(void)module;
	return "backend-visual-marker";
}

/* No external resources are needed by the marker. */
static void
backend_overlay_class_init(BackendOverlayClass *klass)
{
	GST_MODULE_CLASS(klass)->get_name = backend_overlay_name;
}

/* Count dispatches as well as checking their pixel effects. */
static void
backend_overlay_init(BackendOverlay *self)
{
	self->calls = 0;
}

typedef struct {
	GstTerminal *terminal;
	GstWindow *window;
	GstRenderer *renderer;
	GstSelection *selection;
	GObject *fonts;
	gint cw, ch;
} Backend;

/* Dispatch pending server events without an unbounded blocking iteration. */
static void
pump_events(void)
{
	gint64 deadline = g_get_monotonic_time() + 50000;
	do {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	} while (g_get_monotonic_time() < deadline);
}

/* Normal headless GTest runs never open a display. */
static gboolean
enabled(void)
{
	if (g_strcmp0(g_getenv("GST_TEST_BACKEND_VISUAL"), "1") != 0) {
		g_test_skip("Use tests/run-backend-tests.sh to opt into nested displays");
		return FALSE;
	}
	g_assert_cmpstr(g_getenv("GST_TEST_NESTED_DISPLAY"), ==, "1");
	return TRUE;
}

/* Construct the same concrete windows, fonts and renderers used by main.c. */
static void
backend_open(Backend *b)
{
	const gchar *name = g_getenv("GST_TEST_BACKEND");
	g_assert_true(FcInit());
	b->terminal = gst_terminal_new(40, 10);
	b->selection = gst_selection_new(b->terminal);
	if (g_strcmp0(name, "x11") == 0) {
		GstX11Window *window;
		GstFontCache *fonts;
		Display *display;
		window = gst_x11_window_new(40, 10, 10, 20, 2, 0);
		g_assert_nonnull(window);
		b->window = GST_WINDOW(window);
		display = gst_x11_window_get_display(window);
		fonts = gst_font_cache_new();
		b->fonts = G_OBJECT(fonts);
		g_assert_true(gst_font_cache_load_fonts(fonts, display,
			gst_x11_window_get_screen(window), "monospace:pixelsize=16", 0));
		b->cw = gst_font_cache_get_char_width(fonts);
		b->ch = gst_font_cache_get_char_height(fonts);
		b->renderer = GST_RENDERER(gst_x11_renderer_new(b->terminal,
			display, gst_x11_window_get_xid(window),
			gst_x11_window_get_visual(window), gst_x11_window_get_colormap(window),
			gst_x11_window_get_screen(window), gst_x11_window_get_depth(window),
			window, fonts, 2));
		g_assert_true(gst_x11_renderer_load_colors(GST_X11_RENDERER(b->renderer), NULL));
		gst_x11_renderer_set_selection(GST_X11_RENDERER(b->renderer), b->selection);
		gst_x11_renderer_set_win_mode(GST_X11_RENDERER(b->renderer),
			GST_WIN_MODE_VISIBLE | GST_WIN_MODE_FOCUSED);
	} else {
#ifdef GST_HAVE_WAYLAND
		GstCairoFontCache *fonts;
		GstWaylandWindow *window;
		g_assert_cmpstr(name, ==, "wayland");
		fonts = gst_cairo_font_cache_new();
		b->fonts = G_OBJECT(fonts);
		g_assert_true(gst_cairo_font_cache_load_fonts(fonts, "monospace:pixelsize=16", 0));
		b->cw = gst_cairo_font_cache_get_char_width(fonts);
		b->ch = gst_cairo_font_cache_get_char_height(fonts);
		window = gst_wayland_window_new(40, 10, b->cw, b->ch, 2);
		g_assert_nonnull(window);
		b->window = GST_WINDOW(window);
		b->renderer = GST_RENDERER(gst_wayland_renderer_new(b->terminal, window, fonts, 2));
		g_assert_true(gst_wayland_renderer_load_colors(GST_WAYLAND_RENDERER(b->renderer), NULL));
		gst_wayland_renderer_set_selection(GST_WAYLAND_RENDERER(b->renderer), b->selection);
		gst_wayland_renderer_set_win_mode(GST_WAYLAND_RENDERER(b->renderer),
			GST_WIN_MODE_VISIBLE | GST_WIN_MODE_FOCUSED);
#else
		g_error("Wayland backend was not compiled");
#endif
	}
	/* Cursor timers and focus changes are intentionally not connected. */
	gst_terminal_write(b->terminal, "\033[?25l", -1);
	gst_window_set_title(b->window, "GST isolated backend regression");
	gst_window_start_event_watch(b->window);
	gst_window_show(b->window);
	pump_events();
}

/* Fonts must be released before their owning X display is closed. */
static void
backend_close(Backend *b)
{
	g_clear_object(&b->renderer);
	g_clear_object(&b->fonts);
	g_clear_object(&b->window);
	g_clear_object(&b->selection);
	g_clear_object(&b->terminal);
}

/* Resize the actual surface and terminal grid together, as main.c does. */
static void
backend_resize(Backend *b, gint cols, gint rows)
{
	gst_terminal_resize(b->terminal, cols, rows);
	gst_window_resize(b->window, (guint)(cols * b->cw + 4), (guint)(rows * b->ch + 4));
	pump_events();
	gst_renderer_resize(b->renderer, (guint)(cols * b->cw + 4), (guint)(rows * b->ch + 4));
}

/* Capture submitted native buffers, rejecting empty/invalid image layouts. */
static GBytes *
frame(Backend *b, gboolean dirty, gint cols, gint rows)
{
	GBytes *bytes;
	gint y, width, height, stride;
	if (dirty) {
		for (y = 0; y < rows; y++)
			gst_terminal_mark_dirty(b->terminal, y);
	}
	g_assert_true(gst_renderer_start_draw(b->renderer));
	gst_renderer_render(b->renderer);
	gst_renderer_finish_draw(b->renderer);
	pump_events();
	bytes = gst_renderer_capture_screenshot(b->renderer, &width, &height, &stride);
	g_assert_nonnull(bytes);
	/* The nested compositor runs at scale 1. */
	g_assert_cmpint(width, ==, cols * b->cw + 4);
	g_assert_cmpint(height, ==, rows * b->ch + 4);
	g_assert_cmpint(stride, ==, width * 4);
	g_assert_cmpuint(g_bytes_get_size(bytes), ==, (gsize)stride * (gsize)height);
	return bytes;
}

/* Compare within one machine/backend, never against a font-specific golden. */
static void
assert_frame(Backend *b, GBytes *expected, gboolean dirty, gint cols, gint rows)
{
	g_autoptr(GBytes) actual = frame(b, dirty, cols, rows);
	g_assert_true(g_bytes_equal(expected, actual));
}

/* Exercise clusters, fallback glyphs, reallocation, selection and overlay undo. */
static void
test_visual(void)
{
	Backend b = { 0 };
	g_autoptr(GBytes) blank = NULL;
	g_autoptr(GBytes) baseline = NULL;
	g_autoptr(GBytes) changed = NULL;
	g_autoptr(GBytes) enlarged = NULL;
	BackendOverlay *overlay;
	GstModuleManager *manager;
	const guint8 *pixels;
	gsize offset;
	guint i;
	if (!enabled())
		return;
	backend_open(&b);
	backend_resize(&b, 40, 10);
	blank = frame(&b, TRUE, 40, 10);
	/* UTF-8 escapes keep source ASCII while covering combining, CJK and ZWJ. */
	gst_terminal_write(b.terminal,
		"\033[HASCII 0123456789\r\n"
		"e\314\201 A\314\210\314\201 \344\270\255\346\226\207\r\n"
		"\360\237\221\251\342\200\215\360\237\222\273 "
		"\360\237\207\272\360\237\207\270\r\n"
		"\033[1;31mBold\033[0m \033[4munderline\033[0m", -1);
	baseline = frame(&b, TRUE, 40, 10);
	g_assert_false(g_bytes_equal(blank, baseline));
	for (i = 0; i < 3; i++) {
		assert_frame(&b, baseline, FALSE, 40, 10);
		assert_frame(&b, baseline, TRUE, 40, 10);
	}
	gst_selection_set_range(b.selection, 0, 1, 7, 1);
	changed = frame(&b, TRUE, 40, 10);
	g_assert_false(g_bytes_equal(baseline, changed));
	assert_frame(&b, changed, TRUE, 40, 10);
	gst_selection_clear(b.selection);
	assert_frame(&b, baseline, TRUE, 40, 10);
	g_clear_pointer(&changed, g_bytes_unref);
	manager = gst_module_manager_get_default();
	overlay = (BackendOverlay *)g_object_new(backend_overlay_get_type(), NULL);
	g_assert_true(gst_module_manager_register(manager, GST_MODULE(overlay)));
	g_assert_true(gst_module_activate(GST_MODULE(overlay)));
	changed = frame(&b, TRUE, 40, 10);
	g_assert_cmpuint(overlay->calls, >, 0);
	g_assert_false(g_bytes_equal(baseline, changed));
	pixels = (const guint8 *)g_bytes_get_data(changed, NULL);
	offset = ((gsize)((10 * b.ch + 4) / 2 + 2) * (gsize)(40 * b.cw + 4)
		+ (gsize)((40 * b.cw + 4) / 2 + 2)) * 4;
	g_assert_cmpuint(pixels[offset], ==, 255);
	g_assert_cmpuint(pixels[offset + 1], ==, 0);
	g_assert_cmpuint(pixels[offset + 2], ==, 0);
	g_assert_cmpuint(pixels[offset + 3], ==, 255);
	assert_frame(&b, changed, TRUE, 40, 10);
	g_assert_true(gst_module_manager_unregister(manager, "backend-visual-marker"));
	g_object_unref(overlay);
	assert_frame(&b, baseline, TRUE, 40, 10);
	/* Grow then restore without clipping text, testing buffer recreation. */
	backend_resize(&b, 52, 14);
	enlarged = frame(&b, TRUE, 52, 14);
	assert_frame(&b, enlarged, FALSE, 52, 14);
	backend_resize(&b, 40, 10);
	assert_frame(&b, baseline, TRUE, 40, 10);
	backend_close(&b);
}

/* Append signal chunks: the transfer is asynchronous and may be fragmented. */
static void
selection_received(GstWindow *window, const gchar *text, gint length, gpointer data)
{
	(void)window;
	g_assert_cmpint(length, >=, 0);
	g_string_append_len((GString *)data, text, length);
}

/* Two distinct X connections ensure real SelectionRequest/Notify traffic. */
static void
test_clipboard(void)
{
	g_autoptr(GstX11Window) owner = NULL;
	g_autoptr(GstX11Window) receiver = NULL;
	g_autoptr(GString) received = NULL;
	const gchar *payload = "GST clipboard e\314\201 \344\270\255\nsecond line";
	const gchar *expected = "GST clipboard e\314\201 \344\270\255\rsecond line";
	gint64 deadline;
	guint i;
	GstWindow *source;
	GstWindow *target;
	if (!enabled())
		return;
	if (g_strcmp0(g_getenv("GST_TEST_BACKEND"), "wayland") == 0) {
		g_test_skip("Headless compositor has no input serial for wl_data_device selection ownership");
		return;
	}
	owner = gst_x11_window_new(10, 4, 10, 20, 2, 0);
	receiver = gst_x11_window_new(10, 4, 10, 20, 2, 0);
	g_assert_nonnull(owner);
	g_assert_nonnull(receiver);
	gst_window_start_event_watch(GST_WINDOW(owner));
	gst_window_start_event_watch(GST_WINDOW(receiver));
	gst_window_show(GST_WINDOW(owner));
	gst_window_show(GST_WINDOW(receiver));
	received = g_string_new(NULL);
	g_signal_connect(receiver, "selection-notify", G_CALLBACK(selection_received), received);
	g_signal_connect(owner, "selection-notify", G_CALLBACK(selection_received), received);
	/* Transfer back in the opposite direction as well, replacing ownership. */
	for (i = 0; i < 4; i++) {
		source = i < 2 ? GST_WINDOW(owner) : GST_WINDOW(receiver);
		target = i < 2 ? GST_WINDOW(receiver) : GST_WINDOW(owner);
		g_string_truncate(received, 0);
		gst_window_set_selection(source, i < 2 ? payload : expected, i % 2 == 0);
		XSync(gst_x11_window_get_display(GST_X11_WINDOW(source)), False);
		if (i % 2 == 0)
			gst_window_paste_clipboard(target);
		else
			gst_window_paste_primary(target);
		/* Unlike main.c, this window-only fixture has no renderer finish_draw
		 * to flush the buffered XConvertSelection request to the server. */
		XFlush(gst_x11_window_get_display(GST_X11_WINDOW(target)));
		deadline = g_get_monotonic_time() + 3000000;
		while (received->len < strlen(expected) && g_get_monotonic_time() < deadline)
			pump_events();
		g_assert_cmpstr(received->str, ==, expected);
	}
	g_signal_handlers_disconnect_by_data(receiver, received);
	g_signal_handlers_disconnect_by_data(owner, received);
}

/* Capability probing is display-free so the runner can distinguish skips. */
int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--list-backends") == 0) {
		g_print("x11\n");
#ifdef GST_HAVE_WAYLAND
		g_print("wayland\n");
#endif
		return 0;
	}
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/backend/visual", test_visual);
	g_test_add_func("/backend/clipboard", test_clipboard);
	return g_test_run();
}
