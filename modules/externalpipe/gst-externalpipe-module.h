/*
 * gst-externalpipe-module.h - External pipe module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Pipes visible terminal screen content to an external command
 * on keyboard shortcut.
 */

#ifndef GST_EXTERNALPIPE_MODULE_H
#define GST_EXTERNALPIPE_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-input-handler.h"
#include "../../src/interfaces/gst-external-pipe.h"

G_BEGIN_DECLS

#define GST_TYPE_EXTERNALPIPE_MODULE (gst_externalpipe_module_get_type())

G_DECLARE_FINAL_TYPE(GstExternalpipeModule, gst_externalpipe_module,
	GST, EXTERNALPIPE_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_EXTERNALPIPE_MODULE_H */
