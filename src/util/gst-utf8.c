/*
 * gst-utf8.c - GST UTF-8 Utilities Implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _XOPEN_SOURCE 700
#include "gst-utf8.h"
#include <string.h>
#include <wchar.h>

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

/*
 * gst_wcwidth:
 * @rune: a Unicode codepoint
 *
 * Returns the display width of a Unicode character.
 * Uses libc wcwidth() when locale is set (matching st/tmux behavior).
 * Falls back to GLib Unicode properties when wcwidth() returns -1
 * for printable chars (e.g., in C locale). This ensures correct
 * behavior regardless of locale.
 *
 * Key: uses g_unichar_iswide() (NOT g_unichar_iswide_cjk()) so
 * ambiguous-width chars like PUA/Powerline symbols are width 1.
 *
 * Returns: 0 for combining/zero-width, 1 for normal, 2 for wide,
 *          -1 for non-printable control characters
 */
gint
gst_wcwidth(GstRune rune)
{
    gint w;

    w = wcwidth((wchar_t)rune);
    if (w >= 0) {
        return w;
    }

    /*
     * wcwidth returned -1. This happens for control chars or
     * when the locale doesn't support the character (e.g., C locale).
     * Fall back to GLib's locale-independent Unicode properties.
     */
    if (rune == 0) {
        return 0;
    }
    if (rune < 32 || (rune >= 0x7f && rune < 0xa0)) {
        return -1;
    }

    /* Combining marks: zero width */
    if (gst_is_combining(rune)) {
        return 0;
    }

    /* Wide chars (CJK ideographs, etc.) */
    if (g_unichar_iswide((gunichar)rune)) {
        return 2;
    }

    return 1;
}

gboolean
gst_is_combining(GstRune rune)
{
    GUnicodeType type = g_unichar_type((gunichar)rune);

    return (type == G_UNICODE_SPACING_MARK ||
            type == G_UNICODE_ENCLOSING_MARK ||
            type == G_UNICODE_NON_SPACING_MARK);
}
