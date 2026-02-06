/*
 * gst-clipboard.h - Clipboard integration
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_CLIPBOARD_H
#define GST_CLIPBOARD_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_CLIPBOARD (gst_clipboard_get_type())

G_DECLARE_FINAL_TYPE(GstClipboard, gst_clipboard, GST, CLIPBOARD, GObject)

GType
gst_clipboard_get_type(void) G_GNUC_CONST;

/**
 * gst_clipboard_new:
 *
 * Creates a new clipboard handler.
 *
 * Returns: (transfer full): A new #GstClipboard
 */
GstClipboard *
gst_clipboard_new(void);

/**
 * gst_clipboard_get_default:
 *
 * Gets the default shared clipboard instance.
 *
 * Returns: (transfer none): The default #GstClipboard
 */
GstClipboard *
gst_clipboard_get_default(void);

/**
 * gst_clipboard_copy:
 * @self: A #GstClipboard
 * @text: The text to copy
 *
 * Copies text to the clipboard.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_clipboard_copy(
	GstClipboard *self,
	const gchar  *text
);

/**
 * gst_clipboard_paste:
 * @self: A #GstClipboard
 *
 * Gets the current clipboard contents.
 *
 * Returns: (transfer full) (nullable): The clipboard text, or %NULL
 */
gchar *
gst_clipboard_paste(GstClipboard *self);

/**
 * gst_clipboard_clear:
 * @self: A #GstClipboard
 *
 * Clears the clipboard contents.
 */
void
gst_clipboard_clear(GstClipboard *self);

G_END_DECLS

#endif /* GST_CLIPBOARD_H */
