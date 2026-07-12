/*
 * test-mouse.c - Tests for GST mouse report encoding
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests gst_mouse_encode_report(): SGR and classic X10 encoding for
 * press, release, drag, and no-button hover. The hover case guards the
 * regression where a no-button motion was encoded as a left-button drag
 * (code 32) instead of a no-button motion (code 35), which made tmux act
 * as if the mouse button were permanently held.
 */

#include <glib.h>
#include <X11/X.h>
#include "core/gst-mouse.h"

/* Column 4, row 2 -> SGR coords 5;3 (1-based). */
#define COL 4
#define ROW 2

/* ===== SGR mode ===== */

static void
test_mouse_sgr_press_release(void)
{
	gchar  buf[64];
	gssize len;

	/* Left press: button 0, no release, no motion -> ESC[<0;5;3M */
	len = gst_mouse_encode_report(buf, sizeof(buf), 0, COL, ROW,
		FALSE, FALSE, 0, TRUE);
	g_assert_cmpint(len, ==, 9);
	g_assert_cmpstr(buf, ==, "\033[<0;5;3M");

	/* Left release: lowercase 'm' terminator -> ESC[<0;5;3m */
	len = gst_mouse_encode_report(buf, sizeof(buf), 0, COL, ROW,
		TRUE, FALSE, 0, TRUE);
	g_assert_cmpstr(buf, ==, "\033[<0;5;3m");
}

/*
 * The regression guard: a no-button motion (button 3) must encode as
 * code 35 (3 + 32 motion), NOT 32 (left-button drag).
 */
static void
test_mouse_sgr_hover_no_button(void)
{
	gchar buf[64];

	gst_mouse_encode_report(buf, sizeof(buf), 3, COL, ROW,
		FALSE, TRUE, 0, TRUE);
	g_assert_cmpstr(buf, ==, "\033[<35;5;3M");
}

static void
test_mouse_sgr_drag(void)
{
	gchar buf[64];

	/* Left-button drag: button 0 + motion -> code 32 */
	gst_mouse_encode_report(buf, sizeof(buf), 0, COL, ROW,
		FALSE, TRUE, 0, TRUE);
	g_assert_cmpstr(buf, ==, "\033[<32;5;3M");
}

static void
test_mouse_sgr_modifiers(void)
{
	gchar buf[64];

	/* Shift adds 4 */
	gst_mouse_encode_report(buf, sizeof(buf), 0, COL, ROW,
		FALSE, FALSE, ShiftMask, TRUE);
	g_assert_cmpstr(buf, ==, "\033[<4;5;3M");

	/* Alt (Mod1) adds 8 */
	gst_mouse_encode_report(buf, sizeof(buf), 0, COL, ROW,
		FALSE, FALSE, Mod1Mask, TRUE);
	g_assert_cmpstr(buf, ==, "\033[<8;5;3M");

	/* Control adds 16 */
	gst_mouse_encode_report(buf, sizeof(buf), 0, COL, ROW,
		FALSE, FALSE, ControlMask, TRUE);
	g_assert_cmpstr(buf, ==, "\033[<16;5;3M");
}

/* ===== Classic X10 mode ===== */

static void
test_mouse_classic_press(void)
{
	gchar  buf[64];
	gssize len;

	/* Left press: ESC [ M, then 32+cb, 33+col, 33+row */
	len = gst_mouse_encode_report(buf, sizeof(buf), 0, COL, ROW,
		FALSE, FALSE, 0, FALSE);
	g_assert_cmpint(len, ==, 6);
	g_assert_cmpint((guchar)buf[0], ==, 033);
	g_assert_cmpint(buf[1], ==, '[');
	g_assert_cmpint(buf[2], ==, 'M');
	g_assert_cmpint((guchar)buf[3], ==, 32 + 0);
	g_assert_cmpint((guchar)buf[4], ==, 33 + COL);
	g_assert_cmpint((guchar)buf[5], ==, 33 + ROW);
}

static void
test_mouse_classic_hover(void)
{
	gchar buf[64];

	/* No-button motion: cb = 3 + 32 = 35 -> byte 32 + 35 = 67 */
	gst_mouse_encode_report(buf, sizeof(buf), 3, COL, ROW,
		FALSE, TRUE, 0, FALSE);
	g_assert_cmpint((guchar)buf[3], ==, 32 + 35);
}

static void
test_mouse_classic_clamp(void)
{
	gchar buf[64];

	/* Coordinates above 222 clamp to 222 -> byte 33 + 222 = 255 */
	gst_mouse_encode_report(buf, sizeof(buf), 0, 300, 300,
		FALSE, FALSE, 0, FALSE);
	g_assert_cmpint((guchar)buf[4], ==, 255);
	g_assert_cmpint((guchar)buf[5], ==, 255);
}

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/mouse/sgr-press-release", test_mouse_sgr_press_release);
	g_test_add_func("/mouse/sgr-hover-no-button", test_mouse_sgr_hover_no_button);
	g_test_add_func("/mouse/sgr-drag", test_mouse_sgr_drag);
	g_test_add_func("/mouse/sgr-modifiers", test_mouse_sgr_modifiers);
	g_test_add_func("/mouse/classic-press", test_mouse_classic_press);
	g_test_add_func("/mouse/classic-hover", test_mouse_classic_hover);
	g_test_add_func("/mouse/classic-clamp", test_mouse_classic_clamp);

	return g_test_run();
}
