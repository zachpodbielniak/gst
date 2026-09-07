/*
 * test-wayland-input.c - Headless Wayland input/scaling regressions
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <glib.h>

#ifdef GST_HAVE_WAYLAND
#include <sys/socket.h>
/* Exercise listener transactions without requiring a running compositor.
 * Like the module tests, this translation unit owns the private implementation. */
#include "../src/window/gst-wayland-window.c"

/* Fractional rounding must cover the final logical pixel and reject overflow. */
static void
test_scaled_size(void)
{
	g_assert_cmpint(gst_wayland_scaled_size(801, 120), ==, 801);
	g_assert_cmpint(gst_wayland_scaled_size(801, 150), ==, 1002);
	g_assert_cmpint(gst_wayland_scaled_size(801, 180), ==, 1202);
	g_assert_cmpint(gst_wayland_scaled_size(801, 240), ==, 1602);
	g_assert_cmpint(gst_wayland_scaled_size(0, 120), ==, 0);
	g_assert_cmpint(gst_wayland_scaled_size(-1, 120), ==, 0);
	g_assert_cmpint(gst_wayland_scaled_size(1, 0), ==, 0);
	g_assert_cmpint(gst_wayland_scaled_size(G_MAXINT, G_MAXUINT), ==, 0);
}

/* Unentered outputs must not scale the window; spanning uses the maximum. */
static void
test_output_membership(void)
{
	g_autoptr(GstWaylandWindow) window = NULL;
	GstWaylandOutput first = { 0 };
	GstWaylandOutput second = { 0 };

	window = g_object_new(GST_TYPE_WAYLAND_WINDOW, NULL);
	first.window = second.window = window;
	first.scale = 1;
	second.scale = 2;
	/* Identity-only proxies: these callbacks do not issue protocol requests. */
	first.proxy = (struct wl_output *)&first;
	second.proxy = (struct wl_output *)&second;
	window->outputs = g_list_append(window->outputs, &first);
	window->outputs = g_list_append(window->outputs, &second);
	update_scale(window);
	g_assert_cmpuint(window->scale, ==, 120);
	surface_enter(window, NULL, first.proxy);
	surface_enter(window, NULL, second.proxy);
	g_assert_cmpuint(window->scale, ==, 240);
	output_scale(&second, second.proxy, 3);
	output_done(&second, second.proxy);
	g_assert_cmpuint(window->scale, ==, 360);
	surface_leave(window, NULL, second.proxy);
	g_assert_cmpuint(window->scale, ==, 120);
	window->viewport = (struct wp_viewport *)&first;
	fractional_preferred(window, NULL, 150);
	g_assert_cmpuint(window->scale, ==, 150);
	surface_enter(window, NULL, second.proxy);
	g_assert_cmpuint(window->scale, ==, 150);
	fractional_preferred(window, NULL, 0);
	g_assert_cmpuint(window->scale, ==, 150);
	window->viewport = NULL;
	g_clear_pointer(&window->outputs, g_list_free);
}

/* Text commits are not key events and are delivered only at the done boundary. */
static void
collect_commit(GstWaylandWindow *window, const gchar *text, GString *committed)
{
	(void)window;
	g_string_append(committed, text);
}

static void
test_text_transaction(void)
{
	g_autoptr(GstWaylandWindow) window = NULL;
	g_autoptr(GString) committed = NULL;
	gint begin, end;

	window = g_object_new(GST_TYPE_WAYLAND_WINDOW, NULL);
	committed = g_string_new(NULL);
	g_signal_connect(window, "text-commit", G_CALLBACK(collect_commit), committed);
	window->text_active = TRUE;
	window->text_serial = 7;
	text_preedit(window, NULL, "abc", 1, 2);
	text_commit(window, NULL, "\303\251");
	g_assert_cmpstr(committed->str, ==, "");
	g_assert_cmpstr(gst_wayland_window_get_preedit(window, NULL, NULL), ==, "");
	text_done(window, NULL, 6);
	/* Stale serials still deliver text, but defer client state requests. */
	g_assert_true(window->text_defer);
	g_assert_cmpstr(committed->str, ==, "\303\251");
	g_assert_cmpstr(gst_wayland_window_get_preedit(window, &begin, &end), ==, "abc");
	g_assert_cmpint(begin, ==, 1);
	g_assert_cmpint(end, ==, 2);
	text_done(window, NULL, 7);
	g_assert_false(window->text_defer);
	g_assert_cmpstr(gst_wayland_window_get_preedit(window, NULL, NULL), ==, "");
	g_assert_cmpstr(committed->str, ==, "\303\251");
	text_commit(window, NULL, "discard");
	window->text_active = FALSE;
	text_done(window, NULL, 7);
	g_assert_cmpstr(committed->str, ==, "\303\251");
	/* A terminal cannot delete surrounding text through a PTY byte count. */
	text_delete(window, NULL, 4, 2);
	g_assert_cmpstr(committed->str, ==, "\303\251");
}

static void
collect_key(GstWindow *window, guint keysym, guint mods,
	const gchar *text, gint length, GString *committed)
{
	(void)window; (void)keysym; (void)mods;
	g_string_append_len(committed, text, length);
}

/* Real client proxies exercise enable/disable marshaling without a compositor.
 * The socket peer is intentionally idle; no roundtrip is performed. */
