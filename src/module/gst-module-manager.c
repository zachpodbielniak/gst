/*
 * gst-module-manager.c - Module lifecycle management and hook dispatch
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gmodule.h>
#include <gio/gio.h>
#include "gst-module-manager.h"
#include "../config/gst-config.h"
#include "../interfaces/gst-input-handler.h"
#include "../interfaces/gst-output-filter.h"
#include "../interfaces/gst-bell-handler.h"
#include "../interfaces/gst-render-overlay.h"
#include "../interfaces/gst-glyph-transformer.h"
#include "../interfaces/gst-external-pipe.h"
#include "../interfaces/gst-url-handler.h"
#include "../interfaces/gst-color-provider.h"
#include "../interfaces/gst-font-provider.h"
#include "../interfaces/gst-escape-handler.h"

/**
 * SECTION:gst-module-manager
 * @title: GstModuleManager
 * @short_description: Manages module lifecycle and hook dispatch
 *
 * #GstModuleManager handles registration, activation, deactivation,
 * and hook dispatch for terminal extension modules. When a module
 * is registered, the manager introspects its GObject type to detect
 * which interfaces it implements and auto-registers hooks accordingly.
 *
 * Hook dispatch walks a priority-sorted list for each hook point,
 * calling active module handlers. For consumable events (key, mouse)
 * dispatch stops when a handler returns %TRUE. For non-consumable
 * events (bell, overlay) all handlers are called.
 */

/*
 * GstHookEntry:
 *
 * Internal structure tracking a module's registration at a hook point.
 * Stored in priority-sorted lists per hook point.
 */
typedef struct
{
	GstModule    *module;     /* weak ref, owned by modules hash table */
	GstHookPoint  hook_point;
	gint          priority;
} GstHookEntry;

/*
 * Module register entry point function signature.
 * Modules export: G_MODULE_EXPORT GType gst_module_register(void);
 */
typedef GType (*GstModuleRegisterFunc)(void);

struct _GstModuleManager
{
	GObject parent_instance;

	GHashTable  *modules;          /* name (gchar*) -> GstModule* */
	GList       *hooks[GST_HOOK_LAST]; /* priority-sorted GstHookEntry lists */
	GPtrArray   *loaded_gmodules;  /* GModule* handles for dlclose on dispose */
	gpointer     config;           /* weak ref to GstConfig */
	gpointer     terminal;         /* weak ref to GstTerminal */
	gpointer     window;           /* weak ref to GstWindow */
	gpointer     font_cache;       /* weak ref to font cache (X11 or Cairo) */
	gint         backend_type;     /* GstBackendType value */
};

G_DEFINE_TYPE(GstModuleManager, gst_module_manager, G_TYPE_OBJECT)

/* Singleton instance */
static GstModuleManager *default_manager = NULL;

/* ===== Internal helpers ===== */

/*
 * hook_entry_new:
 *
 * Allocates a new hook entry. The module reference is weak
 * (not ref'd here; the modules hash table owns the ref).
 */
static GstHookEntry *
hook_entry_new(
	GstModule    *module,
	GstHookPoint  hook_point,
	gint          priority
){
	GstHookEntry *entry;

	entry = g_slice_new(GstHookEntry);
	entry->module = module;
	entry->hook_point = hook_point;
	entry->priority = priority;

	return entry;
}

static void
hook_entry_free(gpointer data)
{
	g_slice_free(GstHookEntry, data);
}

/*
 * hook_entry_compare:
 *
 * Comparison function for priority-sorted insertion.
 * Lower priority values sort first (run first).
 */
static gint
hook_entry_compare(gconstpointer a, gconstpointer b)
{
	const GstHookEntry *ea = (const GstHookEntry *)a;
	const GstHookEntry *eb = (const GstHookEntry *)b;

	if (ea->priority < eb->priority) return -1;
	if (ea->priority > eb->priority) return 1;
	return 0;
}

/*
 * auto_register_hooks:
 *
 * Introspects the module's GObject type to detect which interfaces
 * it implements, and registers the appropriate hooks automatically.
 */
