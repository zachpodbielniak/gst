/*
 * gst-module-manager.h - Module lifecycle management and hook dispatch
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_MODULE_MANAGER_H
#define GST_MODULE_MANAGER_H

#include <glib-object.h>
#include <gmodule.h>
#include "gst-module.h"
#include "gst-module-info.h"
#include "../gst-enums.h"

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
 * Registers a module with the manager. Automatically detects
 * which interfaces the module implements and registers hooks
 * for each detected interface at the module's current priority.
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
 * Unregisters a module by name. Deactivates the module and
 * removes all of its hook registrations.
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

/* ===== Hook Registration ===== */

/**
 * gst_module_manager_register_hook:
 * @self: A #GstModuleManager
 * @module: The module registering the hook
 * @hook_point: The hook point to register at
 * @priority: Dispatch priority (lower runs first)
 *
 * Manually registers a module for a specific hook point.
 * Normally hooks are auto-detected from interfaces during
 * gst_module_manager_register(), but this allows manual
 * registration for custom hook points.
 */
void
gst_module_manager_register_hook(
	GstModuleManager *self,
	GstModule        *module,
	GstHookPoint      hook_point,
	gint               priority
);

/**
 * gst_module_manager_unregister_hooks:
 * @self: A #GstModuleManager
 * @module: The module whose hooks to remove
 *
 * Removes all hook registrations for the given module.
 */
void
gst_module_manager_unregister_hooks(
	GstModuleManager *self,
	GstModule        *module
);

/* ===== Hook Dispatch ===== */

/**
 * gst_module_manager_dispatch_hook:
 * @self: A #GstModuleManager
 * @hook_point: The hook point to dispatch
 * @event_data: (nullable): Opaque event data passed to handlers
 *
 * Dispatches a generic hook to all registered modules in priority order.
 * For consumable hooks (input), stops when a handler returns %TRUE.
 *
 * Returns: %TRUE if any handler consumed the event
 */
gboolean
gst_module_manager_dispatch_hook(
	GstModuleManager *self,
	GstHookPoint      hook_point,
	gpointer          event_data
);

/**
 * gst_module_manager_dispatch_key_event:
 * @self: A #GstModuleManager
 * @keyval: The key value
 * @keycode: The hardware keycode
 * @state: The modifier state
 *
 * Dispatches a key event to all #GstInputHandler modules.
 * Stops at the first handler that returns %TRUE (consumed).
 *
 * Returns: %TRUE if a module consumed the key event
 */
gboolean
gst_module_manager_dispatch_key_event(
	GstModuleManager *self,
	guint             keyval,
	guint             keycode,
	guint             state
);

/**
 * gst_module_manager_dispatch_mouse_event:
 * @self: A #GstModuleManager
 * @button: The mouse button number (1-9, 4/5 for scroll)
 * @state: The modifier state
 * @col: The terminal column at the click position
 * @row: The terminal row at the click position
 *
 * Dispatches a mouse event to all #GstInputHandler modules.
 * Stops at the first handler that returns %TRUE (consumed).
 *
 * Returns: %TRUE if a module consumed the mouse event
 */
gboolean
gst_module_manager_dispatch_mouse_event(
	GstModuleManager *self,
	guint             button,
	guint             state,
	gint              col,
	gint              row
);

/**
 * gst_module_manager_dispatch_bell:
 * @self: A #GstModuleManager
 *
 * Dispatches a bell event to all #GstBellHandler modules.
 * All registered bell handlers are called (non-consumable).
 */
void
gst_module_manager_dispatch_bell(GstModuleManager *self);

/**
 * gst_module_manager_dispatch_render_overlay:
 * @self: A #GstModuleManager
 * @render_context: (type gpointer): Opaque rendering context
 * @width: Width of the render area in pixels
 * @height: Height of the render area in pixels
 *
 * Dispatches a render overlay event to all #GstRenderOverlay modules.
 * All registered overlay renderers are called (non-consumable).
 */
