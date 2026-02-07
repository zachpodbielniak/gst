/*
 * test-escape.c - Tests for escape sequence parsing
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests VT100/ANSI escape sequences processed by GstTerminal.
 * Each test creates a fresh terminal, writes escape sequences,
 * and verifies the resulting terminal state.
 */

#include <glib.h>
#include "core/gst-terminal.h"
#include "core/gst-line.h"
#include "boxed/gst-glyph.h"
#include "boxed/gst-cursor.h"
#include "gst-enums.h"

/*
 * Helper: write a string to the terminal.
 * Wraps gst_terminal_write with automatic length.
 */
static void
term_write(
	GstTerminal *term,
	const gchar *str
){
	gst_terminal_write(term, str, -1);
}

/*
 * Helper: get the character (rune) at a position.
 */
static GstRune
glyph_at(
	GstTerminal *term,
	gint        col,
	gint        row
){
	GstGlyph *g;

	g = gst_terminal_get_glyph(term, col, row);
	if (g == NULL) {
		return 0;
	}
	return g->rune;
}

/* ===== CSI Cursor Movement Tests ===== */

/*
 * Test CSI A (CUU - Cursor Up).
 * Move cursor up N lines.
 */
static void
test_csi_cursor_up(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);
	gst_terminal_set_cursor_pos(term, 5, 10);

	/* Move up 3 lines */
	term_write(term, "\033[3A");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 5);
	g_assert_cmpint(cursor->y, ==, 7);

	g_object_unref(term);
}

/*
 * Test CSI B (CUD - Cursor Down).
 * Move cursor down N lines.
 */
static void
test_csi_cursor_down(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);
	gst_terminal_set_cursor_pos(term, 5, 10);

	/* Move down 5 lines */
	term_write(term, "\033[5B");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 5);
	g_assert_cmpint(cursor->y, ==, 15);

	g_object_unref(term);
}

/*
 * Test CSI C (CUF - Cursor Forward).
 * Move cursor right N columns.
 */
static void
test_csi_cursor_forward(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);
	gst_terminal_set_cursor_pos(term, 5, 0);

	/* Move right 10 columns */
	term_write(term, "\033[10C");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 15);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/*
 * Test CSI D (CUB - Cursor Back).
 * Move cursor left N columns.
 */
static void
test_csi_cursor_back(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);
	gst_terminal_set_cursor_pos(term, 20, 0);

	/* Move left 8 columns */
	term_write(term, "\033[8D");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 12);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/*
 * Test CSI H (CUP - Cursor Position).
 * Move cursor to row;col (1-based).
 */
static void
test_csi_cursor_position(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	/* Move to row 5, col 10 (1-based) => (9, 4) 0-based */
	term_write(term, "\033[5;10H");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 9);
	g_assert_cmpint(cursor->y, ==, 4);

	g_object_unref(term);
}

/*
 * Test CSI H with default args (move to home).
 */
static void
test_csi_cursor_home(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);
	gst_terminal_set_cursor_pos(term, 40, 12);

	/* CSI H with no args = home */
	term_write(term, "\033[H");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 0);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/*
 * Test CSI G (CHA - Cursor Horizontal Absolute).
 * Move cursor to column N (1-based).
 */
static void
test_csi_cursor_col_abs(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);
	gst_terminal_set_cursor_pos(term, 0, 5);

	/* Move to column 20 (1-based) => col 19 (0-based) */
	term_write(term, "\033[20G");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 19);
	g_assert_cmpint(cursor->y, ==, 5);

	g_object_unref(term);
}

/*
 * Test CSI d (VPA - Vertical Position Absolute).
 * Move cursor to row N (1-based).
 */
static void
test_csi_cursor_row_abs(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);
	gst_terminal_set_cursor_pos(term, 10, 0);

	/* Move to row 15 (1-based) => row 14 (0-based) */
	term_write(term, "\033[15d");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 10);
	g_assert_cmpint(cursor->y, ==, 14);

	g_object_unref(term);
}

/* ===== CSI Erase Tests ===== */

/*
 * Test CSI J (ED - Erase in Display).
 * Mode 0: erase from cursor to end of screen.
 */
static void
test_csi_erase_below(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Fill first 3 rows with 'X' */
	term_write(term, "XXXXX");
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "YYYYY");
	gst_terminal_set_cursor_pos(term, 0, 2);
	term_write(term, "ZZZZZ");

	/* Move to row 1, col 0 and erase below */
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "\033[J");

	/* Row 0 should still have 'X' */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'X');

	/* Row 1 should be cleared */
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, ' ');

	/* Row 2 should be cleared */
	g_assert_cmpuint(glyph_at(term, 0, 2), ==, ' ');

	g_object_unref(term);
}

/*
 * Test CSI 1J (ED - Erase above).
 * Erase from start of screen to cursor.
 */