static void
auto_register_hooks(
	GstModuleManager *self,
	GstModule        *module
){
	GType module_type;
	gint priority;

	module_type = G_OBJECT_TYPE(module);
	priority = gst_module_get_priority(module);

	/* Check each known interface and register the corresponding hook */
	if (g_type_is_a(module_type, GST_TYPE_INPUT_HANDLER))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_KEY_PRESS, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_OUTPUT_FILTER))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_PRE_OUTPUT, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_BELL_HANDLER))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_BELL, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_RENDER_OVERLAY))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_RENDER_OVERLAY, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_GLYPH_TRANSFORMER))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_GLYPH_TRANSFORM, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_EXTERNAL_PIPE))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_EXTERNAL_PIPE, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_URL_HANDLER))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_URL_DETECT, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_COLOR_PROVIDER))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_COLOR_QUERY, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_FONT_PROVIDER))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_FONT_LOAD, priority);
	}

	if (g_type_is_a(module_type, GST_TYPE_ESCAPE_HANDLER))
	{
		gst_module_manager_register_hook(self, module,
			GST_HOOK_ESCAPE_APC, priority);
	}
}

/* ===== GObject lifecycle ===== */

static void
gst_module_manager_dispose(GObject *object)
{
	GstModuleManager *self;
	guint i;

	self = GST_MODULE_MANAGER(object);

	/* Free all hook lists */
	for (i = 0; i < GST_HOOK_LAST; i++)
	{
		g_list_free_full(self->hooks[i], hook_entry_free);
		self->hooks[i] = NULL;
	}

	/* Close loaded GModule handles */
	if (self->loaded_gmodules != NULL)
	{
		for (i = 0; i < self->loaded_gmodules->len; i++)
		{
			GModule *gmod;

			gmod = (GModule *)g_ptr_array_index(self->loaded_gmodules, i);
			if (gmod != NULL)
			{
				g_module_close(gmod);
			}
		}
		g_clear_pointer(&self->loaded_gmodules, g_ptr_array_unref);
	}

	g_clear_pointer(&self->modules, g_hash_table_unref);
	self->config = NULL;
	self->terminal = NULL;
	self->window = NULL;
	self->font_cache = NULL;

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
	guint i;

	self->modules = g_hash_table_new_full(
		g_str_hash,
		g_str_equal,
		g_free,
		g_object_unref
	);

	/* Initialize all hook lists to NULL */
	for (i = 0; i < GST_HOOK_LAST; i++)
	{
		self->hooks[i] = NULL;
	}

	self->loaded_gmodules = g_ptr_array_new();
	self->config = NULL;
	self->terminal = NULL;
	self->window = NULL;
	self->font_cache = NULL;
	self->backend_type = 0;
}

/* ===== Public API: construction ===== */

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
 * Creates one if it does not yet exist.
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

/* ===== Public API: registration ===== */

/**
 * gst_module_manager_register:
 * @self: A #GstModuleManager
 * @module: The module to register
 *
 * Registers a module with the manager. The module is stored by name.
 * Automatically introspects which interfaces the module implements
 * and registers hooks for each detected interface.
 *
 * Returns: %TRUE if registration succeeded, %FALSE if the name is
 *          already registered or the module has no name
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

	/* Auto-detect interfaces and register hooks */
	auto_register_hooks(self, module);

	return TRUE;
}

/**
 * gst_module_manager_unregister:
 * @self: A #GstModuleManager
 * @name: The module name to unregister
 *
 * Unregisters a module by name. Deactivates the module first,
 * then removes all its hook registrations, and finally removes
 * it from the module hash table.
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

	/* Deactivate and remove hooks before removing from table */
	gst_module_deactivate(module);
	gst_module_manager_unregister_hooks(self, module);

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
 * Lists all registered modules as #GstModuleInfo boxed types.
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
			"1.0"
		);

		list = g_list_prepend(list, info);
	}

	return g_list_reverse(list);
}

/* ===== Public API: hook registration ===== */

/**
 * gst_module_manager_register_hook:
 * @self: A #GstModuleManager
 * @module: The module registering the hook
 * @hook_point: The hook point to register at
 * @priority: Dispatch priority (lower runs first)
 *
 * Registers a module at a specific hook point with the given priority.
 * The module is inserted into a priority-sorted list for that hook point.
 */
void
gst_module_manager_register_hook(
	GstModuleManager *self,
	GstModule        *module,
	GstHookPoint      hook_point,
	gint               priority
){
	GstHookEntry *entry;

	g_return_if_fail(GST_IS_MODULE_MANAGER(self));
	g_return_if_fail(GST_IS_MODULE(module));
	g_return_if_fail((guint)hook_point < GST_HOOK_LAST);

	entry = hook_entry_new(module, hook_point, priority);
	self->hooks[hook_point] = g_list_insert_sorted(
		self->hooks[hook_point],
		entry,
		hook_entry_compare
	);
}

