/*
 * gst-module-info.c - Module metadata boxed type
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-module-info.h"

/**
 * SECTION:gst-module-info
 * @title: GstModuleInfo
 * @short_description: Module metadata container
 *
 * #GstModuleInfo is a boxed type that contains metadata about a
 * terminal module, including its name, description, and version.
 */

struct _GstModuleInfo
{
	gchar *name;
	gchar *description;
	gchar *version;
	gint ref_count;
};

G_DEFINE_BOXED_TYPE(
	GstModuleInfo,
	gst_module_info,
	gst_module_info_copy,
	gst_module_info_free
)

/**
 * gst_module_info_new:
 * @name: The module name
 * @description: The module description
 * @version: The module version string
 *
 * Creates a new module info structure.
 *
 * Returns: (transfer full): A new #GstModuleInfo
 */
GstModuleInfo *
gst_module_info_new(
	const gchar *name,
	const gchar *description,
	const gchar *version
){
	GstModuleInfo *info;

	info = g_slice_new0(GstModuleInfo);
	info->name = g_strdup(name != NULL ? name : "");
	info->description = g_strdup(description != NULL ? description : "");
	info->version = g_strdup(version != NULL ? version : "0.0.0");
	info->ref_count = 1;

	return info;
}

/**
 * gst_module_info_copy:
 * @self: A #GstModuleInfo
 *
 * Creates a copy of the module info.
 *
 * Returns: (transfer full): A copy of @self
 */
GstModuleInfo *
gst_module_info_copy(const GstModuleInfo *self)
{
	GstModuleInfo *copy;

	g_return_val_if_fail(self != NULL, NULL);

	copy = gst_module_info_new(
		self->name,
		self->description,
		self->version
	);

	return copy;
}

/**
 * gst_module_info_free:
 * @self: A #GstModuleInfo
 *
 * Frees the module info structure.
 */
void
gst_module_info_free(GstModuleInfo *self)
{
	if (self == NULL)
	{
		return;
	}

	g_free(self->name);
	g_free(self->description);
	g_free(self->version);

	g_slice_free(GstModuleInfo, self);
}

/**
 * gst_module_info_get_name:
 * @self: A #GstModuleInfo
 *
 * Gets the module name.
 *
 * Returns: (transfer none): The module name
 */
const gchar *
gst_module_info_get_name(const GstModuleInfo *self)
{
	g_return_val_if_fail(self != NULL, NULL);

	return self->name;
}

/**
 * gst_module_info_get_description:
 * @self: A #GstModuleInfo
 *
 * Gets the module description.
 *
 * Returns: (transfer none): The module description
 */
const gchar *
gst_module_info_get_description(const GstModuleInfo *self)
{
	g_return_val_if_fail(self != NULL, NULL);

	return self->description;
}

/**
 * gst_module_info_get_version:
 * @self: A #GstModuleInfo
 *
 * Gets the module version string.
 *
 * Returns: (transfer none): The module version
 */
const gchar *
gst_module_info_get_version(const GstModuleInfo *self)
{
	g_return_val_if_fail(self != NULL, NULL);

	return self->version;
}