void
gst_module_manager_dispatch_render_overlay(
	GstModuleManager *self,
	gpointer          render_context,
	gint              width,
	gint              height
);

/**
 * gst_module_manager_dispatch_escape_string:
 * @self: A #GstModuleManager
 * @str_type: The escape string type character ('_' for APC, 'P' for DCS)
 * @buf: The raw string buffer
 * @len: Length of the buffer in bytes
 * @terminal: (type gpointer): The #GstTerminal that received the sequence
 *
 * Dispatches a string-type escape sequence to #GstEscapeHandler modules.
 * Stops at the first handler that returns %TRUE (consumed).
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
);

/* ===== Module Loading ===== */

/**
 * gst_module_manager_load_module:
 * @self: A #GstModuleManager
 * @path: Path to the .so module file
 * @error: (out) (optional): Location to store a #GError on failure
 *
 * Loads a module from a shared object file. The .so must export
 * a `gst_module_register` function that returns a #GType.
 * The module is instantiated, registered, and its hooks are
 * auto-detected from implemented interfaces.
 *
 * Returns: (transfer none) (nullable): The loaded module, or %NULL on error
 */
GstModule *
gst_module_manager_load_module(
	GstModuleManager *self,
	const gchar      *path,
	GError          **error
);

/**
 * gst_module_manager_load_from_directory:
 * @self: A #GstModuleManager
 * @dir_path: Path to a directory containing .so module files
 *
 * Scans a directory for .so files and loads each as a module.
 * Silently skips files that fail to load.
 *
 * Returns: The number of modules successfully loaded
 */
guint
gst_module_manager_load_from_directory(
	GstModuleManager *self,
	const gchar      *dir_path
);

/* ===== Object Accessors ===== */

/**
 * gst_module_manager_set_terminal:
 * @self: A #GstModuleManager
 * @terminal: (type gpointer): The terminal instance (weak ref)
 *
 * Stores a weak reference to the terminal so modules can
 * access it via gst_module_manager_get_terminal().
 */
void
gst_module_manager_set_terminal(
	GstModuleManager *self,
	gpointer          terminal
);

/**
 * gst_module_manager_get_terminal:
 * @self: A #GstModuleManager
 *
 * Gets the stored terminal reference.
 *
 * Returns: (transfer none) (nullable): The terminal, or %NULL
 */
gpointer
gst_module_manager_get_terminal(GstModuleManager *self);

/**
 * gst_module_manager_set_window:
 * @self: A #GstModuleManager
 * @window: (type gpointer): The window instance (weak ref)
 *
 * Stores a weak reference to the window so modules can
 * access it via gst_module_manager_get_window().
 */
void
gst_module_manager_set_window(
	GstModuleManager *self,
	gpointer          window
);

/**
 * gst_module_manager_get_window:
 * @self: A #GstModuleManager
 *
 * Gets the stored window reference.
 *
 * Returns: (transfer none) (nullable): The window, or %NULL
 */
gpointer
gst_module_manager_get_window(GstModuleManager *self);

/**
 * gst_module_manager_set_font_cache:
 * @self: A #GstModuleManager
 * @font_cache: (type gpointer): The font cache instance (weak ref)
 *
 * Stores a weak reference to the font cache (GstFontCache or
 * GstCairoFontCache) so modules can access it.
 */
void
gst_module_manager_set_font_cache(
	GstModuleManager *self,
	gpointer          font_cache
);

/**
 * gst_module_manager_get_font_cache:
 * @self: A #GstModuleManager
 *
 * Gets the stored font cache reference.
 *
 * Returns: (transfer none) (nullable): The font cache, or %NULL
 */
gpointer
gst_module_manager_get_font_cache(GstModuleManager *self);

/**
 * gst_module_manager_set_pty:
 * @self: A #GstModuleManager
 * @pty: (type gpointer): The PTY instance (weak ref)
 *
 * Stores a weak reference to the PTY so modules can
 * access it via gst_module_manager_get_pty().
 */
