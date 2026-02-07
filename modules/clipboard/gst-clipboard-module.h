/*
 * gst-clipboard-module.h - Automatic clipboard sync module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Automatically copies PRIMARY selection to CLIPBOARD when text
 * is selected, so clipboard managers and Ctrl+V paste see it.
 */

#ifndef GST_CLIPBOARD_MODULE_H
#define GST_CLIPBOARD_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"

G_BEGIN_DECLS

#define GST_TYPE_CLIPBOARD_MODULE (gst_clipboard_module_get_type())

G_DECLARE_FINAL_TYPE(GstClipboardModule, gst_clipboard_module,
	GST, CLIPBOARD_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the clipboard module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstClipboardModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_CLIPBOARD_MODULE_H */
