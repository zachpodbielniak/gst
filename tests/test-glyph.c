/*
 * test-glyph.c - Tests for GstGlyph boxed type
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include "boxed/gst-glyph.h"
#include "boxed/gst-cursor.h"
#include "core/gst-line.h"

static void
test_glyph_new(void)
{
    GstGlyph *glyph;

    glyph = gst_glyph_new('A', GST_GLYPH_ATTR_BOLD, 7, 0);
    g_assert_nonnull(glyph);
    g_assert_cmpuint(glyph->rune, ==, 'A');
    g_assert_true(glyph->attr & GST_GLYPH_ATTR_BOLD);
    g_assert_cmpuint(glyph->fg, ==, 7);
    g_assert_cmpuint(glyph->bg, ==, 0);

    gst_glyph_free(glyph);
}

static void
test_glyph_new_simple(void)
{
    GstGlyph *glyph;

    glyph = gst_glyph_new_simple('B');
    g_assert_nonnull(glyph);
    g_assert_cmpuint(glyph->rune, ==, 'B');
    g_assert_cmpuint(glyph->attr, ==, GST_GLYPH_ATTR_NONE);
    g_assert_cmpuint(glyph->fg, ==, GST_COLOR_DEFAULT_FG);
    g_assert_cmpuint(glyph->bg, ==, GST_COLOR_DEFAULT_BG);

    gst_glyph_free(glyph);
}

static void
test_glyph_copy(void)
{
    GstGlyph *original;
    GstGlyph *copy;

    original = gst_glyph_new('C', GST_GLYPH_ATTR_ITALIC, 1, 2);
    copy = gst_glyph_copy(original);

    g_assert_nonnull(copy);
    g_assert_true(gst_glyph_equal(original, copy));
    g_assert_true(original != copy);

    gst_glyph_free(original);
    gst_glyph_free(copy);
}

static void
test_glyph_equal(void)
{
    GstGlyph *a;
    GstGlyph *b;

    a = gst_glyph_new('D', GST_GLYPH_ATTR_NONE, 7, 0);
    b = gst_glyph_new('D', GST_GLYPH_ATTR_NONE, 7, 0);

    g_assert_true(gst_glyph_equal(a, b));

    b->rune = 'E';
    g_assert_false(gst_glyph_equal(a, b));

    gst_glyph_free(a);
    gst_glyph_free(b);
}

static void
test_glyph_is_empty(void)
{
    GstGlyph *space;
    GstGlyph *letter;

    space = gst_glyph_new_simple(' ');
    letter = gst_glyph_new_simple('X');

    g_assert_true(gst_glyph_is_empty(space));
    g_assert_false(gst_glyph_is_empty(letter));

    gst_glyph_free(space);
    gst_glyph_free(letter);
}

static void
test_glyph_attrs(void)
{
    GstGlyph *glyph;

    glyph = gst_glyph_new_simple('F');

    g_assert_false(gst_glyph_has_attr(glyph, GST_GLYPH_ATTR_BOLD));

    gst_glyph_set_attr(glyph, GST_GLYPH_ATTR_BOLD);
    g_assert_true(gst_glyph_has_attr(glyph, GST_GLYPH_ATTR_BOLD));

    gst_glyph_set_attr(glyph, GST_GLYPH_ATTR_ITALIC);
    g_assert_true(gst_glyph_has_attr(glyph, GST_GLYPH_ATTR_BOLD));
    g_assert_true(gst_glyph_has_attr(glyph, GST_GLYPH_ATTR_ITALIC));

    gst_glyph_clear_attr(glyph, GST_GLYPH_ATTR_BOLD);
    g_assert_false(gst_glyph_has_attr(glyph, GST_GLYPH_ATTR_BOLD));
    g_assert_true(gst_glyph_has_attr(glyph, GST_GLYPH_ATTR_ITALIC));

    gst_glyph_free(glyph);
}

static void
test_glyph_wide(void)
{
    GstGlyph *glyph;

    glyph = gst_glyph_new_simple('G');

    g_assert_false(gst_glyph_is_wide(glyph));
    g_assert_false(gst_glyph_is_dummy(glyph));

    gst_glyph_set_attr(glyph, GST_GLYPH_ATTR_WIDE);
    g_assert_true(gst_glyph_is_wide(glyph));

    gst_glyph_free(glyph);
}

static void
test_glyph_reset(void)
{
    GstGlyph *glyph;

    glyph = gst_glyph_new('H', GST_GLYPH_ATTR_BOLD | GST_GLYPH_ATTR_ITALIC, 3, 4);
    gst_glyph_reset(glyph);

    g_assert_cmpuint(glyph->rune, ==, ' ');
    g_assert_cmpuint(glyph->attr, ==, GST_GLYPH_ATTR_NONE);
    g_assert_cmpuint(glyph->fg, ==, GST_COLOR_DEFAULT_FG);
    g_assert_cmpuint(glyph->bg, ==, GST_COLOR_DEFAULT_BG);

    gst_glyph_free(glyph);
}

static void
test_glyph_gtype(void)
{
    GType type;

    type = gst_glyph_get_type();
    g_assert_true(type != G_TYPE_INVALID);
    g_assert_cmpstr(g_type_name(type), ==, "GstGlyph");
}

/* Exercise independent ownership through every boxed copy and line move. */
static void
test_glyph_cluster_ownership(void)
{
	GstGlyph glyph = GST_GLYPH_INIT;
	GstGlyph *copy;
	GstLine *line;
	GstLine *saved;
	GstCursor *cursor;
	GstCursor *saved_cursor;
	gchar buffer[7];
	gchar *text;
	gint i;

	glyph.rune = 'e';
	gst_glyph_append(&glyph, 0x301);
	copy = gst_glyph_copy(&glyph);
	g_assert_true(gst_glyph_equal(&glyph, copy));
	g_assert_true(copy->cluster != glyph.cluster);
	gst_glyph_assign(&glyph, &glyph);
	gst_glyph_append(&glyph, 0x308);
	g_assert_cmpstr(gst_glyph_get_text(copy, buffer), ==, "e\314\201");
	g_assert_false(gst_glyph_equal(&glyph, copy));

	line = gst_line_new(6);
	for (i = 0; i < 6; i++) {
		gst_line_set_glyph(line, i, copy);
	}
	saved = gst_line_copy(line);
	gst_line_delete_chars(line, 1, 2);
	gst_line_insert_blanks(line, 1, 2);
	gst_line_resize(line, 3);
	gst_line_resize(line, 8);
	gst_line_clear_range(line, 0, 2);
	gst_line_clear(line);
	gst_line_free(line);
	text = gst_line_to_string(saved);
	g_assert_cmpstr(text, ==, "e\314\201e\314\201e\314\201e\314\201e\314\201e\314\201");
	g_free(text);
	gst_line_free(saved);

	cursor = gst_cursor_new();
	gst_glyph_assign(&cursor->glyph, copy);
	saved_cursor = gst_cursor_copy(cursor);
	gst_cursor_reset(cursor);
	gst_cursor_restore(cursor, saved_cursor);
	gst_cursor_free(saved_cursor);
	g_assert_cmpstr(gst_glyph_get_text(&cursor->glyph, buffer), ==, "e\314\201");
	gst_cursor_free(cursor);
	gst_glyph_reset(&glyph);
	g_assert_null(glyph.cluster);
	gst_glyph_free(copy);
}

