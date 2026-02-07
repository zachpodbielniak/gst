/*
 * gst-module.c - Abstract base module class
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-module.h"
#include "../gst-enums.h"

/**
 * SECTION:gst-module
 * @title: GstModule
 * @short_description: Abstract base class for terminal modules
 *
 * #GstModule is an abstract base class that defines the interface
 * for terminal extension modules. Modules can add features like
 * scrollback, transparency, URL detection, and more.
 *
 * Each module has a priority that determines dispatch ordering
 * when multiple modules register for the same hook point.
 * Lower priority values run first.
 */

/* Private structure */
typedef struct
{
	gboolean active;
	gint     priority;
} GstModulePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(GstModule, gst_module, G_TYPE_OBJECT)

static void
gst_module_class_init(GstModuleClass *klass)
{
	/* Virtual methods default to NULL - must be implemented by subclasses */
	klass->activate = NULL;
	klass->deactivate = NULL;
	klass->get_name = NULL;
	klass->get_description = NULL;
	klass->configure = NULL;
}

static void
gst_module_init(GstModule *self)
{
	GstModulePrivate *priv;

	priv = gst_module_get_instance_private(self);
	priv->active = FALSE;
	priv->priority = GST_MODULE_PRIORITY_NORMAL;
}

/**
 * gst_module_activate:
 * @self: A #GstModule
 *
 * Activates the module. If the module is already active,
 * returns %TRUE immediately. Calls the subclass activate vfunc
 * if implemented; otherwise marks the module as active directly.
 *
 * Returns: %TRUE if activation succeeded
 */
gboolean
gst_module_activate(GstModule *self)
{
	GstModuleClass *klass;
	GstModulePrivate *priv;
	gboolean result;

	g_return_val_if_fail(GST_IS_MODULE(self), FALSE);

	priv = gst_module_get_instance_private(self);

	if (priv->active)
	{
		return TRUE;
	}

	klass = GST_MODULE_GET_CLASS(self);
	if (klass->activate != NULL)
	{
		result = klass->activate(self);
		if (result)
		{
			priv->active = TRUE;
		}
		return result;
	}

	priv->active = TRUE;
	return TRUE;
}

/**
 * gst_module_deactivate:
 * @self: A #GstModule
 *
 * Deactivates the module. If the module is already inactive,
 * does nothing. Calls the subclass deactivate vfunc if implemented.
 */
void
gst_module_deactivate(GstModule *self)
{
	GstModuleClass *klass;
	GstModulePrivate *priv;

	g_return_if_fail(GST_IS_MODULE(self));

	priv = gst_module_get_instance_private(self);

	if (!priv->active)
	{
		return;
	}

	klass = GST_MODULE_GET_CLASS(self);
	if (klass->deactivate != NULL)
	{
		klass->deactivate(self);
	}

	priv->active = FALSE;
}

/**
 * gst_module_get_name:
 * @self: A #GstModule
 *
 * Gets the module name by calling the subclass get_name vfunc.
 * Returns "unknown" if the subclass does not implement it.
 *
 * Returns: (transfer none): The module name
 */
const gchar *
gst_module_get_name(GstModule *self)
{
	GstModuleClass *klass;

	g_return_val_if_fail(GST_IS_MODULE(self), NULL);

	klass = GST_MODULE_GET_CLASS(self);
	if (klass->get_name != NULL)
	{
		return klass->get_name(self);
	}

	return "unknown";
}

/**
 * gst_module_get_description:
 * @self: A #GstModule
 *
 * Gets the module description by calling the subclass get_description vfunc.
 * Returns an empty string if the subclass does not implement it.
 *
 * Returns: (transfer none): The module description
 */
const gchar *
gst_module_get_description(GstModule *self)
{
	GstModuleClass *klass;

	g_return_val_if_fail(GST_IS_MODULE(self), NULL);

	klass = GST_MODULE_GET_CLASS(self);
	if (klass->get_description != NULL)
	{
		return klass->get_description(self);
	}

	return "";
}

/**
 * gst_module_configure:
 * @self: A #GstModule
 * @config: (type gpointer): A configuration object to pass to the module
 *
 * Configures the module with the given configuration object.
 * Calls the configure vfunc if the subclass implements it.
 * Does nothing if the subclass has no configure vfunc.
 */
void
gst_module_configure(GstModule *self, gpointer config)
{
	GstModuleClass *klass;

	g_return_if_fail(GST_IS_MODULE(self));

	klass = GST_MODULE_GET_CLASS(self);
	if (klass->configure != NULL)
	{
		klass->configure(self, config);
	}
}

/**
 * gst_module_get_priority:
 * @self: A #GstModule
 *
 * Gets the module's hook dispatch priority.
 * Lower values run first during hook dispatch.
 * Defaults to %GST_MODULE_PRIORITY_NORMAL (0).
 *
 * Returns: The priority value
 */
gint
gst_module_get_priority(GstModule *self)
{
	GstModulePrivate *priv;

	g_return_val_if_fail(GST_IS_MODULE(self), GST_MODULE_PRIORITY_NORMAL);

	priv = gst_module_get_instance_private(self);
	return priv->priority;
}

/**
 * gst_module_set_priority:
 * @self: A #GstModule
 * @priority: The priority value (lower runs first)
 *
 * Sets the module's hook dispatch priority.
 * Use #GstModulePriority constants or arbitrary integer values.
 */
void
gst_module_set_priority(GstModule *self, gint priority)
{
	GstModulePrivate *priv;

	g_return_if_fail(GST_IS_MODULE(self));

	priv = gst_module_get_instance_private(self);
	priv->priority = priority;
}

/**
 * gst_module_is_active:
 * @self: A #GstModule
 *
 * Checks whether the module is currently active.
 * A module is active after a successful gst_module_activate()
 * and until gst_module_deactivate() is called.
 *
 * Returns: %TRUE if the module is active
 */
gboolean
gst_module_is_active(GstModule *self)
{
	GstModulePrivate *priv;

	g_return_val_if_fail(GST_IS_MODULE(self), FALSE);

	priv = gst_module_get_instance_private(self);
	return priv->active;
}