/**
 * gst_module_manager_unregister_hooks:
 * @self: A #GstModuleManager
 * @module: The module whose hooks to remove
 *
 * Removes all hook registrations for the given module
 * across all hook points.
 */
void
gst_module_manager_unregister_hooks(
	GstModuleManager *self,
	GstModule        *module
){
	guint i;

	g_return_if_fail(GST_IS_MODULE_MANAGER(self));
	g_return_if_fail(GST_IS_MODULE(module));

	for (i = 0; i < GST_HOOK_LAST; i++)
	{
		GList *l;
		GList *next;

		for (l = self->hooks[i]; l != NULL; l = next)
		{
			GstHookEntry *entry;

			next = l->next;
			entry = (GstHookEntry *)l->data;

			if (entry->module == module)
			{
				self->hooks[i] = g_list_delete_link(
					self->hooks[i], l);
				hook_entry_free(entry);
			}
		}
	}
}

/* ===== Public API: hook dispatch ===== */

/**
 * gst_module_manager_dispatch_hook:
 * @self: A #GstModuleManager
 * @hook_point: The hook point to dispatch
 * @event_data: (nullable): Opaque event data passed to handlers
 *
 * Generic dispatch for extensibility. Currently used internally
 * by the typed dispatchers. Walks the hook list in priority order
 * and calls appropriate interface methods on active modules.
 *
 * Returns: %TRUE if any handler consumed the event
 */
gboolean
gst_module_manager_dispatch_hook(
	GstModuleManager *self,
	GstHookPoint      hook_point,
	gpointer          event_data
){
	GList *l;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), FALSE);
	g_return_val_if_fail((guint)hook_point < GST_HOOK_LAST, FALSE);

	for (l = self->hooks[hook_point]; l != NULL; l = l->next)
	{
		GstHookEntry *entry;

		entry = (GstHookEntry *)l->data;

		if (!gst_module_is_active(entry->module))
		{
			continue;
		}

		/* Dispatch based on hook point type */
		switch (hook_point)
		{
		case GST_HOOK_BELL:
			if (GST_IS_BELL_HANDLER(entry->module))
			{
				gst_bell_handler_handle_bell(
					GST_BELL_HANDLER(entry->module));
			}
			break;

		case GST_HOOK_RENDER_OVERLAY:
			/* Requires render_context, width, height - use typed dispatcher */
			break;

		case GST_HOOK_KEY_PRESS:
			/* Requires keyval, keycode, state - use typed dispatcher */
			break;

		default:
			break;
		}
	}

	return FALSE;
}

/**
 * gst_module_manager_dispatch_key_event:
 * @self: A #GstModuleManager
 * @keyval: The key value
 * @keycode: The hardware keycode
 * @state: The modifier state
 *
 * Dispatches a key event to all #GstInputHandler modules registered
 * at %GST_HOOK_KEY_PRESS. Walks the list in priority order and stops
 * at the first handler that returns %TRUE (consumed the event).
 *
 * Returns: %TRUE if a module consumed the key event
 */
gboolean
gst_module_manager_dispatch_key_event(
	GstModuleManager *self,
	guint             keyval,
	guint             keycode,
	guint             state
){
	GList *l;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), FALSE);

	for (l = self->hooks[GST_HOOK_KEY_PRESS]; l != NULL; l = l->next)
	{
		GstHookEntry *entry;

		entry = (GstHookEntry *)l->data;

		if (!gst_module_is_active(entry->module))
		{
			continue;
		}

		if (GST_IS_INPUT_HANDLER(entry->module))
		{
			if (gst_input_handler_handle_key_event(
				GST_INPUT_HANDLER(entry->module),
				keyval, keycode, state))
			{
				return TRUE;
			}
		}
	}

	return FALSE;
}

/**
 * gst_module_manager_dispatch_bell:
 * @self: A #GstModuleManager
 *
 * Dispatches a bell event to all #GstBellHandler modules registered
 * at %GST_HOOK_BELL. All handlers are called (non-consumable event).
 */
void
gst_module_manager_dispatch_bell(GstModuleManager *self)
{
	GList *l;

	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	for (l = self->hooks[GST_HOOK_BELL]; l != NULL; l = l->next)
	{
		GstHookEntry *entry;

		entry = (GstHookEntry *)l->data;

		if (!gst_module_is_active(entry->module))
		{
			continue;
		}

		if (GST_IS_BELL_HANDLER(entry->module))
		{
			gst_bell_handler_handle_bell(
				GST_BELL_HANDLER(entry->module));
		}
	}
}

