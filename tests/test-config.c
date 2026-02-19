/*
 * test-config.c - Tests for GstConfig YAML loading and GstColorScheme
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "config/gst-config.h"
#include "config/gst-color-scheme.h"
#include "gst-enums.h"

/* ===== Helper: write YAML to a temp file and return the path ===== */

static gchar *
write_temp_yaml(const gchar *yaml_content)
{
	gchar *path;
	GError *error = NULL;
	gint fd;

	fd = g_file_open_tmp("gst-test-XXXXXX.yaml", &path, &error);
	g_assert_no_error(error);
	g_assert_cmpint(fd, >=, 0);

	g_assert_true(g_file_set_contents(path, yaml_content, -1, &error));
	g_assert_no_error(error);
	close(fd);

	return path;
}

/* ===== Test: Default values ===== */

static void
test_config_defaults(void)
{
	g_autoptr(GstConfig) config = NULL;

	config = gst_config_new();
	g_assert_nonnull(config);

	/* Terminal defaults */
	g_assert_nonnull(gst_config_get_shell(config));
	g_assert_cmpstr(gst_config_get_term_name(config), ==, "st-256color");
	g_assert_cmpuint(gst_config_get_tabspaces(config), ==, 8);

	/* Window defaults */
	g_assert_cmpstr(gst_config_get_title(config), ==, "gst");
	g_assert_cmpuint(gst_config_get_cols(config), ==, 80);
	g_assert_cmpuint(gst_config_get_rows(config), ==, 24);
	g_assert_cmpuint(gst_config_get_border_px(config), ==, 2);

	/* Font defaults */
	g_assert_cmpstr(gst_config_get_font_primary(config), ==,
		"Liberation Mono:pixelsize=14:antialias=true:autohint=true");
	g_assert_null(gst_config_get_font_fallbacks(config));

	/* Color defaults */
	g_assert_cmpuint(gst_config_get_fg_index(config), ==, 7);
	g_assert_cmpuint(gst_config_get_bg_index(config), ==, 0);
	g_assert_cmpuint(gst_config_get_cursor_fg_index(config), ==, 0);
	g_assert_cmpuint(gst_config_get_cursor_bg_index(config), ==, 7);
	g_assert_null(gst_config_get_palette_hex(config));
	g_assert_cmpuint(gst_config_get_n_palette(config), ==, 0);

	/* Cursor defaults */
	g_assert_cmpint(gst_config_get_cursor_shape(config), ==,
		GST_CURSOR_SHAPE_BLOCK);
	g_assert_false(gst_config_get_cursor_blink(config));
	g_assert_cmpuint(gst_config_get_blink_rate(config), ==, 500);

	/* Selection defaults */
	g_assert_nonnull(gst_config_get_word_delimiters(config));

	/* Draw latency defaults */
	g_assert_cmpuint(gst_config_get_min_latency(config), ==, 8);
	g_assert_cmpuint(gst_config_get_max_latency(config), ==, 33);

	/* Module config defaults */
	g_assert_true(config->modules.scrollback.enabled);
	g_assert_cmpint(config->modules.scrollback.lines, ==, 10000);
	g_assert_false(config->modules.transparency.enabled);
	g_assert_false(config->modules.sixel.enabled);
}

/* ===== Test: Load terminal section ===== */

static void
test_config_load_terminal(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"terminal:\n"
		"  shell: /bin/zsh\n"
		"  term: xterm-256color\n"
		"  tabspaces: 4\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	g_assert_cmpstr(gst_config_get_shell(config), ==, "/bin/zsh");
	g_assert_cmpstr(gst_config_get_term_name(config), ==, "xterm-256color");
	g_assert_cmpuint(gst_config_get_tabspaces(config), ==, 4);

	/* Other sections should remain at defaults */
	g_assert_cmpuint(gst_config_get_cols(config), ==, 80);
	g_assert_cmpuint(gst_config_get_rows(config), ==, 24);

	g_unlink(path);
}

/* ===== Test: Load window section with geometry ===== */

static void
test_config_load_window(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"window:\n"
		"  title: myterm\n"
		"  geometry: 100x30\n"
		"  border: 5\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	g_assert_cmpstr(gst_config_get_title(config), ==, "myterm");
	g_assert_cmpuint(gst_config_get_cols(config), ==, 100);
	g_assert_cmpuint(gst_config_get_rows(config), ==, 30);
	g_assert_cmpuint(gst_config_get_border_px(config), ==, 5);

	g_unlink(path);
}

