/*
 * gst-urlclick-module.h - URL detection and opening module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Detects URLs in terminal output and opens them on keyboard shortcut.
 */

#ifndef GST_URLCLICK_MODULE_H
#define GST_URLCLICK_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-input-handler.h"
#include "../../src/interfaces/gst-url-handler.h"

G_BEGIN_DECLS

#define GST_TYPE_URLCLICK_MODULE (gst_urlclick_module_get_type())

G_DECLARE_FINAL_TYPE(GstUrlclickModule, gst_urlclick_module,
	GST, URLCLICK_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_URLCLICK_MODULE_H */
