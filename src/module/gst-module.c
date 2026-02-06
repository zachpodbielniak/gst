/*
 * gst-module.c - Abstract base module class
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-module.h"

/**
 * SECTION:gst-module
 * @title: GstModule
 * @short_description: Abstract base class for terminal modules
 *
 * #GstModule is an abstract base class that defines the interface
 * for terminal extension modules. Modules can add features like
 * scrollback, transparency, URL detection, and more.
 */

/* Private structure */
typedef struct
{
	gboolean active;
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
}

static void
gst_module_init(GstModule *self)
{
	GstModulePrivate *priv;

	priv = gst_module_get_instance_private(self);
	priv->active = FALSE;
}

/**
 * gst_module_activate:
 * @self: A #GstModule
 *
 * Activates the module.
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
 * Deactivates the module.
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
 * Gets the module name.
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
 * Gets the module description.
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
