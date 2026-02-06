/*
 * gst-module-manager.c - Module lifecycle management
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-module-manager.h"

/**
 * SECTION:gst-module-manager
 * @title: GstModuleManager
 * @short_description: Manages terminal module lifecycle
 *
 * #GstModuleManager handles registration, activation, and deactivation
 * of terminal extension modules.
 */

struct _GstModuleManager
{
	GObject parent_instance;

	GHashTable *modules;  /* name -> GstModule */
};

G_DEFINE_TYPE(GstModuleManager, gst_module_manager, G_TYPE_OBJECT)

/* Singleton instance */
static GstModuleManager *default_manager = NULL;

static void
gst_module_manager_dispose(GObject *object)
{
	GstModuleManager *self;

	self = GST_MODULE_MANAGER(object);

	g_clear_pointer(&self->modules, g_hash_table_unref);

	G_OBJECT_CLASS(gst_module_manager_parent_class)->dispose(object);
}

static void
gst_module_manager_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_module_manager_parent_class)->finalize(object);
}

static void
gst_module_manager_class_init(GstModuleManagerClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_module_manager_dispose;
	object_class->finalize = gst_module_manager_finalize;
}

static void
gst_module_manager_init(GstModuleManager *self)
{
	self->modules = g_hash_table_new_full(
		g_str_hash,
		g_str_equal,
		g_free,
		g_object_unref
	);
}

/**
 * gst_module_manager_new:
 *
 * Creates a new module manager instance.
 *
 * Returns: (transfer full): A new #GstModuleManager
 */
GstModuleManager *
gst_module_manager_new(void)
{
	return (GstModuleManager *)g_object_new(GST_TYPE_MODULE_MANAGER, NULL);
}

/**
 * gst_module_manager_get_default:
 *
 * Gets the default shared module manager instance.
 *
 * Returns: (transfer none): The default #GstModuleManager
 */
GstModuleManager *
gst_module_manager_get_default(void)
{
	if (default_manager == NULL)
	{
		default_manager = gst_module_manager_new();
	}

	return default_manager;
}

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
){
	const gchar *name;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), FALSE);
	g_return_val_if_fail(GST_IS_MODULE(module), FALSE);

	name = gst_module_get_name(module);
	if (name == NULL)
	{
		return FALSE;
	}

	if (g_hash_table_contains(self->modules, name))
	{
		g_warning("Module '%s' is already registered", name);
		return FALSE;
	}

	g_hash_table_insert(
		self->modules,
		g_strdup(name),
		g_object_ref(module)
	);

	return TRUE;
}

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
){
	GstModule *module;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), FALSE);
	g_return_val_if_fail(name != NULL, FALSE);

	module = (GstModule *)g_hash_table_lookup(self->modules, name);
	if (module == NULL)
	{
		return FALSE;
	}

	/* Deactivate before removal */
	gst_module_deactivate(module);

	return g_hash_table_remove(self->modules, name);
}

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
){
	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), NULL);
	g_return_val_if_fail(name != NULL, NULL);

	return (GstModule *)g_hash_table_lookup(self->modules, name);
}

/**
 * gst_module_manager_list_modules:
 * @self: A #GstModuleManager
 *
 * Lists all registered modules.
 *
 * Returns: (transfer container) (element-type GstModuleInfo): List of module info
 */
GList *
gst_module_manager_list_modules(GstModuleManager *self)
{
	GHashTableIter iter;
	gpointer value;
	GList *list;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), NULL);

	list = NULL;

	g_hash_table_iter_init(&iter, self->modules);
	while (g_hash_table_iter_next(&iter, NULL, &value))
	{
		GstModule *module;
		GstModuleInfo *info;

		module = GST_MODULE(value);
		info = gst_module_info_new(
			gst_module_get_name(module),
			gst_module_get_description(module),
			"1.0"  /* TODO: Add version to module interface */
		);

		list = g_list_prepend(list, info);
	}

	return g_list_reverse(list);
}
