/*
 * gst-dyncolors-module.h - Runtime color change module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Handles OSC 10/11/12 for querying and setting the terminal
 * foreground, background, and cursor color at runtime.
 * Also handles OSC 4 for palette color changes, OSC 104 for reset,
 * and OSC 110/111/112 to reset foreground, background, and cursor.
 */

#ifndef GST_DYNCOLORS_MODULE_H
#define GST_DYNCOLORS_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"

G_BEGIN_DECLS

#define GST_TYPE_DYNCOLORS_MODULE (gst_dyncolors_module_get_type())

G_DECLARE_FINAL_TYPE(GstDyncolorsModule, gst_dyncolors_module,
	GST, DYNCOLORS_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_DYNCOLORS_MODULE_H */
