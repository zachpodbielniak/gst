/*
 * test-glyph.c - Tests for GstGlyph boxed type
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include "boxed/gst-glyph.h"

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

    return g_test_run();
}