static void
test_csi_erase_above(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Fill first 3 rows */
	term_write(term, "XXXXX");
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "YYYYY");
	gst_terminal_set_cursor_pos(term, 0, 2);
	term_write(term, "ZZZZZ");

	/* Move to row 1, col 2 and erase above */
	gst_terminal_set_cursor_pos(term, 2, 1);
	term_write(term, "\033[1J");

	/* Row 0 should be cleared */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, ' ');

	/* Row 1, cols 0-2 should be cleared */
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, ' ');
	g_assert_cmpuint(glyph_at(term, 2, 1), ==, ' ');

	/* Row 2 should still have 'Z' */
	g_assert_cmpuint(glyph_at(term, 0, 2), ==, 'Z');

	g_object_unref(term);
}

/*
 * Test CSI 2J (ED - Erase entire screen).
 */
static void
test_csi_erase_all(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Fill some data */
	term_write(term, "Hello World");
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "Line 2");

	/* Erase entire screen */
	term_write(term, "\033[2J");

	/* Everything should be spaces */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, ' ');
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, ' ');

	g_object_unref(term);
}

/*
 * Test CSI K (EL - Erase in Line).
 * Mode 0: erase from cursor to end of line.
 */
static void
test_csi_erase_line_right(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	term_write(term, "Hello World");

	/* Move to col 5 and erase right */
	gst_terminal_set_cursor_pos(term, 5, 0);
	term_write(term, "\033[K");

	/* "Hello" should remain */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'H');
	g_assert_cmpuint(glyph_at(term, 4, 0), ==, 'o');

	/* Col 5 onward should be spaces */
	g_assert_cmpuint(glyph_at(term, 5, 0), ==, ' ');
	g_assert_cmpuint(glyph_at(term, 6, 0), ==, ' ');

	g_object_unref(term);
}

/*
 * Test CSI 1K (EL - Erase left).
 */
static void
test_csi_erase_line_left(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	term_write(term, "Hello World");

	/* Move to col 5 and erase left */
	gst_terminal_set_cursor_pos(term, 5, 0);
	term_write(term, "\033[1K");

	/* Cols 0-5 should be spaces */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, ' ');
	g_assert_cmpuint(glyph_at(term, 5, 0), ==, ' ');

	/* Col 6 onward should remain ("World" starts at col 6) */
	g_assert_cmpuint(glyph_at(term, 6, 0), ==, 'W');

	g_object_unref(term);
}

/* ===== CSI Insert/Delete Tests ===== */

/*
 * Test CSI L (IL - Insert Lines).
 * Inserts blank lines at cursor row, scrolling existing lines down.
 */
static void
test_csi_insert_lines(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Fill rows 0-2 */
	term_write(term, "AAA");
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "BBB");
	gst_terminal_set_cursor_pos(term, 0, 2);
	term_write(term, "CCC");

	/* Move to row 1 and insert 1 line */
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "\033[L");

	/* Row 0 should still be 'A' */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'A');

	/* Row 1 should be blank (inserted) */
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, ' ');

	/* Row 2 should now be 'B' (pushed down) */
	g_assert_cmpuint(glyph_at(term, 0, 2), ==, 'B');

	/* Row 3 should now be 'C' (pushed down) */
	g_assert_cmpuint(glyph_at(term, 0, 3), ==, 'C');

	g_object_unref(term);
}

/*
 * Test CSI M (DL - Delete Lines).
 * Deletes lines at cursor row, scrolling lines below up.
 */
static void
test_csi_delete_lines(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Fill rows 0-2 */
	term_write(term, "AAA");
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "BBB");
	gst_terminal_set_cursor_pos(term, 0, 2);
	term_write(term, "CCC");

	/* Move to row 1 and delete 1 line */
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "\033[M");

	/* Row 0 should still be 'A' */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'A');

	/* Row 1 should now be 'C' (scrolled up) */
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, 'C');

	/* Row 2 should be blank (scrolled in from bottom) */
	g_assert_cmpuint(glyph_at(term, 0, 2), ==, ' ');

	g_object_unref(term);
}

/*
 * Test CSI @ (ICH - Insert Characters).
 * Inserts blank characters at cursor position.
 */
static void
test_csi_insert_chars(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	term_write(term, "ABCDEF");

	/* Move to col 2 and insert 2 blanks */
	gst_terminal_set_cursor_pos(term, 2, 0);
	term_write(term, "\033[2@");

	/* "AB" should remain */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'A');
	g_assert_cmpuint(glyph_at(term, 1, 0), ==, 'B');

	/* 2 blanks inserted */
	g_assert_cmpuint(glyph_at(term, 2, 0), ==, ' ');
	g_assert_cmpuint(glyph_at(term, 3, 0), ==, ' ');

	/* "CDEF" shifted right */
	g_assert_cmpuint(glyph_at(term, 4, 0), ==, 'C');
	g_assert_cmpuint(glyph_at(term, 5, 0), ==, 'D');

	g_object_unref(term);
}

