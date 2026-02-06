/*
 * gst-utf8.c - GST UTF-8 Utilities Implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-utf8.h"
#include <string.h>

gint
gst_utf8_encode(
    GstRune rune,
    gchar   *buf
){
    return g_unichar_to_utf8((gunichar)rune, buf);
}

GstRune
gst_utf8_decode(
    const gchar *str,
    gint        *len
){
    gunichar c;
    const gchar *next;

    c = g_utf8_get_char_validated(str, -1);
    if (c == (gunichar)-1 || c == (gunichar)-2) {
        if (len) *len = 1;
        return 0xFFFD;  /* Replacement character */
    }

    next = g_utf8_next_char(str);
    if (len) *len = next - str;

    return (GstRune)c;
}

gint
gst_wcwidth(GstRune rune)
{
    if (rune == 0) {
        return 0;
    }

    if (rune < 32 || (rune >= 0x7f && rune < 0xa0)) {
        return -1;
    }

    if (g_unichar_iswide((gunichar)rune)) {
        return 2;
    }

    return 1;
}

gboolean
gst_is_combining(GstRune rune)
{
    GUnicodeType type = g_unichar_type((gunichar)rune);

    return (type == G_UNICODE_COMBINING_MARK ||
            type == G_UNICODE_ENCLOSING_MARK ||
            type == G_UNICODE_NON_SPACING_MARK);
}
