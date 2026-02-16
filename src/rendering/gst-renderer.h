/*
 * gst-renderer.h - Abstract base renderer class
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for terminal renderers. Defines the virtual
 * method table that backends (X11, Wayland, etc.) implement.
 * The renderer receives a terminal reference at construction and
 * uses dirty-line tracking for efficient redraws.
 */

#ifndef GST_RENDERER_H
#define GST_RENDERER_H

#include <glib-object.h>
#include "../gst-types.h"

G_BEGIN_DECLS

#define GST_TYPE_RENDERER (gst_renderer_get_type())

G_DECLARE_DERIVABLE_TYPE(GstRenderer, gst_renderer, GST, RENDERER, GObject)

/**
 * GstRendererClass:
 * @parent_class: The parent class
 * @render: Perform a full rendering pass (iterate dirty lines, draw cursor, flip)
 * @resize: Handle window resize (recreate buffers, update metrics)
 * @clear: Clear the render surface
 * @draw_line: Draw a single line (row) from x1 to x2
 * @draw_cursor: Draw cursor at (cx,cy), erasing old cursor at (ox,oy)
 * @start_draw: Begin a drawing batch (check Xft readiness, etc.)
 * @finish_draw: End a drawing batch (flush, copy buffer to window)
 *
 * The class structure for #GstRenderer.
 */
struct _GstRendererClass
{
	GObjectClass parent_class;

	/* Virtual methods */
	void     (*render)     (GstRenderer *self);
	void     (*resize)     (GstRenderer *self,
	                        guint        width,
	                        guint        height);
	void     (*clear)      (GstRenderer *self);
	void     (*draw_line)  (GstRenderer *self,
	                        gint         row,
	                        gint         x1,
	                        gint         x2);
	void     (*draw_cursor)(GstRenderer *self,
	                        gint         cx,
	                        gint         cy,
	                        gint         ox,
	                        gint         oy);
	gboolean (*start_draw) (GstRenderer *self);
	void     (*finish_draw)(GstRenderer *self);

	/* Screenshot capture: returns RGBA pixel data */
	GBytes * (*capture_screenshot)(GstRenderer *self,
	                               gint        *out_width,
	                               gint        *out_height,
	                               gint        *out_stride);

	/* Padding for future expansion */
	gpointer padding[3];
};

GType
gst_renderer_get_type(void) G_GNUC_CONST;

void
gst_renderer_render(GstRenderer *self);

void
gst_renderer_resize(
	GstRenderer *self,
	guint        width,
	guint        height
);

void
gst_renderer_clear(GstRenderer *self);

void
gst_renderer_draw_line(
	GstRenderer *self,
	gint         row,
	gint         x1,
	gint         x2
);

void
gst_renderer_draw_cursor(
	GstRenderer *self,
	gint         cx,
	gint         cy,
	gint         ox,
	gint         oy
);

gboolean
gst_renderer_start_draw(GstRenderer *self);

void
gst_renderer_finish_draw(GstRenderer *self);

/**
 * gst_renderer_capture_screenshot:
 * @self: A #GstRenderer
 * @out_width: (out) (optional): location to store the image width
 * @out_height: (out) (optional): location to store the image height
 * @out_stride: (out) (optional): location to store the row stride in bytes
 *
 * Captures the current terminal display as raw RGBA pixel data.
 * Each backend converts from its native pixel format to RGBA.
 *
 * Returns: (transfer full) (nullable): a #GBytes containing RGBA pixels,
 *     or %NULL if capture is not supported
 */
GBytes *
gst_renderer_capture_screenshot(
	GstRenderer *self,
	gint        *out_width,
	gint        *out_height,
	gint        *out_stride
);

/**
 * gst_renderer_get_terminal:
 * @self: A #GstRenderer
 *
 * Gets the terminal associated with this renderer.
 *
 * Returns: (transfer none) (nullable): the terminal
 */
GstTerminal *
gst_renderer_get_terminal(GstRenderer *self);

G_END_DECLS

#endif /* GST_RENDERER_H */
