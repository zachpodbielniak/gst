/*
 * gst-module-manager.h - Module lifecycle management
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_MODULE_MANAGER_H
#define GST_MODULE_MANAGER_H

#include <glib-object.h>
#include "gst-module.h"
#include "gst-module-info.h"

G_BEGIN_DECLS

#define GST_TYPE_MODULE_MANAGER (gst_module_manager_get_type())

G_DECLARE_FINAL_TYPE(GstModuleManager, gst_module_manager, GST, MODULE_MANAGER, GObject)

GType
gst_module_manager_get_type(void) G_GNUC_CONST;

/**
 * gst_module_manager_new:
 *
 * Creates a new module manager instance.
 *
 * Returns: (transfer full): A new #GstModuleManager
 */
GstModuleManager *
gst_module_manager_new(void);

/**
 * gst_module_manager_get_default:
 *
 * Gets the default shared module manager instance.
 *
 * Returns: (transfer none): The default #GstModuleManager
 */
GstModuleManager *
gst_module_manager_get_default(void);

/**
 * gst_module_manager_register:
 * @self: A #GstModuleManager
 * @module: The module to register
 *
 * Registers a module with the manager.
 *
 * Returns: %TRUE if registration succeeded
 */
gboolean
gst_module_manager_register(
	GstModuleManager *self,
	GstModule        *module
);

/**
 * gst_module_manager_unregister:
 * @self: A #GstModuleManager
 * @name: The module name to unregister
 *
 * Unregisters a module by name.
 *
 * Returns: %TRUE if the module was found and unregistered
 */
gboolean
gst_module_manager_unregister(
	GstModuleManager *self,
	const gchar      *name
);

/**
 * gst_module_manager_get_module:
 * @self: A #GstModuleManager
 * @name: The module name
 *
 * Gets a registered module by name.
 *
 * Returns: (transfer none) (nullable): The module, or %NULL if not found
 */
GstModule *
gst_module_manager_get_module(
	GstModuleManager *self,
	const gchar      *name
);

/**
 * gst_module_manager_list_modules:
 * @self: A #GstModuleManager
 *
 * Lists all registered modules.
 *
 * Returns: (transfer container) (element-type GstModuleInfo): List of module info
 */
GList *
gst_module_manager_list_modules(GstModuleManager *self);

G_END_DECLS

#endif /* GST_MODULE_MANAGER_H */