/*
 * Test CSI P (DCH - Delete Characters).
 * Deletes characters at cursor position.
 */
static void
test_csi_delete_chars(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	term_write(term, "ABCDEF");

	/* Move to col 2 and delete 2 characters */
	gst_terminal_set_cursor_pos(term, 2, 0);
	term_write(term, "\033[2P");

	/* "AB" should remain */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'A');
	g_assert_cmpuint(glyph_at(term, 1, 0), ==, 'B');

	/* "EF" should shift left to col 2 */
	g_assert_cmpuint(glyph_at(term, 2, 0), ==, 'E');
	g_assert_cmpuint(glyph_at(term, 3, 0), ==, 'F');

	/* Remaining should be blank */
	g_assert_cmpuint(glyph_at(term, 4, 0), ==, ' ');

	g_object_unref(term);
}

/* ===== SGR (Select Graphic Rendition) Tests ===== */

/*
 * Test CSI m (SGR - basic attributes).
 * Verifies bold, italic, underline, reverse, etc.
 */
static void
test_sgr_attributes(void)
{
	GstTerminal *term;
	GstGlyph *glyph;

	term = gst_terminal_new(80, 24);

	/* Set bold, write a character */
	term_write(term, "\033[1mB");

	glyph = gst_terminal_get_glyph(term, 0, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->rune, ==, 'B');
	g_assert_true(glyph->attr & GST_GLYPH_ATTR_BOLD);

	/* Set italic, write a character */
	term_write(term, "\033[3mI");

	glyph = gst_terminal_get_glyph(term, 1, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->rune, ==, 'I');
	g_assert_true(glyph->attr & GST_GLYPH_ATTR_ITALIC);
	/* Bold should also still be active */
	g_assert_true(glyph->attr & GST_GLYPH_ATTR_BOLD);

	/* Reset all attributes */
	term_write(term, "\033[0mN");

	glyph = gst_terminal_get_glyph(term, 2, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->rune, ==, 'N');
	g_assert_cmpuint(glyph->attr & (GST_GLYPH_ATTR_BOLD | GST_GLYPH_ATTR_ITALIC), ==, 0);

	g_object_unref(term);
}

/*
 * Test SGR foreground colors (30-37, 90-97).
 */
static void
test_sgr_fg_colors(void)
{
	GstTerminal *term;
	GstGlyph *glyph;

	term = gst_terminal_new(80, 24);

	/* Set red foreground (31) and write */
	term_write(term, "\033[31mR");

	glyph = gst_terminal_get_glyph(term, 0, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->fg, ==, GST_COLOR_RED);

	/* Set bright green foreground (92) and write */
	term_write(term, "\033[92mG");

	glyph = gst_terminal_get_glyph(term, 1, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->fg, ==, GST_COLOR_BRIGHT_GREEN);

	/* Reset to default foreground (39) */
	term_write(term, "\033[39mD");

	glyph = gst_terminal_get_glyph(term, 2, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->fg, ==, GST_COLOR_DEFAULT_FG);

	g_object_unref(term);
}

/*
 * Test SGR background colors (40-47, 100-107).
 */
static void
test_sgr_bg_colors(void)
{
	GstTerminal *term;
	GstGlyph *glyph;

	term = gst_terminal_new(80, 24);

	/* Set blue background (44) and write */
	term_write(term, "\033[44mB");

	glyph = gst_terminal_get_glyph(term, 0, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->bg, ==, GST_COLOR_BLUE);

	/* Reset to default background (49) */
	term_write(term, "\033[49mD");

	glyph = gst_terminal_get_glyph(term, 1, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->bg, ==, GST_COLOR_DEFAULT_BG);

	g_object_unref(term);
}

/*
 * Test SGR 256-color mode (38;5;N and 48;5;N).
 */
static void
test_sgr_256_colors(void)
{
	GstTerminal *term;
	GstGlyph *glyph;

	term = gst_terminal_new(80, 24);

	/* Set 256-color fg to color 100 */
	term_write(term, "\033[38;5;100mX");

	glyph = gst_terminal_get_glyph(term, 0, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->fg, ==, 100);

	/* Set 256-color bg to color 200 */
	term_write(term, "\033[48;5;200mY");

	glyph = gst_terminal_get_glyph(term, 1, 0);
	g_assert_nonnull(glyph);
	g_assert_cmpuint(glyph->bg, ==, 200);

	g_object_unref(term);
}

/* ===== Control Code Tests ===== */

/*
 * Test carriage return (CR, \r).
 * Moves cursor to column 0.
 */
static void
test_control_cr(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	term_write(term, "Hello");
	term_write(term, "\r");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 0);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/*
 * Test line feed (LF, \n).
 * Moves cursor down one line.
 */
