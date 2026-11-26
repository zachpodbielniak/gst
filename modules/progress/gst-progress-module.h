/*
 * gst-progress-module.h - OSC 9;4 progress in the window title
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shows ConEmu progress by prefixing the window title. Off by default.
 */

#ifndef GST_PROGRESS_MODULE_H
#define GST_PROGRESS_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"

G_BEGIN_DECLS

#define GST_TYPE_PROGRESS_MODULE (gst_progress_module_get_type())

G_DECLARE_FINAL_TYPE(GstProgressModule, gst_progress_module,
	GST, PROGRESS_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_PROGRESS_MODULE_H */
