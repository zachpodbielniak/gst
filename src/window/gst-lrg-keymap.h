/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef GST_LRG_KEYMAP_H
#define GST_LRG_KEYMAP_H

#include <glib.h>
#include <string.h>

/* Raylib printable key identifiers use the US physical ASCII positions.
 * Ctrl/Alt paths bypass its character queue, so apply Shift ourselves there. */
static inline guint
gst_lrg_shift_ascii(
	guint base
){
	const gchar *plain = "1234567890-=[]\\;',./`";
	const gchar *shifted = "!@#$%^&*()_+{}|:\"<>?~";
	const gchar *match;

	if (base >= 'a' && base <= 'z')
		return base - 'a' + 'A';
	if (base == 0 || base > 0x7f)
		return base;
	match = strchr(plain, (gchar)base);
	return match != NULL ? (guint)shifted[match - plain] : base;
}

#endif /* GST_LRG_KEYMAP_H */
