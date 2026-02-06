/*
 * gst-config.h - YAML configuration handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_CONFIG_H
#define GST_CONFIG_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

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
 * gst_config_load_from_file:
 * @self: A #GstConfig
 * @file: The configuration file to load
 * @error: Return location for a #GError
 *
 * Loads configuration from a YAML file.
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
 * gst_config_save_to_file:
 * @self: A #GstConfig
 * @file: The file to save to
 * @error: Return location for a #GError
 *
 * Saves configuration to a YAML file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
gst_config_save_to_file(
	GstConfig  *self,
	GFile      *file,
	GError    **error
);

/**
 * gst_config_get_default:
 *
 * Gets the default shared configuration instance.
 *
 * Returns: (transfer none): The default #GstConfig
 */
GstConfig *
gst_config_get_default(void);

G_END_DECLS

#endif /* GST_CONFIG_H */