static void
test_control_lf(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	term_write(term, "Hello\n");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->y, ==, 1);

	g_object_unref(term);
}

/*
 * Test backspace (BS, \b).
 * Moves cursor left one column.
 */
static void
test_control_bs(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	term_write(term, "AB\b");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 1);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/*
 * Test tab (HT, \t).
 * Moves cursor to next tab stop (default every 8 columns).
 */
static void
test_control_tab(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	term_write(term, "A\t");

	cursor = gst_terminal_get_cursor(term);
	/* Tab stop at col 8 */
	g_assert_cmpint(cursor->x, ==, 8);

	g_object_unref(term);
}

/* ===== Mode Tests ===== */

/*
 * Test DECAWM (auto-wrap mode) via CSI ?7h / ?7l.
 */
static void
test_mode_wrap(void)
{
	GstTerminal *term;

	term = gst_terminal_new(10, 5);

	/* Wrap mode should be on by default */
	g_assert_true(gst_terminal_has_mode(term, GST_MODE_WRAP));

	/* Disable wrap mode */
	term_write(term, "\033[?7l");
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_WRAP));

	/* Re-enable wrap mode */
	term_write(term, "\033[?7h");
	g_assert_true(gst_terminal_has_mode(term, GST_MODE_WRAP));

	g_object_unref(term);
}

/*
 * Test DECTCEM (cursor visibility) via CSI ?25h / ?25l.
 */
static void
test_mode_cursor_visible(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Cursor should be visible by default (HIDE not set) */
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_HIDE));

	/* Hide cursor */
	term_write(term, "\033[?25l");
	g_assert_true(gst_terminal_has_mode(term, GST_MODE_HIDE));

	/* Show cursor */
	term_write(term, "\033[?25h");
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_HIDE));

	g_object_unref(term);
}

/*
 * Test bracketed paste mode via CSI ?2004h / ?2004l.
 */
static void
test_mode_bracketed_paste(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Should be off by default */
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_BRCKTPASTE));

	/* Enable */
	term_write(term, "\033[?2004h");
	g_assert_true(gst_terminal_has_mode(term, GST_MODE_BRCKTPASTE));

	/* Disable */
	term_write(term, "\033[?2004l");
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_BRCKTPASTE));

	g_object_unref(term);
}

/*
 * Test insert mode (IRM) via CSI 4h / 4l.
 */
static void
test_mode_insert(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Should be off by default */
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_INSERT));

	/* Enable IRM */
	term_write(term, "\033[4h");
	g_assert_true(gst_terminal_has_mode(term, GST_MODE_INSERT));

	/* Disable IRM */
	term_write(term, "\033[4l");
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_INSERT));

	g_object_unref(term);
}

/* ===== Scroll Region Tests ===== */

/*
 * Test CSI r (DECSTBM - Set Top and Bottom Margins).
 */
static void
test_csi_scroll_region(void)
{
	GstTerminal *term;
	gint top;
	gint bot;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	/* Set scroll region to rows 5-15 (1-based) => 4-14 (0-based) */
	term_write(term, "\033[5;15r");

	gst_terminal_get_scroll_region(term, &top, &bot);
	g_assert_cmpint(top, ==, 4);
	g_assert_cmpint(bot, ==, 14);

	/* DECSTBM also moves cursor home */
	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 0);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/* ===== Cursor Save/Restore Tests ===== */

/*
 * Test ESC 7 / ESC 8 (DECSC/DECRC - save/restore cursor).
 */
static void
test_cursor_save_restore(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	/* Move to position and save */
	gst_terminal_set_cursor_pos(term, 15, 10);
	term_write(term, "\0337");

	/* Move somewhere else */
	gst_terminal_set_cursor_pos(term, 50, 20);

	/* Restore */
	term_write(term, "\0338");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 15);
	g_assert_cmpint(cursor->y, ==, 10);

	g_object_unref(term);
}

/*
 * Test CSI s / CSI u (ANSI save/restore cursor).
 */
static void
test_csi_cursor_save_restore(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	/* Move to position and save */
	gst_terminal_set_cursor_pos(term, 25, 8);
	term_write(term, "\033[s");

	/* Move somewhere else */
	gst_terminal_set_cursor_pos(term, 0, 0);

	/* Restore */
	term_write(term, "\033[u");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 25);
	g_assert_cmpint(cursor->y, ==, 8);

	g_object_unref(term);
}

/* ===== Character Output Tests ===== */

/*
 * Test basic character output and cursor advancement.
 */
static void
test_char_output(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	term_write(term, "Hello");

	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'H');
	g_assert_cmpuint(glyph_at(term, 1, 0), ==, 'e');
	g_assert_cmpuint(glyph_at(term, 2, 0), ==, 'l');
	g_assert_cmpuint(glyph_at(term, 3, 0), ==, 'l');
	g_assert_cmpuint(glyph_at(term, 4, 0), ==, 'o');

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 5);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/*
 * Test that CR+LF moves cursor properly.
 */
