/*
 * test-selection.c - Tests for GstSelection
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests the selection system: start, extend, clear,
 * selected check, text extraction, and scroll adjustment.
 */

#include <glib.h>
#include "core/gst-terminal.h"
#include "selection/gst-selection.h"
#include "gst-enums.h"

/*
 * Helper: fill a terminal row with a string.
 */
static void
fill_row(
	GstTerminal *term,
	gint        row,
	const gchar *text
){
	gst_terminal_set_cursor_pos(term, 0, row);
	gst_terminal_write(term, text, -1);
}

/* ===== Basic Selection Tests ===== */

/*
 * Test creating an empty selection.
 */
static void
test_selection_new(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	g_assert_nonnull(sel);
	g_assert_true(gst_selection_is_empty(sel));
	g_assert_cmpint(gst_selection_get_mode(sel), ==, GST_SELECTION_IDLE);

	g_object_unref(sel);
	g_object_unref(term);
}

/*
 * Test starting a selection.
 */
static void
test_selection_start(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	gst_selection_start(sel, 5, 3, GST_SELECTION_SNAP_NONE);

	/* After start, mode is EMPTY (not yet dragged) */
	g_assert_cmpint(gst_selection_get_mode(sel), ==, GST_SELECTION_EMPTY);

	g_object_unref(sel);
	g_object_unref(term);
}

/*
 * Test starting with word snap.
 * When snap is set, mode becomes READY immediately.
 */
static void
test_selection_start_snap(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	gst_selection_start(sel, 5, 3, GST_SELECTION_SNAP_WORD);

	/* With snap, mode goes directly to READY */
	g_assert_cmpint(gst_selection_get_mode(sel), ==, GST_SELECTION_READY);

	g_object_unref(sel);
	g_object_unref(term);
}

/*
 * Test extending a selection.
 */
static void
test_selection_extend(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	fill_row(term, 0, "Hello World");

	gst_selection_start(sel, 0, 0, GST_SELECTION_SNAP_NONE);
	gst_selection_extend(sel, 4, 0, GST_SELECTION_TYPE_REGULAR, FALSE);

	/* Should be in READY state */
	g_assert_cmpint(gst_selection_get_mode(sel), ==, GST_SELECTION_READY);

	/* Cells 0-4 on row 0 should be selected */
	g_assert_true(gst_selection_selected(sel, 0, 0));
	g_assert_true(gst_selection_selected(sel, 4, 0));

	/* Cell 5 should NOT be selected */
	g_assert_false(gst_selection_selected(sel, 5, 0));

	/* Row 1 should NOT be selected */
	g_assert_false(gst_selection_selected(sel, 0, 1));

	g_object_unref(sel);
	g_object_unref(term);
}

/*
 * Test finalizing a selection (done=TRUE).
 * Must first extend without done to transition EMPTY->READY,
 * then extend with done to finalize. If extend(done=TRUE) is
 * called while still EMPTY, the selection is cleared (click
 * with no drag).
 */
static void
test_selection_extend_done(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	fill_row(term, 0, "Hello World");

	gst_selection_start(sel, 0, 0, GST_SELECTION_SNAP_NONE);

	/* First extend without done (drag) to transition to READY */
	gst_selection_extend(sel, 10, 0, GST_SELECTION_TYPE_REGULAR, FALSE);
	g_assert_cmpint(gst_selection_get_mode(sel), ==, GST_SELECTION_READY);

	/* Now finalize */
	gst_selection_extend(sel, 10, 0, GST_SELECTION_TYPE_REGULAR, TRUE);

	/* After done, mode goes to IDLE */
	g_assert_cmpint(gst_selection_get_mode(sel), ==, GST_SELECTION_IDLE);

	/* Cells should still be queryable as selected */
	g_assert_true(gst_selection_selected(sel, 5, 0));

	g_object_unref(sel);
	g_object_unref(term);
}

/*
 * Test clearing a selection.
 */
