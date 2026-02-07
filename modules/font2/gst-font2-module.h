/*
 * gst-font2-module.h - Spare/fallback font loading module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Pre-loads fallback fonts (e.g., Nerd Fonts, emoji) into the
 * font ring cache so they are tried before fontconfig's slow
 * system-wide search. Ports st's font2 patch behavior.
 */

#ifndef GST_FONT2_MODULE_H
#define GST_FONT2_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"

G_BEGIN_DECLS

#define GST_TYPE_FONT2_MODULE (gst_font2_module_get_type())

G_DECLARE_FINAL_TYPE(GstFont2Module, gst_font2_module,
	GST, FONT2_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the font2 module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstFont2Module
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_FONT2_MODULE_H */
