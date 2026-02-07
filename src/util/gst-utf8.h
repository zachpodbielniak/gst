/*
 * gst-utf8.h - GST UTF-8 Utilities
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_UTF8_H
#define GST_UTF8_H

#include <glib.h>
#include "../gst-types.h"

G_BEGIN_DECLS

gint gst_utf8_encode(GstRune rune, gchar *buf);

GstRune gst_utf8_decode(const gchar *str, gint *len);

gint gst_wcwidth(GstRune rune);

gboolean gst_is_combining(GstRune rune);

G_END_DECLS

#endif /* GST_UTF8_H */
