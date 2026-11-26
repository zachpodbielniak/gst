/*
 * gst-osc7-module.h - OSC 7 working-directory reports
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Stores a local file:// directory from the shell. Off by default.
 */

#ifndef GST_OSC7_MODULE_H
#define GST_OSC7_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"

G_BEGIN_DECLS

#define GST_TYPE_OSC7_MODULE (gst_osc7_module_get_type())

G_DECLARE_FINAL_TYPE(GstOsc7Module, gst_osc7_module,
	GST, OSC7_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_OSC7_MODULE_H */
