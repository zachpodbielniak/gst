/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Include private implementations to exercise real modules without .so loading.
 * Each entry point/helper is renamed only inside this test translation unit. */
#include <glib.h>

#define gst_module_register history_scrollback_register
#define mark_all_dirty scrollback_mark_all_dirty
#define on_line_scrolled_out scrollback_line_scrolled_out
#include "../modules/scrollback/gst-scrollback-module.c"
#undef gst_module_register
#undef mark_all_dirty
#undef on_line_scrolled_out

#define gst_module_register history_search_register
#define mark_all_dirty search_mark_all_dirty
#define parse_hex_color search_parse_hex_color
#include "../modules/search/gst-search-module.c"
#undef gst_module_register
#undef mark_all_dirty
#undef parse_hex_color

#define gst_module_register history_shell_register
#define mark_all_dirty shell_mark_all_dirty
#define parse_hex_color shell_parse_hex_color
#define on_line_scrolled_out shell_line_scrolled_out
#include "../modules/shell_integration/gst-shellint-module.c"
#undef gst_module_register
#undef mark_all_dirty
#undef parse_hex_color
#undef on_line_scrolled_out

typedef struct {
	GstTerminal *term;
	GstScrollbackModule *history;
	GstSearchModule *search;
	GstShellintModule *shell;
} HistoryFixture;

/* Use the production optional-module lookup path and a deliberately tiny ring. */
static void
history_setup(HistoryFixture *fixture, gconstpointer data)
{
	GstModuleManager *manager = gst_module_manager_get_default();
	(void)data;
	fixture->term = gst_terminal_new(12, 3);
	gst_module_manager_set_terminal(manager, fixture->term);
	fixture->history = g_object_new(GST_TYPE_SCROLLBACK_MODULE, NULL);
	fixture->history->capacity = 3;
	fixture->search = g_object_new(GST_TYPE_SEARCH_MODULE, NULL);
	fixture->shell = g_object_new(GST_TYPE_SHELLINT_MODULE, NULL);
	fixture->shell->retention = 3;
	g_assert_true(gst_module_manager_register(manager, GST_MODULE(fixture->history)));
	g_assert_true(gst_module_activate(GST_MODULE(fixture->history)));
	g_assert_true(gst_module_activate(GST_MODULE(fixture->search)));
	g_assert_true(gst_module_activate(GST_MODULE(fixture->shell)));
}

static void
history_teardown(HistoryFixture *fixture, gconstpointer data)
{
	GstModuleManager *manager = gst_module_manager_get_default();
	(void)data;
	gst_module_deactivate(GST_MODULE(fixture->shell));
	gst_module_deactivate(GST_MODULE(fixture->search));
	gst_module_manager_unregister(manager, "scrollback");
	g_object_unref(fixture->shell);
	g_object_unref(fixture->search);
	g_object_unref(fixture->history);
	gst_module_manager_set_terminal(manager, NULL);
	g_object_unref(fixture->term);
}

/* Exercise OSC's explicit byte length, including a non-NUL-terminated packet. */
static void
history_osc(HistoryFixture *fixture, const gchar *sequence)
{
	g_assert_true(gst_shellint_module_handle_escape_string(
		GST_ESCAPE_HANDLER(fixture->shell), ']', sequence, strlen(sequence), fixture->term));
}

static void
test_history_search(HistoryFixture *fixture, gconstpointer data)
{
	SearchMatch match;
	const GstLine *saved;
	g_autofree gchar *text = NULL;
	(void)data;
	/* CJK lead + dummy occupies two columns; combining acute stays in one. */
	gst_terminal_write(fixture->term, "A\344\270\255e\314\201Z\r\nsecond\r\nthird\r\n", -1);
	g_string_assign(fixture->search->query, "z");
	perform_search(fixture->search);
	g_assert_cmpuint(fixture->search->matches->len, ==, 1);
	match = g_array_index(fixture->search->matches, SearchMatch, 0);
	g_assert_cmpint(match.line_idx, ==, -1);
	g_assert_cmpint(match.col_start, ==, 4);
	g_assert_cmpint(match.col_end, ==, 5);
	navigate_match(fixture->search, 0);
	g_assert_cmpint(fixture->history->scroll_offset, ==, 1);
	g_string_assign(fixture->search->query, "\344\270\255");
	perform_search(fixture->search);
	match = g_array_index(fixture->search->matches, SearchMatch, 0);
	g_assert_cmpint(match.col_start, ==, 1);
	g_assert_cmpint(match.col_end, ==, 3);
	g_string_assign(fixture->search->query, "\314\201");
	perform_search(fixture->search);
	match = g_array_index(fixture->search->matches, SearchMatch, 0);
	g_assert_cmpint(match.col_start, ==, 3);
	g_assert_cmpint(match.col_end, ==, 4);
	saved = history_line(fixture->history, 0);
	text = gst_line_to_string(saved);
	g_assert_true(g_str_has_prefix(text, "A\344\270\255e\314\201Z"));
	fixture->search->use_regex = TRUE;
	g_string_assign(fixture->search->query, "(?=Z)");
	perform_search(fixture->search);
	g_assert_cmpuint(fixture->search->matches->len, ==, 0);
	g_string_assign(fixture->search->query, "[");
	perform_search(fixture->search);
	g_assert_cmpuint(fixture->search->matches->len, ==, 0);
}

