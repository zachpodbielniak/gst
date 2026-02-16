/*
 * gst-config-compiler.h - C configuration compiler
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compiles a user-written C configuration file into a shared object
 * and loads it at runtime. The config source may define
 * GST_BUILD_ARGS to pass extra compiler flags. The compiled .so
 * must export a `gst_config_init` symbol.
 *
 * Search path for config.c:
 *  1. --c-config PATH (explicit override)
 *  2. $XDG_CONFIG_HOME/gst/config.c (~/.config/gst/config.c)
 *  3. SYSCONFDIR/gst/config.c (/etc/gst/config.c)
 *  4. DATADIR/gst/config.c (/usr/share/gst/config.c)
 *  5. ./data/config.c (development fallback)
 */

#ifndef GST_CONFIG_COMPILER_H
#define GST_CONFIG_COMPILER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_CONFIG_COMPILER (gst_config_compiler_get_type())

G_DECLARE_FINAL_TYPE(GstConfigCompiler, gst_config_compiler,
                     GST, CONFIG_COMPILER, GObject)

/**
 * gst_config_compiler_new:
 *
 * Creates a new #GstConfigCompiler. Locates gcc via
 * g_find_program_in_path() and sets the cache directory
 * to $XDG_CACHE_HOME/gst.
 *
 * Returns: (transfer full): A new #GstConfigCompiler
 */
GstConfigCompiler *
gst_config_compiler_new(void);

/**
 * gst_config_compiler_find_config:
 * @self: A #GstConfigCompiler
 *
 * Searches standard paths for a C config file.
 *
 * Search order:
 *  1. $XDG_CONFIG_HOME/gst/config.c
 *  2. SYSCONFDIR/gst/config.c
 *  3. DATADIR/gst/config.c
 *  4. ./data/config.c (development fallback)
 *
 * Returns: (transfer full) (nullable): Path to the config.c,
 *          or %NULL if none found. Free with g_free().
 */
gchar *
gst_config_compiler_find_config(GstConfigCompiler *self);

/**
 * gst_config_compiler_compile:
 * @self: A #GstConfigCompiler
 * @source_path: Path to the C configuration source file
 * @output_path: Path where the compiled .so should be written
 * @error: (nullable): Return location for a #GError
 *
 * Reads the source file, scans for optional GST_BUILD_ARGS,
 * and invokes gcc to compile the source into a shared object.
 * Links against glib-2.0, gobject-2.0, and gmodule-2.0.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
gst_config_compiler_compile(
	GstConfigCompiler  *self,
	const gchar        *source_path,
	const gchar        *output_path,
	GError            **error
);

/**
 * gst_config_compiler_get_cache_path:
 * @self: A #GstConfigCompiler
 *
 * Returns the default path for the compiled config .so.
 * Creates the cache directory if it does not exist.
 *
 * Returns: (transfer full): Path string; free with g_free()
 */
gchar *
gst_config_compiler_get_cache_path(GstConfigCompiler *self);

/**
 * gst_config_compiler_load_and_apply:
 * @self: A #GstConfigCompiler
 * @so_path: Path to the compiled shared object
 * @error: (nullable): Return location for a #GError
 *
 * Opens the .so, looks up `gst_config_init`, and calls it.
 * The init function must have signature:
 *   G_MODULE_EXPORT gboolean gst_config_init(void);
 *
 * Returns: %TRUE if loaded and applied successfully
 */
gboolean
gst_config_compiler_load_and_apply(
	GstConfigCompiler  *self,
	const gchar        *so_path,
	GError            **error
);

G_END_DECLS

#endif /* GST_CONFIG_COMPILER_H */
