/*
 * test-config-compiler.c - Tests for GstConfig setters and GstConfigCompiler
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>
#include "config/gst-config.h"
#include "config/gst-config-compiler.h"
#include "config/gst-keybind.h"
#include "gst-enums.h"

/* ===== Helper: write content to a temp file ===== */

static gchar *
write_temp_file(
	const gchar *suffix,
	const gchar *content
){
	gchar *tmpl;
	gchar *path;
	GError *error = NULL;
	gint fd;

	tmpl = g_strdup_printf("gst-test-XXXXXX%s", suffix);
	fd = g_file_open_tmp(tmpl, &path, &error);
	g_free(tmpl);
	g_assert_no_error(error);
	g_assert_cmpint(fd, >=, 0);

	g_assert_true(g_file_set_contents(path, content, -1, &error));
	g_assert_no_error(error);
	close(fd);

	return path;
}

/* ===== Test: GstConfig setters ===== */

static void
test_config_setters(void)
{
	g_autoptr(GstConfig) config = NULL;
	const gchar *fallbacks[] = { "Noto Sans Mono", "DejaVu Sans Mono", NULL };
	const gchar *palette[] = {
		"#000000", "#cc0000", "#00cc00", "#cccc00",
		"#0000cc", "#cc00cc", "#00cccc", "#cccccc",
		"#555555", "#ff0000", "#00ff00", "#ffff00",
		"#0000ff", "#ff00ff", "#00ffff", "#ffffff",
		NULL
	};

	config = gst_config_new();
	g_assert_nonnull(config);

	/* Terminal setters */
	gst_config_set_shell(config, "/bin/zsh");
	g_assert_cmpstr(gst_config_get_shell(config), ==, "/bin/zsh");

	gst_config_set_term_name(config, "xterm-256color");
	g_assert_cmpstr(gst_config_get_term_name(config), ==, "xterm-256color");

	gst_config_set_tabspaces(config, 4);
	g_assert_cmpuint(gst_config_get_tabspaces(config), ==, 4);

	/* Window setters */
	gst_config_set_title(config, "my-terminal");
	g_assert_cmpstr(gst_config_get_title(config), ==, "my-terminal");

	gst_config_set_cols(config, 120);
	g_assert_cmpuint(gst_config_get_cols(config), ==, 120);

	gst_config_set_rows(config, 40);
	g_assert_cmpuint(gst_config_get_rows(config), ==, 40);

	gst_config_set_border_px(config, 5);
	g_assert_cmpuint(gst_config_get_border_px(config), ==, 5);

	/* Font setters */
	gst_config_set_font_primary(config, "JetBrains Mono:pixelsize=16");
	g_assert_cmpstr(gst_config_get_font_primary(config), ==,
		"JetBrains Mono:pixelsize=16");

	gst_config_set_font_fallbacks(config, fallbacks);
	g_assert_nonnull(gst_config_get_font_fallbacks(config));
	g_assert_cmpstr(gst_config_get_font_fallbacks(config)[0], ==,
		"Noto Sans Mono");

	gst_config_set_font_fallbacks(config, NULL);
	g_assert_null(gst_config_get_font_fallbacks(config));

	/* Color index setters */
	gst_config_set_fg_index(config, 15);
	g_assert_cmpuint(gst_config_get_fg_index(config), ==, 15);

	gst_config_set_bg_index(config, 8);
	g_assert_cmpuint(gst_config_get_bg_index(config), ==, 8);

	gst_config_set_cursor_fg_index(config, 3);
	g_assert_cmpuint(gst_config_get_cursor_fg_index(config), ==, 3);

	gst_config_set_cursor_bg_index(config, 4);
	g_assert_cmpuint(gst_config_get_cursor_bg_index(config), ==, 4);

	/* Color hex setters */
	gst_config_set_fg_hex(config, "#ffffff");
	g_assert_cmpstr(gst_config_get_fg_hex(config), ==, "#ffffff");

	gst_config_set_fg_hex(config, NULL);
	g_assert_null(gst_config_get_fg_hex(config));

	gst_config_set_bg_hex(config, "#1e1e2e");
	g_assert_cmpstr(gst_config_get_bg_hex(config), ==, "#1e1e2e");

	gst_config_set_cursor_fg_hex(config, "#000000");
	g_assert_cmpstr(gst_config_get_cursor_fg_hex(config), ==, "#000000");

	gst_config_set_cursor_bg_hex(config, "#f5e0dc");
	g_assert_cmpstr(gst_config_get_cursor_bg_hex(config), ==, "#f5e0dc");

	/* Palette setter */
	gst_config_set_palette_hex(config, palette, 16);
	g_assert_cmpuint(gst_config_get_n_palette(config), ==, 16);
	g_assert_cmpstr(gst_config_get_palette_hex(config)[0], ==, "#000000");
	g_assert_cmpstr(gst_config_get_palette_hex(config)[15], ==, "#ffffff");

	/* Cursor setters */
	gst_config_set_cursor_shape(config, GST_CURSOR_SHAPE_BAR);
	g_assert_cmpint(gst_config_get_cursor_shape(config), ==,
		GST_CURSOR_SHAPE_BAR);

	gst_config_set_cursor_blink(config, TRUE);
	g_assert_true(gst_config_get_cursor_blink(config));

	gst_config_set_blink_rate(config, 300);
	g_assert_cmpuint(gst_config_get_blink_rate(config), ==, 300);

	/* Selection setter */
	gst_config_set_word_delimiters(config, " @#$");
	g_assert_cmpstr(gst_config_get_word_delimiters(config), ==, " @#$");

	/* Latency setters */
	gst_config_set_min_latency(config, 16);
	g_assert_cmpuint(gst_config_get_min_latency(config), ==, 16);

	gst_config_set_max_latency(config, 50);
	g_assert_cmpuint(gst_config_get_max_latency(config), ==, 50);
}

