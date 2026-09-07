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

/* A saved insertion point at the resized edge must not overwrite the last cell. */
static void
test_saved_primary_resize(gconstpointer mode)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 4);
	g_autofree gchar *enter = g_strdup_printf("\033[?%dh", GPOINTER_TO_INT(mode));
	g_autofree gchar *leave = g_strdup_printf("\033[?%dl", GPOINTER_TO_INT(mode));
	g_autofree gchar *text = NULL;

	gst_terminal_write(term, "abcde\0337", -1);
	gst_terminal_write(term, enter, -1);
	gst_terminal_write(term, "\033[HXY\0337", -1);
	gst_terminal_resize(term, 5, 4);
	gst_terminal_write(term, leave, -1);
	if (GPOINTER_TO_INT(mode) == 1047) {
		gst_terminal_write(term, "\0338", -1);
	}
	g_assert_cmpint(gst_terminal_get_cursor(term)->x, ==, 4);
	g_assert_cmpint(gst_terminal_get_cursor(term)->y, ==, 0);
	g_assert_true(gst_cursor_is_wrap_pending(gst_terminal_get_cursor(term)));
	gst_terminal_write(term, "F", -1);
	gst_terminal_resize(term, 8, 4);
	text = gst_line_to_string(gst_terminal_get_line(term, 0));
	g_assert_cmpstr(text, ==, "abcdeF  ");
}

/* Cursor coordinates on non-text wide-wrap padding still need remapping. */
static void
test_reflow_padding_cursor(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(4, 4);
	g_autofree gchar *text = NULL;

	gst_terminal_write(term, "abc\347\225\214Z", -1);
	gst_terminal_set_cursor_pos(term, 3, 0);
	gst_terminal_cursor_save(term);
	gst_terminal_resize(term, 8, 4);
	g_assert_cmpint(gst_terminal_get_cursor(term)->x, ==, 3);
	gst_terminal_set_cursor_pos(term, 0, 0);
	gst_terminal_cursor_restore(term);
	g_assert_cmpint(gst_terminal_get_cursor(term)->x, ==, 3);
	text = gst_line_to_string(gst_terminal_get_line(term, 0));
	g_assert_cmpstr(text, ==, "abc\347\225\214Z  ");
}

/* Long mark runs, REP snapshots and screen edits must own independent text. */
static void
test_long_cluster_rep_edits(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 3);
	g_autoptr(GString) expected = g_string_new("e");
	guint i;

	gst_terminal_put_char(term, 'e');
	for (i = 0; i < 32768; i++) {
		gst_terminal_put_char(term, 0x301);
		g_string_append(expected, "\314\201");
	}
	gst_terminal_write(term, "\033[2b", -1);
	g_assert_cmpstr(gst_terminal_get_glyph(term, 2, 0)->cluster, ==, expected->str);
	gst_terminal_write(term, "\033[H\033[P\033[@", -1);
	g_assert_cmpstr(gst_terminal_get_glyph(term, 1, 0)->cluster, ==, expected->str);
	g_assert_cmpstr(gst_terminal_get_glyph(term, 2, 0)->cluster, ==, expected->str);
	gst_terminal_write(term, "\033[X\033[4G\033[b", -1);
	g_assert_cmpstr(gst_terminal_get_glyph(term, 3, 0)->cluster, ==, expected->str);
	gst_terminal_reset(term, TRUE);
	gst_terminal_write(term, "\033[b", -1);
	g_assert_null(gst_terminal_get_glyph(term, 0, 0)->cluster);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, ' ');
}

/* History callbacks see the old grid and must copy the temporary new-width row. */
static void
capture_resize_history(GstTerminal *term, GstLine *line, gint cols, GPtrArray *history)
{
	g_assert_cmpint(gst_terminal_get_cols(term), ==, 8);
	g_assert_cmpint(cols, ==, 4);
	g_assert_cmpint(line->len, ==, 4);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'a');
	g_ptr_array_add(history, gst_line_copy(line));
}

/* The resize notification is the publication point for dimensions and cursors. */
static void
check_resize_published(GstTerminal *term, gint cols, gint rows, GPtrArray *history)
{
	g_assert_cmpuint(history->len, ==, 1);
	g_assert_cmpint(cols, ==, 4);
	g_assert_cmpint(rows, ==, 2);
	g_assert_cmpint(gst_terminal_get_cols(term), ==, cols);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 'e');
	g_assert_cmpint(gst_terminal_get_cursor(term)->x, ==, 3);
	g_assert_cmpint(gst_terminal_get_cursor(term)->y, ==, 1);
	g_assert_true(gst_cursor_is_wrap_pending(gst_terminal_get_cursor(term)));
}