/* Copies must describe their own allocation, not the source's spare capacity. */
static void
test_cluster_growth_copies(void)
{
	GstGlyph glyph = GST_GLYPH_INIT;
	GstGlyph *copy;
	GstLine *line, *line_copy;
	GstCursor *cursor, *cursor_copy;
	gsize capacity;
	guint i, growths = 0;

	glyph.rune = 'e';
	capacity = 0;
	for (i = 0; i < 65536; i++) {
		gst_glyph_append(&glyph, 0x301);
		if (capacity != glyph.cluster_capacity) {
			capacity = glyph.cluster_capacity;
			growths++;
		}
	}
	g_assert_cmpuint(growths, <, 20);
	g_assert_cmpuint(glyph.cluster_len, ==, 131073);
	g_assert_cmpuint(strlen(glyph.cluster), ==, glyph.cluster_len);
	copy = gst_glyph_copy(&glyph);
	line = gst_line_new(2);
	gst_line_set_glyph(line, 0, &glyph);
	line_copy = gst_line_copy(line);
	cursor = gst_cursor_new();
	gst_glyph_assign(&cursor->glyph, &glyph);
	cursor_copy = gst_cursor_copy(cursor);
	gst_glyph_assign(&glyph, &glyph);
	gst_glyph_append(&glyph, 0x308);
	gst_glyph_append(copy, 0x308);
	gst_glyph_append(&line_copy->glyphs[0], 0x308);
	gst_glyph_append(&cursor_copy->glyph, 0x308);
	g_assert_true(gst_glyph_equal(&glyph, copy));
	g_assert_true(gst_glyph_equal(&glyph, &line_copy->glyphs[0]));
	g_assert_true(gst_glyph_equal(&glyph, &cursor_copy->glyph));
	g_assert_cmpuint(line->glyphs[0].cluster_len, ==, 131073);
	gst_glyph_clear(&glyph);
	gst_glyph_free(copy);
	gst_line_free(line);
	gst_line_free(line_copy);
	gst_cursor_free(cursor);
	gst_cursor_free(cursor_copy);
}

int
main(
    int     argc,
    char    **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/glyph/new", test_glyph_new);
    g_test_add_func("/glyph/new-simple", test_glyph_new_simple);
    g_test_add_func("/glyph/copy", test_glyph_copy);
    g_test_add_func("/glyph/equal", test_glyph_equal);
    g_test_add_func("/glyph/is-empty", test_glyph_is_empty);
    g_test_add_func("/glyph/attrs", test_glyph_attrs);
    g_test_add_func("/glyph/wide", test_glyph_wide);
    g_test_add_func("/glyph/reset", test_glyph_reset);
    g_test_add_func("/glyph/gtype", test_glyph_gtype);
	g_test_add_func("/glyph/cluster-ownership", test_glyph_cluster_ownership);
	g_test_add_func("/glyph/cluster-growth-copies", test_cluster_growth_copies);

    return g_test_run();
}
