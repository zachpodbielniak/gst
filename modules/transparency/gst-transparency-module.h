/*
 * gst-transparency-module.h - Window transparency module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Controls window opacity via _NET_WM_WINDOW_OPACITY with focus tracking.
 */

#ifndef GST_TRANSPARENCY_MODULE_H
#define GST_TRANSPARENCY_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-render-overlay.h"

G_BEGIN_DECLS

#define GST_TYPE_TRANSPARENCY_MODULE (gst_transparency_module_get_type())

G_DECLARE_FINAL_TYPE(GstTransparencyModule, gst_transparency_module,
	GST, TRANSPARENCY_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_TRANSPARENCY_MODULE_H */
