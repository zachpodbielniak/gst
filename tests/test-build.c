/*
 * test-build.c - GNU make integration regressions
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

/* A failed module must fail the parent even if a later directory is skipped. */
static void
test_module_failure(void)
{
	g_autoptr(GSubprocess) process = NULL;
	g_autoptr(GError) error = NULL;

	process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
		G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error, "make", "modules",
		"MCP=0", "MAKE=false",
		"MODULE_DIRS=modules/boxdraw /nonexistent-gst-review", NULL);
	g_assert_no_error(error);
	g_assert_true(g_subprocess_wait(process, NULL, &error));
	g_assert_no_error(error);
	g_assert_false(g_subprocess_get_successful(process));
}

/* The tests directory must not silently satisfy the public tests target. */
static void
test_tests_target(void)
{
	g_autoptr(GSubprocess) process = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *output = NULL;

	process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
		G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error, "make", "-n", "tests", NULL);
	g_assert_no_error(error);
	g_assert_true(g_subprocess_communicate_utf8(process, NULL, NULL,
		&output, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(g_subprocess_get_successful(process));
	g_assert_nonnull(strstr(output, "Running tests..."));
}

/* Once the module builder ran, requesting its output must not use the
 * generic shared-object rule, which lacks the MCP includes and libraries. */
static void
test_mcp_module_build_order(void)
{
	g_autoptr(GSubprocess) probe = NULL;
	g_autoptr(GSubprocess) process = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *output = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *option = NULL;
	g_autofree gchar *target = NULL;

	probe = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
		G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error, "pkg-config", "--exists",
		"libsoup-3.0", "libdex-1", "libpng", NULL);
	g_assert_no_error(error);
	g_assert_true(g_subprocess_wait(probe, NULL, &error));
	g_assert_no_error(error);
	if (!g_subprocess_get_successful(probe)) {
		g_test_skip("MCP build dependencies are unavailable");
		return;
	}
	directory = g_dir_make_tmp("gst-build-order-XXXXXX", &error);
	g_assert_no_error(error);
	option = g_strdup_printf("OUTDIR=%s", directory);
	target = g_build_filename(directory, "modules", "mcp.so", NULL);
	/* The dry-run clean goal disables dependency-file inclusion. Otherwise
	 * make may regenerate main.d with a temporary OUTDIR even under -n.
	 * No clean recipe executes in this dry run. */
	process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
		G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error, "make", "-n",
		"-o", "modules", "MCP=1", option, "clean", target, NULL);
	g_assert_no_error(error);
	g_assert_true(g_subprocess_communicate_utf8(process, NULL, NULL,
		&output, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(g_subprocess_get_successful(process));
	g_assert_null(strstr(output, "gst-mcp-module.c"));
	g_assert_cmpint(g_rmdir(directory), ==, 0);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/build/module-failure", test_module_failure);
	g_test_add_func("/build/tests-target", test_tests_target);
	g_test_add_func("/build/mcp-module-build-order", test_mcp_module_build_order);
	return g_test_run();
}