static void
test_selection_clear(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	gst_selection_start(sel, 0, 0, GST_SELECTION_SNAP_NONE);
	gst_selection_extend(sel, 10, 0, GST_SELECTION_TYPE_REGULAR, FALSE);
	gst_selection_clear(sel);

	g_assert_true(gst_selection_is_empty(sel));
	g_assert_false(gst_selection_selected(sel, 5, 0));

	g_object_unref(sel);
	g_object_unref(term);
}

/* ===== Multi-line Selection Tests ===== */

/*
 * Test multi-line regular selection.
 */
static void
test_selection_multiline(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	fill_row(term, 0, "Line 0 text");
	fill_row(term, 1, "Line 1 text");
	fill_row(term, 2, "Line 2 text");

	/* Select from row 0 col 5 to row 2 col 3 */
	gst_selection_start(sel, 5, 0, GST_SELECTION_SNAP_NONE);
	gst_selection_extend(sel, 3, 2, GST_SELECTION_TYPE_REGULAR, FALSE);

	/* Row 0: cols 5+ should be selected, cols 0-4 not */
	g_assert_false(gst_selection_selected(sel, 4, 0));
	g_assert_true(gst_selection_selected(sel, 5, 0));
	g_assert_true(gst_selection_selected(sel, 10, 0));

	/* Row 1: entire row should be selected */
	g_assert_true(gst_selection_selected(sel, 0, 1));
	g_assert_true(gst_selection_selected(sel, 10, 1));

	/* Row 2: cols 0-3 should be selected */
	g_assert_true(gst_selection_selected(sel, 0, 2));
	g_assert_true(gst_selection_selected(sel, 3, 2));
	g_assert_false(gst_selection_selected(sel, 4, 2));

	g_object_unref(sel);
	g_object_unref(term);
}

/* ===== Text Extraction Tests ===== */

/*
 * Test extracting text from a single-line selection.
 */
static void
test_selection_get_text_single(void)
{
	GstTerminal *term;
	GstSelection *sel;
	gchar *text;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	fill_row(term, 0, "Hello World");

	/* Select "Hello" (cols 0-4) */
	gst_selection_set_range(sel, 0, 0, 4, 0);

	text = gst_selection_get_text(sel);
	g_assert_nonnull(text);
	/* Single-line selection within line, no trailing newline */
	g_assert_cmpstr(text, ==, "Hello");

	g_free(text);
	g_object_unref(sel);
	g_object_unref(term);
}

/*
 * Test extracting text from a multi-line selection.
 */
static void
test_selection_get_text_multiline(void)
{
	GstTerminal *term;
	GstSelection *sel;
	gchar *text;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	fill_row(term, 0, "AAA");
	fill_row(term, 1, "BBB");
	fill_row(term, 2, "CCC");

	/* Select all three rows */
	gst_selection_set_range(sel, 0, 0, 2, 2);

	text = gst_selection_get_text(sel);
	g_assert_nonnull(text);
	/* Lines get newlines between them; last line may or may not */
	g_assert_cmpstr(text, ==, "AAA\nBBB\nCCC");

	g_free(text);
	g_object_unref(sel);
	g_object_unref(term);
}

/* ===== Rectangular Selection Tests ===== */

/*
 * Test rectangular selection checking.
 */
static void
test_selection_rectangular(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	fill_row(term, 0, "Hello World Here");
	fill_row(term, 1, "Foo   Bar   Baz");
	fill_row(term, 2, "AAAA  BBBB  CCC");

	/* Start regular then extend as rectangular */
	gst_selection_start(sel, 6, 0, GST_SELECTION_SNAP_NONE);
	gst_selection_extend(sel, 10, 2, GST_SELECTION_TYPE_RECTANGULAR, FALSE);

	/* In rectangular mode, only cols 6-10 on rows 0-2 are selected */
	g_assert_true(gst_selection_selected(sel, 6, 0));
	g_assert_true(gst_selection_selected(sel, 10, 1));
	g_assert_true(gst_selection_selected(sel, 8, 2));

	/* Col 5 should NOT be selected */
	g_assert_false(gst_selection_selected(sel, 5, 1));

	/* Col 11 should NOT be selected */
	g_assert_false(gst_selection_selected(sel, 11, 1));

	g_object_unref(sel);
	g_object_unref(term);
}