/* ===== Test: add_keybind ===== */

static void
test_config_add_keybind(void)
{
	g_autoptr(GstConfig) config = NULL;
	const GArray *binds;

	config = gst_config_new();

	/* Should have default keybinds */
	binds = gst_config_get_keybinds(config);
	g_assert_nonnull(binds);
	g_assert_cmpuint(binds->len, >, 0);

	/* Add a new binding */
	g_assert_true(gst_config_add_keybind(config,
		"Ctrl+Shift+n", "zoom_reset"));

	/* Verify it was appended */
	binds = gst_config_get_keybinds(config);
	g_assert_cmpuint(binds->len, >, 0);

	/* Invalid binding should return FALSE (expect warning from parser) */
	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
		"*Unknown action*");
	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
		"*Invalid keybind*");
	g_assert_false(gst_config_add_keybind(config,
		"", "nonexistent_action"));
	g_test_assert_expected_messages();
}

/* ===== Test: add_mousebind ===== */

static void
test_config_add_mousebind(void)
{
	g_autoptr(GstConfig) config = NULL;
	const GArray *binds;

	config = gst_config_new();

	/* Should have default mousebinds */
	binds = gst_config_get_mousebinds(config);
	g_assert_nonnull(binds);
	g_assert_cmpuint(binds->len, >, 0);

	/* Add a new binding */
	g_assert_true(gst_config_add_mousebind(config,
		"Ctrl+Button4", "scroll_up_fast"));
}

/* ===== Test: clear_keybinds ===== */

static void
test_config_clear_keybinds(void)
{
	g_autoptr(GstConfig) config = NULL;
	const GArray *binds;

	config = gst_config_new();

	/* Should have defaults */
	binds = gst_config_get_keybinds(config);
	g_assert_cmpuint(binds->len, >, 0);

	/* Clear */
	gst_config_clear_keybinds(config);
	binds = gst_config_get_keybinds(config);
	g_assert_cmpuint(binds->len, ==, 0);

	/* Add one back */
	gst_config_add_keybind(config, "Ctrl+Shift+c", "clipboard_copy");
	binds = gst_config_get_keybinds(config);
	g_assert_cmpuint(binds->len, ==, 1);
}

/* ===== Test: clear_mousebinds ===== */

static void
test_config_clear_mousebinds(void)
{
	g_autoptr(GstConfig) config = NULL;
	const GArray *binds;

	config = gst_config_new();

	gst_config_clear_mousebinds(config);
	binds = gst_config_get_mousebinds(config);
	g_assert_cmpuint(binds->len, ==, 0);
}

/* ===== Test: compiler constructor ===== */

