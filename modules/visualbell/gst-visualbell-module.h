/*
 * gst-visualbell-module.h - Visual bell notification module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sample module that handles terminal bell events with a visual
 * notification instead of (or in addition to) the default X11 urgency hint.
 */

#ifndef GST_VISUALBELL_MODULE_H
#define GST_VISUALBELL_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-bell-handler.h"

G_BEGIN_DECLS

#define GST_TYPE_VISUALBELL_MODULE (gst_visualbell_module_get_type())

G_DECLARE_FINAL_TYPE(GstVisualbellModule, gst_visualbell_module,
	GST, VISUALBELL_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the visual bell module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstVisualbellModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_VISUALBELL_MODULE_H */
