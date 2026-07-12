/*
 * gst-mouse.h - GST Mouse Report Encoding
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GST_MOUSE_H
#define GST_MOUSE_H

#include <glib.h>

G_BEGIN_DECLS

gssize
gst_mouse_encode_report(
	gchar    *buf,
	gsize    buflen,
	gint     button,
	gint     col,
	gint     row,
	gboolean release,
	gboolean motion,
	guint    state,
	gboolean sgr
);

G_END_DECLS

#endif /* GST_MOUSE_H */
