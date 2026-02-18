/*
 * gst-config.h - YAML configuration handling
 *
 * Copyright (C) 2026 Zach Podbielniak
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
 * gst_config_get_fg_hex:
 * @self: A #GstConfig
 *
 * Gets the direct hex color for the foreground, if specified.
 *
 * Returns: (transfer none) (nullable): "#RRGGBB" string, or %NULL
 *          if foreground uses a palette index instead
 */
const gchar *
gst_config_get_fg_hex(GstConfig *self);

/**
 * gst_config_get_bg_hex:
 * @self: A #GstConfig
 *
 * Gets the direct hex color for the background, if specified.
 *
 * Returns: (transfer none) (nullable): "#RRGGBB" string, or %NULL
 *          if background uses a palette index instead
 */
const gchar *
gst_config_get_bg_hex(GstConfig *self);

/**
 * gst_config_get_cursor_fg_hex:
 * @self: A #GstConfig
 *
 * Gets the direct hex color for the cursor foreground, if specified.
 *
 * Returns: (transfer none) (nullable): "#RRGGBB" string, or %NULL
 *          if cursor foreground uses a palette index instead
 */
const gchar *
gst_config_get_cursor_fg_hex(GstConfig *self);

/**
 * gst_config_get_cursor_bg_hex:
 * @self: A #GstConfig
 *
 * Gets the direct hex color for the cursor background, if specified.
 *
 * Returns: (transfer none) (nullable): "#RRGGBB" string, or %NULL
 *          if cursor background uses a palette index instead
 */
const gchar *
gst_config_get_cursor_bg_hex(GstConfig *self);

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

/* ===== Module config setters ===== */

/**
 * gst_config_set_module_config_string:
 * @self: A #GstConfig
 * @module_name: Module name (e.g. "scrollback")
 * @key: Configuration key within the module
 * @value: String value to set
 *
 * Sets a string value in a module's configuration section.
 * Creates the module mapping if it does not exist.
 */
void
gst_config_set_module_config_string(
	GstConfig   *self,
	const gchar *module_name,
	const gchar *key,
	const gchar *value
);

/**
 * gst_config_set_module_config_int:
 * @self: A #GstConfig
 * @module_name: Module name (e.g. "scrollback")
 * @key: Configuration key within the module
 * @value: Integer value to set
 *
 * Sets an integer value in a module's configuration section.
 * Creates the module mapping if it does not exist.
 */
void
gst_config_set_module_config_int(
	GstConfig   *self,
	const gchar *module_name,
	const gchar *key,
	gint64       value
);

/**
 * gst_config_set_module_config_double:
 * @self: A #GstConfig
 * @module_name: Module name (e.g. "transparency")
 * @key: Configuration key within the module
 * @value: Double value to set
 *
 * Sets a double value in a module's configuration section.
 * Creates the module mapping if it does not exist.
 */
void
gst_config_set_module_config_double(
	GstConfig   *self,
	const gchar *module_name,
	const gchar *key,
	gdouble      value
);

/**
 * gst_config_set_module_config_bool:
 * @self: A #GstConfig
 * @module_name: Module name (e.g. "visualbell")
 * @key: Configuration key within the module
 * @value: Boolean value to set
 *
 * Sets a boolean value in a module's configuration section.
 * Creates the module mapping if it does not exist.
 */
void
gst_config_set_module_config_bool(
	GstConfig   *self,
	const gchar *module_name,
	const gchar *key,
	gboolean     value
);

/**
 * gst_config_set_module_config_strv:
 * @self: A #GstConfig
 * @module_name: Module name (e.g. "font2")
 * @key: Configuration key within the module
 * @strv: (array zero-terminated=1): NULL-terminated array of strings
 *
 * Sets a string array value in a module's configuration section.
 * Creates the module mapping if it does not exist.
 */
void
gst_config_set_module_config_strv(
	GstConfig          *self,
	const gchar        *module_name,
	const gchar        *key,
	const gchar *const *strv
);

/**
 * gst_config_set_module_config_sub_bool:
 * @self: A #GstConfig
 * @module_name: Module name (e.g. "mcp")
 * @sub_name: Sub-mapping name (e.g. "tools")
 * @key: Configuration key within the sub-mapping
 * @value: Boolean value to set
 *
 * Sets a boolean value in a sub-mapping within a module's configuration.
 * Creates the module mapping and sub-mapping if they do not exist.
 */
