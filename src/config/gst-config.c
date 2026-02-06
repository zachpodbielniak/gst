/*
 * gst-config.c - YAML configuration handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Loads terminal configuration from YAML files using yaml-glib.
 * Each top-level YAML section (terminal, window, font, colors,
 * cursor, selection, draw, modules) is parsed by a dedicated
 * static helper. Missing sections or keys silently use defaults.
 */

#include "gst-config.h"

#include <string.h>
#include <stdlib.h>

/**
 * SECTION:gst-config
 * @title: GstConfig
 * @short_description: Terminal configuration management
 *
 * #GstConfig handles loading and saving of terminal configuration
 * from YAML files. It provides access to all configurable options
 * including terminal, window, font, color, cursor, selection,
 * draw latency, and per-module settings.
 */

/* ===== Error domain ===== */

G_DEFINE_QUARK(gst-config-error-quark, gst_config_error)

/* ===== Private struct ===== */

struct _GstConfig
{
	GObject parent_instance;

	/* Terminal */
	gchar *shell;
	gchar *term_name;
	guint tabspaces;

	/* Window */
	gchar *title;
	guint default_cols;
	guint default_rows;
	guint border_px;

	/* Font */
	gchar *font_primary;
	gchar **font_fallbacks;  /* NULL-terminated strv */

	/* Colors */
	guint fg_index;
	guint bg_index;
	guint cursor_fg_index;
	guint cursor_bg_index;
	gchar **palette_hex;     /* NULL-terminated strv of "#RRGGBB" */
	guint n_palette;

	/* Cursor */
	GstCursorShape cursor_shape;
	gboolean cursor_blink;
	guint blink_rate;

	/* Selection */
	gchar *word_delimiters;

	/* Draw latency */
	guint min_latency;
	guint max_latency;

	/* Module configs — raw YAML mapping keyed by module name */
	YamlMapping *module_configs;

	/* Key and mouse bindings */
	GArray *keybinds;     /* GArray of GstKeybind */
	GArray *mousebinds;   /* GArray of GstMousebind */
};

G_DEFINE_TYPE(GstConfig, gst_config, G_TYPE_OBJECT)

/* Singleton instance */
static GstConfig *default_config = NULL;

/* ===== Dispose / finalize ===== */

static void
gst_config_dispose(GObject *object)
{
	GstConfig *self;

	self = GST_CONFIG(object);

	g_clear_pointer(&self->shell, g_free);
	g_clear_pointer(&self->term_name, g_free);
	g_clear_pointer(&self->title, g_free);
	g_clear_pointer(&self->font_primary, g_free);
	g_clear_pointer(&self->font_fallbacks, g_strfreev);
	g_clear_pointer(&self->palette_hex, g_strfreev);
	g_clear_pointer(&self->word_delimiters, g_free);
	g_clear_pointer(&self->module_configs, yaml_mapping_unref);
	g_clear_pointer(&self->keybinds, g_array_unref);
	g_clear_pointer(&self->mousebinds, g_array_unref);

	G_OBJECT_CLASS(gst_config_parent_class)->dispose(object);
}

static void
gst_config_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_config_parent_class)->finalize(object);
}

/* ===== Class / instance init ===== */

static void
gst_config_class_init(GstConfigClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_config_dispose;
	object_class->finalize = gst_config_finalize;
}

