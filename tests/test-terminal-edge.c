/*
 * test-terminal-edge.c - Input boundary and escape termination regressions
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <glib.h>
#include "core/gst-terminal.h"
#include "selection/gst-selection.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTerminal, g_object_unref)

/* Large positive movement must clamp to the bottom/right, never overflow. */
static void
test_large_cursor_move(gconstpointer sequence)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 4);
	gint col;
	gint row;

	gst_terminal_write(term, "\033[2;2H", -1);
	gst_terminal_write(term, (const gchar *)sequence, -1);
	col = gst_terminal_get_cursor(term)->x;
	row = gst_terminal_get_cursor(term)->y;
	g_assert_cmpint(col, ==, 7);
	g_assert_cmpint(row, ==, 3);
}

/* Reset must discard bytes that belonged to the previous decoder state. */
static void
test_reset_partial_utf8(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 4);

	gst_terminal_write(term, "\xc3", 1);
	gst_terminal_reset(term, TRUE);
	gst_terminal_write(term, "\xa9X", 2);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'X');
}

/* A NUL cannot complete a UTF-8 sequence, even across separate PTY reads. */
static void
test_nul_after_partial(gconstpointer split)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 4);
	const gchar data[] = { (gchar)0xc3, '\0', 'B' };

	if (GPOINTER_TO_INT(split)) {
		gst_terminal_write(term, data, 1);
		gst_terminal_write(term, data + 1, 2);
	} else {
		gst_terminal_write(term, data, sizeof(data));
	}
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'B');
}

/* ST is a two-byte terminator and its backslash must not become a glyph. */
static void
test_string_terminator(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 4);

	gst_terminal_write(term, "\033]2;title\033", -1);
	gst_terminal_write(term, "\\X", -1);
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "title");
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'X');
}

/* CAN/SUB cancel an unfinished command without applying its contents. */
static void
test_string_cancel(gconstpointer sequence)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 4);

	gst_terminal_set_title(term, "original");
	gst_terminal_write(term, (const gchar *)sequence, -1);
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "original");
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'X');
}

/* Only the OSC command number is delimited; the title can contain ';'. */
static void
test_title_semicolon(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 4);

	gst_terminal_write(term, "\033]2;first;second\a", -1);
	g_assert_cmpstr(gst_terminal_get_title(term), ==, "first;second");
}

/* Pointer motion may be coalesced into the button release event. */
static void
test_selection_release_moves(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 4);
	g_autoptr(GstSelection) selection = gst_selection_new(term);
	g_autofree gchar *text = NULL;

	gst_terminal_write(term, "abcdef", -1);
	gst_selection_start(selection, 0, 0, GST_SELECTION_SNAP_NONE);
	gst_selection_extend(selection, 3, 0, GST_SELECTION_TYPE_REGULAR, TRUE);
	text = gst_selection_get_text(selection);
	g_assert_cmpstr(text, ==, "abcd");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_data_func("/edge/cursor/large-relative",
		"\033[2147483647B\033[2147483647C", test_large_cursor_move);
	g_test_add_data_func("/edge/cursor/large-absolute-origin",
		"\033[3;4r\033[?6h\033[2147483647;2147483647H", test_large_cursor_move);
	g_test_add_data_func("/edge/cursor/beyond-int",
		"\033[4294967295;4294967295H", test_large_cursor_move);
	g_test_add_func("/edge/reset/partial-utf8", test_reset_partial_utf8);
	g_test_add_data_func("/edge/utf8/nul-after-partial", GINT_TO_POINTER(FALSE), test_nul_after_partial);
	g_test_add_data_func("/edge/utf8/nul-after-split-partial", GINT_TO_POINTER(TRUE), test_nul_after_partial);
	g_test_add_func("/edge/string/st", test_string_terminator);
	g_test_add_data_func("/edge/string/can", "\033]2;bad\030X", test_string_cancel);
	g_test_add_data_func("/edge/string/sub", "\033]2;bad\032X", test_string_cancel);
	g_test_add_func("/edge/string/title-semicolon", test_title_semicolon);
	g_test_add_func("/edge/selection/release-moves", test_selection_release_moves);
	return g_test_run();
}
