/*
 * gst-config-compiler.h - C configuration compiler
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compiles a user-written C configuration file into a shared object
 * and loads it at runtime.  Uses the crispy library for compilation
 * and content-hash caching (SHA256).  The config source may define
 * CRISPY_PARAMS to pass extra compiler flags.  The compiled .so
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
 * @error: (nullable): return location for a #GError
 *
 * Creates a new #GstConfigCompiler backed by the crispy library.
 * Probes gcc for its version and caches pkg-config output for
 * the default GLib/GObject/GIO libraries.  Uses SHA256 content-
 * hash caching in $XDG_CACHE_HOME/gst.
 *
 * Returns: (transfer full) (nullable): a new #GstConfigCompiler,
 *          or %NULL if gcc is not found
 */
GstConfigCompiler *
gst_config_compiler_new(GError **error);

/**
 * gst_config_compiler_find_config:
 * @self: a #GstConfigCompiler
 *
 * Searches standard paths for a C config file.
 *
 * Search order:
 *  1. $XDG_CONFIG_HOME/gst/config.c
 *  2. SYSCONFDIR/gst/config.c
 *  3. DATADIR/gst/config.c
 *  4. ./data/config.c (development fallback)
 *
 * Returns: (transfer full) (nullable): path to the config.c,
 *          or %NULL if none found.  Free with g_free().
 */
gchar *
gst_config_compiler_find_config(GstConfigCompiler *self);

/**
 * gst_config_compiler_compile:
 * @self: a #GstConfigCompiler
 * @source_path: path to the C configuration source file
 * @force: if %TRUE, bypass cache and force recompilation
 * @error: (nullable): return location for a #GError
 *
 * Reads the source file, scans for an optional CRISPY_PARAMS
 * define, computes a SHA256 content hash, and compiles to a
 * shared object if no valid cached artifact exists (or if
 * @force is %TRUE).
 *
 * Returns: (transfer full) (nullable): path to the compiled .so,
 *          or %NULL on error.  Free with g_free().
 */
gchar *
gst_config_compiler_compile(
	GstConfigCompiler  *self,
	const gchar        *source_path,
	gboolean            force,
	GError            **error
);

/**
 * gst_config_compiler_load_and_apply:
 * @self: a #GstConfigCompiler
 * @so_path: path to the compiled shared object
 * @error: (nullable): return location for a #GError
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
