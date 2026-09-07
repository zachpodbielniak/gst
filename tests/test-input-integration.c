/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Exercise private driver callbacks without opening a display or spawning a PTY.
 * Only the byte sink is replaced; routing and protocol encoding remain real.
 */
#include "core/gst-pty.h"

static GString *input_bytes;

static void
input_capture_write(GstPty *unused, const gchar *data, gssize len)
{
	(void)unused;
	g_string_append_len(input_bytes, data, len);
}

#define gst_pty_write input_capture_write
#define gst_pty_write_no_echo input_capture_write
#define main input_application_main
#include "../src/main.c"
#undef main
#undef gst_pty_write
#undef gst_pty_write_no_echo

#define gst_module_register input_search_register
#include "../modules/search/gst-search-module.c"
#undef gst_module_register

/* A concrete base window needs no backend resources to emit input signals. */
typedef struct { GstWindow parent; } InputWindow;
typedef struct { GstWindowClass parent; } InputWindowClass;
G_DEFINE_TYPE(InputWindow, input_window, GST_TYPE_WINDOW)
static void input_window_class_init(InputWindowClass *klass) { (void)klass; }
static void input_window_init(InputWindow *self) { (void)self; }

typedef struct {
	guint extended;
	guint legacy;
	guint keyval;
	guint base;
	guint code;
	guint state;
	guint event;
	gboolean handled;
} InputEvents;

/* Verify all seven arguments, including text byte length, across fallback. */
static gboolean
input_extended(GstWindow *win, guint keyval, guint base, guint code,
	guint state, guint event, const gchar *text, gint len, InputEvents *events)
{
	(void)win;
	events->extended++;
	events->keyval = keyval;
	events->base = base;
	events->code = code;
	events->state = state;
	events->event = event;
	g_assert_cmpstr(text, ==, "x");
	g_assert_cmpint(len, ==, 1);
	return events->handled;
}

static void
input_legacy(GstWindow *win, guint keyval, guint state,
	const gchar *text, gint len, InputEvents *events)
{
	(void)win;
	(void)keyval;
	(void)state;
	g_assert_cmpstr(text, ==, "x");
	g_assert_cmpint(len, ==, 1);
	events->legacy++;
}

static void
test_input_window(void)
{
	g_autoptr(GstWindow) win = g_object_new(input_window_get_type(), NULL);
	InputEvents events = { 0 };
	GSignalQuery query;
	guint event;

	g_signal_query(g_signal_lookup("key-event", GST_TYPE_WINDOW), &query);
	g_assert_cmpuint(query.n_params, ==, 7);
	g_assert_true(query.return_type == G_TYPE_BOOLEAN);
	g_signal_connect(win, "key-event", G_CALLBACK(input_extended), &events);
	g_signal_connect(win, "key-press", G_CALLBACK(input_legacy), &events);
	for (event = 1; event <= 3; event++) {
		gst_window_emit_key_event(win, XK_X, XK_x, 53, Mod1Mask, event, "x", 1);
		g_assert_cmpuint(events.keyval, ==, XK_X);
		g_assert_cmpuint(events.base, ==, XK_x);
		g_assert_cmpuint(events.code, ==, 53);
		g_assert_cmpuint(events.state, ==, Mod1Mask);
		g_assert_cmpuint(events.event, ==, event);
	}
	g_assert_cmpuint(events.extended, ==, 3);
	g_assert_cmpuint(events.legacy, ==, 2);
	events.handled = TRUE;
	for (event = 1; event <= 3; event++)
		gst_window_emit_key_event(win, XK_x, XK_x, 53, 0, event, "x", 1);
	g_assert_cmpuint(events.extended, ==, 6);
	g_assert_cmpuint(events.legacy, ==, 2);
}

/* Left/right modifiers retain their bit until both physical keys are up. */
static void
test_input_modifier_pairs(void)
{
	static const struct { guint left; guint mask; } pairs[] = {
		{ XK_Shift_L, ShiftMask }, { XK_Control_L, ControlMask },
		{ XK_Alt_L, Mod1Mask }, { XK_Super_L, Mod4Mask },
		{ XK_Hyper_L, Mod3Mask }, { XK_Meta_L, Mod5Mask }
	};
	g_autoptr(GstWindow) win = g_object_new(input_window_get_type(), NULL);
	InputEvents events = { 0 };
	guint i;

	events.handled = TRUE;
	g_signal_connect(win, "key-event", G_CALLBACK(input_extended), &events);
	for (i = 0; i < G_N_ELEMENTS(pairs); i++) {
		guint left = pairs[i].left;
		guint mask = pairs[i].mask;
		gst_window_emit_key_event(win, left, left, 1, LockMask, 1, "x", 1);
		g_assert_cmpuint(events.state, ==, LockMask | mask);
		gst_window_emit_key_event(win, left + 1, left + 1, 2, mask, 1, "x", 1);
		gst_window_emit_key_event(win, left, left, 1, mask, 3, "x", 1);
		g_assert_cmpuint(events.state, ==, mask);
		gst_window_emit_key_event(win, left + 1, left + 1, 2, mask, 3, "x", 1);
		g_assert_cmpuint(events.state, ==, 0);
		/* Lost releases must not survive focus loss. */
		gst_window_emit_key_event(win, left, left, 1, 0, 1, "x", 1);
		g_signal_emit_by_name(win, "focus-change", FALSE);
		gst_window_emit_key_event(win, left + 1, left + 1, 2, mask, 3, "x", 1);
		g_assert_cmpuint(events.state, ==, 0);
	}
}