/**
 * gst_module_manager_dispatch_render_overlay:
 * @self: A #GstModuleManager
 * @render_context: (type gpointer): Opaque rendering context
 * @width: Width of the render area in pixels
 * @height: Height of the render area in pixels
 *
 * Dispatches a render overlay event to all #GstRenderOverlay modules.
 * All handlers are called (non-consumable event).
 */
void
gst_module_manager_dispatch_render_overlay(
	GstModuleManager *self,
	gpointer          render_context,
	gint              width,
	gint              height
){
	GList *l;

	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	for (l = self->hooks[GST_HOOK_RENDER_OVERLAY]; l != NULL; l = l->next)
	{
		GstHookEntry *entry;

		entry = (GstHookEntry *)l->data;

		if (!gst_module_is_active(entry->module))
		{
			continue;
		}

		if (GST_IS_RENDER_OVERLAY(entry->module))
		{
			gst_render_overlay_render(
				GST_RENDER_OVERLAY(entry->module),
				render_context, width, height);
		}
	}
}

/* ===== Public API: module loading ===== */

/**
 * gst_module_manager_load_module:
 * @self: A #GstModuleManager
 * @path: Path to the .so module file
 * @error: (out) (optional): Location to store a #GError on failure
 *
 * Loads a module from a shared object file. The .so must export
 * a `gst_module_register` symbol that is a function returning GType.
 *
 * Loading sequence:
 * 1. g_module_open() with G_MODULE_BIND_LOCAL
 * 2. g_module_symbol() to find gst_module_register
 * 3. Call entry point to get the module GType
 * 4. g_object_new() to instantiate
 * 5. gst_module_manager_register() to auto-detect interfaces
 * 6. Store GModule handle for cleanup on dispose
 *
 * Returns: (transfer none) (nullable): The loaded module, or %NULL on error
 */
GstModule *
gst_module_manager_load_module(
	GstModuleManager *self,
	const gchar      *path,
	GError          **error
){
	GModule *gmod;
	GstModuleRegisterFunc register_func;
	GType module_type;
	GstModule *module;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), NULL);
	g_return_val_if_fail(path != NULL, NULL);

	/* Open the shared library */
	gmod = g_module_open(path, G_MODULE_BIND_LOCAL);
	if (gmod == NULL)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"Failed to open module '%s': %s",
			path, g_module_error());
		return NULL;
	}

	/* Find the entry point */
	if (!g_module_symbol(gmod, "gst_module_register",
		(gpointer *)&register_func))
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"Module '%s' missing gst_module_register symbol: %s",
			path, g_module_error());
		g_module_close(gmod);
		return NULL;
	}

	/* Call entry point to get the GType */
	module_type = register_func();
	if (!g_type_is_a(module_type, GST_TYPE_MODULE))
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"Module '%s': registered type is not a GstModule subclass",
			path);
		g_module_close(gmod);
		return NULL;
	}

	/* Instantiate the module */
	module = (GstModule *)g_object_new(module_type, NULL);
	if (module == NULL)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"Module '%s': failed to instantiate", path);
		g_module_close(gmod);
		return NULL;
	}

	/* Register with the manager (auto-detects interfaces) */
	if (!gst_module_manager_register(self, module))
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			"Module '%s': registration failed (duplicate name '%s'?)",
			path, gst_module_get_name(module));
		g_object_unref(module);
		g_module_close(gmod);
		return NULL;
	}

	/* Keep the GModule handle alive and store for cleanup */
	g_module_make_resident(gmod);
	g_ptr_array_add(self->loaded_gmodules, gmod);

	/* The hash table now owns the ref; drop ours */
	g_object_unref(module);

	g_debug("Loaded module '%s' from %s",
		gst_module_get_name(module), path);

	return module;
}

/**
 * gst_module_manager_load_from_directory:
 * @self: A #GstModuleManager
 * @dir_path: Path to a directory containing .so module files
 *
 * Scans a directory for files ending in ".so" and attempts to
 * load each one as a module. Files that fail to load are logged
 * at debug level and silently skipped.
 *
 * Returns: The number of modules successfully loaded
 */
