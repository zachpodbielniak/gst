/*
 * gst-color-scheme.h - Terminal color scheme handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_COLOR_SCHEME_H
#define GST_COLOR_SCHEME_H

#include <glib-object.h>

G_BEGIN_DECLS

/* Forward declaration — full type in gst-config.h */
struct _GstConfig;

#define GST_TYPE_COLOR_SCHEME (gst_color_scheme_get_type())

G_DECLARE_FINAL_TYPE(GstColorScheme, gst_color_scheme, GST, COLOR_SCHEME, GObject)

GType
gst_color_scheme_get_type(void) G_GNUC_CONST;

/**
 * gst_color_scheme_new:
 * @name: The name of the color scheme
 *
 * Creates a new color scheme with default colors.
 *
 * Returns: (transfer full): A new #GstColorScheme
 */
GstColorScheme *
gst_color_scheme_new(const gchar *name);

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
gst_color_scheme_get_name(GstColorScheme *self);

/**
 * gst_color_scheme_get_foreground:
 * @self: A #GstColorScheme
 *
 * Gets the default foreground color as ARGB.
 *
 * Returns: The foreground color
 */
guint32
gst_color_scheme_get_foreground(GstColorScheme *self);

/**
 * gst_color_scheme_get_background:
 * @self: A #GstColorScheme
 *
 * Gets the default background color as ARGB.
 *
 * Returns: The background color
 */
guint32
gst_color_scheme_get_background(GstColorScheme *self);

/**
 * gst_color_scheme_get_cursor_color:
 * @self: A #GstColorScheme
 *
 * Gets the cursor color as ARGB.
 *
 * Returns: The cursor color
 */
guint32
gst_color_scheme_get_cursor_color(GstColorScheme *self);

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
);

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
);

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
);

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
);

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
);

/* ===== Config integration ===== */

/**
 * gst_color_scheme_load_from_config:
 * @self: A #GstColorScheme
 * @config: A #GstConfig with palette data
 *
 * Applies palette colors from a configuration object.
 * Reads the palette_hex entries from @config and overwrites
 * the corresponding palette indices (0-15). Also sets
 * foreground/background from the configured palette indices.
 *
 * Returns: %TRUE on success, %FALSE if a hex color could not be parsed
 */
gboolean
gst_color_scheme_load_from_config(
	GstColorScheme     *self,
	struct _GstConfig  *config
);

G_END_DECLS

#endif /* GST_COLOR_SCHEME_H */