static void
test_crlf(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	term_write(term, "Line1\r\nLine2");

	/* Line1 on row 0 */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'L');
	g_assert_cmpuint(glyph_at(term, 4, 0), ==, '1');

	/* Line2 on row 1 */
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, 'L');
	g_assert_cmpuint(glyph_at(term, 4, 1), ==, '2');

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 5);
	g_assert_cmpint(cursor->y, ==, 1);

	g_object_unref(term);
}

/* ===== Scrolling Tests ===== */

/*
 * Test CSI S (SU - Scroll Up).
 * Scrolls the screen up by N lines.
 */
static void
test_csi_scroll_up(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Fill rows 0-2 */
	term_write(term, "AAA");
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "BBB");
	gst_terminal_set_cursor_pos(term, 0, 2);
	term_write(term, "CCC");

	/* Scroll up 1 line */
	term_write(term, "\033[S");

	/* Row 0 should now have 'B' (was row 1) */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'B');

	/* Row 1 should now have 'C' (was row 2) */
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, 'C');

	g_object_unref(term);
}

/*
 * Test CSI T (SD - Scroll Down).
 * Scrolls the screen down by N lines.
 */
static void
test_csi_scroll_down(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	/* Fill rows 0-2 */
	term_write(term, "AAA");
	gst_terminal_set_cursor_pos(term, 0, 1);
	term_write(term, "BBB");
	gst_terminal_set_cursor_pos(term, 0, 2);
	term_write(term, "CCC");

	/* Scroll down 1 line */
	term_write(term, "\033[T");

	/* Row 0 should be blank (scrolled in from top) */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, ' ');

	/* Row 1 should now have 'A' (was row 0) */
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, 'A');

	/* Row 2 should now have 'B' (was row 1) */
	g_assert_cmpuint(glyph_at(term, 0, 2), ==, 'B');

	g_object_unref(term);
}

/* ===== Title/OSC Tests ===== */

/*
 * Test OSC 0 (Set Window Title and Icon).
 * Sends ESC ] 0 ; title ST.
 */
static void
test_osc_title(void)
{
	GstTerminal *term;
	const gchar *title;

	term = gst_terminal_new(80, 24);

	/* OSC 0 ; title BEL */
	term_write(term, "\033]0;My Terminal\007");

	title = gst_terminal_get_title(term);
	g_assert_nonnull(title);
	g_assert_cmpstr(title, ==, "My Terminal");

	g_object_unref(term);
}

/*
 * Test OSC 2 (Set Window Title only).
 */
static void
test_osc_title_only(void)
{
	GstTerminal *term;
	const gchar *title;

	term = gst_terminal_new(80, 24);

	/* OSC 2 ; title ST */
	term_write(term, "\033]2;Title Only\033\\");

	title = gst_terminal_get_title(term);
	g_assert_nonnull(title);
	g_assert_cmpstr(title, ==, "Title Only");

	g_object_unref(term);
}

/* ===== CSI Erase Character Test ===== */

/*
 * Test CSI X (ECH - Erase Characters).
 * Erases N characters at cursor without moving cursor.
 */
static void
test_csi_erase_chars(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	term_write(term, "ABCDEF");

	/* Move to col 2 and erase 3 characters */
	gst_terminal_set_cursor_pos(term, 2, 0);
	term_write(term, "\033[3X");

	/* AB should remain */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'A');
	g_assert_cmpuint(glyph_at(term, 1, 0), ==, 'B');

	/* CDE should be erased */
	g_assert_cmpuint(glyph_at(term, 2, 0), ==, ' ');
	g_assert_cmpuint(glyph_at(term, 3, 0), ==, ' ');
	g_assert_cmpuint(glyph_at(term, 4, 0), ==, ' ');

	/* F should remain */
	g_assert_cmpuint(glyph_at(term, 5, 0), ==, 'F');

	/* Cursor should not move */
	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 2);

	g_object_unref(term);
}

/* ===== Alternate Screen Test ===== */

/*
 * Test alternate screen buffer switch via CSI ?1049h / ?1049l.
 * Cursor position carries over between screens; 1049h does NOT
 * home the cursor. Characters written after switch go to cursor pos.
 */
static void
test_altscreen(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	/* Write to primary screen */
	term_write(term, "Primary");
	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 7);

	g_assert_false(gst_terminal_is_altscreen(term));

	/* Switch to alternate screen */
	term_write(term, "\033[?1049h");
	g_assert_true(gst_terminal_is_altscreen(term));

	/* Alt screen position (0,0) should be blank */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, ' ');

	/* Cursor home, then write to alt screen */
	term_write(term, "\033[H");
	term_write(term, "Alternate");

	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'A');
	g_assert_cmpuint(glyph_at(term, 1, 0), ==, 'l');

	/* Switch back to primary screen */
	term_write(term, "\033[?1049l");
	g_assert_false(gst_terminal_is_altscreen(term));

	/* Primary screen content should be restored */
	g_assert_cmpuint(glyph_at(term, 0, 0), ==, 'P');
	g_assert_cmpuint(glyph_at(term, 1, 0), ==, 'r');

	g_object_unref(term);
}