/* ===== Scroll Adjustment Tests ===== */

/*
 * Test selection scroll adjustment.
 * When the terminal scrolls, selection coordinates shift.
 */
static void
test_selection_scroll(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	fill_row(term, 5, "Selected text");

	/* Select row 5 */
	gst_selection_set_range(sel, 0, 5, 12, 5);

	g_assert_true(gst_selection_selected(sel, 0, 5));

	/* Scroll up by 1 (selection shifts down) */
	gst_selection_scroll(sel, 0, -1);

	/* Row 5 should no longer be selected (shifted to row 6) */
	/* But since we scrolled by -1, coords shifted: ob.y/oe.y -= 1 => row 4 */
	/* Actually selscroll adds n to ob.y/oe.y, so scroll(-1) => row 4 */
	g_assert_false(gst_selection_selected(sel, 0, 5));
	g_assert_true(gst_selection_selected(sel, 0, 4));

	g_object_unref(sel);
	g_object_unref(term);
}

/*
 * Test that selection is cleared when it straddles scroll boundary.
 */
static void
test_selection_scroll_clear(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 10);
	sel = gst_selection_new(term);

	/* Set scroll region to rows 2-7 */
	gst_terminal_set_scroll_region(term, 2, 7);

	/* Selection spans across scroll boundary: row 1 (outside) to row 5 (inside) */
	gst_selection_set_range(sel, 0, 1, 10, 5);

	g_assert_false(gst_selection_is_empty(sel));

	/* Scrolling should clear it since it straddles the boundary */
	gst_selection_scroll(sel, 2, 1);

	g_assert_true(gst_selection_is_empty(sel));

	g_object_unref(sel);
	g_object_unref(term);
}

/* ===== Alt Screen Tests ===== */

/*
 * Test that selection on primary screen is not visible on alt screen.
 */
static void
test_selection_altscreen(void)
{
	GstTerminal *term;
	GstSelection *sel;

	term = gst_terminal_new(80, 24);
	sel = gst_selection_new(term);

	fill_row(term, 0, "Primary text");

	/* Make selection on primary screen */
	gst_selection_set_range(sel, 0, 0, 11, 0);

	g_assert_true(gst_selection_selected(sel, 5, 0));

	/* Switch to alt screen */
	gst_terminal_write(term, "\033[?1049h", -1);

	/* Selection should not be visible on alt screen */
	g_assert_false(gst_selection_selected(sel, 5, 0));

	/* Switch back */
	gst_terminal_write(term, "\033[?1049l", -1);

	/* Selection should be visible again */
	g_assert_true(gst_selection_selected(sel, 5, 0));

	g_object_unref(sel);
	g_object_unref(term);
}

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/selection/new", test_selection_new);
	g_test_add_func("/selection/start", test_selection_start);
	g_test_add_func("/selection/start-snap", test_selection_start_snap);
	g_test_add_func("/selection/extend", test_selection_extend);
	g_test_add_func("/selection/extend-done", test_selection_extend_done);
	g_test_add_func("/selection/clear", test_selection_clear);
	g_test_add_func("/selection/multiline", test_selection_multiline);
	g_test_add_func("/selection/get-text-single", test_selection_get_text_single);
	g_test_add_func("/selection/get-text-multiline", test_selection_get_text_multiline);
	g_test_add_func("/selection/rectangular", test_selection_rectangular);
	g_test_add_func("/selection/scroll", test_selection_scroll);
	g_test_add_func("/selection/scroll-clear", test_selection_scroll_clear);
	g_test_add_func("/selection/altscreen", test_selection_altscreen);

	return g_test_run();
}