/* ===== Test: Load font section with fallbacks ===== */

static void
test_config_load_font(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;
	const gchar *const *fallbacks;

	path = write_temp_yaml(
		"font:\n"
		"  primary: \"Fira Code:pixelsize=16\"\n"
		"  fallback:\n"
		"    - \"Noto Emoji:pixelsize=16\"\n"
		"    - \"Symbols Nerd Font:pixelsize=16\"\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	g_assert_cmpstr(gst_config_get_font_primary(config), ==,
		"Fira Code:pixelsize=16");

	fallbacks = gst_config_get_font_fallbacks(config);
	g_assert_nonnull(fallbacks);
	g_assert_cmpstr(fallbacks[0], ==, "Noto Emoji:pixelsize=16");
	g_assert_cmpstr(fallbacks[1], ==, "Symbols Nerd Font:pixelsize=16");
	g_assert_null(fallbacks[2]);

	g_unlink(path);
}

/* ===== Test: Load colors section with palette ===== */

static void
test_config_load_colors(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;
	const gchar *const *palette;

	path = write_temp_yaml(
		"colors:\n"
		"  foreground: 15\n"
		"  background: 0\n"
		"  cursor_fg: 0\n"
		"  cursor_bg: 15\n"
		"  palette:\n"
		"    - \"#1e1e2e\"\n"
		"    - \"#f38ba8\"\n"
		"    - \"#a6e3a1\"\n"
		"    - \"#f9e2af\"\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	g_assert_cmpuint(gst_config_get_fg_index(config), ==, 15);
	g_assert_cmpuint(gst_config_get_bg_index(config), ==, 0);
	g_assert_cmpuint(gst_config_get_cursor_fg_index(config), ==, 0);
	g_assert_cmpuint(gst_config_get_cursor_bg_index(config), ==, 15);

	palette = gst_config_get_palette_hex(config);
	g_assert_nonnull(palette);
	g_assert_cmpuint(gst_config_get_n_palette(config), ==, 4);
	g_assert_cmpstr(palette[0], ==, "#1e1e2e");
	g_assert_cmpstr(palette[1], ==, "#f38ba8");
	g_assert_cmpstr(palette[2], ==, "#a6e3a1");
	g_assert_cmpstr(palette[3], ==, "#f9e2af");
	g_assert_null(palette[4]);

	g_unlink(path);
}

/* ===== Test: Load cursor section ===== */

static void
test_config_load_cursor(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"cursor:\n"
		"  shape: underline\n"
		"  blink: true\n"
		"  blink_rate: 250\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	g_assert_cmpint(gst_config_get_cursor_shape(config), ==,
		GST_CURSOR_SHAPE_UNDERLINE);
	g_assert_true(gst_config_get_cursor_blink(config));
	g_assert_cmpuint(gst_config_get_blink_rate(config), ==, 250);

	g_unlink(path);
}

/* ===== Test: Cursor shape "bar" ===== */

static void
test_config_cursor_bar(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"cursor:\n"
		"  shape: bar\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	g_assert_cmpint(gst_config_get_cursor_shape(config), ==,
		GST_CURSOR_SHAPE_BAR);

	g_unlink(path);
}

/* ===== Test: Missing sections use defaults ===== */

static void
test_config_missing_sections(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	/* Only terminal section — everything else stays default */
	path = write_temp_yaml(
		"terminal:\n"
		"  shell: /bin/fish\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	g_assert_cmpstr(gst_config_get_shell(config), ==, "/bin/fish");

	/* Window defaults */
	g_assert_cmpstr(gst_config_get_title(config), ==, "gst");
	g_assert_cmpuint(gst_config_get_cols(config), ==, 80);
	g_assert_cmpuint(gst_config_get_rows(config), ==, 24);

	/* Font defaults */
	g_assert_cmpstr(gst_config_get_font_primary(config), ==,
		"Liberation Mono:pixelsize=14:antialias=true:autohint=true");

	/* Cursor defaults */
	g_assert_cmpint(gst_config_get_cursor_shape(config), ==,
		GST_CURSOR_SHAPE_BLOCK);
	g_assert_false(gst_config_get_cursor_blink(config));

	g_unlink(path);
}

/* ===== Test: Invalid YAML returns error ===== */

static void
test_config_invalid_yaml(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	/*
	 * Use YAML that fails parsing: an unmatched flow sequence
	 * bracket followed by an invalid key context triggers a parse error.
	 */
	path = write_temp_yaml(":\n  :\n    - [\n");

	config = gst_config_new();
	g_assert_false(gst_config_load_from_path(config, path, &error));
	g_assert_nonnull(error);
	g_assert_cmpuint(error->domain, ==, GST_CONFIG_ERROR);
	g_error_free(error);

	g_unlink(path);
}

/* ===== Test: Invalid geometry returns error ===== */

static void
test_config_invalid_geometry(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"window:\n"
		"  geometry: not-a-geometry\n"
	);

	config = gst_config_new();
	g_assert_false(gst_config_load_from_path(config, path, &error));
	g_assert_nonnull(error);
	g_assert_cmpuint(error->domain, ==, GST_CONFIG_ERROR);
	g_assert_cmpint(error->code, ==, GST_CONFIG_ERROR_INVALID_VALUE);
	g_error_free(error);

	g_unlink(path);
}