/* ===== Response Signal Test ===== */

static gchar *response_data = NULL;

static void
on_response(
	GstTerminal *term,
	const gchar *data,
	glong       len,
	gpointer    user_data
){
	g_free(response_data);
	response_data = g_strndup(data, (gsize)len);
}

/*
 * Test device attributes (DA) response.
 * CSI c should emit a response signal with device attributes.
 */
static void
test_response_da(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	g_signal_connect(term, "response", G_CALLBACK(on_response), NULL);

	/* Send DA request */
	term_write(term, "\033[c");

	/* Should have received a DA response */
	g_assert_nonnull(response_data);
	/* DA response starts with CSI ? */
	g_assert_true(g_str_has_prefix(response_data, "\033[?"));

	g_free(response_data);
	response_data = NULL;

	g_object_unref(term);
}

/*
 * Test cursor position report (DSR).
 * CSI 6n should emit response with cursor position.
 */
static void
test_response_dsr(void)
{
	GstTerminal *term;

	term = gst_terminal_new(80, 24);

	g_signal_connect(term, "response", G_CALLBACK(on_response), NULL);

	/* Move cursor to row 5, col 10 (0-based) */
	gst_terminal_set_cursor_pos(term, 10, 5);

	/* Request cursor position report */
	term_write(term, "\033[6n");

	/* Should have received position report (1-based: row 6, col 11) */
	g_assert_nonnull(response_data);
	g_assert_cmpstr(response_data, ==, "\033[6;11R");

	g_free(response_data);
	response_data = NULL;

	g_object_unref(term);
}

/* ===== Stale CSI Args Tests ===== */

/*
 * Test that CSI args are zeroed between parses.
 * After DECSTBM sets args[1]=24, a bare CUP (CSI H) should
 * move to (0,0), not (0,23) from a stale arg.
 */
static void
test_csi_stale_args_cleared(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	/* Set scroll region: CSI 1;24 r -> args[0]=1, args[1]=24 */
	term_write(term, "\033[1;24r");

	/* Move cursor somewhere visible */
	gst_terminal_set_cursor_pos(term, 10, 10);

	/* CSI H with no args = cursor home (0,0) */
	term_write(term, "\033[H");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 0);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/*
 * Test stale args after multi-arg SGR then CUP.
 * A long SGR like CSI 1;31;42m fills multiple arg slots.
 * A subsequent CSI H should still move to (0,0).
 */
static void
test_csi_stale_args_cup_after_sgr(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	/* SGR with 3 args: bold, red fg, green bg */
	term_write(term, "\033[1;31;42m");

	/* Move cursor somewhere */
	gst_terminal_set_cursor_pos(term, 20, 15);

	/* CUP with no args = home */
	term_write(term, "\033[H");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 0);
	g_assert_cmpint(cursor->y, ==, 0);

	g_object_unref(term);
}

/*
 * Test DECSTBM with no args resets scroll region.
 * CSI 5;20 r sets region, then CSI r should reset to full screen.
 */
static void
test_decstbm_no_args_reset(void)
{
	GstTerminal *term;
	gint top;
	gint bot;

	term = gst_terminal_new(80, 24);

	/* Set a restricted scroll region */
	term_write(term, "\033[5;20r");

	gst_terminal_get_scroll_region(term, &top, &bot);
	g_assert_cmpint(top, ==, 4);
	g_assert_cmpint(bot, ==, 19);

	/* Reset scroll region with bare CSI r */
	term_write(term, "\033[r");

	gst_terminal_get_scroll_region(term, &top, &bot);
	g_assert_cmpint(top, ==, 0);
	g_assert_cmpint(bot, ==, 23);

	g_object_unref(term);
}

/*
 * Test UTF-8 sequence split across two write() calls.
 * The 2-byte UTF-8 character U+00E9 (e-acute, 0xC3 0xA9) is
 * split: first byte in one write, second byte in the next.
 */
static void
test_utf8_split_boundary(void)
{
	GstTerminal *term;
	GstRune rune;

	term = gst_terminal_new(80, 24);

	/* Write first byte of U+00E9 (0xC3) */
	gst_terminal_write(term, "\xC3", 1);

	/* Write second byte (0xA9) */
	gst_terminal_write(term, "\xA9", 1);

	/* The combined bytes should produce U+00E9 at (0,0) */
	rune = glyph_at(term, 0, 0);
	g_assert_cmpuint(rune, ==, 0x00E9);

	g_object_unref(term);
}

