/*
 * gst-base64.h - GST Base64 Utilities
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_BASE64_H
#define GST_BASE64_H

#include <glib.h>

G_BEGIN_DECLS

gchar *gst_base64_encode(const guchar *data, gsize len);

guchar *gst_base64_decode(const gchar *str, gsize *out_len);

G_END_DECLS

#endif /* GST_BASE64_H */
