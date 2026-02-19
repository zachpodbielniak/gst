/*
 * gst-ligatures-module.h - Font ligature rendering module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Uses HarfBuzz to shape runs of glyphs and render font ligatures
 * (e.g. "calt", "liga") via the abstract render context's
 * draw_glyph_id() vtable entry.
 */

#ifndef GST_LIGATURES_MODULE_H
#define GST_LIGATURES_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-glyph-transformer.h"

G_BEGIN_DECLS

#define GST_TYPE_LIGATURES_MODULE (gst_ligatures_module_get_type())

G_DECLARE_FINAL_TYPE(GstLigaturesModule, gst_ligatures_module,
	GST, LIGATURES_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the ligatures module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstLigaturesModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_LIGATURES_MODULE_H */