static void
gst_config_init(GstConfig *self)
{
	const gchar *env_shell;

	/* Terminal defaults */
	env_shell = g_getenv("SHELL");
	self->shell = g_strdup((env_shell != NULL) ? env_shell : "/bin/bash");
	self->term_name = g_strdup("st-256color");
	self->tabspaces = 8;

	/* Window defaults */
	self->title = g_strdup("gst");
	self->default_cols = 80;
	self->default_rows = 24;
	self->border_px = 2;

	/* Font defaults */
	self->font_primary = g_strdup(
		"Liberation Mono:pixelsize=14:antialias=true:autohint=true");
	self->font_fallbacks = NULL;

	/* Color defaults — indices into the palette */
	self->fg_index = 7;
	self->bg_index = 0;
	self->cursor_fg_index = 0;
	self->cursor_bg_index = 7;
	self->palette_hex = NULL;
	self->n_palette = 0;

	/* Cursor defaults */
	self->cursor_shape = GST_CURSOR_SHAPE_BLOCK;
	self->cursor_blink = FALSE;
	self->blink_rate = 500;

	/* Selection defaults */
	self->word_delimiters = g_strdup(" `'\"()[]{}|");

	/* Draw latency defaults */
	self->min_latency = 8;
	self->max_latency = 33;

	/* No module configs yet */
	self->module_configs = NULL;

	/* Default key bindings (match data/default-config.yaml) */
	self->keybinds = g_array_new(FALSE, TRUE, sizeof(GstKeybind));
	{
		GstKeybind kb;

		gst_keybind_parse("Ctrl+Shift+c", "clipboard_copy", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Ctrl+Shift+v", "clipboard_paste", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Shift+Insert", "paste_primary", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Shift+Page_Up", "scroll_up", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Shift+Page_Down", "scroll_down", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Ctrl+Shift+Page_Up", "scroll_top", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Ctrl+Shift+Page_Down", "scroll_bottom", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Ctrl+Shift+Home", "scroll_top", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Ctrl+Shift+End", "scroll_bottom", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Ctrl+Shift+plus", "zoom_in", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Ctrl+Shift+minus", "zoom_out", &kb);
		g_array_append_val(self->keybinds, kb);

		gst_keybind_parse("Ctrl+Shift+0", "zoom_reset", &kb);
		g_array_append_val(self->keybinds, kb);
	}

	/* Default mouse bindings */
	self->mousebinds = g_array_new(FALSE, TRUE, sizeof(GstMousebind));
	{
		GstMousebind mb;

		gst_mousebind_parse("Button4", "scroll_up", &mb);
		g_array_append_val(self->mousebinds, mb);

		gst_mousebind_parse("Button5", "scroll_down", &mb);
		g_array_append_val(self->mousebinds, mb);

		gst_mousebind_parse("Shift+Button4", "scroll_up_fast", &mb);
		g_array_append_val(self->mousebinds, mb);

		gst_mousebind_parse("Shift+Button5", "scroll_down_fast", &mb);
		g_array_append_val(self->mousebinds, mb);
	}
}

/* ===== YAML section loaders ===== */

/*
 * load_terminal_section:
 *
 * Parse the "terminal:" mapping for shell, term, tabspaces.
 */
static gboolean
load_terminal_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	const gchar *str_val;
	gint64 int_val;

	section = yaml_mapping_get_mapping_member(root, "terminal");
	if (section == NULL) {
		return TRUE;
	}

	if (yaml_mapping_has_member(section, "shell")) {
		str_val = yaml_mapping_get_string_member(section, "shell");
		if (str_val != NULL) {
			g_free(self->shell);
			self->shell = g_strdup(str_val);
		}
	}

	if (yaml_mapping_has_member(section, "term")) {
		str_val = yaml_mapping_get_string_member(section, "term");
		if (str_val != NULL) {
			g_free(self->term_name);
			self->term_name = g_strdup(str_val);
		}
	}

	if (yaml_mapping_has_member(section, "tabspaces")) {
		int_val = yaml_mapping_get_int_member(section, "tabspaces");
		if (int_val < 1 || int_val > 64) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"tabspaces must be 1-64, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->tabspaces = (guint)int_val;
	}

	return TRUE;
}

/*
 * load_window_section:
 *
 * Parse the "window:" mapping for title, geometry (COLSxROWS), border.
 */
static gboolean
load_window_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	const gchar *str_val;
	gint64 int_val;

	section = yaml_mapping_get_mapping_member(root, "window");
	if (section == NULL) {
		return TRUE;
	}

	if (yaml_mapping_has_member(section, "title")) {
		str_val = yaml_mapping_get_string_member(section, "title");
		if (str_val != NULL) {
			g_free(self->title);
			self->title = g_strdup(str_val);
		}
	}

	/* geometry: "COLSxROWS" */
	if (yaml_mapping_has_member(section, "geometry")) {
		str_val = yaml_mapping_get_string_member(section, "geometry");
		if (str_val != NULL) {
			gint c, r;
			gchar x;

			if (sscanf(str_val, "%d%c%d", &c, &x, &r) == 3 &&
				(x == 'x' || x == 'X') &&
				c >= 1 && c <= 32767 && r >= 1 && r <= 32767)
			{
				self->default_cols = (guint)c;
				self->default_rows = (guint)r;
			} else {
				g_set_error(error, GST_CONFIG_ERROR,
					GST_CONFIG_ERROR_INVALID_VALUE,
					"Invalid geometry: '%s' (expected COLSxROWS)",
					str_val);
				return FALSE;
			}
		}
	}

	if (yaml_mapping_has_member(section, "border")) {
		int_val = yaml_mapping_get_int_member(section, "border");
		if (int_val < 0 || int_val > 100) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"border must be 0-100, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->border_px = (guint)int_val;
	}

	return TRUE;
}

