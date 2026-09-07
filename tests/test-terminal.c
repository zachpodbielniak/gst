/*
 * test-terminal.c - Tests for GstTerminal
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include "core/gst-terminal.h"
#include "core/gst-line.h"

static void
test_terminal_new(void)
{
    GstTerminal *term;

    term = gst_terminal_new(80, 24);
    g_assert_nonnull(term);
    g_assert_cmpint(gst_terminal_get_cols(term), ==, 80);
    g_assert_cmpint(gst_terminal_get_rows(term), ==, 24);

    g_object_unref(term);
}

static void
test_terminal_resize(void)
{
    GstTerminal *term;

    term = gst_terminal_new(80, 24);
    gst_terminal_resize(term, 120, 40);

    g_assert_cmpint(gst_terminal_get_cols(term), ==, 120);
    g_assert_cmpint(gst_terminal_get_rows(term), ==, 40);

    g_object_unref(term);
}

static void
test_terminal_cursor(void)
{
    GstTerminal *term;
    GstCursor *cursor;

    term = gst_terminal_new(80, 24);
    cursor = gst_terminal_get_cursor(term);

    g_assert_nonnull(cursor);
    g_assert_cmpint(cursor->x, ==, 0);
    g_assert_cmpint(cursor->y, ==, 0);

    gst_terminal_set_cursor_pos(term, 10, 5);
    g_assert_cmpint(cursor->x, ==, 10);
    g_assert_cmpint(cursor->y, ==, 5);

    g_object_unref(term);
}

static void
test_terminal_put_char(void)
{
    GstTerminal *term;
    GstGlyph *glyph;
    GstCursor *cursor;

    term = gst_terminal_new(80, 24);
    gst_terminal_put_char(term, 'A');

    /* Character should be at (0,0), cursor at (1,0) */
    glyph = gst_terminal_get_glyph(term, 0, 0);
    g_assert_nonnull(glyph);
    g_assert_cmpuint(glyph->rune, ==, 'A');

    cursor = gst_terminal_get_cursor(term);
    g_assert_cmpint(cursor->x, ==, 1);
    g_assert_cmpint(cursor->y, ==, 0);

    g_object_unref(term);
}

static void
test_terminal_modes(void)
{
    GstTerminal *term;

    term = gst_terminal_new(80, 24);

    /* Default modes should include WRAP and UTF8 */
    g_assert_true(gst_terminal_has_mode(term, GST_MODE_WRAP));
    g_assert_true(gst_terminal_has_mode(term, GST_MODE_UTF8));
    g_assert_false(gst_terminal_has_mode(term, GST_MODE_INSERT));

    /* Set insert mode */
    gst_terminal_set_mode(term, GST_MODE_INSERT, TRUE);
    g_assert_true(gst_terminal_has_mode(term, GST_MODE_INSERT));

    /* Clear insert mode */
    gst_terminal_set_mode(term, GST_MODE_INSERT, FALSE);
    g_assert_false(gst_terminal_has_mode(term, GST_MODE_INSERT));

    g_object_unref(term);
}

static void
test_terminal_altscreen_mode_matches_buffer(void)
{
	GstTerminal *term;

	term = gst_terminal_new(8, 2);
	gst_terminal_put_char(term, 'P');

	gst_terminal_set_mode(term, GST_MODE_ALTSCREEN, TRUE);
	g_assert_true(gst_terminal_has_mode(term, GST_MODE_ALTSCREEN));
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, ' ');
	gst_terminal_set_cursor_pos(term, 0, 0);
	gst_terminal_put_char(term, 'A');

	/* Setting an already enabled mode must not swap back to primary. */
	gst_terminal_set_mode(term, GST_MODE_ALTSCREEN, TRUE);
	g_assert_true(gst_terminal_has_mode(term, GST_MODE_ALTSCREEN));
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'A');

	gst_terminal_set_mode(term, GST_MODE_ALTSCREEN, FALSE);
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_ALTSCREEN));
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'P');

	gst_terminal_set_mode(term, GST_MODE_ALTSCREEN, FALSE);
	g_assert_false(gst_terminal_has_mode(term, GST_MODE_ALTSCREEN));
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'P');

	/* Both buffers survive a complete leave/reenter cycle. */
	gst_terminal_set_mode(term, GST_MODE_ALTSCREEN, TRUE);
	g_assert_true(gst_terminal_has_mode(term, GST_MODE_ALTSCREEN));
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'A');

	g_object_unref(term);
}

