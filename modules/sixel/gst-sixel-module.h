/*
 * gst-sixel-module.h - DEC Sixel graphics protocol module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the DEC Sixel graphics protocol for displaying inline
 * images in the terminal. Handles DCS escape sequences of the form:
 *   ESC P <params> q <sixel-data> ESC \
 *
 * Implements GstEscapeHandler (to receive DCS sequences) and
 * GstRenderOverlay (to draw decoded images on screen).
 */

#ifndef GST_SIXEL_MODULE_H
#define GST_SIXEL_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"
#include "../../src/interfaces/gst-render-overlay.h"

G_BEGIN_DECLS

#define GST_TYPE_SIXEL_MODULE (gst_sixel_module_get_type())

G_DECLARE_FINAL_TYPE(GstSixelModule, gst_sixel_module,
	GST, SIXEL_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the sixel module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstSixelModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_SIXEL_MODULE_H */
