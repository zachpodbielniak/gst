/*
 * gst-boxdraw-module.h - Box-drawing glyph transformer module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Renders Unicode box-drawing characters (U+2500-U+259F) using
 * X11 line/rectangle primitives for pixel-perfect alignment.
 */

#ifndef GST_BOXDRAW_MODULE_H
#define GST_BOXDRAW_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-glyph-transformer.h"

G_BEGIN_DECLS

#define GST_TYPE_BOXDRAW_MODULE (gst_boxdraw_module_get_type())

G_DECLARE_FINAL_TYPE(GstBoxdrawModule, gst_boxdraw_module,
	GST, BOXDRAW_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point.
 *
 * Returns: The #GType for #GstBoxdrawModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_BOXDRAW_MODULE_H */
