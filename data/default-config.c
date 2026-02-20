/*
 * GST - Default C Configuration
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Install: cp default-config.c ~/.config/gst/config.c
 * Auto-compile: gst (compiles with content-hash caching via crispy)
 * Manual compile:
 *   gcc $(pkg-config --cflags --libs glib-2.0 gobject-2.0 gmodule-2.0 gst) \
 *       -std=gnu89 -shared -fPIC -o config.so config.c
 *
 * The CRISPY_PARAMS define below is optional. If present, the
 * config compiler extracts it and passes the value as extra flags
 * to gcc. Supports shell expansion (e.g. $(pkg-config ...)).
 * Remove or modify it if you need custom include paths or
 * additional libraries.
 */

/* #define CRISPY_PARAMS "-I/custom/path -lmylib" */

#include <gst/gst.h>

/*
 * gst_config_init:
 *
 * Entry point called after YAML config is loaded but before
 * modules are activated. Any values set here override YAML.
 *
 * The GstConfig singleton is available via gst_config_get_default().
 * The GstModuleManager singleton is available via
 * gst_module_manager_get_default().
 *
 * You have full access to all GObject APIs, all GST types, and
 * any library you link against. There is no restriction on what
 * you can do here.
 *
 * Returns: TRUE on success, FALSE to fall back to YAML-only config.
 */
G_MODULE_EXPORT gboolean
gst_config_init(void)
{
	GstConfig *config;

	config = gst_config_get_default();

	/* --- Terminal --- */
	/* gst_config_set_shell(config, "/bin/bash"); */
	/* gst_config_set_term_name(config, "gst-256color"); */
	/* gst_config_set_tabspaces(config, 8); */

	/* --- Window --- */
	/* gst_config_set_title(config, "gst"); */
	/* gst_config_set_cols(config, 80); */
	/* gst_config_set_rows(config, 24); */
	/* gst_config_set_border_px(config, 2); */

	/* --- Font --- */
	/* gst_config_set_font_primary(config,
	 *     "Liberation Mono:pixelsize=14:antialias=true:autohint=true"); */

	/* --- Colors --- */
	/* gst_config_set_fg_index(config, 7); */
	/* gst_config_set_bg_index(config, 0); */
	/* gst_config_set_fg_hex(config, "#cdd6f4"); */
	/* gst_config_set_bg_hex(config, "#1e1e2e"); */

	/* --- Cursor --- */
	/* gst_config_set_cursor_shape(config, GST_CURSOR_SHAPE_BLOCK); */
	/* gst_config_set_cursor_blink(config, FALSE); */
	/* gst_config_set_blink_rate(config, 500); */

	/* --- Selection --- */
	/* gst_config_set_word_delimiters(config, " `'\"()[]{}|"); */

	/* --- Draw latency --- */
	/* gst_config_set_min_latency(config, 8); */
	/* gst_config_set_max_latency(config, 33); */

	/* --- Keybinds (append to existing, or clear first) --- */
	/* gst_config_clear_keybinds(config); */
	/* gst_config_add_keybind(config, "Ctrl+Shift+c", "clipboard_copy"); */
	/* gst_config_add_keybind(config, "Ctrl+Shift+v", "clipboard_paste"); */

	/* --- Module config (direct struct access) --- */
	/* config->modules.scrollback.enabled = TRUE; */
	/* config->modules.scrollback.lines = 10000; */
	/* config->modules.transparency.opacity = 0.9; */
	/* GST_CONFIG_SET_STRING(config->modules.urlclick.opener, "xdg-open"); */
	/* config->modules.sixel.enabled = TRUE; */
	/* config->modules.sixel.max_width = 4096; */
	/* config->modules.mcp.enabled = TRUE; */
	/* config->modules.mcp.tools.read_screen = TRUE; */

	(void)config;
	return TRUE;
}