static void
test_terminal_clear(void)
{
    GstTerminal *term;
    GstGlyph *glyph;

    term = gst_terminal_new(80, 24);

    /* Put a character and then clear */
    gst_terminal_put_char(term, 'Z');
    gst_terminal_clear(term);

    glyph = gst_terminal_get_glyph(term, 0, 0);
    g_assert_nonnull(glyph);
    g_assert_cmpuint(glyph->rune, ==, ' ');

    g_object_unref(term);
}

static void
test_terminal_scroll_region(void)
{
    GstTerminal *term;
    gint top;
    gint bot;

    term = gst_terminal_new(80, 24);

    /* Default scroll region is full screen */
    gst_terminal_get_scroll_region(term, &top, &bot);
    g_assert_cmpint(top, ==, 0);
    g_assert_cmpint(bot, ==, 23);

    /* Set custom region */
    gst_terminal_set_scroll_region(term, 5, 15);
    gst_terminal_get_scroll_region(term, &top, &bot);
    g_assert_cmpint(top, ==, 5);
    g_assert_cmpint(bot, ==, 15);

    g_object_unref(term);
}

static void
test_terminal_reset(void)
{
    GstTerminal *term;
    GstCursor *cursor;

    term = gst_terminal_new(80, 24);

    /* Move cursor and set modes */
    gst_terminal_set_cursor_pos(term, 40, 12);
    gst_terminal_set_mode(term, GST_MODE_INSERT, TRUE);

    /* Reset */
    gst_terminal_reset(term, TRUE);

    cursor = gst_terminal_get_cursor(term);
    g_assert_cmpint(cursor->x, ==, 0);
    g_assert_cmpint(cursor->y, ==, 0);
    g_assert_false(gst_terminal_has_mode(term, GST_MODE_INSERT));
    g_assert_true(gst_terminal_has_mode(term, GST_MODE_WRAP));

    g_object_unref(term);
}

/* Feed one byte at a time: segmentation must survive both UTF-8 and PTY splits. */
static void
test_terminal_clusters(void)
{
	static const gchar *clusters[] = {
		"e\314\201\314\210",
		"\360\237\221\251\342\200\215\360\237\222\273",
		"\360\237\221\215\360\237\217\275",
		"\360\237\207\272\360\237\207\270",
		"1\357\270\217\342\203\243",
		"\342\235\244\357\270\217",
		"\341\204\200\341\205\241\341\206\250"
	};
	GstTerminal *term;
	GstGlyph *glyph;
	gchar buffer[7];
	guint i;
	const gchar *p;

	term = gst_terminal_new(16, 3);
	for (i = 0; i < G_N_ELEMENTS(clusters); i++) {
		gst_terminal_reset(term, TRUE);
		for (p = clusters[i]; *p != '\0'; p++) {
			gst_terminal_write(term, p, 1);
		}
		glyph = gst_terminal_get_glyph(term, 0, 0);
		g_assert_cmpuint(glyph->rune, ==, g_utf8_get_char(clusters[i]));
		g_assert_cmpstr(gst_glyph_get_text(glyph, buffer), ==, clusters[i]);
		g_assert_cmpint(gst_terminal_get_cursor(term)->x, ==, i == 0 ? 1 : 2);
		gst_terminal_write(term, "!", 1);
		g_assert_cmpuint(gst_terminal_get_glyph(term, i == 0 ? 1 : 2, 0)->rune, ==, '!');
	}
	/* Regional indicators pair, not one unbounded flag cluster. */
	gst_terminal_reset(term, TRUE);
	gst_terminal_write(term, clusters[3], -1);
	gst_terminal_write(term, clusters[3], -1);
	g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(term, 2, 0), buffer), ==, clusters[3]);
	g_object_unref(term);
}

