/*
 * GST - Example C Configuration (Catppuccin Mocha)
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This example demonstrates what the C config can do beyond YAML:
 *
 *  - Programmatic color palette generation
 *  - Keybinds using loops and helper functions
 *  - Access to module manager and GObject APIs
 *  - Custom helper functions
 *
 * Install: cp example-config.c ~/.config/gst/config.c
 */

#include <gst/gst.h>

/* --- Helper functions --- */

/*
 * set_catppuccin_mocha:
 * @config: the #GstConfig singleton
 *
 * Applies the Catppuccin Mocha color scheme.
 * https://github.com/catppuccin/catppuccin
 */
static void
set_catppuccin_mocha(GstConfig *config)
{
	/*
	 * Catppuccin Mocha palette (16 standard colors)
	 * 0-7:  normal, 8-15: bright
	 */
	const gchar *palette[] = {
		"#45475a",  /* 0  black   (Surface1) */
		"#f38ba8",  /* 1  red     (Red) */
		"#a6e3a1",  /* 2  green   (Green) */
		"#f9e2af",  /* 3  yellow  (Yellow) */
		"#89b4fa",  /* 4  blue    (Blue) */
		"#f5c2e7",  /* 5  magenta (Pink) */
		"#94e2d5",  /* 6  cyan    (Teal) */
		"#bac2de",  /* 7  white   (Subtext1) */
		"#585b70",  /* 8  bright black   (Surface2) */
		"#f38ba8",  /* 9  bright red     (Red) */
		"#a6e3a1",  /* 10 bright green   (Green) */
		"#f9e2af",  /* 11 bright yellow  (Yellow) */
		"#89b4fa",  /* 12 bright blue    (Blue) */
		"#f5c2e7",  /* 13 bright magenta (Pink) */
		"#94e2d5",  /* 14 bright cyan    (Teal) */
		"#a6adc8",  /* 15 bright white   (Subtext0) */
		NULL
	};

	gst_config_set_palette_hex(config, palette, 16);

	/* Direct foreground/background/cursor colors */
	gst_config_set_fg_hex(config, "#cdd6f4");   /* Text */
	gst_config_set_bg_hex(config, "#1e1e2e");   /* Base */
	gst_config_set_cursor_fg_hex(config, "#1e1e2e"); /* Base */
	gst_config_set_cursor_bg_hex(config, "#f5e0dc"); /* Rosewater */
}

/*
 * setup_keybinds:
 * @config: the #GstConfig singleton
 *
 * Sets up custom key bindings. Demonstrates replacing defaults
 * and using programmatic binding setup.
 */
static void
setup_keybinds(GstConfig *config)
{
	/* Clear defaults and build our own set */
	gst_config_clear_keybinds(config);

	/* Clipboard */
	gst_config_add_keybind(config, "Ctrl+Shift+c", "clipboard_copy");
	gst_config_add_keybind(config, "Ctrl+Shift+v", "clipboard_paste");
	gst_config_add_keybind(config, "Shift+Insert", "paste_primary");

	/* Scrollback */
	gst_config_add_keybind(config, "Shift+Page_Up", "scroll_up");
	gst_config_add_keybind(config, "Shift+Page_Down", "scroll_down");
	gst_config_add_keybind(config, "Ctrl+Shift+Home", "scroll_top");
	gst_config_add_keybind(config, "Ctrl+Shift+End", "scroll_bottom");

	/* Zoom */
	gst_config_add_keybind(config, "Ctrl+Shift+plus", "zoom_in");
	gst_config_add_keybind(config, "Ctrl+Shift+minus", "zoom_out");
	gst_config_add_keybind(config, "Ctrl+Shift+0", "zoom_reset");
}

/**
 * gst_config_init:
 *
 * Entry point called by the config compiler after the shared object
 * is loaded. This runs after YAML config, so values set here
 * take precedence.
 *
 * Returns: TRUE on success, FALSE to fall back to YAML-only config.
 */
G_MODULE_EXPORT gboolean
gst_config_init(void)
{
	GstConfig *config;

	config = gst_config_get_default();

	/* Font */
	gst_config_set_font_primary(config,
		"JetBrains Mono:pixelsize=14:antialias=true:autohint=true");

	/* Colors */
	set_catppuccin_mocha(config);

	/* Cursor */
	gst_config_set_cursor_shape(config, GST_CURSOR_SHAPE_BAR);
	gst_config_set_cursor_blink(config, TRUE);
	gst_config_set_blink_rate(config, 600);

	/* Window */
	gst_config_set_border_px(config, 4);

	/* Draw latency */
	gst_config_set_min_latency(config, 8);
	gst_config_set_max_latency(config, 33);

	/* Keybinds */
	setup_keybinds(config);

	return TRUE;
}
