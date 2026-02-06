/*
 * gst.h - GST (GObject Simple Terminal) Main Header
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This is the main umbrella header for the GST library.
 * Include this header to get access to all public GST APIs.
 *
 * GST is a GObject-based terminal emulator library with modular
 * extensibility through a plugin system.
 */

#ifndef GST_H
#define GST_H

#define GST_INSIDE

/* Core headers */
#include "gst-types.h"
#include "gst-enums.h"
#include "gst-version.h"

/* Boxed types */
#include "boxed/gst-glyph.h"
#include "boxed/gst-cursor.h"

/* Core classes */
#include "core/gst-line.h"
#include "core/gst-terminal.h"
#include "core/gst-pty.h"
#include "core/gst-escape-parser.h"

/* Rendering */
#include "rendering/gst-renderer.h"
#include "rendering/gst-x11-renderer.h"
#include "rendering/gst-font-cache.h"

/* Window */
#include "window/gst-window.h"
#include "window/gst-x11-window.h"

/* Configuration */
#include "config/gst-config.h"
#include "config/gst-color-scheme.h"

/* Module system */
#include "module/gst-module.h"
#include "module/gst-module-manager.h"
#include "module/gst-module-info.h"

/* Selection */
#include "selection/gst-selection.h"
#include "selection/gst-clipboard.h"

/* Interfaces */
#include "interfaces/gst-color-provider.h"
#include "interfaces/gst-input-handler.h"
#include "interfaces/gst-output-filter.h"
#include "interfaces/gst-render-overlay.h"
#include "interfaces/gst-font-provider.h"
#include "interfaces/gst-url-handler.h"
#include "interfaces/gst-glyph-transformer.h"
#include "interfaces/gst-bell-handler.h"
#include "interfaces/gst-external-pipe.h"

/* Utilities */
#include "util/gst-utf8.h"
#include "util/gst-base64.h"

#undef GST_INSIDE

G_BEGIN_DECLS

/**
 * gst_init:
 * @argc: (inout) (optional): address of argc (from main)
 * @argv: (inout) (array length=argc) (optional): address of argv (from main)
 *
 * Initializes the GST library. This function must be called before
 * using any GST functions. It will initialize GLib and other
 * dependencies.
 *
 * This function is idempotent - calling it multiple times has no effect.
 */
void gst_init(int *argc, char ***argv);

/**
 * gst_init_check:
 * @argc: (inout) (optional): address of argc (from main)
 * @argv: (inout) (array length=argc) (optional): address of argv (from main)
 * @error: (out) (optional): location to store error
 *
 * Initializes the GST library, returning %FALSE on failure.
 * Unlike gst_init(), this function does not call exit() on failure.
 *
 * Returns: %TRUE if initialization succeeded, %FALSE on failure
 */
gboolean gst_init_check(int *argc, char ***argv, GError **error);

/**
 * gst_is_initialized:
 *
 * Checks if GST has been initialized.
 *
 * Returns: %TRUE if GST has been initialized
 */
gboolean gst_is_initialized(void);

G_END_DECLS

#endif /* GST_H */