/* Pending wrap and a wide dummy must not redirect a combining mark. */
static void
test_terminal_cluster_margins(void)
{
	GstTerminal *term;
	gchar buffer[7];

	term = gst_terminal_new(4, 3);
	gst_terminal_write(term, "abcd\314\201", -1);
	g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(term, 3, 0), buffer), ==, "d\314\201");
	g_assert_true(gst_cursor_is_wrap_pending(gst_terminal_get_cursor(term)));
	gst_terminal_reset(term, TRUE);
	gst_terminal_write(term, "ab\347\225\214\314\201", -1);
	g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(term, 2, 0), buffer), ==, "\347\225\214\314\201");
	g_assert_true(gst_glyph_is_dummy(gst_terminal_get_glyph(term, 3, 0)));
	/* A text-presentation base can widen only after VS16 arrives. */
	gst_terminal_reset(term, TRUE);
	gst_terminal_write(term, "abc1\357\270\217\342\203\243", -1);
	g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(term, 0, 1), buffer), ==, "1\357\270\217\342\203\243");
	g_assert_true(gst_line_is_wrapped(gst_terminal_get_line(term, 0)));
	gst_terminal_resize(term, 8, 3);
	g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(term, 3, 0), buffer), ==, "1\357\270\217\342\203\243");
	g_assert_cmpint(gst_terminal_get_cursor(term)->x, ==, 5);
	g_object_unref(term);
}

/* Copies made by history callbacks must outlive resize and terminal teardown. */
static void
capture_reflow_line(GstTerminal *term, GstLine *line, gint cols, gpointer data)
{
	GPtrArray *history = (GPtrArray *)data;

	(void)term;
	g_assert_cmpint(line->len, ==, cols);
	g_ptr_array_add(history, gst_line_copy(line));
}

static void
test_terminal_reflow(void)
{
	GstTerminal *term;
	GstCursor *cursor;
	gchar *text;

	/* A hard break remains hard; a soft break disappears when widened. */
	term = gst_terminal_new(4, 5);
	gst_terminal_write(term, "abcdef\r\nXY", -1);
	gst_terminal_cursor_save(term);
	gst_terminal_resize(term, 8, 5);
	text = gst_line_to_string(gst_terminal_get_line(term, 0));
	g_assert_cmpstr(text, ==, "abcdef  ");
	g_free(text);
	g_assert_false(gst_line_is_wrapped(gst_terminal_get_line(term, 0)));
	text = gst_line_to_string(gst_terminal_get_line(term, 1));
	g_assert_cmpstr(text, ==, "XY      ");
	g_free(text);
	cursor = gst_terminal_get_cursor(term);
	g_assert_cmpint(cursor->x, ==, 2);
	g_assert_cmpint(cursor->y, ==, 1);
	gst_terminal_resize(term, 3, 5);
	g_assert_cmpint(cursor->y, ==, 2);
	gst_terminal_move_to(term, 0, 0);
	gst_terminal_cursor_restore(term);
	g_assert_cmpint(cursor->x, ==, 2);
	g_assert_cmpint(cursor->y, ==, 2);
	g_object_unref(term);

	/* Reflow a pending-wrap cursor to an ordinary insertion point. */
	term = gst_terminal_new(4, 3);
	gst_terminal_write(term, "abcd", -1);
	gst_terminal_resize(term, 8, 3);
	g_assert_cmpint(gst_terminal_get_cursor(term)->x, ==, 4);
	g_assert_false(gst_cursor_is_wrap_pending(gst_terminal_get_cursor(term)));
	gst_terminal_write(term, "e", -1);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 4, 0)->rune, ==, 'e');
	g_object_unref(term);
}

static void
test_terminal_resize_history_alt(void)
{
	GstTerminal *term;
	GPtrArray *history;
	gchar *text;

	history = g_ptr_array_new_with_free_func((GDestroyNotify)gst_line_free);
	term = gst_terminal_new(4, 3);
	g_signal_connect(term, "line-scrolled-out", G_CALLBACK(capture_reflow_line), history);
	gst_terminal_write(term, "e\314\201\r\nB\r\nC", -1);
	gst_terminal_resize(term, 4, 2);
	g_assert_cmpuint(history->len, ==, 1);
	text = gst_line_to_string((GstLine *)g_ptr_array_index(history, 0));
	g_assert_cmpstr(text, ==, "e\314\201   ");
	g_free(text);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'B');
	g_assert_cmpint(gst_terminal_get_cursor(term)->y, ==, 1);
	gst_terminal_cursor_save(term);
	gst_terminal_set_mode(term, GST_MODE_ALTSCREEN, TRUE);
	gst_terminal_write(term, "\033[H123456789\r\nA\r\nB", -1);
	g_assert_cmpuint(history->len, ==, 1);
	gst_terminal_resize(term, 2, 1);
	/* Primary displacement is preserved even while alternate is active. */
	g_assert_cmpuint(history->len, ==, 2);
	gst_terminal_set_mode(term, GST_MODE_ALTSCREEN, FALSE);
	gst_terminal_cursor_restore(term);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'C');
	g_assert_cmpint(gst_terminal_get_cursor(term)->y, ==, 0);
	g_object_unref(term);
	text = gst_line_to_string((GstLine *)g_ptr_array_index(history, 0));
	g_assert_cmpstr(text, ==, "e\314\201   ");
	g_free(text);
	g_ptr_array_unref(history);
}