/*
 * load_font_section:
 *
 * Parse the "font:" mapping for primary string and fallback sequence.
 */
static gboolean
load_font_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	YamlSequence *fallbacks;
	const gchar *str_val;
	guint i;
	guint len;

	(void)error;

	section = yaml_mapping_get_mapping_member(root, "font");
	if (section == NULL) {
		return TRUE;
	}

	if (yaml_mapping_has_member(section, "primary")) {
		str_val = yaml_mapping_get_string_member(section, "primary");
		if (str_val != NULL) {
			g_free(self->font_primary);
			self->font_primary = g_strdup(str_val);
		}
	}

	if (yaml_mapping_has_member(section, "fallback")) {
		fallbacks = yaml_mapping_get_sequence_member(section, "fallback");
		if (fallbacks != NULL) {
			len = yaml_sequence_get_length(fallbacks);

			g_strfreev(self->font_fallbacks);
			self->font_fallbacks = g_new0(gchar *, len + 1);

			for (i = 0; i < len; i++) {
				str_val = yaml_sequence_get_string_element(fallbacks, i);
				self->font_fallbacks[i] = g_strdup(
					(str_val != NULL) ? str_val : "");
			}
			self->font_fallbacks[len] = NULL;
		}
	}

	return TRUE;
}

/*
 * load_colors_section:
 *
 * Parse the "colors:" mapping for foreground, background, cursor
 * indices and the 16-color palette hex sequence.
 */
static gboolean
load_colors_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	YamlSequence *palette;
	gint64 int_val;
	guint i;
	guint len;

	section = yaml_mapping_get_mapping_member(root, "colors");
	if (section == NULL) {
		return TRUE;
	}

	if (yaml_mapping_has_member(section, "foreground")) {
		int_val = yaml_mapping_get_int_member(section, "foreground");
		if (int_val < 0 || int_val > 255) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"foreground index must be 0-255, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->fg_index = (guint)int_val;
	}

	if (yaml_mapping_has_member(section, "background")) {
		int_val = yaml_mapping_get_int_member(section, "background");
		if (int_val < 0 || int_val > 255) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"background index must be 0-255, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->bg_index = (guint)int_val;
	}

	if (yaml_mapping_has_member(section, "cursor_fg")) {
		int_val = yaml_mapping_get_int_member(section, "cursor_fg");
		if (int_val < 0 || int_val > 255) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"cursor_fg index must be 0-255, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->cursor_fg_index = (guint)int_val;
	}

	if (yaml_mapping_has_member(section, "cursor_bg")) {
		int_val = yaml_mapping_get_int_member(section, "cursor_bg");
		if (int_val < 0 || int_val > 255) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"cursor_bg index must be 0-255, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->cursor_bg_index = (guint)int_val;
	}

	/* palette: sequence of "#RRGGBB" strings */
	if (yaml_mapping_has_member(section, "palette")) {
		palette = yaml_mapping_get_sequence_member(section, "palette");
		if (palette != NULL) {
			len = yaml_sequence_get_length(palette);
			if (len > 16) {
				len = 16;
			}

			g_strfreev(self->palette_hex);
			self->palette_hex = g_new0(gchar *, len + 1);
			self->n_palette = len;

			for (i = 0; i < len; i++) {
				const gchar *hex;

				hex = yaml_sequence_get_string_element(palette, i);
				self->palette_hex[i] = g_strdup(
					(hex != NULL) ? hex : "#000000");
			}
			self->palette_hex[len] = NULL;
		}
	}

	return TRUE;
}

/*
 * load_cursor_section:
 *
 * Parse the "cursor:" mapping for shape, blink, blink_rate.
 */
static gboolean
load_cursor_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	const gchar *str_val;
	gint64 int_val;

	section = yaml_mapping_get_mapping_member(root, "cursor");
	if (section == NULL) {
		return TRUE;
	}

	/* shape: "block", "underline", or "bar" */
	if (yaml_mapping_has_member(section, "shape")) {
		str_val = yaml_mapping_get_string_member(section, "shape");
		if (str_val != NULL) {
			if (g_ascii_strcasecmp(str_val, "block") == 0) {
				self->cursor_shape = GST_CURSOR_SHAPE_BLOCK;
			} else if (g_ascii_strcasecmp(str_val, "underline") == 0) {
				self->cursor_shape = GST_CURSOR_SHAPE_UNDERLINE;
			} else if (g_ascii_strcasecmp(str_val, "bar") == 0) {
				self->cursor_shape = GST_CURSOR_SHAPE_BAR;
			} else {
				g_set_error(error, GST_CONFIG_ERROR,
					GST_CONFIG_ERROR_INVALID_VALUE,
					"Invalid cursor shape: '%s' "
					"(expected block, underline, or bar)",
					str_val);
				return FALSE;
			}
		}
	}

	if (yaml_mapping_has_member(section, "blink")) {
		self->cursor_blink = yaml_mapping_get_boolean_member(
			section, "blink");
	}

	if (yaml_mapping_has_member(section, "blink_rate")) {
		int_val = yaml_mapping_get_int_member(section, "blink_rate");
		if (int_val < 50 || int_val > 5000) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"blink_rate must be 50-5000 ms, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->blink_rate = (guint)int_val;
	}

	return TRUE;
}

