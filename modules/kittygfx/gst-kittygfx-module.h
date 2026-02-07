/*
 * gst-kittygfx-module.h - Kitty graphics protocol module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the Kitty graphics protocol for displaying inline images
 * in the terminal. Handles APC escape sequences of the form:
 *   ESC_G <key>=<val>[,...];<base64_payload> ST
 *
 * Implements GstEscapeHandler (to receive APC sequences) and
 * GstRenderOverlay (to draw images on screen).
 */

#ifndef GST_KITTYGFX_MODULE_H
#define GST_KITTYGFX_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-escape-handler.h"
#include "../../src/interfaces/gst-render-overlay.h"

G_BEGIN_DECLS

#define GST_TYPE_KITTYGFX_MODULE (gst_kittygfx_module_get_type())

G_DECLARE_FINAL_TYPE(GstKittygfxModule, gst_kittygfx_module,
	GST, KITTYGFX_MODULE, GstModule)

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType of the kittygfx module
 * so the module manager can instantiate it.
 *
 * Returns: The #GType for #GstKittygfxModule
 */
G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_KITTYGFX_MODULE_H */
