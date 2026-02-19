/*
 * gst-osc52-module.h - Remote clipboard via OSC 52
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Handles OSC 52 escape sequences for remote clipboard access.
 * Allows programs running in the terminal to set the system
 * clipboard without direct access to the display server.
 */

#ifndef GST_OSC52_MODULE_H
#define GST_OSC52_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"

G_BEGIN_DECLS

#define GST_TYPE_OSC52_MODULE (gst_osc52_module_get_type())

G_DECLARE_FINAL_TYPE(GstOsc52Module, gst_osc52_module,
	GST, OSC52_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_OSC52_MODULE_H */