static void
test_history_ring(HistoryFixture *fixture, gconstpointer data)
{
	g_autofree gchar *text = NULL;
	(void)data;
	gst_terminal_write(fixture->term, "one\r\ntwo\r\nthree\r\n", -1);
	gst_scrollback_module_set_scroll_offset(fixture->history, 1);
	gst_terminal_write(fixture->term, "four\r\n", -1);
	g_assert_cmpint(fixture->history->scroll_offset, ==, 2);
	gst_terminal_write(fixture->term, "five\r\nsix\r\n", -1);
	g_assert_cmpint(fixture->history->count, ==, 3);
	g_assert_cmpint(fixture->history->scroll_offset, ==, 3);
	text = gst_line_to_string(history_line(fixture->history, 2));
	g_assert_true(g_str_has_prefix(text, "two"));
	g_assert_null(history_line(fixture->history, 3));
	g_assert_null(history_line(fixture->history, -1));
	g_string_assign(fixture->search->query, "one");
	perform_search(fixture->search);
	g_assert_cmpuint(fixture->search->matches->len, ==, 0);
	gst_module_deactivate(GST_MODULE(fixture->history));
	g_assert_null(g_object_get_data(G_OBJECT(fixture->history), "gst-history-api"));
}

static void
test_history_output(HistoryFixture *fixture, gconstpointer data)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) error = NULL;
	GstCursor before;
	const gchar bounded[] = {'1', '3', '3', ';', 'A'};
	(void)data;
	g_assert_true(gst_shellint_module_handle_escape_string(GST_ESCAPE_HANDLER(fixture->shell),
		']', bounded, sizeof(bounded), fixture->term));
	gst_terminal_write(fixture->term, "$ ", -1);
	history_osc(fixture, "133;B");
	gst_terminal_write(fixture->term, "cmd\r\n", -1);
	history_osc(fixture, "133;C");
	gst_terminal_write(fixture->term, "alpha\r\nbeta\r\n", -1);
	history_osc(fixture, "133;D;0");
	text = gst_shellint_module_dup_output(fixture->shell, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(text, ==, "alpha\nbeta\n");
	g_clear_pointer(&text, g_free);
	before = *gst_terminal_get_cursor(fixture->term);
	g_assert_true(gst_shellint_module_handle_key_event(GST_INPUT_HANDLER(fixture->shell),
		XK_Up, 0, ControlMask | ShiftMask));
	g_assert_cmpint(gst_terminal_get_cursor(fixture->term)->x, ==, before.x);
	g_assert_cmpint(gst_terminal_get_cursor(fixture->term)->y, ==, before.y);
	g_assert_cmpint(fixture->history->scroll_offset, ==, 1);
	gst_terminal_write(fixture->term, "next\r\nmore\r\nlast\r\ngone\r\n", -1);
	text = gst_shellint_module_dup_output(fixture->shell, &error);
	g_assert_null(text);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_history_partial_row(HistoryFixture *fixture, gconstpointer data)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) error = NULL;
	(void)data;
	history_osc(fixture, "133;A");
	gst_terminal_write(fixture->term, "$ cmd", -1);
	history_osc(fixture, "133;C");
	gst_terminal_write(fixture->term, "out", -1);
	history_osc(fixture, "133;D;7");
	gst_terminal_write(fixture->term, "$ ", -1);
	text = gst_shellint_module_dup_output(fixture->shell, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(text, ==, "out");
}

static void
test_history_zone_bound(HistoryFixture *fixture, gconstpointer data)
{
	guint i;
	(void)data;
	for (i = 0; i < MAX_ZONES + 20; i++)
		history_osc(fixture, "133;A");
	g_assert_cmpuint(fixture->shell->zones->len, ==, MAX_ZONES);
}

/* Soft wraps are joined, hard line breaks retained, and invalid actions do
 * not spawn a shell or silently overwrite clipboard contents. */
static void
test_history_wrap_actions(HistoryFixture *fixture, gconstpointer data)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) error = NULL;
	(void)data;
	history_osc(fixture, "133;A");
	history_osc(fixture, "133;C");
	gst_terminal_write(fixture->term, "abcdefghijklmn", -1);
	history_osc(fixture, "133;D;0");
	text = gst_shellint_module_dup_output(fixture->shell, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(text, ==, "abcdefghijklmn");
	g_assert_false(gst_shellint_module_copy_output(fixture->shell, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED);
	g_clear_error(&error);
	g_assert_false(gst_shellint_module_export_output(fixture->shell, "", &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error(&error);
	g_assert_cmpuint(g_signal_lookup("copy-command-output", GST_TYPE_SHELLINT_MODULE), !=, 0);
	g_assert_cmpuint(g_signal_lookup("export-command-output", GST_TYPE_SHELLINT_MODULE), !=, 0);
	/* Current core reflow has no anchor mapping; safe invalidation is explicit. */
	gst_terminal_resize(fixture->term, 8, 3);
	g_assert_cmpuint(fixture->shell->zones->len, ==, 0);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add("/history/search-wide-cluster", HistoryFixture, NULL,
		history_setup, test_history_search, history_teardown);
	g_test_add("/history/ring-eviction", HistoryFixture, NULL,
		history_setup, test_history_ring, history_teardown);
	g_test_add("/history/output-scroll-navigation", HistoryFixture, NULL,
		history_setup, test_history_output, history_teardown);
	g_test_add("/history/output-partial-row", HistoryFixture, NULL,
		history_setup, test_history_partial_row, history_teardown);
	g_test_add("/history/zone-bound", HistoryFixture, NULL,
		history_setup, test_history_zone_bound, history_teardown);
	g_test_add("/history/wrap-actions-resize", HistoryFixture, NULL,
		history_setup, test_history_wrap_actions, history_teardown);
	return g_test_run();
}