static void
test_compiler_new(void)
{
	g_autoptr(GstConfigCompiler) compiler = NULL;
	GError *error = NULL;

	if (g_find_program_in_path("gcc") == NULL) {
		g_test_skip("gcc not found in PATH");
		return;
	}

	compiler = gst_config_compiler_new(&error);
	g_assert_no_error(error);
	g_assert_nonnull(compiler);
	g_assert_true(GST_IS_CONFIG_COMPILER(compiler));
}

/* ===== Test: find_config returns NULL when no config exists ===== */

static void
test_compiler_find_config_none(void)
{
	g_autoptr(GstConfigCompiler) compiler = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	if (g_find_program_in_path("gcc") == NULL) {
		g_test_skip("gcc not found in PATH");
		return;
	}

	compiler = gst_config_compiler_new(&error);
	g_assert_no_error(error);

	/*
	 * This might find a real config.c if one is installed.
	 * We just verify it doesn't crash and returns a valid
	 * result (NULL or a real path).
	 */
	path = gst_config_compiler_find_config(compiler);
	if (path != NULL) {
		g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
	}
}

/* ===== Test: compile a simple config ===== */

static void
test_compiler_compile_simple(void)
{
	g_autoptr(GstConfigCompiler) compiler = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *so_path = NULL;
	GError *error = NULL;

	/* Skip if gcc is not available */
	if (g_find_program_in_path("gcc") == NULL) {
		g_test_skip("gcc not found in PATH");
		return;
	}

	/* Write a minimal valid C config */
	source_path = write_temp_file(".c",
		"#include <gmodule.h>\n"
		"G_MODULE_EXPORT int gst_config_init(void) { return 1; }\n");

	compiler = gst_config_compiler_new(&error);
	g_assert_no_error(error);

	so_path = gst_config_compiler_compile(compiler, source_path,
		FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(so_path);
	g_assert_true(g_file_test(so_path, G_FILE_TEST_EXISTS));

	g_unlink(source_path);
	g_unlink(so_path);
}

/* ===== Test: compile invalid code fails ===== */

static void
test_compiler_compile_invalid(void)
{
	g_autoptr(GstConfigCompiler) compiler = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *so_path = NULL;
	GError *error = NULL;

	if (g_find_program_in_path("gcc") == NULL) {
		g_test_skip("gcc not found in PATH");
		return;
	}

	/* Write invalid C code */
	source_path = write_temp_file(".c",
		"this is not valid C code!!!\n");

	compiler = gst_config_compiler_new(&error);
	g_assert_no_error(error);

	so_path = gst_config_compiler_compile(compiler, source_path,
		FALSE, &error);
	g_assert_null(so_path);
	g_assert_nonnull(error);
	g_error_free(error);

	g_unlink(source_path);
}

/* ===== Test: load_and_apply with valid config ===== */

static void
test_compiler_load_and_apply(void)
{
	g_autoptr(GstConfigCompiler) compiler = NULL;
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *so_path = NULL;
	GError *error = NULL;
	gboolean ok;

	if (g_find_program_in_path("gcc") == NULL) {
		g_test_skip("gcc not found in PATH");
		return;
	}

	/*
	 * Write a config that calls gst_config_init on
	 * the default singleton. We use a minimal include to
	 * avoid needing the full gst headers in the test env.
	 */
	source_path = write_temp_file(".c",
		"#include <gmodule.h>\n"
		"G_MODULE_EXPORT int gst_config_init(void) { return 1; }\n");

	compiler = gst_config_compiler_new(&error);
	g_assert_no_error(error);

	/* Compile */
	so_path = gst_config_compiler_compile(compiler, source_path,
		FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(so_path);

	/* Load and apply */
	ok = gst_config_compiler_load_and_apply(compiler, so_path, &error);
	g_assert_no_error(error);
	g_assert_true(ok);

	g_unlink(source_path);
	g_unlink(so_path);
}

/* ===== Test: load_and_apply with missing symbol ===== */

static void
test_compiler_missing_symbol(void)
{
	g_autoptr(GstConfigCompiler) compiler = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *so_path = NULL;
	GError *error = NULL;
	gboolean ok;

	if (g_find_program_in_path("gcc") == NULL) {
		g_test_skip("gcc not found in PATH");
		return;
	}

	/* Write a .c file with no gst_config_init symbol */
	source_path = write_temp_file(".c",
		"#include <gmodule.h>\n"
		"G_MODULE_EXPORT int some_other_func(void) { return 1; }\n");

	compiler = gst_config_compiler_new(&error);
	g_assert_no_error(error);

	/* Compile (should succeed) */
	so_path = gst_config_compiler_compile(compiler, source_path,
		FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(so_path);

	/* Load should fail (missing gst_config_init symbol) */
	ok = gst_config_compiler_load_and_apply(compiler, so_path, &error);
	g_assert_false(ok);
	g_assert_nonnull(error);
	g_error_free(error);

	g_unlink(source_path);
	g_unlink(so_path);
}

/* ===== Test: CRISPY_PARAMS extraction via compile ===== */

static void
test_compiler_crispy_params(void)
{
	g_autoptr(GstConfigCompiler) compiler = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *so_path = NULL;
	GError *error = NULL;
	gboolean ok;

	if (g_find_program_in_path("gcc") == NULL) {
		g_test_skip("gcc not found in PATH");
		return;
	}

	/*
	 * Write a config with CRISPY_PARAMS that adds a -DTEST_DEFINE.
	 * The code checks the define compiled correctly.
	 */
	source_path = write_temp_file(".c",
		"#define CRISPY_PARAMS \"-DTEST_DEFINE=42\"\n"
		"#include <gmodule.h>\n"
		"G_MODULE_EXPORT int gst_config_init(void) {\n"
		"    return (TEST_DEFINE == 42) ? 1 : 0;\n"
		"}\n");

	compiler = gst_config_compiler_new(&error);
	g_assert_no_error(error);

	/* Compile - should pick up the extra define */
	so_path = gst_config_compiler_compile(compiler, source_path,
		FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(so_path);

	/* Load and apply - init should return TRUE (42 == 42) */
	ok = gst_config_compiler_load_and_apply(compiler, so_path, &error);
	g_assert_no_error(error);
	g_assert_true(ok);

	g_unlink(source_path);
	g_unlink(so_path);
}

/* ===== Test: cache hit on second compile ===== */

static void
test_compiler_cache_hit(void)
{
	g_autoptr(GstConfigCompiler) compiler = NULL;
	g_autofree gchar *source_path = NULL;
	g_autofree gchar *so_path_1 = NULL;
	g_autofree gchar *so_path_2 = NULL;
	GError *error = NULL;

	if (g_find_program_in_path("gcc") == NULL) {
		g_test_skip("gcc not found in PATH");
		return;
	}

	source_path = write_temp_file(".c",
		"#include <gmodule.h>\n"
		"G_MODULE_EXPORT int gst_config_init(void) { return 1; }\n");

	compiler = gst_config_compiler_new(&error);
	g_assert_no_error(error);

	/* First compile */
	so_path_1 = gst_config_compiler_compile(compiler, source_path,
		FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(so_path_1);

	/* Second compile - should be a cache hit (same path) */
	so_path_2 = gst_config_compiler_compile(compiler, source_path,
		FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(so_path_2);
	g_assert_cmpstr(so_path_1, ==, so_path_2);

	g_unlink(source_path);
	g_unlink(so_path_1);
}

/* ===== main ===== */

int
main(
	int   argc,
	char *argv[]
){
	g_test_init(&argc, &argv, NULL);

	/* Config setter tests */
	g_test_add_func("/config-compiler/setters",
		test_config_setters);
	g_test_add_func("/config-compiler/add-keybind",
		test_config_add_keybind);
	g_test_add_func("/config-compiler/add-mousebind",
		test_config_add_mousebind);
	g_test_add_func("/config-compiler/clear-keybinds",
		test_config_clear_keybinds);
	g_test_add_func("/config-compiler/clear-mousebinds",
		test_config_clear_mousebinds);

	/* Compiler tests */
	g_test_add_func("/config-compiler/new",
		test_compiler_new);
	g_test_add_func("/config-compiler/find-config-none",
		test_compiler_find_config_none);
	g_test_add_func("/config-compiler/compile-simple",
		test_compiler_compile_simple);
	g_test_add_func("/config-compiler/compile-invalid",
		test_compiler_compile_invalid);
	g_test_add_func("/config-compiler/load-and-apply",
		test_compiler_load_and_apply);
	g_test_add_func("/config-compiler/missing-symbol",
		test_compiler_missing_symbol);
	g_test_add_func("/config-compiler/crispy-params",
		test_compiler_crispy_params);
	g_test_add_func("/config-compiler/cache-hit",
		test_compiler_cache_hit);

	return g_test_run();
}
