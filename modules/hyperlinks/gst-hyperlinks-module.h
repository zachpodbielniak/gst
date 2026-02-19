/*
 * gst-hyperlinks-module.h - OSC 8 explicit hyperlink module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements OSC 8 explicit hyperlinks with Ctrl+click to open.
 * Tracks URI spans across the terminal screen, underlines hovered
 * spans, and opens the target URI via a configurable opener command.
 */

#ifndef GST_HYPERLINKS_MODULE_H
#define GST_HYPERLINKS_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"
#include "../../src/interfaces/gst-input-handler.h"
#include "../../src/interfaces/gst-render-overlay.h"

G_BEGIN_DECLS

#define GST_TYPE_HYPERLINKS_MODULE (gst_hyperlinks_module_get_type())

G_DECLARE_FINAL_TYPE(GstHyperlinksModule, gst_hyperlinks_module,
	GST, HYPERLINKS_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the hyperlinks module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstHyperlinksModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_HYPERLINKS_MODULE_H */
