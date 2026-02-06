/*
 * gst-color-scheme.c - Terminal color scheme handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-color-scheme.h"
#include "gst-config.h"

#include <string.h>
#include <stdlib.h>

/**
 * SECTION:gst-color-scheme
 * @title: GstColorScheme
 * @short_description: Terminal color palette management
 *
 * #GstColorScheme manages the 256-color palette used for terminal
 * rendering, including the 16 standard colors and extended palette.
 * Colors are stored as ARGB (0xAARRGGBB).
 */

#define GST_COLOR_PALETTE_SIZE (256)

struct _GstColorScheme
{
	GObject parent_instance;

	gchar *name;
	guint32 foreground;
	guint32 background;
	guint32 cursor_color;
	guint32 palette[GST_COLOR_PALETTE_SIZE];
};

G_DEFINE_TYPE(GstColorScheme, gst_color_scheme, G_TYPE_OBJECT)

static void
gst_color_scheme_dispose(GObject *object)
{
	GstColorScheme *self;

	self = GST_COLOR_SCHEME(object);

	g_clear_pointer(&self->name, g_free);

	G_OBJECT_CLASS(gst_color_scheme_parent_class)->dispose(object);
}

static void
gst_color_scheme_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_color_scheme_parent_class)->finalize(object);
}

static void
gst_color_scheme_init_default_palette(GstColorScheme *self)
{
	guint i;

	/*
	 * Initialize the standard 16 colors (0-15)
	 * Using typical xterm-like defaults
	 */

	/* Normal colors (0-7) */
	self->palette[0] = 0xFF000000;  /* black */
	self->palette[1] = 0xFFCD0000;  /* red */
	self->palette[2] = 0xFF00CD00;  /* green */
	self->palette[3] = 0xFFCDCD00;  /* yellow */
	self->palette[4] = 0xFF0000EE;  /* blue */
	self->palette[5] = 0xFFCD00CD;  /* magenta */
	self->palette[6] = 0xFF00CDCD;  /* cyan */
	self->palette[7] = 0xFFE5E5E5;  /* white */

	/* Bright colors (8-15) */
	self->palette[8] = 0xFF7F7F7F;   /* bright black (gray) */
	self->palette[9] = 0xFFFF0000;   /* bright red */
	self->palette[10] = 0xFF00FF00;  /* bright green */
	self->palette[11] = 0xFFFFFF00;  /* bright yellow */
	self->palette[12] = 0xFF5C5CFF;  /* bright blue */
	self->palette[13] = 0xFFFF00FF;  /* bright magenta */
	self->palette[14] = 0xFF00FFFF;  /* bright cyan */
	self->palette[15] = 0xFFFFFFFF;  /* bright white */

	/*
	 * Initialize 216 color cube (16-231)
	 * 6x6x6 RGB cube
	 */
	for (i = 0; i < 216; i++)
	{
		guint r, g, b;
		guint idx;

		idx = i + 16;
		r = (i / 36) % 6;
		g = (i / 6) % 6;
		b = i % 6;

		/* Convert 0-5 to 0-255 range */
		r = (r > 0) ? (r * 40 + 55) : 0;
		g = (g > 0) ? (g * 40 + 55) : 0;
		b = (b > 0) ? (b * 40 + 55) : 0;

		self->palette[idx] = 0xFF000000 | (r << 16) | (g << 8) | b;
	}

	/* Initialize grayscale (232-255) */
	for (i = 0; i < 24; i++)
	{
		guint gray;
		guint idx;

		idx = i + 232;
		gray = i * 10 + 8;

		self->palette[idx] = 0xFF000000 | (gray << 16) | (gray << 8) | gray;
	}
}

static void
gst_color_scheme_class_init(GstColorSchemeClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_color_scheme_dispose;
	object_class->finalize = gst_color_scheme_finalize;
}

static void
gst_color_scheme_init(GstColorScheme *self)
{
	self->name = g_strdup("default");
	self->foreground = 0xFFFFFFFF;  /* white */
	self->background = 0xFF000000;  /* black */
	self->cursor_color = 0xFFFFFFFF;

	gst_color_scheme_init_default_palette(self);
}

/**
 * gst_color_scheme_new:
 * @name: The name of the color scheme
 *
 * Creates a new color scheme with default colors.
 *
 * Returns: (transfer full): A new #GstColorScheme
 */
GstColorScheme *
gst_color_scheme_new(const gchar *name)
{
	GstColorScheme *scheme;

	scheme = (GstColorScheme *)g_object_new(GST_TYPE_COLOR_SCHEME, NULL);

	if (name != NULL)
	{
		g_free(scheme->name);
		scheme->name = g_strdup(name);
	}

	return scheme;
}

/* ===== Getters ===== */

/**
 * gst_color_scheme_get_name:
 * @self: A #GstColorScheme
 *
 * Gets the name of the color scheme.
 *
 * Returns: (transfer none): The color scheme name
 */
const gchar *
gst_color_scheme_get_name(GstColorScheme *self)
{
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), NULL);

	return self->name;
}

/**
 * gst_color_scheme_get_foreground:
 * @self: A #GstColorScheme
 *
 * Gets the default foreground color as ARGB.
 *
 * Returns: The foreground color
 */
guint32
gst_color_scheme_get_foreground(GstColorScheme *self)
{
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), 0xFFFFFFFF);

	return self->foreground;
}

/**
 * gst_color_scheme_get_background:
 * @self: A #GstColorScheme
 *
 * Gets the default background color as ARGB.
 *
 * Returns: The background color
 */
