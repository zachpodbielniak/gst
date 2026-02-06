/*
 * gst-config.h - YAML configuration handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * #GstConfig manages loading and saving of terminal configuration
 * from YAML files using yaml-glib. It provides getters for all
 * configurable options (terminal, window, font, colors, cursor,
 * selection, draw latency, and per-module config).
 *
 * Configuration search path:
 *  1. --config PATH (explicit override)
 *  2. $XDG_CONFIG_HOME/gst/config.yaml (~/.config/gst/config.yaml)
 *  3. /etc/gst/config.yaml
 *  4. /usr/share/gst/config.yaml
 *  5. Built-in defaults (no file needed)
 */

#ifndef GST_CONFIG_H
#define GST_CONFIG_H

#include <glib-object.h>
#include <gio/gio.h>
#include "../gst-enums.h"
#include "gst-keybind.h"
#include <yaml-glib.h>

G_BEGIN_DECLS

/* ===== Error domain ===== */

#define GST_CONFIG_ERROR (gst_config_error_quark())

GQuark
gst_config_error_quark(void);

/**
 * GstConfigError:
 * @GST_CONFIG_ERROR_PARSE: YAML parse error
 * @GST_CONFIG_ERROR_INVALID_VALUE: Value out of range or wrong type
 * @GST_CONFIG_ERROR_IO: File I/O error
 *
 * Error codes for #GstConfig operations.
 */
typedef enum {
	GST_CONFIG_ERROR_PARSE,
	GST_CONFIG_ERROR_INVALID_VALUE,
	GST_CONFIG_ERROR_IO
} GstConfigError;

/* ===== GstConfig type ===== */

#define GST_TYPE_CONFIG (gst_config_get_type())

G_DECLARE_FINAL_TYPE(GstConfig, gst_config, GST, CONFIG, GObject)

GType
gst_config_get_type(void) G_GNUC_CONST;

/**
 * gst_config_new:
 *
 * Creates a new configuration instance with default values.
 *
 * Returns: (transfer full): A new #GstConfig
 */
GstConfig *
gst_config_new(void);

/**
 * gst_config_get_default:
 *
 * Gets the default shared configuration instance.
 * The singleton is created on first call with built-in defaults.
 *
 * Returns: (transfer none): The default #GstConfig
 */
GstConfig *
gst_config_get_default(void);

/* ===== Loading / saving ===== */

/**
 * gst_config_load_from_file:
 * @self: A #GstConfig
 * @file: The configuration file to load
 * @error: (nullable): Return location for a #GError
 *
 * Loads configuration from a YAML file via GFile.
 * Missing sections use defaults; invalid values set @error.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
gst_config_load_from_file(
	GstConfig  *self,
	GFile      *file,
	GError    **error
);

/**
 * gst_config_load_from_path:
 * @self: A #GstConfig
 * @path: Filesystem path to the YAML configuration file
 * @error: (nullable): Return location for a #GError
 *
 * Convenience wrapper around gst_config_load_from_file() that
 * takes a string path instead of a #GFile.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
gst_config_load_from_path(
	GstConfig   *self,
	const gchar *path,
	GError     **error
);

/**
 * gst_config_save_to_file:
 * @self: A #GstConfig
 * @file: The file to save to
 * @error: (nullable): Return location for a #GError
 *
 * Saves the current configuration to a YAML file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
gst_config_save_to_file(
	GstConfig  *self,
	GFile      *file,
	GError    **error
);

/* ===== Terminal getters ===== */

/**
 * gst_config_get_shell:
 * @self: A #GstConfig
 *
 * Gets the shell command to spawn (e.g. "/bin/bash").
 *
 * Returns: (transfer none): The shell path
 */
const gchar *
gst_config_get_shell(GstConfig *self);

/**
 * gst_config_get_term_name:
 * @self: A #GstConfig
 *
 * Gets the TERM environment variable value (e.g. "st-256color").
 *
 * Returns: (transfer none): The TERM name
 */
const gchar *
gst_config_get_term_name(GstConfig *self);

/**
 * gst_config_get_tabspaces:
 * @self: A #GstConfig
 *
 * Gets the number of spaces per tab stop.
 *
 * Returns: Tab stop width
 */
guint
gst_config_get_tabspaces(GstConfig *self);

/* ===== Window getters ===== */

/**
 * gst_config_get_title:
 * @self: A #GstConfig
 *
 * Gets the default window title.
 *
 * Returns: (transfer none): The window title
 */
const gchar *
gst_config_get_title(GstConfig *self);

/**
 * gst_config_get_cols:
 * @self: A #GstConfig
 *
 * Gets the default number of terminal columns.
 *
 * Returns: Column count
 */
guint
gst_config_get_cols(GstConfig *self);

/**
 * gst_config_get_rows:
 * @self: A #GstConfig
 *
 * Gets the default number of terminal rows.
 *
 * Returns: Row count
 */
guint
gst_config_get_rows(GstConfig *self);

/**
 * gst_config_get_border_px:
 * @self: A #GstConfig
 *
 * Gets the border padding in pixels around the terminal area.
 *
 * Returns: Border padding in pixels
 */
guint
gst_config_get_border_px(GstConfig *self);

/* ===== Font getters ===== */

/**
 * gst_config_get_font_primary:
 * @self: A #GstConfig
 *
 * Gets the primary font specification string (fontconfig format).
 *
 * Returns: (transfer none): The primary font string
 */
const gchar *
gst_config_get_font_primary(GstConfig *self);

/**
 * gst_config_get_font_fallbacks:
 * @self: A #GstConfig
 *
 * Gets the list of fallback font specification strings.
 *
 * Returns: (transfer none) (array zero-terminated=1) (nullable):
 *          NULL-terminated array of font strings, or %NULL if none
 */