/*
 * load_selection_section:
 *
 * Parse the "selection:" mapping for word_delimiters.
 */
static gboolean
load_selection_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	const gchar *str_val;

	(void)error;

	section = yaml_mapping_get_mapping_member(root, "selection");
	if (section == NULL) {
		return TRUE;
	}

	if (yaml_mapping_has_member(section, "word_delimiters")) {
		str_val = yaml_mapping_get_string_member(
			section, "word_delimiters");
		if (str_val != NULL) {
			g_free(self->word_delimiters);
			self->word_delimiters = g_strdup(str_val);
		}
	}

	return TRUE;
}

/*
 * load_draw_section:
 *
 * Parse the "draw:" mapping for min_latency and max_latency.
 */
static gboolean
load_draw_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	gint64 int_val;

	section = yaml_mapping_get_mapping_member(root, "draw");
	if (section == NULL) {
		return TRUE;
	}

	if (yaml_mapping_has_member(section, "min_latency")) {
		int_val = yaml_mapping_get_int_member(section, "min_latency");
		if (int_val < 1 || int_val > 1000) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"min_latency must be 1-1000 ms, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->min_latency = (guint)int_val;
	}

	if (yaml_mapping_has_member(section, "max_latency")) {
		int_val = yaml_mapping_get_int_member(section, "max_latency");
		if (int_val < 1 || int_val > 1000) {
			g_set_error(error, GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_INVALID_VALUE,
				"max_latency must be 1-1000 ms, got %" G_GINT64_FORMAT,
				int_val);
			return FALSE;
		}
		self->max_latency = (guint)int_val;
	}

	return TRUE;
}

/*
 * load_modules_section:
 *
 * Store the "modules:" mapping as-is for module system to query.
 */
static gboolean
load_modules_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;

	(void)error;

	section = yaml_mapping_get_mapping_member(root, "modules");
	if (section == NULL) {
		return TRUE;
	}

	g_clear_pointer(&self->module_configs, yaml_mapping_unref);
	self->module_configs = yaml_mapping_ref(section);

	return TRUE;
}

/*
 * load_keybinds_section:
 *
 * Parse the "keybinds:" mapping. Each key is a binding string
 * (e.g. "Ctrl+Shift+c"), each value is an action string
 * (e.g. "clipboard_copy"). If the section is present, it fully
 * replaces the built-in default keybinds.
 */
static gboolean
load_keybinds_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	guint i;
	guint size;

	(void)error;

	section = yaml_mapping_get_mapping_member(root, "keybinds");
	if (section == NULL) {
		return TRUE;
	}

	/* Replace defaults: clear existing bindings */
	g_array_set_size(self->keybinds, 0);

	size = yaml_mapping_get_size(section);
	for (i = 0; i < size; i++) {
		const gchar *key_str;
		YamlNode *val_node;
		const gchar *action_str;
		GstKeybind kb;

		key_str = yaml_mapping_get_key(section, i);
		val_node = yaml_mapping_get_value(section, i);
		action_str = yaml_node_get_string(val_node);

		if (key_str == NULL || action_str == NULL) {
			continue;
		}

		if (!gst_keybind_parse(key_str, action_str, &kb)) {
			g_warning("Ignoring invalid keybind: '%s' -> '%s'",
				key_str, action_str);
			continue;
		}

		g_array_append_val(self->keybinds, kb);
	}

	return TRUE;
}

/*
 * load_mousebinds_section:
 *
 * Parse the "mousebinds:" mapping. Same pattern as keybinds:
 * if present, fully replaces defaults.
 */
