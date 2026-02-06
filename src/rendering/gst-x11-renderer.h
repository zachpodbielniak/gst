/*
 * gst-x11-renderer.h - X11 renderer implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_X11_RENDERER_H
#define GST_X11_RENDERER_H

#include <glib-object.h>
#include "gst-renderer.h"

G_BEGIN_DECLS

#define GST_TYPE_X11_RENDERER (gst_x11_renderer_get_type())

G_DECLARE_FINAL_TYPE(GstX11Renderer, gst_x11_renderer, GST, X11_RENDERER, GstRenderer)

GType
gst_x11_renderer_get_type(void) G_GNUC_CONST;

/**
 * gst_x11_renderer_new:
 *
 * Creates a new X11 renderer instance.
 *
 * Returns: (transfer full): A new #GstX11Renderer
 */
GstX11Renderer *
gst_x11_renderer_new(void);

G_END_DECLS

#endif /* GST_X11_RENDERER_H */
