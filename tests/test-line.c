/*
 * test-line.c - Tests for GstLine
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include "core/gst-line.h"

static void
test_line_delete_chars_large_count(void)
{
	GstLine *line;
	gchar *text;
	gint i;

	line = gst_line_new(4);
	for (i = 0; i < line->len; i++) {
		gst_line_get_glyph(line, i)->rune = 'A' + i;
	}
	gst_line_set_dirty(line, FALSE);

	/* Clamp before adding col: 1 + G_MAXINT overflows a signed gint. */
	gst_line_delete_chars(line, 1, G_MAXINT);

	text = gst_line_to_string(line);
	g_assert_cmpstr(text, ==, "A   ");
	g_assert_true(gst_line_is_dirty(line));
	g_free(text);
	gst_line_free(line);
}

int
main(
	int argc,
	char **argv
){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/line/delete-chars-large-count",
		test_line_delete_chars_large_count);
	return g_test_run();
}
