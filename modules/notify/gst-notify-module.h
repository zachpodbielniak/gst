/*
 * gst-notify-module.h - Desktop notification module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sends desktop notifications on OSC 9/777/99 escape sequences.
 * Uses notify-send subprocess for delivery.
 */

#ifndef GST_NOTIFY_MODULE_H
#define GST_NOTIFY_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"

G_BEGIN_DECLS

#define GST_TYPE_NOTIFY_MODULE (gst_notify_module_get_type())

G_DECLARE_FINAL_TYPE(GstNotifyModule, gst_notify_module,
	GST, NOTIFY_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_NOTIFY_MODULE_H */
