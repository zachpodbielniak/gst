/*
 * gst-module-info.h - Module metadata boxed type
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_MODULE_INFO_H
#define GST_MODULE_INFO_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_MODULE_INFO (gst_module_info_get_type())

/**
 * GstModuleInfo:
 *
 * An opaque structure containing module metadata.
 */
typedef struct _GstModuleInfo GstModuleInfo;

GType
gst_module_info_get_type(void) G_GNUC_CONST;

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
);

/**
 * gst_module_info_copy:
 * @self: A #GstModuleInfo
 *
 * Creates a copy of the module info.
 *
 * Returns: (transfer full): A copy of @self
 */
GstModuleInfo *
gst_module_info_copy(const GstModuleInfo *self);

/**
 * gst_module_info_free:
 * @self: A #GstModuleInfo
 *
 * Frees the module info structure.
 */
void
gst_module_info_free(GstModuleInfo *self);

/**
 * gst_module_info_get_name:
 * @self: A #GstModuleInfo
 *
 * Gets the module name.
 *
 * Returns: (transfer none): The module name
 */
const gchar *
gst_module_info_get_name(const GstModuleInfo *self);

/**
 * gst_module_info_get_description:
 * @self: A #GstModuleInfo
 *
 * Gets the module description.
 *
 * Returns: (transfer none): The module description
 */
const gchar *
gst_module_info_get_description(const GstModuleInfo *self);

/**
 * gst_module_info_get_version:
 * @self: A #GstModuleInfo
 *
 * Gets the module version string.
 *
 * Returns: (transfer none): The module version
 */
const gchar *
gst_module_info_get_version(const GstModuleInfo *self);

G_END_DECLS

#endif /* GST_MODULE_INFO_H */