void
gst_module_manager_set_pty(
	GstModuleManager *self,
	gpointer          pty
);

/**
 * gst_module_manager_get_pty:
 * @self: A #GstModuleManager
 *
 * Gets the stored PTY reference.
 *
 * Returns: (transfer none) (nullable): The PTY, or %NULL
 */
gpointer
gst_module_manager_get_pty(GstModuleManager *self);

/**
 * gst_module_manager_set_renderer:
 * @self: A #GstModuleManager
 * @renderer: (type gpointer): The renderer instance (weak ref)
 *
 * Stores a weak reference to the renderer so modules can
 * access it via gst_module_manager_get_renderer().
 */
void
gst_module_manager_set_renderer(
	GstModuleManager *self,
	gpointer          renderer
);

/**
 * gst_module_manager_get_renderer:
 * @self: A #GstModuleManager
 *
 * Gets the stored renderer reference.
 *
 * Returns: (transfer none) (nullable): The renderer, or %NULL
 */
gpointer
gst_module_manager_get_renderer(GstModuleManager *self);

/**
 * gst_module_manager_set_color_scheme:
 * @self: A #GstModuleManager
 * @color_scheme: (type gpointer): The color scheme instance (weak ref)
 *
 * Stores a weak reference to the color scheme so modules can
 * modify palette colors at runtime (e.g. OSC 10/11/12).
 */
void
gst_module_manager_set_color_scheme(
	GstModuleManager *self,
	gpointer          color_scheme
);

/**
 * gst_module_manager_get_color_scheme:
 * @self: A #GstModuleManager
 *
 * Gets the stored color scheme reference.
 *
 * Returns: (transfer none) (nullable): The color scheme, or %NULL
 */
gpointer
gst_module_manager_get_color_scheme(GstModuleManager *self);

/**
 * gst_module_manager_set_backend_type:
 * @self: A #GstModuleManager
 * @backend_type: The active rendering backend type
 *
 * Stores the active backend type so modules can determine
 * which font cache API to use.
 */
void
gst_module_manager_set_backend_type(
	GstModuleManager *self,
	gint              backend_type
);

/**
 * gst_module_manager_get_backend_type:
 * @self: A #GstModuleManager
 *
 * Gets the stored backend type.
 *
 * Returns: The #GstBackendType value
 */
gint
gst_module_manager_get_backend_type(GstModuleManager *self);

/* ===== Selection Handler Dispatch ===== */

/**
 * gst_module_manager_dispatch_selection_done:
 * @self: A #GstModuleManager
 * @text: The selected text as a UTF-8 string
 * @len: Length of @text in bytes
 *
 * Dispatches a selection-done event to all #GstSelectionHandler modules.
 * All registered selection handlers are called (non-consumable).
 */
void
gst_module_manager_dispatch_selection_done(
	GstModuleManager *self,
	const gchar      *text,
	gint              len
);

/* ===== Glyph Transform Dispatch ===== */

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
 * Dispatches a glyph transform to all #GstGlyphTransformer modules.
 * Stops at the first handler that returns %TRUE (consumed the glyph).
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
);

/* ===== Config Integration ===== */

/**
 * gst_module_manager_set_config:
 * @self: A #GstModuleManager
 * @config: (type gpointer): The #GstConfig to pass to modules
 *
 * Sets the configuration object. When modules are activated,
 * their configure vfunc is called with this config.
 */
void
gst_module_manager_set_config(
	GstModuleManager *self,
	gpointer          config
);

/**
 * gst_module_manager_activate_all:
 * @self: A #GstModuleManager
 *
 * Activates all registered modules. Calls configure with the
 * stored config (if set) before activating each module.
 */
void
gst_module_manager_activate_all(GstModuleManager *self);

/**
 * gst_module_manager_deactivate_all:
 * @self: A #GstModuleManager
 *
 * Deactivates all registered modules.
 */
void
gst_module_manager_deactivate_all(GstModuleManager *self);

G_END_DECLS

#endif /* GST_MODULE_MANAGER_H */