/* ===== Stale CSI Mode Tests ===== */

/*
 * Test that csi_mode is not stale across sequences.
 * After CSI ?25h (DECSET with '?' prefix), a subsequent CSI 5;1H
 * (CUP) should move to row 4, col 0 -- not be misinterpreted
 * due to leftover '?' in csi_mode from the prior sequence.
 */
static void
test_csi_mode_not_stale(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(80, 24);

	/* DECSET: show cursor (CSI ? 25 h) - uses '?' prefix */
	term_write(term, "\033[?25h");

	/* CUP: move to row 5, col 1 (1-based) => (0, 4) 0-based */
	term_write(term, "\033[5;1H");

	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->y, ==, 4);
	g_assert_cmpint(cursor->x, ==, 0);

	g_object_unref(term);
}

/* ===== Cursor Restore WRAPNEXT Tests ===== */

/*
 * Test that cursor restore preserves WRAPNEXT state.
 * When a character is written at the last column, WRAPNEXT is set.
 * Saving and restoring the cursor should preserve this flag, so
 * the next character triggers a wrap-newline instead of overwriting.
 */
static void
test_cursor_restore_preserves_wrapnext(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(10, 5);

	/* Move to last column (col 9 on 10-col terminal) */
	gst_terminal_set_cursor_pos(term, 9, 0);

	/* Write a character to trigger WRAPNEXT */
	term_write(term, "X");

	/* Verify WRAPNEXT is set */
	cursor = gst_terminal_get_cursor(term);
	g_assert_true(cursor->state & GST_CURSOR_STATE_WRAPNEXT);

	/* Save cursor (ESC 7 = DECSC) */
	term_write(term, "\0337");

	/* Move somewhere else to disturb cursor */
	gst_terminal_set_cursor_pos(term, 0, 2);

	/* Restore cursor (ESC 8 = DECRC) */
	term_write(term, "\0338");

	/* WRAPNEXT should still be set after restore */
	cursor = gst_terminal_get_cursor(term);
	g_assert_true(cursor->state & GST_CURSOR_STATE_WRAPNEXT);

	/*
	 * Writing another character should wrap to the next line,
	 * not overwrite the last column.
	 */
	term_write(term, "Y");
	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->y, ==, 1);
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, 'Y');

	g_object_unref(term);
}

/* ===== REP (CSI b) Tests ===== */

/*
 * Test that REP wraps at line end.
 * Position cursor near end of line and repeat a character past the
 * last column. Characters should wrap to the next line instead of
 * being dropped or silently clamped.
 */
static void
test_rep_wraps_at_line_end(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(10, 5);

	/* Move to col 7 (3 columns before end on 10-col terminal) */
	gst_terminal_set_cursor_pos(term, 7, 0);

	/* Write 'A' to set lastc */
	term_write(term, "A");

	/* REP 5 times: cursor at col 8, need to place 5 more 'A's */
	/* cols 8,9 fill row 0, then wrap: cols 0,1,2 on row 1 */
	term_write(term, "\033[5b");

	/* Row 0 should have 'A' at cols 8 and 9 */
	g_assert_cmpuint(glyph_at(term, 8, 0), ==, 'A');
	g_assert_cmpuint(glyph_at(term, 9, 0), ==, 'A');

	/* Row 1 should have 'A' at cols 0, 1, 2 (3 remaining) */
	g_assert_cmpuint(glyph_at(term, 0, 1), ==, 'A');
	g_assert_cmpuint(glyph_at(term, 1, 1), ==, 'A');
	g_assert_cmpuint(glyph_at(term, 2, 1), ==, 'A');

	/* Cursor should be on row 1 */
	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->y, ==, 1);

	g_object_unref(term);
}

/*
 * Test that REP sets WRAPNEXT when filling the last column.
 * This verifies REP uses put_char behavior rather than raw
 * setchar + manual cursor advance.
 */