static void
test_text_lifecycle(void)
{
	g_autoptr(GstWaylandWindow) window = NULL;
	gint sockets[2];

	g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), ==, 0);
	window = g_object_new(GST_TYPE_WAYLAND_WINDOW, NULL);
	window->display = wl_display_connect_to_fd(sockets[0]);
	g_assert_nonnull(window->display);
	window->registry = wl_display_get_registry(window->display);
	registry_global(window, window->registry, 1, "wl_compositor", 4);
	registry_global(window, window->registry, 2, "wl_seat", 5);
	registry_global(window, window->registry, 3, "zwp_text_input_manager_v3", 1);
	g_assert_nonnull(window->text_input);
	window->surface = wl_compositor_create_surface(window->compositor);
	gst_wayland_window_set_text_input_enabled(window, TRUE);
	g_assert_false(window->text_active);
	text_enter(window, window->text_input, window->surface);
	g_assert_true(window->text_active);
	g_assert_cmpuint(window->text_serial, ==, 1);
	gst_wayland_window_set_text_cursor(window, 16, 24, 8, 16);
	g_assert_cmpuint(window->text_serial, ==, 2);
	gst_wayland_window_set_text_cursor(window, 16, 24, 8, 16);
	g_assert_cmpuint(window->text_serial, ==, 2);
	text_preedit(window, window->text_input, "pending", 0, 0);
	text_done(window, window->text_input, 1);
	gst_wayland_window_set_text_cursor(window, 24, 24, 8, 16);
	g_assert_cmpuint(window->text_serial, ==, 2);
	g_assert_true(window->text_dirty);
	text_done(window, window->text_input, 2);
	g_assert_cmpuint(window->text_serial, ==, 3);
	text_leave(window, window->text_input, window->surface);
	g_assert_false(window->text_active);
	g_assert_cmpuint(window->text_serial, ==, 4);
	g_assert_cmpstr(gst_wayland_window_get_preedit(window, NULL, NULL), ==, "");
	gst_wayland_window_set_text_input_enabled(window, FALSE);
	text_enter(window, window->text_input, window->surface);
	g_assert_false(window->text_active);
	registry_global_remove(window, window->registry, 2);
	g_assert_null(window->seat);
	g_assert_null(window->text_input);
	registry_global(window, window->registry, 4, "wl_seat", 5);
	g_assert_nonnull(window->text_input);
	g_assert_cmpuint(window->text_serial, ==, 0);
	g_clear_object(&window);
	close(sockets[1]);
}

/* Test the actual key emission path, including Compose repeat and focus reset. */
static void
test_compose(void)
{
	g_autoptr(GstWaylandWindow) window = NULL;
	g_autoptr(GString) committed = NULL;
	struct xkb_rule_names names = { 0 };
	xkb_keycode_t acute, letter, multi, a_key;
	const gchar *rules;

	window = g_object_new(GST_TYPE_WAYLAND_WINDOW, NULL);
	committed = g_string_new(NULL);
	window->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	names.layout = "us";
	names.variant = "intl";
	names.options = "compose:ralt";
	window->xkb_keymap = xkb_keymap_new_from_names(window->xkb_ctx, &names,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	g_assert_nonnull(window->xkb_keymap);
	window->xkb_state = xkb_state_new(window->xkb_keymap);
	rules = "<dead_acute> <e> : \"\303\251\" eacute\n"
		"<Multi_key> <a> <e> : \"\303\246\" ae\n";
	window->compose_table = xkb_compose_table_new_from_buffer(window->xkb_ctx,
		rules, strlen(rules), "C", XKB_COMPOSE_FORMAT_TEXT_V1,
		XKB_COMPOSE_COMPILE_NO_FLAGS);
	g_assert_nonnull(window->compose_table);
	window->compose_state = xkb_compose_state_new(window->compose_table,
		XKB_COMPOSE_STATE_NO_FLAGS);
	g_signal_connect(window, "key-press", G_CALLBACK(collect_key), committed);
	acute = xkb_keymap_key_by_name(window->xkb_keymap, "AC11") - 8;
	letter = xkb_keymap_key_by_name(window->xkb_keymap, "AD03") - 8;
	multi = xkb_keymap_key_by_name(window->xkb_keymap, "RALT") - 8;
	a_key = xkb_keymap_key_by_name(window->xkb_keymap, "AC01") - 8;
	g_assert_false(emit_key_event(window, acute, FALSE));
	g_assert_cmpstr(committed->str, ==, "");
	g_assert_true(emit_key_event(window, letter, FALSE));
	g_assert_cmpstr(committed->str, ==, "\303\251");
	g_assert_true(emit_key_event(window, letter, TRUE));
	g_assert_cmpstr(committed->str, ==, "\303\251\303\251");
	g_string_truncate(committed, 0);
	g_assert_false(emit_key_event(window, acute, FALSE));
	keyboard_leave(window, NULL, 0, NULL);
	g_assert_true(emit_key_event(window, letter, FALSE));
	g_assert_cmpstr(committed->str, ==, "e");
	g_string_truncate(committed, 0);
	g_assert_false(emit_key_event(window, multi, FALSE));
	g_assert_false(emit_key_event(window, a_key, FALSE));
	g_assert_true(emit_key_event(window, letter, FALSE));
	g_assert_cmpstr(committed->str, ==, "\303\246");
}
#endif

/* This file remains buildable in X11-only configurations. */
int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
#ifdef GST_HAVE_WAYLAND
	g_test_add_func("/wayland/scaling/size", test_scaled_size);
	g_test_add_func("/wayland/scaling/outputs", test_output_membership);
	g_test_add_func("/wayland/input/transaction", test_text_transaction);
	g_test_add_func("/wayland/input/lifecycle", test_text_lifecycle);
	g_test_add_func("/wayland/input/compose", test_compose);
#endif
	return g_test_run();
}
