/*
 * gst-scrollback-module.h - Scrollback buffer module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ring buffer scrollback with keyboard navigation and overlay rendering.
 */

#ifndef GST_SCROLLBACK_MODULE_H
#define GST_SCROLLBACK_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-input-handler.h"
#include "../../src/interfaces/gst-render-overlay.h"

G_BEGIN_DECLS

#define GST_TYPE_SCROLLBACK_MODULE (gst_scrollback_module_get_type())

G_DECLARE_FINAL_TYPE(GstScrollbackModule, gst_scrollback_module,
	GST, SCROLLBACK_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_SCROLLBACK_MODULE_H */
