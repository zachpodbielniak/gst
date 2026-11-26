/*
 * test-progress.c - OSC 9;4 prefixes and restores the window title
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <string.h>

#include "config/gst-config.h"
#include "module/gst-module-manager.h"

#include "../modules/progress/gst-progress-module.c"

static gboolean
feed(
	GstProgressModule *module,
	GstTerminal       *term,
	const gchar       *buf
){
	return gst_progress_module_handle_escape_string(
		GST_ESCAPE_HANDLER(module), ']', buf, strlen(buf), term);
}

static void
test_config_flag(void)
{
	g_autoptr(GstConfig) config = NULL;
	GError *error = NULL;

	config = gst_config_new();
	g_assert_false(config->modules.progress.enabled);

	g_assert_true(gst_config_load_from_path(config,
		"data/default-config.yaml", &error));
	g_assert_no_error(error);
	g_assert_false(config->modules.progress.enabled);

	g_assert_true(gst_config_load_from_path(config,
		"data/all-modules.yaml", &error));
	g_assert_no_error(error);
	g_assert_true(config->modules.progress.enabled);
}

static void
test_progress_titles(void)
{
	GstTerminal *term;
	GstModuleManager *mgr;
	GstProgressModule *module;
	const gchar *bare;

	term = gst_terminal_new(80, 24);
	gst_terminal_set_title(term, "gst");
	mgr = gst_module_manager_get_default();
	gst_module_manager_set_terminal(mgr, term);

	module = g_object_new(GST_TYPE_PROGRESS_MODULE, NULL);
	g_assert_cmpint(gst_module_get_priority(GST_MODULE(module)), ==, -1);
	g_assert_true(gst_module_activate(GST_MODULE(module)));

	g_assert_false(feed(module, term, "9;build finished"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "gst");

	g_assert_true(feed(module, term, "9;4;1;42"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[42%] gst");

	gst_terminal_set_title(term, "vim");
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[42%] vim");

	g_assert_true(feed(module, term, "9;4;0"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "vim");

	g_assert_true(feed(module, term, "9;4;3"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[~] vim");

	g_assert_true(feed(module, term, "9;4;2"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[!] vim");

	g_assert_true(feed(module, term, "9;4;2;8"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[! 8%] vim");

	g_assert_true(feed(module, term, "9;4;4;7"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[= 7%] vim");

	g_assert_true(feed(module, term, "9;4;1;150"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[100%] vim");

	g_assert_true(feed(module, term, "9;4;1;-4"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[0%] vim");

	bare = gst_terminal_get_title(term);
	g_assert_true(feed(module, term, "9;4"));
	g_assert_true(feed(module, term, "9;4;"));
	g_assert_true(feed(module, term, "9;4;9"));
	g_assert_true(feed(module, term, "9;4;1;nope"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, bare);

	g_assert_false(feed(module, term, "10;#ffffff"));
	gst_module_deactivate(GST_MODULE(module));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "vim");

	g_object_unref(module);
	gst_module_manager_set_terminal(mgr, NULL);
	g_object_unref(term);
}

static void
test_progress_null_title(void)
{
	GstTerminal *term;
	GstModuleManager *mgr;
	GstProgressModule *module;

	term = gst_terminal_new(8, 4);
	mgr = gst_module_manager_get_default();
	gst_module_manager_set_terminal(mgr, term);
	module = g_object_new(GST_TYPE_PROGRESS_MODULE, NULL);
	g_assert_true(gst_module_activate(GST_MODULE(module)));

	g_assert_null(gst_terminal_get_title(term));
	g_assert_true(feed(module, term, "9;4;1"));
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "[0%] ");
	g_assert_true(feed(module, term, "9;4;0;5"));
	g_assert_null(gst_terminal_get_title(term));

	gst_module_deactivate(GST_MODULE(module));
	g_object_unref(module);
	gst_module_manager_set_terminal(mgr, NULL);
	g_object_unref(term);
}

int
main(
	int   argc,
	char **argv
){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/progress/config", test_config_flag);
	g_test_add_func("/progress/titles", test_progress_titles);
	g_test_add_func("/progress/null-title", test_progress_null_title);
	return g_test_run();
}