const gchar *const *
gst_config_get_font_fallbacks(GstConfig *self);

/* ===== Color getters ===== */

/**
 * gst_config_get_fg_index:
 * @self: A #GstConfig
 *
 * Gets the palette index used for the default foreground color.
 *
 * Returns: Foreground color palette index
 */
guint
gst_config_get_fg_index(GstConfig *self);

/**
 * gst_config_get_bg_index:
 * @self: A #GstConfig
 *
 * Gets the palette index used for the default background color.
 *
 * Returns: Background color palette index
 */
guint
gst_config_get_bg_index(GstConfig *self);

/**
 * gst_config_get_cursor_fg_index:
 * @self: A #GstConfig
 *
 * Gets the palette index for the cursor foreground.
 *
 * Returns: Cursor foreground palette index
 */
guint
gst_config_get_cursor_fg_index(GstConfig *self);

/**
 * gst_config_get_cursor_bg_index:
 * @self: A #GstConfig
 *
 * Gets the palette index for the cursor background.
 *
 * Returns: Cursor background palette index
 */
guint
gst_config_get_cursor_bg_index(GstConfig *self);

/**
 * gst_config_get_palette_hex:
 * @self: A #GstConfig
 *
 * Gets the hex color strings for the 16-color palette (indices 0-15).
 * Each entry is a "#RRGGBB" string.
 *
 * Returns: (transfer none) (array zero-terminated=1) (nullable):
 *          NULL-terminated array of hex color strings, or %NULL
 *          if the built-in palette should be used
 */
const gchar *const *
gst_config_get_palette_hex(GstConfig *self);

/**
 * gst_config_get_n_palette:
 * @self: A #GstConfig
 *
 * Gets the number of palette entries loaded from config.
 *
 * Returns: Number of palette hex entries (0 if using built-in defaults)
 */
guint
gst_config_get_n_palette(GstConfig *self);

/* ===== Cursor getters ===== */

/**
 * gst_config_get_cursor_shape:
 * @self: A #GstConfig
 *
 * Gets the cursor shape (block, underline, or bar).
 *
 * Returns: The #GstCursorShape
 */
GstCursorShape
gst_config_get_cursor_shape(GstConfig *self);

/**
 * gst_config_get_cursor_blink:
 * @self: A #GstConfig
 *
 * Gets whether the cursor should blink.
 *
 * Returns: %TRUE if blinking is enabled
 */
gboolean
gst_config_get_cursor_blink(GstConfig *self);

/**
 * gst_config_get_blink_rate:
 * @self: A #GstConfig
 *
 * Gets the cursor blink rate in milliseconds.
 *
 * Returns: Blink rate in ms
 */
guint
gst_config_get_blink_rate(GstConfig *self);

/* ===== Selection getters ===== */

/**
 * gst_config_get_word_delimiters:
 * @self: A #GstConfig
 *
 * Gets the string of characters used as word delimiters for
 * double-click word selection.
 *
 * Returns: (transfer none): The word delimiter characters
 */
const gchar *
gst_config_get_word_delimiters(GstConfig *self);

/* ===== Draw latency getters ===== */

/**
 * gst_config_get_min_latency:
 * @self: A #GstConfig
 *
 * Gets the minimum draw latency in milliseconds.
 * The renderer waits at least this long for more data before drawing.
 *
 * Returns: Minimum latency in ms
 */
guint
gst_config_get_min_latency(GstConfig *self);

/**
 * gst_config_get_max_latency:
 * @self: A #GstConfig
 *
 * Gets the maximum draw latency in milliseconds.
 * The renderer draws immediately if this threshold is exceeded.
 *
 * Returns: Maximum latency in ms
 */
guint
gst_config_get_max_latency(GstConfig *self);

/* ===== Module config ===== */

/**
 * gst_config_get_module_config:
 * @self: A #GstConfig
 * @module_name: The name of the module (e.g. "scrollback")
 *
 * Gets the raw YAML mapping for a module's configuration section.
 * Modules can query their own sub-keys from this mapping.
 *
 * Returns: (transfer none) (nullable): The module's #YamlMapping,
 *          or %NULL if no config exists for @module_name
 */
YamlMapping *
gst_config_get_module_config(
	GstConfig   *self,
	const gchar *module_name
);

/* ===== Key binding getters ===== */

/**
 * gst_config_get_keybinds:
 * @self: A #GstConfig
 *
 * Gets the configured key bindings array.
 *
 * Returns: (transfer none) (element-type GstKeybind): The key bindings
 */
const GArray *
gst_config_get_keybinds(GstConfig *self);

/**
 * gst_config_get_mousebinds:
 * @self: A #GstConfig
 *
 * Gets the configured mouse bindings array.
 *
 * Returns: (transfer none) (element-type GstMousebind): The mouse bindings
 */
const GArray *
gst_config_get_mousebinds(GstConfig *self);

/**
 * gst_config_lookup_key_action:
 * @self: A #GstConfig
 * @keyval: X11 keysym
 * @state: X11 modifier state
 *
 * Convenience wrapper: looks up a key action from the config's bindings.
 *
 * Returns: The matching #GstAction, or %GST_ACTION_NONE
 */
GstAction
gst_config_lookup_key_action(
	GstConfig *self,
	guint     keyval,
	guint     state
);

/**
 * gst_config_lookup_mouse_action:
 * @self: A #GstConfig
 * @button: Mouse button number
 * @state: X11 modifier state
 *
 * Convenience wrapper: looks up a mouse action from the config's bindings.
 *
 * Returns: The matching #GstAction, or %GST_ACTION_NONE
 */
GstAction
gst_config_lookup_mouse_action(
	GstConfig *self,
	guint     button,
	guint     state
);

G_END_DECLS

#endif /* GST_CONFIG_H */
