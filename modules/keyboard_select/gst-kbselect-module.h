/*
 * gst-kbselect-module.h - Vim-like keyboard selection module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Vim-like modal editing for the terminal: navigate, visually
 * select, search, and yank text from the terminal screen and
 * scrollback. Ports st's keyboard_select patch behavior.
 */

#ifndef GST_KBSELECT_MODULE_H
#define GST_KBSELECT_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-input-handler.h"
#include "../../src/interfaces/gst-render-overlay.h"

G_BEGIN_DECLS

#define GST_TYPE_KBSELECT_MODULE (gst_kbselect_module_get_type())

G_DECLARE_FINAL_TYPE(GstKbselectModule, gst_kbselect_module,
	GST, KBSELECT_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the keyboard_select module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstKbselectModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_KBSELECT_MODULE_H */