static gboolean
load_mousebinds_section(
	GstConfig   *self,
	YamlMapping *root,
	GError     **error
){
	YamlMapping *section;
	guint i;
	guint size;

	(void)error;

	section = yaml_mapping_get_mapping_member(root, "mousebinds");
	if (section == NULL) {
		return TRUE;
	}

	/* Replace defaults: clear existing bindings */
	g_array_set_size(self->mousebinds, 0);

	size = yaml_mapping_get_size(section);
	for (i = 0; i < size; i++) {
		const gchar *key_str;
		YamlNode *val_node;
		const gchar *action_str;
		GstMousebind mb;

		key_str = yaml_mapping_get_key(section, i);
		val_node = yaml_mapping_get_value(section, i);
		action_str = yaml_node_get_string(val_node);

		if (key_str == NULL || action_str == NULL) {
			continue;
		}

		if (!gst_mousebind_parse(key_str, action_str, &mb)) {
			g_warning("Ignoring invalid mousebind: '%s' -> '%s'",
				key_str, action_str);
			continue;
		}

		g_array_append_val(self->mousebinds, mb);
	}

	return TRUE;
}

/* ===== YAML save helpers ===== */

/*
 * build_terminal_section:
 *
 * Add the "terminal:" section to a YAML builder.
 */
static void
build_terminal_section(
	GstConfig   *self,
	YamlBuilder *builder
){
	yaml_builder_set_member_name(builder, "terminal");
	yaml_builder_begin_mapping(builder);

	yaml_builder_set_member_name(builder, "shell");
	yaml_builder_add_string_value(builder, self->shell);

	yaml_builder_set_member_name(builder, "term");
	yaml_builder_add_string_value(builder, self->term_name);

	yaml_builder_set_member_name(builder, "tabspaces");
	yaml_builder_add_int_value(builder, (gint64)self->tabspaces);

	yaml_builder_end_mapping(builder);
}

/*
 * build_window_section:
 *
 * Add the "window:" section to a YAML builder.
 */
static void
build_window_section(
	GstConfig   *self,
	YamlBuilder *builder
){
	g_autofree gchar *geometry = NULL;

	geometry = g_strdup_printf("%ux%u",
		self->default_cols, self->default_rows);

	yaml_builder_set_member_name(builder, "window");
	yaml_builder_begin_mapping(builder);

	yaml_builder_set_member_name(builder, "title");
	yaml_builder_add_string_value(builder, self->title);

	yaml_builder_set_member_name(builder, "geometry");
	yaml_builder_add_string_value(builder, geometry);

	yaml_builder_set_member_name(builder, "border");
	yaml_builder_add_int_value(builder, (gint64)self->border_px);

	yaml_builder_end_mapping(builder);
}

/*
 * build_font_section:
 *
 * Add the "font:" section to a YAML builder.
 */
static void
build_font_section(
	GstConfig   *self,
	YamlBuilder *builder
){
	yaml_builder_set_member_name(builder, "font");
	yaml_builder_begin_mapping(builder);

	yaml_builder_set_member_name(builder, "primary");
	yaml_builder_add_string_value(builder, self->font_primary);

	if (self->font_fallbacks != NULL) {
		guint i;

		yaml_builder_set_member_name(builder, "fallback");
		yaml_builder_begin_sequence(builder);

		for (i = 0; self->font_fallbacks[i] != NULL; i++) {
			yaml_builder_add_string_value(builder,
				self->font_fallbacks[i]);
		}

		yaml_builder_end_sequence(builder);
	}

	yaml_builder_end_mapping(builder);
}

/*
 * build_colors_section:
 *
 * Add the "colors:" section to a YAML builder.
 */
static void
build_colors_section(
	GstConfig   *self,
	YamlBuilder *builder
){
	yaml_builder_set_member_name(builder, "colors");
	yaml_builder_begin_mapping(builder);

	yaml_builder_set_member_name(builder, "foreground");
	yaml_builder_add_int_value(builder, (gint64)self->fg_index);

	yaml_builder_set_member_name(builder, "background");
	yaml_builder_add_int_value(builder, (gint64)self->bg_index);

	yaml_builder_set_member_name(builder, "cursor_fg");
	yaml_builder_add_int_value(builder, (gint64)self->cursor_fg_index);

	yaml_builder_set_member_name(builder, "cursor_bg");
	yaml_builder_add_int_value(builder, (gint64)self->cursor_bg_index);

	if (self->palette_hex != NULL) {
		guint i;

		yaml_builder_set_member_name(builder, "palette");
		yaml_builder_begin_sequence(builder);

		for (i = 0; self->palette_hex[i] != NULL; i++) {
			yaml_builder_add_string_value(builder,
				self->palette_hex[i]);
		}

		yaml_builder_end_sequence(builder);
	}

	yaml_builder_end_mapping(builder);
}

/*
 * build_cursor_section:
 *
 * Add the "cursor:" section to a YAML builder.
 */
