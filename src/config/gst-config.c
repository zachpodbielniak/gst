/*
 * gst-config.c - YAML configuration handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-config.h"

/**
 * SECTION:gst-config
 * @title: GstConfig
 * @short_description: Terminal configuration management
 *
 * #GstConfig handles loading and saving of terminal configuration
 * from YAML files. It provides access to all configurable options.
 */

struct _GstConfig
{
	GObject parent_instance;

	/* Font settings */
	gchar *font_family;
	gdouble font_size;

	/* Window settings */
	guint default_cols;
	guint default_rows;

	/* TODO: Add more configuration fields */
};

G_DEFINE_TYPE(GstConfig, gst_config, G_TYPE_OBJECT)

/* Singleton instance */
static GstConfig *default_config = NULL;

static void
gst_config_dispose(GObject *object)
{
	GstConfig *self;

	self = GST_CONFIG(object);

	g_clear_pointer(&self->font_family, g_free);

	G_OBJECT_CLASS(gst_config_parent_class)->dispose(object);
}

static void
gst_config_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_config_parent_class)->finalize(object);
}

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
	/* Set defaults */
	self->font_family = g_strdup("monospace");
	self->font_size = 12.0;
	self->default_cols = 80;
	self->default_rows = 24;
}

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
){
	g_return_val_if_fail(GST_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(G_IS_FILE(file), FALSE);

	/* TODO: Implement YAML parsing with yaml-glib */
	(void)error;

	return TRUE;
}

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
){
	g_return_val_if_fail(GST_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(G_IS_FILE(file), FALSE);

	/* TODO: Implement YAML serialization with yaml-glib */
	(void)error;

	return TRUE;
}

/**
 * gst_config_get_default:
 *
 * Gets the default shared configuration instance.
 *
 * Returns: (transfer none): The default #GstConfig
 */
GstConfig *
gst_config_get_default(void)
{
	if (default_config == NULL)
	{
		default_config = gst_config_new();
	}

	return default_config;
}