void
gst_config_set_module_config_sub_bool(
	GstConfig   *self,
	const gchar *module_name,
	const gchar *sub_name,
	const gchar *key,
	gboolean     value
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

/* ===== Setters (for C config API) ===== */

/**
 * gst_config_set_shell:
 * @self: A #GstConfig
 * @shell: Shell command path
 *
 * Sets the shell command to spawn.
 */
void
gst_config_set_shell(
	GstConfig   *self,
	const gchar *shell
);

/**
 * gst_config_set_term_name:
 * @self: A #GstConfig
 * @term_name: TERM environment variable value
 *
 * Sets the TERM environment variable value.
 */
void
gst_config_set_term_name(
	GstConfig   *self,
	const gchar *term_name
);

/**
 * gst_config_set_tabspaces:
 * @self: A #GstConfig
 * @tabspaces: Number of spaces per tab stop (1-64)
 *
 * Sets the tab stop width.
 */
void
gst_config_set_tabspaces(
	GstConfig *self,
	guint      tabspaces
);

/**
 * gst_config_set_title:
 * @self: A #GstConfig
 * @title: Default window title
 *
 * Sets the default window title.
 */
void
gst_config_set_title(
	GstConfig   *self,
	const gchar *title
);

/**
 * gst_config_set_cols:
 * @self: A #GstConfig
 * @cols: Default column count
 *
 * Sets the default number of terminal columns.
 */
void
gst_config_set_cols(
	GstConfig *self,
	guint      cols
);

/**
 * gst_config_set_rows:
 * @self: A #GstConfig
 * @rows: Default row count
 *
 * Sets the default number of terminal rows.
 */
void
gst_config_set_rows(
	GstConfig *self,
	guint      rows
);

/**
 * gst_config_set_border_px:
 * @self: A #GstConfig
 * @border_px: Border padding in pixels (0-100)
 *
 * Sets the border padding.
 */
void
gst_config_set_border_px(
	GstConfig *self,
	guint      border_px
);

/**
 * gst_config_set_font_primary:
 * @self: A #GstConfig
 * @font: Font specification string (fontconfig format)
 *
 * Sets the primary font specification.
 */
void
gst_config_set_font_primary(
	GstConfig   *self,
	const gchar *font
);

/**
 * gst_config_set_font_fallbacks:
 * @self: A #GstConfig
 * @fallbacks: (array zero-terminated=1) (nullable): NULL-terminated
 *             array of fallback font strings, or %NULL to clear
 *
 * Sets the fallback font list.
 */
void
gst_config_set_font_fallbacks(
	GstConfig          *self,
	const gchar *const *fallbacks
);

/**
 * gst_config_set_fg_index:
 * @self: A #GstConfig
 * @index: Palette index (0-255)
 *
 * Sets the foreground color palette index.
 */
void
gst_config_set_fg_index(
	GstConfig *self,
	guint      index
);

/**
 * gst_config_set_bg_index:
 * @self: A #GstConfig
 * @index: Palette index (0-255)
 *
 * Sets the background color palette index.
 */
void
gst_config_set_bg_index(
	GstConfig *self,
	guint      index
);

/**
 * gst_config_set_cursor_fg_index:
 * @self: A #GstConfig
 * @index: Palette index (0-255)
 *
 * Sets the cursor foreground color palette index.
 */
void
gst_config_set_cursor_fg_index(
	GstConfig *self,
	guint      index
);

/**
 * gst_config_set_cursor_bg_index:
 * @self: A #GstConfig
 * @index: Palette index (0-255)
 *
 * Sets the cursor background color palette index.
 */
void
gst_config_set_cursor_bg_index(
	GstConfig *self,
	guint      index
);

/**
 * gst_config_set_fg_hex:
 * @self: A #GstConfig
 * @hex: (nullable): "#RRGGBB" string, or %NULL to use palette index
 *
 * Sets a direct hex foreground color override.
 */
void
gst_config_set_fg_hex(
	GstConfig   *self,
	const gchar *hex
);

/**
 * gst_config_set_bg_hex:
 * @self: A #GstConfig
 * @hex: (nullable): "#RRGGBB" string, or %NULL to use palette index
 *
 * Sets a direct hex background color override.
 */
void
gst_config_set_bg_hex(
	GstConfig   *self,
	const gchar *hex
);

/**
 * gst_config_set_cursor_fg_hex:
 * @self: A #GstConfig
 * @hex: (nullable): "#RRGGBB" string, or %NULL to use palette index
 *
 * Sets a direct hex cursor foreground color override.
 */
void
gst_config_set_cursor_fg_hex(
	GstConfig   *self,
	const gchar *hex
);

/**
 * gst_config_set_cursor_bg_hex:
 * @self: A #GstConfig
 * @hex: (nullable): "#RRGGBB" string, or %NULL to use palette index
 *
 * Sets a direct hex cursor background color override.
 */
void
gst_config_set_cursor_bg_hex(
	GstConfig   *self,
	const gchar *hex
);

/**
 * gst_config_set_palette_hex:
 * @self: A #GstConfig
 * @palette: (array zero-terminated=1): NULL-terminated array of
 *           "#RRGGBB" strings
 * @n_colors: Number of palette entries (max 16)
 *
 * Sets the 16-color palette from hex strings.
 */
void
gst_config_set_palette_hex(
	GstConfig          *self,
	const gchar *const *palette,
	guint               n_colors
);

/**
 * gst_config_set_cursor_shape:
 * @self: A #GstConfig
 * @shape: The #GstCursorShape
 *
 * Sets the cursor shape.
 */
void
gst_config_set_cursor_shape(
	GstConfig      *self,
	GstCursorShape  shape
);

/**
 * gst_config_set_cursor_blink:
 * @self: A #GstConfig
 * @blink: %TRUE to enable cursor blinking
 *
 * Sets whether the cursor should blink.
 */
void
gst_config_set_cursor_blink(
	GstConfig *self,
	gboolean   blink
);

/**
 * gst_config_set_blink_rate:
 * @self: A #GstConfig
 * @rate_ms: Blink rate in milliseconds (50-5000)
 *
 * Sets the cursor blink rate.
 */
void
gst_config_set_blink_rate(
	GstConfig *self,
	guint      rate_ms
);

/**
 * gst_config_set_word_delimiters:
 * @self: A #GstConfig
 * @delimiters: Word delimiter characters
 *
 * Sets the word delimiter string for double-click selection.
 */
void
gst_config_set_word_delimiters(
	GstConfig   *self,
	const gchar *delimiters
);

/**
 * gst_config_set_min_latency:
 * @self: A #GstConfig
 * @ms: Minimum draw latency in milliseconds (1-1000)
 *
 * Sets the minimum draw latency.
 */
void
gst_config_set_min_latency(
	GstConfig *self,
	guint      ms
);

/**
 * gst_config_set_max_latency:
 * @self: A #GstConfig
 * @ms: Maximum draw latency in milliseconds (1-1000)
 *
 * Sets the maximum draw latency.
 */
void
gst_config_set_max_latency(
	GstConfig *self,
	guint      ms
);

/* ===== Keybind / mousebind management ===== */

/**
 * gst_config_add_keybind:
 * @self: A #GstConfig
 * @key_str: Key binding string (e.g. "Ctrl+Shift+c")
 * @action_str: Action name string (e.g. "clipboard_copy")
 *
 * Appends a keybind to the existing bindings array.
 * Unlike YAML loading (which replaces all defaults), this
 * adds to the current set.
 *
 * Returns: %TRUE if the binding was parsed and added
 */
gboolean
gst_config_add_keybind(
	GstConfig   *self,
	const gchar *key_str,
	const gchar *action_str
);

/**
 * gst_config_add_mousebind:
 * @self: A #GstConfig
 * @key_str: Mouse binding string (e.g. "Shift+Button4")
 * @action_str: Action name string (e.g. "scroll_up_fast")
 *
 * Appends a mousebind to the existing bindings array.
 *
 * Returns: %TRUE if the binding was parsed and added
 */
gboolean
gst_config_add_mousebind(
	GstConfig   *self,
	const gchar *key_str,
	const gchar *action_str
);

/**
 * gst_config_clear_keybinds:
 * @self: A #GstConfig
 *
 * Removes all current keybinds. Use before adding a complete
 * custom set in a C config.
 */
void
gst_config_clear_keybinds(GstConfig *self);

/**
 * gst_config_clear_mousebinds:
 * @self: A #GstConfig
 *
 * Removes all current mousebinds.
 */
void
gst_config_clear_mousebinds(GstConfig *self);

G_END_DECLS

#endif /* GST_CONFIG_H */