static void
test_rep_uses_put_char_behavior(void)
{
	GstTerminal *term;
	GstCursor *cursor;

	term = gst_terminal_new(10, 5);

	/* Move to col 8 (second to last on 10-col terminal) */
	gst_terminal_set_cursor_pos(term, 8, 0);

	/* Write 'B' to set lastc, cursor now at col 9 */
	term_write(term, "B");

	/* REP 1 time: writes 'B' at col 9 (last column) */
	term_write(term, "\033[1b");

	/* WRAPNEXT should be set since we filled the last column */
	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 9);
	g_assert_true(cursor->state & GST_CURSOR_STATE_WRAPNEXT);

	/* The character at col 9 should be 'B' */
	g_assert_cmpuint(glyph_at(term, 9, 0), ==, 'B');

	g_object_unref(term);
}

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);

	/* CSI Cursor Movement */
	g_test_add_func("/escape/csi/cursor-up", test_csi_cursor_up);
	g_test_add_func("/escape/csi/cursor-down", test_csi_cursor_down);
	g_test_add_func("/escape/csi/cursor-forward", test_csi_cursor_forward);
	g_test_add_func("/escape/csi/cursor-back", test_csi_cursor_back);
	g_test_add_func("/escape/csi/cursor-position", test_csi_cursor_position);
	g_test_add_func("/escape/csi/cursor-home", test_csi_cursor_home);
	g_test_add_func("/escape/csi/cursor-col-abs", test_csi_cursor_col_abs);
	g_test_add_func("/escape/csi/cursor-row-abs", test_csi_cursor_row_abs);

	/* CSI Erase */
	g_test_add_func("/escape/csi/erase-below", test_csi_erase_below);
	g_test_add_func("/escape/csi/erase-above", test_csi_erase_above);
	g_test_add_func("/escape/csi/erase-all", test_csi_erase_all);
	g_test_add_func("/escape/csi/erase-line-right", test_csi_erase_line_right);
	g_test_add_func("/escape/csi/erase-line-left", test_csi_erase_line_left);
	g_test_add_func("/escape/csi/erase-chars", test_csi_erase_chars);

	/* CSI Insert/Delete */
	g_test_add_func("/escape/csi/insert-lines", test_csi_insert_lines);
	g_test_add_func("/escape/csi/delete-lines", test_csi_delete_lines);
	g_test_add_func("/escape/csi/insert-chars", test_csi_insert_chars);
	g_test_add_func("/escape/csi/delete-chars", test_csi_delete_chars);

	/* SGR */
	g_test_add_func("/escape/sgr/attributes", test_sgr_attributes);
	g_test_add_func("/escape/sgr/fg-colors", test_sgr_fg_colors);
	g_test_add_func("/escape/sgr/bg-colors", test_sgr_bg_colors);
	g_test_add_func("/escape/sgr/256-colors", test_sgr_256_colors);

	/* Control Codes */
	g_test_add_func("/escape/control/cr", test_control_cr);
	g_test_add_func("/escape/control/lf", test_control_lf);
	g_test_add_func("/escape/control/bs", test_control_bs);
	g_test_add_func("/escape/control/tab", test_control_tab);

	/* Modes */
	g_test_add_func("/escape/mode/wrap", test_mode_wrap);
	g_test_add_func("/escape/mode/cursor-visible", test_mode_cursor_visible);
	g_test_add_func("/escape/mode/bracketed-paste", test_mode_bracketed_paste);
	g_test_add_func("/escape/mode/insert", test_mode_insert);

	/* Scroll Region */
	g_test_add_func("/escape/csi/scroll-region", test_csi_scroll_region);

	/* Cursor Save/Restore */
	g_test_add_func("/escape/cursor/save-restore", test_cursor_save_restore);
	g_test_add_func("/escape/cursor/csi-save-restore", test_csi_cursor_save_restore);

	/* Character Output */
	g_test_add_func("/escape/output/chars", test_char_output);
	g_test_add_func("/escape/output/crlf", test_crlf);

	/* Scrolling */
	g_test_add_func("/escape/csi/scroll-up", test_csi_scroll_up);
	g_test_add_func("/escape/csi/scroll-down", test_csi_scroll_down);

	/* OSC */
	g_test_add_func("/escape/osc/title", test_osc_title);
	g_test_add_func("/escape/osc/title-only", test_osc_title_only);

	/* Alternate Screen */
	g_test_add_func("/escape/altscreen", test_altscreen);

	/* Response */
	g_test_add_func("/escape/response/da", test_response_da);
	g_test_add_func("/escape/response/dsr", test_response_dsr);

	/* Stale CSI Args */
	g_test_add_func("/escape/csi/stale-args-cleared", test_csi_stale_args_cleared);
	g_test_add_func("/escape/csi/stale-args-cup-after-sgr", test_csi_stale_args_cup_after_sgr);

	/* DECSTBM Reset */
	g_test_add_func("/escape/csi/decstbm-no-args-reset", test_decstbm_no_args_reset);

	/* UTF-8 Split Boundary */
	g_test_add_func("/escape/utf8/split-boundary", test_utf8_split_boundary);

	/* Stale CSI Mode */
	g_test_add_func("/escape/csi/mode-not-stale", test_csi_mode_not_stale);

	/* Cursor Restore WRAPNEXT */
	g_test_add_func("/escape/cursor/restore-preserves-wrapnext",
	    test_cursor_restore_preserves_wrapnext);

	/* REP (CSI b) */
	g_test_add_func("/escape/csi/rep-wraps-at-line-end",
	    test_rep_wraps_at_line_end);
	g_test_add_func("/escape/csi/rep-uses-put-char-behavior",
	    test_rep_uses_put_char_behavior);

	return g_test_run();
}
