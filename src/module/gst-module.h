/*
 * gst-module.h - Abstract base module class
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_MODULE_H
#define GST_MODULE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_MODULE (gst_module_get_type())

G_DECLARE_DERIVABLE_TYPE(GstModule, gst_module, GST, MODULE, GObject)

/**
 * GstModuleClass:
 * @parent_class: The parent class
 * @activate: Virtual method to activate the module
 * @deactivate: Virtual method to deactivate the module
 * @get_name: Virtual method to get the module name
 * @get_description: Virtual method to get the module description
 * @configure: Virtual method to configure the module with a config object
 *
 * The class structure for #GstModule.
 */
struct _GstModuleClass
{
	GObjectClass parent_class;

	/* Virtual methods */
	gboolean     (*activate)        (GstModule *self);
	void         (*deactivate)      (GstModule *self);
	const gchar *(*get_name)        (GstModule *self);
	const gchar *(*get_description) (GstModule *self);
	void         (*configure)       (GstModule *self, gpointer config);

	/* Padding for future expansion */
	gpointer padding[7];
};

GType
gst_module_get_type(void) G_GNUC_CONST;

/**
 * gst_module_activate:
 * @self: A #GstModule
 *
 * Activates the module.
 *
 * Returns: %TRUE if activation succeeded
 */
gboolean
gst_module_activate(GstModule *self);

/**
 * gst_module_deactivate:
 * @self: A #GstModule
 *
 * Deactivates the module.
 */
void
gst_module_deactivate(GstModule *self);

/**
 * gst_module_get_name:
 * @self: A #GstModule
 *
 * Gets the module name.
 *
 * Returns: (transfer none): The module name
 */
const gchar *
gst_module_get_name(GstModule *self);

/**
 * gst_module_get_description:
 * @self: A #GstModule
 *
 * Gets the module description.
 *
 * Returns: (transfer none): The module description
 */
const gchar *
gst_module_get_description(GstModule *self);

/**
 * gst_module_configure:
 * @self: A #GstModule
 * @config: (type gpointer): A configuration object to pass to the module
 *
 * Configures the module with the given configuration object.
 * Calls the configure vfunc if the subclass implements it.
 */
void
gst_module_configure(GstModule *self, gpointer config);

/**
 * gst_module_get_priority:
 * @self: A #GstModule
 *
 * Gets the module's hook dispatch priority.
 * Lower values run first during hook dispatch.
 *
 * Returns: The priority value
 */
gint
gst_module_get_priority(GstModule *self);

/**
 * gst_module_set_priority:
 * @self: A #GstModule
 * @priority: The priority value (lower runs first)
 *
 * Sets the module's hook dispatch priority.
 */
void
gst_module_set_priority(GstModule *self, gint priority);

/**
 * gst_module_is_active:
 * @self: A #GstModule
 *
 * Checks whether the module is currently active.
 *
 * Returns: %TRUE if the module is active
 */
gboolean
gst_module_is_active(GstModule *self);

G_END_DECLS

#endif /* GST_MODULE_H */