static void
build_cursor_section(
	GstConfig   *self,
	YamlBuilder *builder
){
	const gchar *shape_str;

	switch (self->cursor_shape) {
	case GST_CURSOR_SHAPE_UNDERLINE:
		shape_str = "underline";
		break;
	case GST_CURSOR_SHAPE_BAR:
		shape_str = "bar";
		break;
	default:
		shape_str = "block";
		break;
	}

	yaml_builder_set_member_name(builder, "cursor");
	yaml_builder_begin_mapping(builder);

	yaml_builder_set_member_name(builder, "shape");
	yaml_builder_add_string_value(builder, shape_str);

	yaml_builder_set_member_name(builder, "blink");
	yaml_builder_add_boolean_value(builder, self->cursor_blink);

	yaml_builder_set_member_name(builder, "blink_rate");
	yaml_builder_add_int_value(builder, (gint64)self->blink_rate);

	yaml_builder_end_mapping(builder);
}

/*
 * build_selection_section:
 *
 * Add the "selection:" section to a YAML builder.
 */
static void
build_selection_section(
	GstConfig   *self,
	YamlBuilder *builder
){
	yaml_builder_set_member_name(builder, "selection");
	yaml_builder_begin_mapping(builder);

	yaml_builder_set_member_name(builder, "word_delimiters");
	yaml_builder_add_string_value(builder, self->word_delimiters);

	yaml_builder_end_mapping(builder);
}

/* ===== Public API ===== */

/**
 * gst_config_new:
 *
 * Creates a new configuration instance with default values.
 *
 * Returns: (transfer full): A new #GstConfig
 */
GstConfig *
gst_config_new(void)
{
	return (GstConfig *)g_object_new(GST_TYPE_CONFIG, NULL);
}

/**
 * gst_config_load_from_file:
 * @self: A #GstConfig
 * @file: The configuration file to load
 * @error: (nullable): Return location for a #GError
 *
 * Loads configuration from a YAML file. Parses each top-level
 * section and updates the corresponding fields. Missing sections
 * or keys are left at their defaults.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
gst_config_load_from_file(
	GstConfig  *self,
	GFile      *file,
	GError    **error
){
	g_autoptr(YamlParser) parser = NULL;
	YamlNode *root;
	YamlMapping *root_map;

	g_return_val_if_fail(GST_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(G_IS_FILE(file), FALSE);

	/* Parse the YAML file */
	parser = yaml_parser_new();

	if (!yaml_parser_load_from_gfile(parser, file, NULL, error)) {
		/* Wrap parse errors in our domain */
		if (error != NULL && *error != NULL) {
			GError *wrapped;

			wrapped = g_error_new(GST_CONFIG_ERROR,
				GST_CONFIG_ERROR_PARSE,
				"Failed to parse config: %s",
				(*error)->message);
			g_error_free(*error);
			*error = wrapped;
		}
		return FALSE;
	}

	/* Get the root mapping */
	root = yaml_parser_get_root(parser);
	if (root == NULL) {
		g_set_error(error, GST_CONFIG_ERROR,
			GST_CONFIG_ERROR_PARSE,
			"Config file is empty");
		return FALSE;
	}

	root_map = yaml_node_get_mapping(root);
	if (root_map == NULL) {
		g_set_error(error, GST_CONFIG_ERROR,
			GST_CONFIG_ERROR_PARSE,
			"Config root is not a mapping");
		return FALSE;
	}

	/* Load each section — short-circuit on first error */
	if (!load_terminal_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_window_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_font_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_colors_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_cursor_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_selection_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_draw_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_modules_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_keybinds_section(self, root_map, error)) {
		return FALSE;
	}
	if (!load_mousebinds_section(self, root_map, error)) {
		return FALSE;
	}

	return TRUE;
}