guint
gst_module_manager_load_from_directory(
	GstModuleManager *self,
	const gchar      *dir_path
){
	GDir *dir;
	const gchar *filename;
	guint count;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), 0);
	g_return_val_if_fail(dir_path != NULL, 0);

	dir = g_dir_open(dir_path, 0, NULL);
	if (dir == NULL)
	{
		/* Directory doesn't exist or can't be read - not an error */
		return 0;
	}

	count = 0;

	while ((filename = g_dir_read_name(dir)) != NULL)
	{
		g_autofree gchar *path = NULL;
		GError *error = NULL;

		/* Only load .so files */
		if (!g_str_has_suffix(filename, ".so"))
		{
			continue;
		}

		path = g_build_filename(dir_path, filename, NULL);

		if (gst_module_manager_load_module(self, path, &error) != NULL)
		{
			count++;
		}
		else
		{
			g_debug("Skipping module '%s': %s",
				filename, error->message);
			g_error_free(error);
		}
	}

	g_dir_close(dir);

	return count;
}

/* ===== Public API: object accessors ===== */

/**
 * gst_module_manager_set_terminal:
 * @self: A #GstModuleManager
 * @terminal: (type gpointer): The terminal instance (weak ref)
 *
 * Stores a weak reference to the terminal for module access.
 */
void
gst_module_manager_set_terminal(
	GstModuleManager *self,
	gpointer          terminal
){
	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	self->terminal = terminal;
}

/**
 * gst_module_manager_get_terminal:
 * @self: A #GstModuleManager
 *
 * Gets the stored terminal reference.
 *
 * Returns: (transfer none) (nullable): The terminal, or %NULL
 */
gpointer
gst_module_manager_get_terminal(GstModuleManager *self)
{
	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), NULL);

	return self->terminal;
}

/**
 * gst_module_manager_set_window:
 * @self: A #GstModuleManager
 * @window: (type gpointer): The window instance (weak ref)
 *
 * Stores a weak reference to the window for module access.
 */
void
gst_module_manager_set_window(
	GstModuleManager *self,
	gpointer          window
){
	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	self->window = window;
}

/**
 * gst_module_manager_get_window:
 * @self: A #GstModuleManager
 *
 * Gets the stored window reference.
 *
 * Returns: (transfer none) (nullable): The window, or %NULL
 */
gpointer
gst_module_manager_get_window(GstModuleManager *self)
{
	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), NULL);

	return self->window;
}

/* ===== Public API: font cache and backend type accessors ===== */

/**
 * gst_module_manager_set_font_cache:
 * @self: A #GstModuleManager
 * @font_cache: (type gpointer): The font cache instance (weak ref)
 *
 * Stores a weak reference to the font cache for module access.
 */
void
gst_module_manager_set_font_cache(
	GstModuleManager *self,
	gpointer          font_cache
){
	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	self->font_cache = font_cache;
}

/**
 * gst_module_manager_get_font_cache:
 * @self: A #GstModuleManager
 *
 * Gets the stored font cache reference.
 *
 * Returns: (transfer none) (nullable): The font cache, or %NULL
 */
gpointer
gst_module_manager_get_font_cache(GstModuleManager *self)
{
	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), NULL);

	return self->font_cache;
}

/**
 * gst_module_manager_set_backend_type:
 * @self: A #GstModuleManager
 * @backend_type: The active rendering backend type
 *
 * Stores the active backend type for module access.
 */
void
gst_module_manager_set_backend_type(
	GstModuleManager *self,
	gint              backend_type
){
	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	self->backend_type = backend_type;
}

/**
 * gst_module_manager_get_backend_type:
 * @self: A #GstModuleManager
 *
 * Gets the stored backend type.
 *
 * Returns: The #GstBackendType value
 */
gint
gst_module_manager_get_backend_type(GstModuleManager *self)
{
	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), 0);

	return self->backend_type;
}

/* ===== Public API: glyph transform dispatch ===== */

/**
 * gst_module_manager_dispatch_glyph_transform:
 * @self: A #GstModuleManager
 * @codepoint: Unicode codepoint of the glyph
 * @render_context: (type gpointer): Opaque rendering context
 * @x: X pixel position
 * @y: Y pixel position
 * @width: Cell width in pixels
 * @height: Cell height in pixels
 *
 * Dispatches a glyph transform to all #GstGlyphTransformer modules
 * registered at %GST_HOOK_GLYPH_TRANSFORM. Walks in priority order
 * and stops at the first handler that returns %TRUE (consumed).
 *
 * Returns: %TRUE if a module consumed (rendered) the glyph
 */