guint32
gst_color_scheme_get_background(GstColorScheme *self)
{
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), 0xFF000000);

	return self->background;
}

/**
 * gst_color_scheme_get_cursor_color:
 * @self: A #GstColorScheme
 *
 * Gets the cursor color as ARGB.
 *
 * Returns: The cursor color
 */
guint32
gst_color_scheme_get_cursor_color(GstColorScheme *self)
{
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), 0xFFFFFFFF);

	return self->cursor_color;
}

/**
 * gst_color_scheme_get_color:
 * @self: A #GstColorScheme
 * @index: The color index (0-255)
 *
 * Gets a palette color by index.
 *
 * Returns: The color as ARGB
 */
guint32
gst_color_scheme_get_color(
	GstColorScheme *self,
	guint           index
){
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), 0);
	g_return_val_if_fail(index < GST_COLOR_PALETTE_SIZE, 0);

	return self->palette[index];
}

/* ===== Setters ===== */

/**
 * gst_color_scheme_set_foreground:
 * @self: A #GstColorScheme
 * @color: ARGB color value
 *
 * Sets the default foreground color.
 */
void
gst_color_scheme_set_foreground(
	GstColorScheme *self,
	guint32         color
){
	g_return_if_fail(GST_IS_COLOR_SCHEME(self));

	self->foreground = color;
}

/**
 * gst_color_scheme_set_background:
 * @self: A #GstColorScheme
 * @color: ARGB color value
 *
 * Sets the default background color.
 */
void
gst_color_scheme_set_background(
	GstColorScheme *self,
	guint32         color
){
	g_return_if_fail(GST_IS_COLOR_SCHEME(self));

	self->background = color;
}

/**
 * gst_color_scheme_set_cursor_color:
 * @self: A #GstColorScheme
 * @color: ARGB color value
 *
 * Sets the cursor color.
 */
void
gst_color_scheme_set_cursor_color(
	GstColorScheme *self,
	guint32         color
){
	g_return_if_fail(GST_IS_COLOR_SCHEME(self));

	self->cursor_color = color;
}

/**
 * gst_color_scheme_set_color:
 * @self: A #GstColorScheme
 * @index: The color index (0-255)
 * @color: ARGB color value
 *
 * Sets a palette color by index.
 */
void
gst_color_scheme_set_color(
	GstColorScheme *self,
	guint           index,
	guint32         color
){
	g_return_if_fail(GST_IS_COLOR_SCHEME(self));
	g_return_if_fail(index < GST_COLOR_PALETTE_SIZE);

	self->palette[index] = color;
}

/* ===== Config integration ===== */

/*
 * parse_hex_color:
 * @hex: A "#RRGGBB" string
 * @out_color: (out): The parsed ARGB color (0xFFRRGGBB)
 *
 * Converts a hex color string to an ARGB value with full alpha.
 *
 * Returns: %TRUE if parsing succeeded
 */
static gboolean
parse_hex_color(
	const gchar *hex,
	guint32     *out_color
){
	gulong val;
	gchar *endptr;

	if (hex == NULL || hex[0] != '#' || strlen(hex) != 7) {
		return FALSE;
	}

	/* Parse the 6 hex digits after '#' */
	val = strtoul(hex + 1, &endptr, 16);
	if (*endptr != '\0') {
		return FALSE;
	}

	/* ARGB with full alpha */
	*out_color = 0xFF000000 | (guint32)(val & 0x00FFFFFF);
	return TRUE;
}

/**
 * gst_color_scheme_load_from_config:
 * @self: A #GstColorScheme
 * @config: A #GstConfig with palette data
 *
 * Applies palette colors from a configuration object.
 * Reads the palette_hex entries from @config and overwrites
 * the corresponding palette indices. Sets foreground and
 * background from the configured palette indices.
 *
 * Returns: %TRUE on success, %FALSE if a hex color could not be parsed
 */
gboolean
gst_color_scheme_load_from_config(
	GstColorScheme *self,
	GstConfig      *config
){
	const gchar *const *palette_hex;
	guint n_palette;
	guint fg_idx;
	guint bg_idx;
	guint cursor_fg_idx;
	guint cursor_bg_idx;
	guint i;

	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), FALSE);
	g_return_val_if_fail(config != NULL, FALSE);

	/* Apply palette hex colors (overwrite indices 0-N) */
	palette_hex = gst_config_get_palette_hex(config);
	n_palette = gst_config_get_n_palette(config);

	for (i = 0; i < n_palette && palette_hex != NULL; i++) {
		guint32 color;

		if (!parse_hex_color(palette_hex[i], &color)) {
			g_warning("Invalid palette color at index %u: '%s'",
				i, palette_hex[i]);
			return FALSE;
		}
		self->palette[i] = color;
	}

	/*
	 * Set foreground/background from palette indices.
	 * The config stores indices into the palette, so
	 * look up the ARGB value from the (possibly overwritten) palette.
	 */
	fg_idx = gst_config_get_fg_index(config);
	bg_idx = gst_config_get_bg_index(config);
	cursor_fg_idx = gst_config_get_cursor_fg_index(config);
	cursor_bg_idx = gst_config_get_cursor_bg_index(config);

	if (fg_idx < GST_COLOR_PALETTE_SIZE) {
		self->foreground = self->palette[fg_idx];
	}
	if (bg_idx < GST_COLOR_PALETTE_SIZE) {
		self->background = self->palette[bg_idx];
	}
	if (cursor_bg_idx < GST_COLOR_PALETTE_SIZE) {
		self->cursor_color = self->palette[cursor_bg_idx];
	}

	(void)cursor_fg_idx;

	return TRUE;
}