/* Neither history lifetime nor notification ordering may discard packed data. */
static void
test_reflow_signal_order(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 2);
	g_autoptr(GPtrArray) history = g_ptr_array_new_with_free_func((GDestroyNotify)gst_line_free);
	g_autoptr(GString) text = g_string_new(NULL);
	gint y;

	gst_terminal_write(term, "abcdefghijkl", -1);
	g_signal_connect(term, "line-scrolled-out", G_CALLBACK(capture_resize_history), history);
	g_signal_connect(term, "resize", G_CALLBACK(check_resize_published), history);
	gst_terminal_resize(term, 4, 2);
	for (y = -1; y < 2; y++) {
		GstLine *line = y < 0 ? (GstLine *)g_ptr_array_index(history, 0)
		    : gst_terminal_get_line(term, y);
		g_autofree gchar *row = gst_line_to_string_range(line, 0, line->used);

		g_string_append(text, row);
	}
	g_assert_cmpstr(text->str, ==, "abcdefghijkl");
}

/* Cursor attribute snapshots own their clusters, including through reset. */
static void
test_cursor_cluster_reset(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 3);
	GstCursor *cursor = gst_terminal_get_cursor(term);

	cursor->glyph.rune = 'e';
	gst_glyph_append(&cursor->glyph, 0x301);
	gst_terminal_cursor_save(term);
	gst_glyph_reset(&cursor->glyph);
	gst_terminal_cursor_restore(term);
	g_assert_cmpstr(cursor->glyph.cluster, ==, "e\314\201");
	gst_terminal_write(term, "\033[?1047h", -1);
	gst_terminal_cursor_save(term);
	gst_terminal_reset(term, TRUE);
	gst_terminal_cursor_restore(term);
	g_assert_null(cursor->glyph.cluster);
	g_assert_cmpuint(cursor->glyph.rune, ==, ' ');
	gst_terminal_write(term, "\033[?1047h\0338", -1);
	g_assert_null(cursor->glyph.cluster);
}

/* Kitty's private-use placeholder and row/column diacritics form one cell. */
static void
test_placeholder_marks(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 3);
	g_autoptr(GString) expected = g_string_new(NULL);
	g_autoptr(GstSelection) selection = gst_selection_new(term);
	g_autofree gchar *text = NULL;

	g_string_append_unichar(expected, 0x10eeee);
	g_string_append_unichar(expected, 0x305);
	g_string_append_unichar(expected, 0x30d);
	gst_terminal_write(term, expected->str, -1);
	g_assert_cmpuint(gst_terminal_get_glyph(term, 0, 0)->rune, ==, 0x10eeee);
	g_assert_cmpstr(gst_terminal_get_glyph(term, 0, 0)->cluster, ==, expected->str);
	g_assert_cmpint(gst_terminal_get_cursor(term)->x, ==, 1);
	gst_selection_start(selection, 0, 0, GST_SELECTION_SNAP_NONE);
	gst_selection_extend(selection, 1, 0, GST_SELECTION_TYPE_REGULAR, FALSE);
	gst_selection_extend(selection, 0, 0, GST_SELECTION_TYPE_REGULAR, TRUE);
	text = gst_selection_get_text(selection);
	g_assert_cmpstr(text, ==, expected->str);
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
	g_test_add_data_func("/edge/reflow/saved-primary-1047", GINT_TO_POINTER(1047), test_saved_primary_resize);
	g_test_add_data_func("/edge/reflow/saved-primary-1049", GINT_TO_POINTER(1049), test_saved_primary_resize);
	g_test_add_func("/edge/reflow/padding-cursor", test_reflow_padding_cursor);
	g_test_add_func("/edge/reflow/signal-order", test_reflow_signal_order);
	g_test_add_func("/edge/reset/cursor-clusters", test_cursor_cluster_reset);
	g_test_add_func("/edge/unicode/long-rep-edits", test_long_cluster_rep_edits);
	g_test_add_func("/edge/unicode/placeholder-marks", test_placeholder_marks);
	return g_test_run();
}
