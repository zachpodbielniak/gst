/*
 * test-terminal.c - Tests for GstTerminal
 *
 * Copyright (C) 2024 Zach Podbielniak
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
    g_test_add_func("/terminal/clear", test_terminal_clear);
    g_test_add_func("/terminal/scroll-region", test_terminal_scroll_region);
    g_test_add_func("/terminal/reset", test_terminal_reset);

    return g_test_run();
}