/* ===== Test: Invalid cursor shape returns error ===== */

static void
test_config_invalid_cursor_shape(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"cursor:\n"
		"  shape: triangle\n"
	);

	config = gst_config_new();
	g_assert_false(gst_config_load_from_path(config, path, &error));
	g_assert_nonnull(error);
	g_assert_cmpuint(error->domain, ==, GST_CONFIG_ERROR);
	g_assert_cmpint(error->code, ==, GST_CONFIG_ERROR_INVALID_VALUE);
	g_error_free(error);

	g_unlink(path);
}

/* ===== Test: get_default returns valid singleton ===== */

static void
test_config_get_default(void)
{
	GstConfig *config1;
	GstConfig *config2;

	config1 = gst_config_get_default();
	g_assert_nonnull(config1);

	config2 = gst_config_get_default();
	g_assert_true(config1 == config2);
}

/* ===== Test: Load full config (default-config.yaml template) ===== */

static void
test_config_load_full(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;
	const gchar *const *palette;

	path = write_temp_yaml(
		"terminal:\n"
		"  shell: /bin/bash\n"
		"  term: st-256color\n"
		"  tabspaces: 8\n"
		"\n"
		"window:\n"
		"  title: gst\n"
		"  geometry: 80x24\n"
		"  border: 2\n"
		"\n"
		"font:\n"
		"  primary: \"Liberation Mono:pixelsize=14\"\n"
		"  fallback:\n"
		"    - \"Noto Color Emoji:pixelsize=14\"\n"
		"\n"
		"colors:\n"
		"  foreground: 7\n"
		"  background: 0\n"
		"  cursor_fg: 0\n"
		"  cursor_bg: 7\n"
		"  palette:\n"
		"    - \"#000000\"\n"
		"    - \"#cc0000\"\n"
		"\n"
		"cursor:\n"
		"  shape: block\n"
		"  blink: false\n"
		"  blink_rate: 500\n"
		"\n"
		"selection:\n"
		"  word_delimiters: \" `'\\\"()[]{}|\"\n"
		"\n"
		"modules:\n"
		"  scrollback:\n"
		"    enabled: true\n"
		"    lines: 10000\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	/* Verify all sections loaded */
	g_assert_cmpstr(gst_config_get_shell(config), ==, "/bin/bash");
	g_assert_cmpstr(gst_config_get_title(config), ==, "gst");
	g_assert_cmpuint(gst_config_get_cols(config), ==, 80);
	g_assert_cmpuint(gst_config_get_rows(config), ==, 24);

	g_assert_cmpstr(gst_config_get_font_primary(config), ==,
		"Liberation Mono:pixelsize=14");

	palette = gst_config_get_palette_hex(config);
	g_assert_nonnull(palette);
	g_assert_cmpuint(gst_config_get_n_palette(config), ==, 2);
	g_assert_cmpstr(palette[0], ==, "#000000");
	g_assert_cmpstr(palette[1], ==, "#cc0000");

	g_assert_cmpint(gst_config_get_cursor_shape(config), ==,
		GST_CURSOR_SHAPE_BLOCK);
	g_assert_false(gst_config_get_cursor_blink(config));

	/* Module config (direct struct access) */
	g_assert_true(config->modules.scrollback.enabled);
	g_assert_cmpint(config->modules.scrollback.lines, ==, 10000);

	g_unlink(path);
}

/* ===== Test: GstColorScheme setters ===== */

static void
test_color_scheme_setters(void)
{
	g_autoptr(GstColorScheme) scheme = NULL;

	scheme = gst_color_scheme_new("test");
	g_assert_nonnull(scheme);

	/* Test set_foreground */
	gst_color_scheme_set_foreground(scheme, 0xFFAABBCC);
	g_assert_cmphex(gst_color_scheme_get_foreground(scheme), ==, 0xFFAABBCC);

	/* Test set_background */
	gst_color_scheme_set_background(scheme, 0xFF112233);
	g_assert_cmphex(gst_color_scheme_get_background(scheme), ==, 0xFF112233);

	/* Test set_cursor_color */
	gst_color_scheme_set_cursor_color(scheme, 0xFF445566);
	g_assert_cmphex(gst_color_scheme_get_cursor_color(scheme), ==, 0xFF445566);

	/* Test set_color */
	gst_color_scheme_set_color(scheme, 0, 0xFF111111);
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 0), ==, 0xFF111111);

	gst_color_scheme_set_color(scheme, 15, 0xFFEEEEEE);
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 15), ==, 0xFFEEEEEE);

	/* Test boundary index (255) */
	gst_color_scheme_set_color(scheme, 255, 0xFF999999);
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 255), ==, 0xFF999999);
}