static void
test_terminal_wide_reflow_edits(void)
{
	GstTerminal *term;
	gchar *text;

	term = gst_terminal_new(4, 6);
	gst_terminal_write(term, "abc\347\225\214\314\201Z", -1);
	gst_terminal_resize(term, 1, 6);
	gst_terminal_resize(term, 8, 6);
	text = gst_line_to_string(gst_terminal_get_line(term, 0));
	g_assert_cmpstr(text, ==, "abc\347\225\214\314\201Z  ");
	g_free(text);
	gst_terminal_clear_region(term, 4, 0, 4, 0);
	g_assert_false(gst_glyph_is_wide(gst_terminal_get_glyph(term, 3, 0)));
	g_assert_false(gst_glyph_is_dummy(gst_terminal_get_glyph(term, 4, 0)));
	g_object_unref(term);
}

/* Explicit spaces and a cluster interrupted only by resize are real content. */
static void
test_terminal_reflow_spaces_and_stream(void)
{
	GstTerminal *term;
	gchar buffer[7];
	gchar *text;

	term = gst_terminal_new(4, 6);
	gst_terminal_write(term, "a   \r\nB", -1);
	gst_terminal_resize(term, 2, 6);
	g_assert_cmpint(gst_terminal_get_line(term, 1)->used, ==, 2);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 2)->rune, ==, 'B');
	gst_terminal_resize(term, 8, 6);
	g_assert_cmpint(gst_terminal_get_line(term, 0)->used, ==, 4);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 1)->rune, ==, 'B');
	gst_terminal_reset(term, TRUE);
	gst_terminal_write(term, "abcde", -1);
	gst_terminal_resize(term, 3, 6);
	gst_terminal_write(term, "\314\201", -1);
	g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(term, 1, 1), buffer), ==, "e\314\201");
	gst_terminal_write(term, "\033[2b", -1);
	g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(term, 2, 1), buffer), ==, "e\314\201");
	g_assert_cmpstr(gst_glyph_get_text(gst_terminal_get_glyph(term, 0, 2), buffer), ==, "e\314\201");
	gst_terminal_reset(term, TRUE);
	gst_terminal_resize(term, 4, 3);
	gst_terminal_set_mode(term, GST_MODE_ALTSCREEN, TRUE);
	gst_terminal_write(term, "abcdef", -1);
	gst_terminal_resize(term, 8, 3);
	text = gst_line_to_string(gst_terminal_get_line(term, 0));
	g_assert_cmpstr(text, ==, "abcd    ");
	g_free(text);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 1)->rune, ==, 'e');
	g_assert_false(gst_line_is_wrapped(gst_terminal_get_line(term, 0)));
	g_object_unref(term);
}

int
main(
    int     argc,
    char    **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/terminal/new", test_terminal_new);
    g_test_add_func("/terminal/resize", test_terminal_resize);
    g_test_add_func("/terminal/cursor", test_terminal_cursor);
    g_test_add_func("/terminal/put-char", test_terminal_put_char);
    g_test_add_func("/terminal/modes", test_terminal_modes);
	g_test_add_func("/terminal/altscreen-mode-matches-buffer",
		test_terminal_altscreen_mode_matches_buffer);
    g_test_add_func("/terminal/clear", test_terminal_clear);
    g_test_add_func("/terminal/scroll-region", test_terminal_scroll_region);
    g_test_add_func("/terminal/reset", test_terminal_reset);
	g_test_add_func("/terminal/unicode/clusters", test_terminal_clusters);
	g_test_add_func("/terminal/unicode/margins", test_terminal_cluster_margins);
	g_test_add_func("/terminal/reflow/logical-lines", test_terminal_reflow);
	g_test_add_func("/terminal/reflow/history-alt", test_terminal_resize_history_alt);
	g_test_add_func("/terminal/reflow/wide-edits", test_terminal_wide_reflow_edits);
	g_test_add_func("/terminal/reflow/spaces-stream-alt", test_terminal_reflow_spaces_and_stream);

    return g_test_run();
}