/**
 * gst_config_load_from_path:
 * @self: A #GstConfig
 * @path: Filesystem path to the YAML configuration file
 * @error: (nullable): Return location for a #GError
 *
 * Convenience wrapper that creates a #GFile from @path and
 * delegates to gst_config_load_from_file().
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
gst_config_load_from_path(
	GstConfig   *self,
	const gchar *path,
	GError     **error
){
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail(GST_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(path != NULL, FALSE);

	file = g_file_new_for_path(path);
	return gst_config_load_from_file(self, file, error);
}

/**
 * gst_config_save_to_file:
 * @self: A #GstConfig
 * @file: The file to save to
 * @error: (nullable): Return location for a #GError
 *
 * Saves the current configuration to a YAML file.
 * Builds the full document tree and writes it out.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
gst_config_save_to_file(
	GstConfig  *self,
	GFile      *file,
	GError    **error
){
	g_autoptr(YamlBuilder) builder = NULL;
	g_autoptr(YamlGenerator) generator = NULL;
	YamlNode *root;

	g_return_val_if_fail(GST_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(G_IS_FILE(file), FALSE);

	/* Build the YAML document */
	builder = yaml_builder_new();

	yaml_builder_begin_mapping(builder);

	build_terminal_section(self, builder);
	build_window_section(self, builder);
	build_font_section(self, builder);
	build_colors_section(self, builder);
	build_cursor_section(self, builder);
	build_selection_section(self, builder);

	yaml_builder_end_mapping(builder);

	/* Generate output */
	root = yaml_builder_get_root(builder);
	if (root == NULL) {
		g_set_error(error, GST_CONFIG_ERROR,
			GST_CONFIG_ERROR_IO,
			"Failed to build YAML document");
		return FALSE;
	}

	generator = yaml_generator_new();
	yaml_generator_set_root(generator, root);
	yaml_generator_set_indent(generator, 2);

	return yaml_generator_to_gfile(generator, file, NULL, error);
}

/**
 * gst_config_get_default:
 *
 * Gets the default shared configuration instance.
 * The singleton is created on first call with built-in defaults.
 *
 * Returns: (transfer none): The default #GstConfig
 */
GstConfig *
gst_config_get_default(void)
{
	if (default_config == NULL) {
		default_config = gst_config_new();
	}

	return default_config;
}

/* ===== Getters ===== */

/**
 * gst_config_get_shell:
 * @self: A #GstConfig
 *
 * Gets the shell command to spawn.
 *
 * Returns: (transfer none): The shell path
 */
const gchar *
gst_config_get_shell(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), "/bin/bash");

	return self->shell;
}

/**
 * gst_config_get_term_name:
 * @self: A #GstConfig
 *
 * Gets the TERM environment variable value.
 *
 * Returns: (transfer none): The TERM name
 */
const gchar *
gst_config_get_term_name(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), "st-256color");

	return self->term_name;
}

/**
 * gst_config_get_tabspaces:
 * @self: A #GstConfig
 *
 * Gets the number of spaces per tab stop.
 *
 * Returns: Tab stop width
 */
guint
gst_config_get_tabspaces(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 8);

	return self->tabspaces;
}

/**
 * gst_config_get_title:
 * @self: A #GstConfig
 *
 * Gets the default window title.
 *
 * Returns: (transfer none): The window title
 */
const gchar *
gst_config_get_title(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), "gst");

	return self->title;
}

/**
 * gst_config_get_cols:
 * @self: A #GstConfig
 *
 * Gets the default number of terminal columns.
 *
 * Returns: Column count
 */
guint
gst_config_get_cols(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 80);

	return self->default_cols;
}

/**
 * gst_config_get_rows:
 * @self: A #GstConfig
 *
 * Gets the default number of terminal rows.
 *
 * Returns: Row count
 */
guint
gst_config_get_rows(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 24);

	return self->default_rows;
}

/**
 * gst_config_get_border_px:
 * @self: A #GstConfig
 *
 * Gets the border padding in pixels.
 *
 * Returns: Border padding in pixels
 */
guint
gst_config_get_border_px(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 2);

	return self->border_px;
}

/**
 * gst_config_get_font_primary:
 * @self: A #GstConfig
 *
 * Gets the primary font specification string.
 *
 * Returns: (transfer none): The primary font string
 */
const gchar *
gst_config_get_font_primary(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), "monospace");

	return self->font_primary;
}

/**
 * gst_config_get_font_fallbacks:
 * @self: A #GstConfig
 *
 * Gets the list of fallback font specification strings.
 *
 * Returns: (transfer none) (array zero-terminated=1) (nullable):
 *          NULL-terminated array of font strings, or %NULL
 */
const gchar *const *
gst_config_get_font_fallbacks(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), NULL);

	return (const gchar *const *)self->font_fallbacks;
}

/**
 * gst_config_get_fg_index:
 * @self: A #GstConfig
 *
 * Gets the palette index for the default foreground color.
 *
 * Returns: Foreground color palette index
 */
guint
gst_config_get_fg_index(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 7);

	return self->fg_index;
}

/**
 * gst_config_get_bg_index:
 * @self: A #GstConfig
 *
 * Gets the palette index for the default background color.
 *
 * Returns: Background color palette index
 */
guint
gst_config_get_bg_index(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 0);

	return self->bg_index;
}

