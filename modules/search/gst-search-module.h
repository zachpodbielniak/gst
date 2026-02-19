/*
 * gst-search-module.h - Interactive scrollback text search module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Provides interactive text search through visible terminal content
 * with match highlighting and navigation. Activated via a configurable
 * keybind (default Ctrl+Shift+f), search mode intercepts all key input
 * for query entry and match navigation (Enter/Shift+Enter).
 */

#ifndef GST_SEARCH_MODULE_H
#define GST_SEARCH_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-input-handler.h"
#include "../../src/interfaces/gst-render-overlay.h"

G_BEGIN_DECLS

#define GST_TYPE_SEARCH_MODULE (gst_search_module_get_type())

G_DECLARE_FINAL_TYPE(GstSearchModule, gst_search_module,
	GST, SEARCH_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the search module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstSearchModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_SEARCH_MODULE_H */