/* ===== Test: Color scheme from config ===== */

static void
test_color_scheme_from_config(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autoptr(GstColorScheme) scheme = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"colors:\n"
		"  foreground: 7\n"
		"  background: 0\n"
		"  cursor_fg: 0\n"
		"  cursor_bg: 7\n"
		"  palette:\n"
		"    - \"#1e1e2e\"\n"
		"    - \"#f38ba8\"\n"
		"    - \"#a6e3a1\"\n"
		"    - \"#f9e2af\"\n"
		"    - \"#89b4fa\"\n"
		"    - \"#cba6f7\"\n"
		"    - \"#94e2d5\"\n"
		"    - \"#cdd6f4\"\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	scheme = gst_color_scheme_new("catppuccin");
	g_assert_true(gst_color_scheme_load_from_config(scheme, config));

	/* Palette index 0 = #1e1e2e = 0xFF1E1E2E */
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 0), ==, 0xFF1E1E2E);

	/* Palette index 1 = #f38ba8 = 0xFFF38BA8 */
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 1), ==, 0xFFF38BA8);

	/* Palette index 7 = #cdd6f4 = 0xFFCDD6F4 */
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 7), ==, 0xFFCDD6F4);

	/* Foreground = palette[7] = #cdd6f4 */
	g_assert_cmphex(gst_color_scheme_get_foreground(scheme), ==, 0xFFCDD6F4);

	/* Background = palette[0] = #1e1e2e */
	g_assert_cmphex(gst_color_scheme_get_background(scheme), ==, 0xFF1E1E2E);

	/* Cursor = palette[7] (cursor_bg) */
	g_assert_cmphex(gst_color_scheme_get_cursor_color(scheme), ==, 0xFFCDD6F4);

	/* Indices 8+ should still be default xterm values */
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 8), ==, 0xFF7F7F7F);

	g_unlink(path);
}

/* ===== Test: Color scheme defaults ===== */

static void
test_color_scheme_defaults(void)
{
	g_autoptr(GstColorScheme) scheme = NULL;

	scheme = gst_color_scheme_new("default");
	g_assert_nonnull(scheme);

	g_assert_cmpstr(gst_color_scheme_get_name(scheme), ==, "default");
	g_assert_cmphex(gst_color_scheme_get_foreground(scheme), ==, 0xFFFFFFFF);
	g_assert_cmphex(gst_color_scheme_get_background(scheme), ==, 0xFF000000);
	g_assert_cmphex(gst_color_scheme_get_cursor_color(scheme), ==, 0xFFFFFFFF);

	/* Standard palette colors */
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 0), ==, 0xFF000000);
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 7), ==, 0xFFE5E5E5);
	g_assert_cmphex(gst_color_scheme_get_color(scheme, 15), ==, 0xFFFFFFFF);
}

/* ===== Test: Module config access (direct struct) ===== */

static void
test_config_module_config(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"modules:\n"
		"  scrollback:\n"
		"    enabled: true\n"
		"    lines: 5000\n"
		"  transparency:\n"
		"    enabled: false\n"
		"    opacity: 0.95\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	/* Scrollback: enabled + lines overridden from YAML */
	g_assert_true(config->modules.scrollback.enabled);
	g_assert_cmpint(config->modules.scrollback.lines, ==, 5000);

	/* Transparency: disabled, opacity overridden */
	g_assert_false(config->modules.transparency.enabled);
	g_assert_cmpfloat_with_epsilon(
		config->modules.transparency.opacity, 0.95, 0.001);

	/* Default field not in YAML keeps its init value */
	g_assert_cmpint(config->modules.scrollback.mouse_scroll_lines, ==, 3);

	g_unlink(path);
}

