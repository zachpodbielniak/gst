/*
 * test-lrg-backend.c - Tests for the libregnum (LRG) backend enums/helpers
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Covers the GstLrgRenderMode parsing helpers and the GstBackendType /
 * GstLrgRenderMode GType registrations. These are compiled unconditionally
 * (in gst-enums.c), so the test runs regardless of LRG_BACKEND. The window
 * and renderer themselves need a raylib/GL context and are exercised by the
 * manual --lrg verification, not here.
 */

#include <glib.h>
#include "gst-enums.h"

/* ===== render mode: from_string ===== */

static void
test_render_mode_from_string(void)
{
	GstLrgRenderMode mode = GST_LRG_RENDER_MODE_3DVR;

	/* Bare --lrg (NULL / empty) defaults to 2D and succeeds. */
	g_assert_true(gst_lrg_render_mode_from_string(NULL, &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);

	mode = GST_LRG_RENDER_MODE_3DVR;
	g_assert_true(gst_lrg_render_mode_from_string("", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);

	/* Explicit modes. */
	g_assert_true(gst_lrg_render_mode_from_string("2d", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);

	g_assert_true(gst_lrg_render_mode_from_string("3d", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_3D);

	g_assert_true(gst_lrg_render_mode_from_string("3dvr", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_3DVR);

	/* Case-insensitive. */
	g_assert_true(gst_lrg_render_mode_from_string("2D", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);
}

static void
test_render_mode_from_string_invalid(void)
{
	GstLrgRenderMode mode = GST_LRG_RENDER_MODE_3D;

	/* Unknown modes fail and leave the out parameter at 2D. */
	g_assert_false(gst_lrg_render_mode_from_string("bogus", &mode));
	g_assert_cmpint(mode, ==, GST_LRG_RENDER_MODE_2D);

	/* A NULL out parameter is tolerated. */
	g_assert_true(gst_lrg_render_mode_from_string("3d", NULL));
	g_assert_false(gst_lrg_render_mode_from_string("nope", NULL));
}

/* ===== render mode: to_string ===== */

static void
test_render_mode_to_string(void)
{
	g_assert_cmpstr(gst_lrg_render_mode_to_string(GST_LRG_RENDER_MODE_2D),
		==, "2d");
	g_assert_cmpstr(gst_lrg_render_mode_to_string(GST_LRG_RENDER_MODE_3D),
		==, "3d");
	g_assert_cmpstr(gst_lrg_render_mode_to_string(GST_LRG_RENDER_MODE_3DVR),
		==, "3dvr");
}

/* ===== render mode: is_implemented ===== */

static void
test_render_mode_is_implemented(void)
{
	g_assert_true(gst_lrg_render_mode_is_implemented(GST_LRG_RENDER_MODE_2D));
	g_assert_false(gst_lrg_render_mode_is_implemented(GST_LRG_RENDER_MODE_3D));
	g_assert_false(gst_lrg_render_mode_is_implemented(GST_LRG_RENDER_MODE_3DVR));
}

/* ===== GType registrations ===== */

static void
test_backend_type_registered(void)
{
	GEnumClass *klass;
	GEnumValue *value;

	g_assert_true(G_TYPE_IS_ENUM(GST_TYPE_BACKEND_TYPE));

	klass = g_type_class_ref(GST_TYPE_BACKEND_TYPE);

	value = g_enum_get_value(klass, GST_BACKEND_LRG);
	g_assert_nonnull(value);
	g_assert_cmpstr(value->value_nick, ==, "lrg");

	/* The existing backends remain registered. */
	g_assert_nonnull(g_enum_get_value(klass, GST_BACKEND_X11));
	g_assert_nonnull(g_enum_get_value(klass, GST_BACKEND_WAYLAND));

	g_type_class_unref(klass);
}

static void
test_render_mode_type_registered(void)
{
	GEnumClass *klass;
	GEnumValue *value;

	g_assert_true(G_TYPE_IS_ENUM(GST_TYPE_LRG_RENDER_MODE));

	klass = g_type_class_ref(GST_TYPE_LRG_RENDER_MODE);

	value = g_enum_get_value_by_nick(klass, "2d");
	g_assert_nonnull(value);
	g_assert_cmpint(value->value, ==, GST_LRG_RENDER_MODE_2D);

	g_type_class_unref(klass);
}

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/lrg/render-mode/from-string",
		test_render_mode_from_string);
	g_test_add_func("/lrg/render-mode/from-string-invalid",
		test_render_mode_from_string_invalid);
	g_test_add_func("/lrg/render-mode/to-string",
		test_render_mode_to_string);
	g_test_add_func("/lrg/render-mode/is-implemented",
		test_render_mode_is_implemented);
	g_test_add_func("/lrg/backend-type/registered",
		test_backend_type_registered);
	g_test_add_func("/lrg/render-mode/type-registered",
		test_render_mode_type_registered);

	return g_test_run();
}
