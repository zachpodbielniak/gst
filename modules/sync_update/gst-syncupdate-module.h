/*
 * gst-syncupdate-module.h - Synchronized update (mode 2026) module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements DEC private mode 2026 (synchronized updates) to
 * eliminate flicker during rapid screen updates. When a program
 * sends CSI ? 2026 h, rendering is suppressed until CSI ? 2026 l
 * arrives (or a safety timeout fires), at which point a full
 * redraw is triggered.
 */

#ifndef GST_SYNCUPDATE_MODULE_H
#define GST_SYNCUPDATE_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"

G_BEGIN_DECLS

#define GST_TYPE_SYNCUPDATE_MODULE (gst_syncupdate_module_get_type())

G_DECLARE_FINAL_TYPE(GstSyncupdateModule, gst_syncupdate_module,
	GST, SYNCUPDATE_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the sync update module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstSyncupdateModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_SYNCUPDATE_MODULE_H */