gboolean
gst_module_manager_dispatch_glyph_transform(
	GstModuleManager *self,
	gunichar          codepoint,
	gpointer          render_context,
	gint              x,
	gint              y,
	gint              width,
	gint              height
){
	GList *l;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), FALSE);

	for (l = self->hooks[GST_HOOK_GLYPH_TRANSFORM]; l != NULL; l = l->next)
	{
		GstHookEntry *entry;

		entry = (GstHookEntry *)l->data;

		if (!gst_module_is_active(entry->module))
		{
			continue;
		}

		if (GST_IS_GLYPH_TRANSFORMER(entry->module))
		{
			if (gst_glyph_transformer_transform_glyph(
				GST_GLYPH_TRANSFORMER(entry->module),
				codepoint, render_context,
				x, y, width, height))
			{
				return TRUE;
			}
		}
	}

	return FALSE;
}

/* ===== Public API: escape handler dispatch ===== */

/**
 * gst_module_manager_dispatch_escape_string:
 * @self: A #GstModuleManager
 * @str_type: The escape string type character ('_' for APC, 'P' for DCS)
 * @buf: The raw string buffer
 * @len: Length of the buffer in bytes
 * @terminal: (type gpointer): The #GstTerminal that received the sequence
 *
 * Dispatches a string-type escape sequence to all #GstEscapeHandler
 * modules registered at %GST_HOOK_ESCAPE_APC. Walks in priority order
 * and stops at the first handler that returns %TRUE (consumed).
 *
 * Returns: %TRUE if a module consumed the escape sequence
 */
gboolean
gst_module_manager_dispatch_escape_string(
	GstModuleManager *self,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GList *l;

	g_return_val_if_fail(GST_IS_MODULE_MANAGER(self), FALSE);

	for (l = self->hooks[GST_HOOK_ESCAPE_APC]; l != NULL; l = l->next)
	{
		GstHookEntry *entry;

		entry = (GstHookEntry *)l->data;

		if (!gst_module_is_active(entry->module))
		{
			continue;
		}

		if (GST_IS_ESCAPE_HANDLER(entry->module))
		{
			if (gst_escape_handler_handle_escape_string(
				GST_ESCAPE_HANDLER(entry->module),
				str_type, buf, len, terminal))
			{
				return TRUE;
			}
		}
	}

	return FALSE;
}

/* ===== Public API: config integration ===== */

/**
 * gst_module_manager_set_config:
 * @self: A #GstModuleManager
 * @config: (type gpointer): The #GstConfig to pass to modules
 *
 * Stores a weak reference to the configuration object.
 * This config is passed to each module's configure vfunc
 * when gst_module_manager_activate_all() is called.
 */
void
gst_module_manager_set_config(
	GstModuleManager *self,
	gpointer          config
){
	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	self->config = config;
}

/**
 * gst_module_manager_activate_all:
 * @self: A #GstModuleManager
 *
 * Iterates all registered modules, calls configure (if config is set),
 * then activates each module. Modules that fail to activate are logged
 * at warning level.
 */
void
gst_module_manager_activate_all(GstModuleManager *self)
{
	GHashTableIter iter;
	gpointer value;

	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	g_hash_table_iter_init(&iter, self->modules);
	while (g_hash_table_iter_next(&iter, NULL, &value))
	{
		GstModule *module;

		module = GST_MODULE(value);

		/* Configure before activating */
		if (self->config != NULL)
		{
			gst_module_configure(module, self->config);
		}

		/* Check if module is disabled by config */
		if (self->config != NULL)
		{
			YamlMapping *mod_cfg;

			mod_cfg = gst_config_get_module_config(
				(GstConfig *)self->config,
				gst_module_get_name(module));
			if (mod_cfg != NULL &&
				yaml_mapping_has_member(mod_cfg, "enabled") &&
				!yaml_mapping_get_boolean_member(mod_cfg, "enabled"))
			{
				g_debug("Module '%s' disabled by config",
					gst_module_get_name(module));
				continue;
			}
		}

		if (!gst_module_activate(module))
		{
			g_warning("Failed to activate module '%s'",
				gst_module_get_name(module));
		}
	}
}

/**
 * gst_module_manager_deactivate_all:
 * @self: A #GstModuleManager
 *
 * Deactivates all registered modules.
 */
void
gst_module_manager_deactivate_all(GstModuleManager *self)
{
	GHashTableIter iter;
	gpointer value;

	g_return_if_fail(GST_IS_MODULE_MANAGER(self));

	g_hash_table_iter_init(&iter, self->modules);
	while (g_hash_table_iter_next(&iter, NULL, &value))
	{
		gst_module_deactivate(GST_MODULE(value));
	}
}
