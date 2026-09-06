/*
 * test-sixel.c - Regressions for sixel image dimensions and pixels
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>

/* Exercise the real decoder, which is private to the loadable module. */
#include "../modules/sixel/gst-sixel-module.c"

static void
assert_red_column(GstSixelModule *module)
{
	static const guint8 red[] = { 187, 0, 0, 255 };
	GHashTableIter iter;
	gpointer value;
	SixelPlacement *placement;
	gint row;

	g_assert_cmpuint(g_hash_table_size(module->placements), ==, 1);
	g_hash_table_iter_init(&iter, module->placements);
	g_assert_true(g_hash_table_iter_next(&iter, NULL, &value));
	placement = (SixelPlacement *)value;
	g_assert_cmpint(placement->width, ==, 1);
	g_assert_cmpint(placement->height, ==, 6);

	/* Read rows exactly as the renderer does, using the advertised stride. */
	for (row = 0; row < placement->height; row++) {
		g_assert_cmpmem(placement->data + row * placement->stride,
			sizeof(red), red, sizeof(red));
	}
}

static void
test_sixel_compact_rows(void)
{
	GstSixelModule *module;
	const gchar *sequence;

	module = g_object_new(GST_TYPE_SIXEL_MODULE, NULL);
	g_assert_true(gst_module_activate(GST_MODULE(module)));
	sequence = "q#1~";
	g_assert_true(sixel_handle_escape(GST_ESCAPE_HANDLER(module),
		'P', sequence, strlen(sequence), NULL));

	assert_red_column(module);

	gst_module_deactivate(GST_MODULE(module));
	g_object_unref(module);
}

static void
test_sixel_repeat_height_limit(void)
{
	GstSixelModule *module;
	const gchar *sequence;

	module = g_object_new(GST_TYPE_SIXEL_MODULE, NULL);
	module->max_width = 1;
	module->max_height = 6;
	g_assert_true(gst_module_activate(GST_MODULE(module)));

	/* The second band cannot fit: it must not enlarge the visible image. */
	sequence = "q#1~-!1~";
	g_assert_true(sixel_handle_escape(GST_ESCAPE_HANDLER(module),
		'P', sequence, strlen(sequence), NULL));

	assert_red_column(module);

	gst_module_deactivate(GST_MODULE(module));
	g_object_unref(module);
}

/* Empty DCS data cannot manufacture a one-pixel image. */
static void
test_sixel_empty(void)
{
	GstSixelModule *module;

	module = g_object_new(GST_TYPE_SIXEL_MODULE, NULL);
	g_assert_true(gst_module_activate(GST_MODULE(module)));
	sixel_handle_escape(GST_ESCAPE_HANDLER(module), 'P', "q#1", 3, NULL);
	g_assert_cmpuint(g_hash_table_size(module->placements), ==, 0);
	gst_module_deactivate(GST_MODULE(module));
	g_object_unref(module);
}

/* Decimal parameters must saturate instead of wrapping signed integers. */
static void
test_sixel_large_repeat(void)
{
	GstSixelModule *module;
	const gchar *sequence = "q#1!2147483648~";

	module = g_object_new(GST_TYPE_SIXEL_MODULE, NULL);
	module->max_width = 1;
	module->max_height = 6;
	g_assert_true(gst_module_activate(GST_MODULE(module)));
	g_assert_true(sixel_handle_escape(GST_ESCAPE_HANDLER(module),
		'P', sequence, strlen(sequence), NULL));
	assert_red_column(module);
	gst_module_deactivate(GST_MODULE(module));
	g_object_unref(module);
}

/* A register larger than the palette must not alias register 1 on overflow. */
static void
test_sixel_large_color(void)
{
	GstSixelModule *module;
	const gchar *sequence = "q#4294967297;2;100;0;0#1~";

	module = g_object_new(GST_TYPE_SIXEL_MODULE, NULL);
	g_assert_true(gst_module_activate(GST_MODULE(module)));
	g_assert_true(sixel_handle_escape(GST_ESCAPE_HANDLER(module),
		'P', sequence, strlen(sequence), NULL));
	assert_red_column(module);
	gst_module_deactivate(GST_MODULE(module));
	g_object_unref(module);
}

int
main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/sixel/decode/compact-rows", test_sixel_compact_rows);
	g_test_add_func("/sixel/decode/empty", test_sixel_empty);
	g_test_add_func("/sixel/decode/large-repeat", test_sixel_large_repeat);
	g_test_add_func("/sixel/decode/large-color", test_sixel_large_color);
	g_test_add_func("/sixel/decode/repeat-height-limit",
		test_sixel_repeat_height_limit);

	return g_test_run();
}