/* ===== Test: Selection section ===== */

static void
test_config_load_selection(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;

	path = write_temp_yaml(
		"selection:\n"
		"  word_delimiters: \"hello world\"\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	g_assert_cmpstr(gst_config_get_word_delimiters(config), ==,
		"hello world");

	g_unlink(path);
}

/* ===== Test: Save and reload round-trip ===== */

static void
test_config_save_roundtrip(void)
{
	g_autoptr(GstConfig) config1 = NULL;
	g_autoptr(GstConfig) config2 = NULL;
	g_autofree gchar *load_path = NULL;
	g_autofree gchar *save_path = NULL;
	g_autoptr(GFile) save_file = NULL;
	GError *error = NULL;

	/* Load a config */
	load_path = write_temp_yaml(
		"terminal:\n"
		"  shell: /bin/zsh\n"
		"  term: xterm-256color\n"
		"  tabspaces: 4\n"
		"window:\n"
		"  title: myterm\n"
		"  geometry: 120x40\n"
		"  border: 3\n"
		"cursor:\n"
		"  shape: bar\n"
		"  blink: true\n"
		"  blink_rate: 250\n"
	);

	config1 = gst_config_new();
	g_assert_true(gst_config_load_from_path(config1, load_path, &error));
	g_assert_no_error(error);

	/* Save to a new temp file */
	{
		gint fd;

		fd = g_file_open_tmp("gst-test-save-XXXXXX.yaml",
			&save_path, &error);
		g_assert_no_error(error);
		close(fd);
	}

	save_file = g_file_new_for_path(save_path);
	g_assert_true(gst_config_save_to_file(config1, save_file, &error));
	g_assert_no_error(error);

	/* Reload and verify */
	config2 = gst_config_new();
	g_assert_true(gst_config_load_from_path(config2, save_path, &error));
	g_assert_no_error(error);

	g_assert_cmpstr(gst_config_get_shell(config2), ==, "/bin/zsh");
	g_assert_cmpstr(gst_config_get_term_name(config2), ==,
		"xterm-256color");
	g_assert_cmpuint(gst_config_get_tabspaces(config2), ==, 4);
	g_assert_cmpstr(gst_config_get_title(config2), ==, "myterm");
	g_assert_cmpuint(gst_config_get_cols(config2), ==, 120);
	g_assert_cmpuint(gst_config_get_rows(config2), ==, 40);
	g_assert_cmpuint(gst_config_get_border_px(config2), ==, 3);
	g_assert_cmpint(gst_config_get_cursor_shape(config2), ==,
		GST_CURSOR_SHAPE_BAR);
	g_assert_true(gst_config_get_cursor_blink(config2));
	g_assert_cmpuint(gst_config_get_blink_rate(config2), ==, 250);

	g_unlink(load_path);
	g_unlink(save_path);
}

/* ===== Main ===== */

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);

	/* GstConfig tests */
	g_test_add_func("/config/defaults", test_config_defaults);
	g_test_add_func("/config/load-terminal", test_config_load_terminal);
	g_test_add_func("/config/load-window", test_config_load_window);
	g_test_add_func("/config/load-font", test_config_load_font);
	g_test_add_func("/config/load-colors", test_config_load_colors);
	g_test_add_func("/config/load-cursor", test_config_load_cursor);
	g_test_add_func("/config/cursor-bar", test_config_cursor_bar);
	g_test_add_func("/config/missing-sections", test_config_missing_sections);
	g_test_add_func("/config/invalid-yaml", test_config_invalid_yaml);
	g_test_add_func("/config/invalid-geometry", test_config_invalid_geometry);
	g_test_add_func("/config/invalid-cursor-shape", test_config_invalid_cursor_shape);
	g_test_add_func("/config/get-default", test_config_get_default);
	g_test_add_func("/config/load-full", test_config_load_full);
	g_test_add_func("/config/load-selection", test_config_load_selection);
	g_test_add_func("/config/module-config", test_config_module_config);
	g_test_add_func("/config/save-roundtrip", test_config_save_roundtrip);

	/* GstColorScheme tests */
	g_test_add_func("/color-scheme/defaults", test_color_scheme_defaults);
	g_test_add_func("/color-scheme/setters", test_color_scheme_setters);
	g_test_add_func("/color-scheme/from-config", test_color_scheme_from_config);

	return g_test_run();
}
