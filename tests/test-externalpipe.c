/*
 * test-externalpipe.c - External pipe text and child lifecycle regressions
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <glib.h>
#include <errno.h>
#include <sys/wait.h>
#include "../modules/externalpipe/gst-externalpipe-module.c"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTerminal, g_object_unref)

/* Fire-and-forget commands must not leave zombies owned by the terminal. */
static void
test_pipe_reaps_children(void)
{
	gint status;
	pid_t child;
	gint64 deadline;

	g_assert_true(spawn_pipe("exit 0", NULL, 0));
	deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
	do {
		child = waitpid(-1, &status, WNOHANG);
		if (child != 0) break;
		g_usleep(1000);
	} while (g_get_monotonic_time() < deadline);
	g_assert_cmpint(child, ==, -1);
	g_assert_cmpint(errno, ==, ECHILD);
}

/* The trailing cell of a wide rune is padding, not a space in exported text. */
static void
test_pipe_wide_text(void)
{
	GstTerminal *term;
	GstModuleManager *manager;
	gchar *text;

	term = gst_terminal_new(4, 1);
	manager = gst_module_manager_get_default();
	gst_module_manager_set_terminal(manager, term);
	gst_terminal_write(term, "\xe6\x97\xa5X", -1);
	text = collect_screen_text();
	g_assert_cmpstr(text, ==, "\xe6\x97\xa5X \n");
	g_free(text);
	gst_module_manager_set_terminal(manager, NULL);
	g_object_unref(term);
}

/* Export arbitrarily long owned clusters without a scalar-sized staging buffer. */
static void
test_pipe_cluster_text(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(3, 1);
	GstModuleManager *manager = gst_module_manager_get_default();
	GstGlyph glyph = GST_GLYPH_INIT;
	g_autoptr(GString) expected = g_string_new("e");
	g_autofree gchar *text = NULL;
	gint i;

	glyph.rune = 'e';
	for (i = 0; i < 1024; i++) {
		gst_glyph_append(&glyph, 0x301);
		g_string_append(expected, "\314\201");
	}
	gst_line_set_glyph(gst_terminal_get_line(term, 0), 0, &glyph);
	gst_glyph_clear(&glyph);
	g_string_append(expected, "  \n");
	gst_module_manager_set_terminal(manager, term);
	text = collect_screen_text();
	g_assert_cmpstr(text, ==, expected->str);
	gst_module_manager_set_terminal(manager, NULL);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/externalpipe/reaps-children", test_pipe_reaps_children);
	g_test_add_func("/externalpipe/wide-text", test_pipe_wide_text);
	g_test_add_func("/externalpipe/cluster-text", test_pipe_cluster_text);
	return g_test_run();
}