/**
 * gst_config_get_cursor_fg_index:
 * @self: A #GstConfig
 *
 * Gets the palette index for the cursor foreground.
 *
 * Returns: Cursor foreground palette index
 */
guint
gst_config_get_cursor_fg_index(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 0);

	return self->cursor_fg_index;
}

/**
 * gst_config_get_cursor_bg_index:
 * @self: A #GstConfig
 *
 * Gets the palette index for the cursor background.
 *
 * Returns: Cursor background palette index
 */
guint
gst_config_get_cursor_bg_index(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 7);

	return self->cursor_bg_index;
}

/**
 * gst_config_get_palette_hex:
 * @self: A #GstConfig
 *
 * Gets the hex color strings for the 16-color palette.
 *
 * Returns: (transfer none) (array zero-terminated=1) (nullable):
 *          NULL-terminated array of hex strings, or %NULL
 */
const gchar *const *
gst_config_get_palette_hex(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), NULL);

	return (const gchar *const *)self->palette_hex;
}

/**
 * gst_config_get_n_palette:
 * @self: A #GstConfig
 *
 * Gets the number of palette entries loaded from config.
 *
 * Returns: Number of palette hex entries
 */
guint
gst_config_get_n_palette(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 0);

	return self->n_palette;
}

/**
 * gst_config_get_cursor_shape:
 * @self: A #GstConfig
 *
 * Gets the cursor shape.
 *
 * Returns: The #GstCursorShape
 */
GstCursorShape
gst_config_get_cursor_shape(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), GST_CURSOR_SHAPE_BLOCK);

	return self->cursor_shape;
}

/**
 * gst_config_get_cursor_blink:
 * @self: A #GstConfig
 *
 * Gets whether the cursor should blink.
 *
 * Returns: %TRUE if blinking is enabled
 */
gboolean
gst_config_get_cursor_blink(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), FALSE);

	return self->cursor_blink;
}

/**
 * gst_config_get_blink_rate:
 * @self: A #GstConfig
 *
 * Gets the cursor blink rate in milliseconds.
 *
 * Returns: Blink rate in ms
 */
guint
gst_config_get_blink_rate(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 500);

	return self->blink_rate;
}

/**
 * gst_config_get_word_delimiters:
 * @self: A #GstConfig
 *
 * Gets the word delimiter characters for selection.
 *
 * Returns: (transfer none): The word delimiter string
 */
const gchar *
gst_config_get_word_delimiters(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), " ");

	return self->word_delimiters;
}

/**
 * gst_config_get_min_latency:
 * @self: A #GstConfig
 *
 * Gets the minimum draw latency in milliseconds.
 *
 * Returns: Minimum latency in ms
 */
guint
gst_config_get_min_latency(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 8);

	return self->min_latency;
}

/**
 * gst_config_get_max_latency:
 * @self: A #GstConfig
 *
 * Gets the maximum draw latency in milliseconds.
 *
 * Returns: Maximum latency in ms
 */
guint
gst_config_get_max_latency(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), 33);

	return self->max_latency;
}

/**
 * gst_config_get_module_config:
 * @self: A #GstConfig
 * @module_name: The name of the module
 *
 * Gets the raw YAML mapping for a module's config section.
 *
 * Returns: (transfer none) (nullable): The module's #YamlMapping,
 *          or %NULL if no config exists
 */
YamlMapping *
gst_config_get_module_config(
	GstConfig   *self,
	const gchar *module_name
){
	g_return_val_if_fail(GST_IS_CONFIG(self), NULL);
	g_return_val_if_fail(module_name != NULL, NULL);

	if (self->module_configs == NULL) {
		return NULL;
	}

	if (!yaml_mapping_has_member(self->module_configs, module_name)) {
		return NULL;
	}

	return yaml_mapping_get_mapping_member(
		self->module_configs, module_name);
}

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
gst_config_get_keybinds(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), NULL);

	return self->keybinds;
}

/**
 * gst_config_get_mousebinds:
 * @self: A #GstConfig
 *
 * Gets the configured mouse bindings array.
 *
 * Returns: (transfer none) (element-type GstMousebind): The mouse bindings
 */
const GArray *
gst_config_get_mousebinds(GstConfig *self)
{
	g_return_val_if_fail(GST_IS_CONFIG(self), NULL);

	return self->mousebinds;
}

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
){
	g_return_val_if_fail(GST_IS_CONFIG(self), GST_ACTION_NONE);

	return gst_keybind_lookup(self->keybinds, keyval, state);
}

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
){
	g_return_val_if_fail(GST_IS_CONFIG(self), GST_ACTION_NONE);

	return gst_mousebind_lookup(self->mousebinds, button, state);
}
