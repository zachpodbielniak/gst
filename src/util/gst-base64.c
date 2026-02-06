/*
 * gst-base64.c - GST Base64 Utilities Implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-base64.h"

gchar *
gst_base64_encode(
    const guchar    *data,
    gsize           len
){
    return g_base64_encode(data, len);
}

guchar *
gst_base64_decode(
    const gchar *str,
    gsize       *out_len
){
    return g_base64_decode(str, out_len);
}