/* A lower-priority observer detects modal input leaking to ordinary hooks. */
typedef struct { GstModule parent; guint calls; } InputProbe;
typedef struct { GstModuleClass parent; } InputProbeClass;
static void input_probe_iface_init(GstInputHandlerInterface *iface);
G_DEFINE_TYPE_WITH_CODE(InputProbe, input_probe, GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER, input_probe_iface_init))
static const gchar *input_probe_name(GstModule *module) { (void)module; return "shell_integration"; }
static gboolean
input_probe_key(GstInputHandler *handler, guint keyval, guint code, guint state)
{
	(void)keyval;
	(void)code;
	(void)state;
	((InputProbe *)handler)->calls++;
	return FALSE;
}
static void input_probe_iface_init(GstInputHandlerInterface *iface) { iface->handle_key_event = input_probe_key; }
static void
input_probe_class_init(InputProbeClass *klass)
{
	GST_MODULE_CLASS(klass)->get_name = input_probe_name;
	/* Mirror only the optional action contract, not the implementation. */
	g_signal_new("copy-command-output", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, g_signal_accumulator_true_handled, NULL, NULL, G_TYPE_BOOLEAN, 0);
	g_signal_new("export-command-output", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, g_signal_accumulator_true_handled, NULL, NULL, G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
}
static void input_probe_init(InputProbe *self) { self->calls = 0; }

static gboolean
input_copy_action(InputProbe *probe, guint *calls)
{
	(void)probe;
	(*calls)++;
	return TRUE;
}

static gboolean
input_export_action(InputProbe *probe, const gchar *editor, guint *calls)
{
	(void)probe;
	g_assert_cmpstr(editor, ==, gst_config_get_editor(gst_config_get_default()));
	(*calls)++;
	return TRUE;
}

/* Notifications must expose the new state synchronously, not infer key values. */
static void
input_ownership_changed(GstModuleManager *manager, GArray *states)
{
	gboolean active = gst_module_manager_has_local_input(manager);
	g_array_append_val(states, active);
}

static void
test_input_search_lifecycle(void)
{
	GstModuleManager *manager = gst_module_manager_get_default();
	g_autoptr(GstSearchModule) search = g_object_new(GST_TYPE_SEARCH_MODULE, NULL);
	InputProbe *probe = g_object_new(input_probe_get_type(), NULL);
	g_autoptr(GArray) states = g_array_new(FALSE, FALSE, sizeof(gboolean));
	gulong notify_id;
	guint i;

	/* Register the ordinary handler first and give it an early priority. */
	gst_module_set_priority(GST_MODULE(probe), -100);
	g_assert_true(gst_module_manager_register(manager, GST_MODULE(probe)));
	g_assert_true(gst_module_activate(GST_MODULE(probe)));
	g_assert_true(gst_module_manager_register(manager, GST_MODULE(search)));
	notify_id = g_signal_connect(manager, "local-input-changed",
		G_CALLBACK(input_ownership_changed), states);
	g_assert_false(gst_module_manager_has_local_input(manager));
	g_assert_true(gst_module_activate(GST_MODULE(search)));
	for (i = 0; i < 2; i++) {
		g_assert_true(gst_module_manager_dispatch_key_event(manager, XK_F, 41,
			ControlMask | ShiftMask));
		g_assert_true(gst_module_manager_has_local_input(manager));
		/* Repeated module activation must not silently exit an open prompt. */
		g_assert_true(gst_module_activate(GST_MODULE(search)));
		g_assert_true(gst_module_manager_has_local_input(manager));
		g_assert_true(gst_module_manager_dispatch_key_event(manager, XK_Return, 36, 0));
		g_assert_true(gst_module_manager_has_local_input(manager));
		g_assert_cmpuint(probe->calls, ==, 0);
		if (i == 0) {
			gst_module_deactivate(GST_MODULE(search));
			g_assert_false(gst_module_manager_has_local_input(manager));
			g_assert_true(gst_module_activate(GST_MODULE(search)));
		} else {
			g_assert_true(gst_module_manager_dispatch_key_event(manager, XK_Escape, 9, 0));
		}
		g_assert_false(gst_module_manager_has_local_input(manager));
	}
	g_assert_cmpuint(states->len, ==, 4);
	for (i = 0; i < states->len; i++)
		g_assert_cmpint(g_array_index(states, gboolean, i), ==, i % 2 == 0);
	g_assert_false(gst_module_manager_dispatch_key_event(manager, XK_a, 38, 0));
	g_assert_cmpuint(probe->calls, ==, 1);
	g_assert_true(gst_module_manager_dispatch_key_event(manager, XK_f, 41,
		ControlMask | ShiftMask));
	g_assert_true(gst_module_manager_unregister(manager, "search"));
	g_assert_false(gst_module_manager_has_local_input(manager));
	g_assert_cmpuint(states->len, ==, 6);
	g_assert_false(g_array_index(states, gboolean, 5));
	/* An externally retained, unregistered module must no longer notify us. */
	g_assert_true(gst_module_activate(GST_MODULE(search)));
	g_assert_true(gst_input_handler_handle_key_event(GST_INPUT_HANDLER(search),
		XK_f, 41, ControlMask | ShiftMask));
	g_assert_cmpuint(states->len, ==, 6);
	gst_module_deactivate(GST_MODULE(search));
	g_signal_handler_disconnect(manager, notify_id);
	g_assert_true(gst_module_manager_unregister(manager, "shell_integration"));
	g_object_unref(probe);
}

/* Real main callback: local repeats/releases, pre-modal releases and lock policy. */
static void
test_input_driver(void)
{
	GstModuleManager *manager = gst_module_manager_get_default();
	g_autoptr(GstSearchModule) search = g_object_new(GST_TYPE_SEARCH_MODULE, NULL);
	InputProbe *probe = g_object_new(input_probe_get_type(), NULL);
	GstConfig *config = gst_config_get_default();
	GstKeybind binding;
	guint old_bindings = config->keybinds->len;
	guint copies = 0;
	guint exports = 0;

	input_bytes = g_string_new(NULL);
	terminal = gst_terminal_new(20, 4);
	window = g_object_new(input_window_get_type(), NULL);
	gst_module_manager_set_terminal(manager, terminal);
	gst_module_manager_set_config(manager, config);
	g_assert_true(gst_keybind_parse("F7", "copy-command-output", &binding));
	g_array_append_val(config->keybinds, binding);
	g_assert_true(gst_keybind_parse("Alt+F8", "export-command-output", &binding));
	g_array_append_val(config->keybinds, binding);
	g_assert_true(gst_module_manager_register(manager, GST_MODULE(probe)));
	g_assert_true(gst_module_activate(GST_MODULE(probe)));
	g_signal_connect(probe, "copy-command-output", G_CALLBACK(input_copy_action), &copies);
	g_signal_connect(probe, "export-command-output", G_CALLBACK(input_export_action), &exports);
	g_assert_true(gst_module_manager_register(manager, GST_MODULE(search)));
	g_assert_true(gst_module_activate(GST_MODULE(search)));
	g_signal_connect(terminal, "response", G_CALLBACK(on_terminal_response), NULL);
	g_signal_connect(window, "key-event", G_CALLBACK(on_key_event), NULL);
	g_signal_connect(window, "key-press", G_CALLBACK(on_key_press), NULL);
	gst_terminal_write(terminal, "\033[>11u", -1);
	g_assert_true(on_key_event(window, XK_x, XK_x, 53, 0, 0, "x", 1, NULL));
	g_assert_true(on_key_event(window, XK_x, XK_x, 53, 0, 4, "x", 1, NULL));
	g_assert_cmpuint(input_bytes->len, ==, 0);
	/* Custom output bindings skip the shell key hook before emitting actions. */
	gst_window_emit_key_event(window, XK_F7, XK_F7, 73, 0, 1, "", 0);
	gst_window_emit_key_event(window, XK_F7, XK_F7, 73, 0, 3, NULL, 0);
	gst_window_emit_key_event(window, XK_F8, XK_F8, 74, Mod1Mask, 1, "", 0);
	gst_window_emit_key_event(window, XK_F8, XK_F8, 74, 0, 3, NULL, 0);
	g_assert_cmpuint(copies, ==, 1);
	g_assert_cmpuint(exports, ==, 1);
	g_assert_cmpuint(probe->calls, ==, 0);
	g_assert_cmpuint(input_bytes->len, ==, 0);

	/* Shifted press must release using the original unshifted identity. */
	gst_window_emit_key_event(window, XK_A, XK_a, 38, ShiftMask, 1, "A", 1);
	g_assert_cmpstr(input_bytes->str, ==, "\033[97;2u");
	g_string_truncate(input_bytes, 0);
	gst_window_emit_key_event(window, XK_F, XK_f, 41, ControlMask | ShiftMask, 1, "F", 1);
	gst_window_emit_key_event(window, XK_F7, XK_F7, 73, 0, 1, "", 0);
	gst_window_emit_key_event(window, XK_F7, XK_F7, 73, 0, 3, NULL, 0);
	g_assert_cmpuint(copies, ==, 1);
	on_selection_notify(window, "paste", 5, NULL);
#ifdef GST_HAVE_WAYLAND
	on_text_commit(NULL, "commit", NULL);
#endif
	gst_window_emit_key_event(window, XK_a, XK_a, 38, 0, 3, NULL, 0);
	g_assert_cmpstr(input_bytes->str, ==, "\033[97;1:3u");
	g_string_truncate(input_bytes, 0);
	gst_window_emit_key_event(window, XK_f, XK_f, 41, 0, 3, NULL, 0);
	gst_window_emit_key_event(window, XK_Escape, XK_Escape, 9, 0, 1, "", 0);
	gst_window_emit_key_event(window, XK_Escape, XK_Escape, 9, 0, 3, NULL, 0);
	/* Repeat of a formerly local key cannot become child input. */
	gst_window_emit_key_event(window, XK_f, XK_f, 41, 0, 2, "f", 1);
	g_assert_cmpuint(input_bytes->len, ==, 0);

	/* Compose commits bypass negotiated key reporting, without a fake release. */
	gst_window_emit_key_event(window, NoSymbol, NoSymbol, 42, 0, 1, "\303\251", 2);
	gst_window_emit_key_event(window, NoSymbol, NoSymbol, 42, 0, 3, NULL, 0);
	g_assert_cmpstr(input_bytes->str, ==, "\303\251");
	g_string_truncate(input_bytes, 0);
#ifdef GST_HAVE_WAYLAND
	/* IME bytes are neither Kitty events nor bracketed paste transactions. */
	gst_terminal_write(terminal, "\033[?2004h", -1);
	on_text_commit(NULL, "\303\251", NULL);
	g_assert_cmpstr(input_bytes->str, ==, "\303\251");
	g_string_truncate(input_bytes, 0);
#endif
	gst_window_emit_key_event(window, XK_b, XK_b, 56, 0, 1, "b", 1);
	g_string_truncate(input_bytes, 0);
	gst_terminal_write(terminal, "\033[2h\033[2h", -1);
	g_assert_true(gst_terminal_has_mode(terminal, GST_MODE_KBDLOCK));
	on_selection_notify(window, "paste", 5, NULL);
#ifdef GST_HAVE_WAYLAND
	on_text_commit(NULL, "commit", NULL);
#endif
	gst_window_emit_key_event(window, XK_b, XK_b, 56, 0, 3, NULL, 0);
	gst_terminal_write(terminal, "\033[2l\033[2l", -1);
	g_assert_false(gst_terminal_has_mode(terminal, GST_MODE_KBDLOCK));
	gst_window_emit_key_event(window, XK_b, XK_b, 56, 0, 2, "b", 1);
	g_assert_cmpuint(input_bytes->len, ==, 0);
	input_stopped = TRUE;
	on_selection_notify(window, "paste", 5, NULL);
#ifdef GST_HAVE_WAYLAND
	on_text_commit(NULL, "commit", NULL);
#endif
	gst_window_emit_key_event(window, XK_c, XK_c, 54, 0, 1, "c", 1);
	gst_window_emit_key_event(window, XK_c, XK_c, 54, 0, 3, NULL, 0);
	g_assert_cmpuint(input_bytes->len, ==, 0);
	input_stopped = FALSE;

	g_assert_true(gst_module_manager_unregister(manager, "search"));
	g_assert_true(gst_module_manager_unregister(manager, "shell_integration"));
	g_object_unref(probe);
	g_array_set_size(config->keybinds, old_bindings);
	gst_module_manager_set_config(manager, NULL);
	gst_module_manager_set_terminal(manager, NULL);
	g_clear_object(&window);
	g_clear_object(&terminal);
	g_clear_pointer(&forwarded_keys, g_hash_table_unref);
	if (draw_timeout_id != 0) {
		g_source_remove(draw_timeout_id);
		draw_timeout_id = 0;
	}
	drawing = FALSE;
	g_string_free(input_bytes, TRUE);
	input_bytes = NULL;
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/input/window-fallback", test_input_window);
	g_test_add_func("/input/modifier-pairs", test_input_modifier_pairs);
	g_test_add_func("/input/search-lifecycle", test_input_search_lifecycle);
	g_test_add_func("/input/driver", test_input_driver);
	return g_test_run();
}
