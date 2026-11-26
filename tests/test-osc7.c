/*
 * test-osc7.c - OSC 7 stores a local directory and yields the title
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "config/gst-config.h"
#include "module/gst-module-manager.h"

#include "../modules/osc7/gst-osc7-module.c"

static gboolean
feed(
	GstOsc7Module *module,
	GstTerminal   *term,
	const gchar   *buf
){
	return gst_osc7_module_handle_escape_string(
		GST_ESCAPE_HANDLER(module), ']', buf, strlen(buf), term);
}

static void
test_config_flag(void)
{
	g_autoptr(GstConfig) config = NULL;
	GError *error = NULL;

	config = gst_config_new();
	g_assert_false(config->modules.osc7.enabled);

	g_assert_true(gst_config_load_from_path(config,
		"data/default-config.yaml", &error));
	g_assert_no_error(error);
	g_assert_false(config->modules.osc7.enabled);

	g_assert_true(gst_config_load_from_path(config,
		"data/all-modules.yaml", &error));
	g_assert_no_error(error);
	g_assert_true(config->modules.osc7.enabled);
}

static void
test_osc7_directory(void)
{
	GstTerminal *term = NULL;
	g_autofree gchar *parent = NULL;
	g_autofree gchar *spaced = NULL;
	g_autofree gchar *escaped = NULL;
	g_autofree gchar *report = NULL;
	g_autofree gchar *local_host = NULL;
	g_autofree gchar *fqdn = NULL;
	GstModuleManager *mgr;
	GstOsc7Module *module;
	GError *error = NULL;
	const gchar *mine;

	parent = g_dir_make_tmp("gst-osc7-XXXXXX", &error);
	g_assert_no_error(error);
	spaced = g_build_filename(parent, "a b", NULL);
	g_assert_cmpint(g_mkdir(spaced, 0700), ==, 0);

	term = gst_terminal_new(80, 24);
	mgr = gst_module_manager_get_default();
	gst_module_manager_set_terminal(mgr, term);
	module = g_object_new(GST_TYPE_OSC7_MODULE, NULL);
	g_assert_true(gst_module_activate(GST_MODULE(module)));

	g_assert_false(feed(module, term, "8;file:///tmp"));
	g_assert_null(module->cwd);

	g_assert_true(feed(module, term, "7;file:///tmp"));
	g_assert_cmpstr(module->cwd, ==, "/tmp");
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "tmp");

	g_assert_true(feed(module, term, "7;file://localhost/tmp"));
	g_assert_cmpstr(module->cwd, ==, "/tmp");

	mine = g_get_host_name();
	local_host = g_strdup_printf("7;file://%s/tmp", mine);
	g_assert_true(feed(module, term, local_host));
	g_assert_cmpstr(module->cwd, ==, "/tmp");

	fqdn = g_strdup_printf("7;file://%s.example/tmp", mine);
	g_assert_true(feed(module, term, fqdn));
	g_assert_cmpstr(module->cwd, ==, "/tmp");

	escaped = g_uri_escape_string(spaced, "/", FALSE);
	report = g_strdup_printf("7;file://%s", escaped);
	g_assert_true(feed(module, term, report));
	g_assert_cmpstr(module->cwd, ==, spaced);
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "a b");

	g_assert_true(feed(module, term, "7;file:///"));
	g_assert_cmpstr(module->cwd, ==, "/");
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "/");

	gst_terminal_set_title(term, "vim");
	g_assert_true(feed(module, term, "7;file:///tmp"));
	g_assert_cmpstr(module->cwd, ==, "/tmp");
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "vim");

	g_assert_true(feed(module, term, "7;file://evil.example/tmp"));
	g_assert_cmpstr(module->cwd, ==, "/tmp");
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "vim");

	g_assert_true(feed(module, term, "7;file:///no/such/osc7"));
	g_assert_cmpstr(module->cwd, ==, "/tmp");

	g_assert_true(feed(module, term, "7;file:///tmp/%0a"));
	g_assert_cmpstr(module->cwd, ==, "/tmp");

	g_assert_true(feed(module, term, "7;file:///tmp/%zz"));
	g_assert_cmpstr(module->cwd, ==, "/tmp");

	gst_module_deactivate(GST_MODULE(module));
	g_object_unref(module);
	gst_module_manager_set_terminal(mgr, NULL);
	g_object_unref(term);
	g_rmdir(spaced);
	g_rmdir(parent);
}

int
main(
	int   argc,
	char **argv
){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/osc7/config", test_config_flag);
	g_test_add_func("/osc7/directory", test_osc7_directory);
	return g_test_run();
}
