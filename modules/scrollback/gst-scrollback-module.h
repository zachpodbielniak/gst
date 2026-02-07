/*
 * gst-scrollback-module.h - Scrollback buffer module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ring buffer scrollback with keyboard navigation and overlay rendering.
 */

#ifndef GST_SCROLLBACK_MODULE_H
#define GST_SCROLLBACK_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-input-handler.h"
#include "../../src/interfaces/gst-render-overlay.h"
#include "../../src/boxed/gst-glyph.h"

G_BEGIN_DECLS

#define GST_TYPE_SCROLLBACK_MODULE (gst_scrollback_module_get_type())

G_DECLARE_FINAL_TYPE(GstScrollbackModule, gst_scrollback_module,
	GST, SCROLLBACK_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

/**
 * gst_scrollback_module_get_count:
 * @self: A #GstScrollbackModule
 *
 * Gets the total number of lines stored in the scrollback buffer.
 *
 * Returns: the number of stored scrollback lines
 */
gint
gst_scrollback_module_get_count(GstScrollbackModule *self);

/**
 * gst_scrollback_module_get_scroll_offset:
 * @self: A #GstScrollbackModule
 *
 * Gets the current scroll offset. 0 means live view,
 * positive values mean viewing history.
 *
 * Returns: the current scroll offset
 */
gint
gst_scrollback_module_get_scroll_offset(GstScrollbackModule *self);

/**
 * gst_scrollback_module_set_scroll_offset:
 * @self: A #GstScrollbackModule
 * @offset: new scroll offset (clamped to [0, count])
 *
 * Sets the scroll position. Triggers a redraw if the
 * offset changed.
 */
void
gst_scrollback_module_set_scroll_offset(
	GstScrollbackModule *self,
	gint                 offset
);

/**
 * gst_scrollback_module_get_line_glyphs:
 * @self: A #GstScrollbackModule
 * @index: line index (0 = most recent, positive = older)
 * @cols_out: (out): number of columns in the returned line
 *
 * Gets the glyph data for a scrollback line. Index 0 is the
 * most recently scrolled-out line.
 *
 * Returns: (transfer none) (nullable): the glyph array, or %NULL
 */
const GstGlyph *
gst_scrollback_module_get_line_glyphs(
	GstScrollbackModule *self,
	gint                 index,
	gint                *cols_out
);

G_END_DECLS

#endif /* GST_SCROLLBACK_MODULE_H */
