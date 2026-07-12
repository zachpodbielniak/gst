/*
 * gst-mouse.c - GST Mouse Report Encoding
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-mouse.h"

#include <X11/X.h>

/**
 * gst_mouse_encode_report:
 * @buf: (out caller-allocates): buffer to receive the encoded sequence
 * @buflen: size of @buf in bytes (must be at least 6)
 * @button: protocol button code: 0=left, 1=middle, 2=right,
 *   3=none/release, 64=wheel-up, 65=wheel-down
 * @col: 0-based column
 * @row: 0-based row
 * @release: %TRUE for a button-release event
 * @motion: %TRUE for a motion (drag/hover) event
 * @state: X11-style modifier mask (%ShiftMask, %Mod1Mask, %ControlMask)
 * @sgr: %TRUE to encode in SGR extended mode, %FALSE for classic X10
 *
 * Encodes a single xterm mouse report into @buf. This is the pure encoder
 * shared by the terminal's mouse-reporting path; it has no dependency on
 * terminal or PTY state so it can be unit-tested directly.
 *
 * A no-button motion (hover, only sent in any-event tracking mode) must use
 * @button = 3, so it encodes as code 35 (3 + the 32 motion offset) rather than
 * as a left-button drag (code 32). Getting this wrong makes applications such
 * as tmux treat every mouse move as a held-button drag.
 *
 * Returns: the number of bytes written to @buf
 */
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
){
	gint len;
	gint cb;

	g_return_val_if_fail(buf != NULL, 0);
	g_return_val_if_fail(buflen >= 6, 0);

	/* Build the button code with modifier bits */
	cb = button;

	/* Motion events add 32 to the button code */
	if (motion) {
		cb += 32;
	}

	/* Encode modifier keys into the button code */
	if (state & ShiftMask)   cb += 4;
	if (state & Mod1Mask)    cb += 8;
	if (state & ControlMask) cb += 16;

	if (sgr) {
		/*
		 * SGR extended mode: ESC [ < Cb ; Cx ; Cy M/m
		 * Coordinates are 1-based decimal, no offset.
		 * 'M' for press/motion, 'm' for release.
		 */
		len = g_snprintf(buf, buflen, "\033[<%d;%d;%d%c",
			cb, col + 1, row + 1, release ? 'm' : 'M');
	} else {
		/*
		 * Classic X10/normal mode: ESC [ M Cb Cx Cy
		 * Cb has +32 offset, coordinates have +33 offset.
		 * Coordinates are clamped to 223 (255 - 32) max.
		 */
		if (col > 222) col = 222;
		if (row > 222) row = 222;
		buf[0] = '\033';
		buf[1] = '[';
		buf[2] = 'M';
		buf[3] = (gchar)(32 + cb);
		buf[4] = (gchar)(33 + col);
		buf[5] = (gchar)(33 + row);
		len = 6;
	}

	return (gssize)len;
}
