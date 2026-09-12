/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Exercise module activation and history actions without a display or shell.
 */
#include <glib.h>

#define gst_module_register shortcut_scroll_register
#define mark_all_dirty shortcut_scroll_dirty
#include "../modules/scrollback/gst-scrollback-module.c"
#undef mark_all_dirty
#undef gst_module_register

#define gst_module_register shortcut_select_register
#define mark_all_dirty shortcut_select_dirty
#include "../modules/keyboard_select/gst-kbselect-module.c"
#undef mark_all_dirty
#undef gst_module_register

#define gst_module_register shortcut_pipe_register
#include "../modules/externalpipe/gst-externalpipe-module.c"
#undef gst_module_register

/* Configured triggers must handle letters, punctuation and locks identically.
 * A missing command is the observable consumption boundary; no process runs. */
static void
test_module_triggers(void)
{
	static const struct {
		const gchar *key;
		guint translated;
		guint base;
	} cases[] = {
		{ "Ctrl+Shift+e", XK_E, XK_e },
		{ "Ctrl+Shift+1", XK_exclam, XK_1 },
		{ "Ctrl+Shift+minus", XK_underscore, XK_minus },
		{ "Ctrl+Shift+Escape", XK_Escape, XK_Escape },
		{ "CONTROL+SHIFT+Enter", XK_Return, XK_Return },
		{ "Ctrl+Shift+Space", XK_space, XK_space }
	};
	g_autoptr(GstConfig) config = gst_config_new();
	GstKbselectModule *select = g_object_new(GST_TYPE_KBSELECT_MODULE, NULL);
	GstExternalpipeModule *pipe = g_object_new(GST_TYPE_EXTERNALPIPE_MODULE, NULL);
	guint i;
	guint locks;

	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_free(config->modules.keyboard_select.key);
		config->modules.keyboard_select.key = g_strdup(cases[i].key);
		g_free(config->modules.externalpipe.key);
		config->modules.externalpipe.key = g_strdup(cases[i].key);
		gst_module_configure(GST_MODULE(select), config);
		gst_module_configure(GST_MODULE(pipe), config);
		for (locks = 0; locks < 8; locks++) {
			guint state = ControlMask | ShiftMask | ((locks & 1) ? LockMask : 0) |
				((locks & 2) ? Mod2Mask : 0) | ((locks & 4) ? Mod3Mask : 0);
			guint keyval = cases[i].translated;

			if ((locks & 1) && keyval == XK_E)
				keyval = XK_e;
			g_assert_false(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(select),
				keyval, cases[i].base, 20, state | Mod4Mask));
			g_assert_false(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(pipe),
				keyval, cases[i].base, 20, state | Mod4Mask));
			g_assert_true(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(select),
				keyval, cases[i].base, 20, state));
			g_assert_cmpint(select->mode, ==, KBS_MODE_NORMAL);
			/* Modal text uses the translated slash; base fallback cannot alter it. */
			g_assert_true(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(select),
				XK_slash, XK_7, 16, ShiftMask));
			g_assert_cmpint(select->mode, ==, KBS_MODE_SEARCH);
			exit_mode(select);
			g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*no command configured*");
			g_assert_true(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(pipe),
				keyval, cases[i].base, 20, state));
			g_test_assert_expected_messages();
		}
	}
	/* Legacy callers still reach new handlers exactly once. */
	g_assert_true(gst_input_handler_handle_key_event(GST_INPUT_HANDLER(select),
		XK_space, 65, ControlMask | ShiftMask));
	g_assert_cmpint(select->mode, ==, KBS_MODE_NORMAL);
	exit_mode(select);
	g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*no command configured*");
	g_assert_true(gst_input_handler_handle_key_event(GST_INPUT_HANDLER(pipe),
		XK_space, 65, ControlMask | ShiftMask));
	g_test_assert_expected_messages();
	g_object_unref(pipe);
	g_object_unref(select);
}

/* Use real ring history and all six scroll actions, including shifted custom
 * punctuation. Replaced defaults and extra modifiers must not move history. */
static void
test_scroll_shortcuts(void)
{
	static const struct {
		const gchar *action;
		guint keyval;
		guint base;
		gint offset;
	} cases[] = {
		{ "scroll_up", XK_exclam, XK_1, 14 },
		{ "scroll_down", XK_at, XK_2, 6 },
		{ "scroll_top", XK_numbersign, XK_3, -1 },
		{ "scroll_bottom", XK_dollar, XK_4, 0 },
		{ "scroll_up_fast", XK_percent, XK_5, 22 },
		{ "scroll_down_fast", XK_asciicircum, XK_6, 0 }
	};
	GstModuleManager *manager = gst_module_manager_get_default();
	g_autoptr(GstConfig) config = gst_config_new();
	GstTerminal *term = gst_terminal_new(10, 4);
	GstScrollbackModule *scroll = g_object_new(GST_TYPE_SCROLLBACK_MODULE, NULL);
	guint i;

	gst_module_manager_set_terminal(manager, term);
	gst_module_configure(GST_MODULE(scroll), config);
	g_assert_true(gst_module_activate(GST_MODULE(scroll)));
	for (i = 0; i < 40; i++)
		gst_terminal_write(term, "line\r\n", -1);
	g_assert_cmpint(scroll->count, >, 22);
	/* Ctrl+Shift+PageUp is top, not the Shift+PageUp page increment. */
	g_assert_true(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(scroll),
		XK_Page_Up, XK_Page_Up, 112, ControlMask | ShiftMask));
	g_assert_cmpint(scroll->scroll_offset, ==, scroll->count);
	g_assert_true(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(scroll),
		XK_Page_Down, XK_Page_Down, 117, ControlMask | ShiftMask));
	g_assert_cmpint(scroll->scroll_offset, ==, 0);

	gst_config_clear_keybinds(config);
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autofree gchar *key = g_strdup_printf("Ctrl+Shift+%c", (gchar)cases[i].base);
		gst_config_add_keybind(config, key, cases[i].action);
	}
	g_assert_false(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(scroll),
		XK_Page_Up, XK_Page_Up, 112, ShiftMask));
	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		scroll->scroll_offset = 10;
		g_assert_false(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(scroll),
			cases[i].keyval, cases[i].base, 20, ControlMask | ShiftMask | Mod4Mask));
		g_assert_cmpint(scroll->scroll_offset, ==, 10);
		g_assert_true(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(scroll),
			cases[i].keyval, cases[i].base, 20, ControlMask | ShiftMask | LockMask));
		g_assert_cmpint(scroll->scroll_offset, ==,
			cases[i].offset < 0 ? scroll->count : cases[i].offset);
	}
	gst_terminal_write(term, "\033[?1049h", -1);
	g_assert_false(gst_input_handler_handle_key_event_full(GST_INPUT_HANDLER(scroll),
		XK_exclam, XK_1, 20, ControlMask | ShiftMask));
	gst_module_deactivate(GST_MODULE(scroll));
	gst_module_manager_set_terminal(manager, NULL);
	g_object_unref(scroll);
	g_object_unref(term);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/module-shortcuts/triggers", test_module_triggers);
	g_test_add_func("/module-shortcuts/scroll", test_scroll_shortcuts);
	return g_test_run();
}
