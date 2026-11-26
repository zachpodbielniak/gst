/*
 * test-dyncolors.c - OSC 110/111/112 reset the saved colors
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <string.h>

#include "module/gst-module-manager.h"

#include "../modules/dynamic_colors/gst-dyncolors-module.c"

static gboolean
feed(
	GstDyncolorsModule *module,
	const gchar        *buf
){
	return gst_dyncolors_module_handle_escape_string(
		GST_ESCAPE_HANDLER(module), ']', buf, strlen(buf), NULL);
}

static void
test_reset_one_color(void)
{
	g_autoptr(GstColorScheme) scheme = NULL;
	GstDyncolorsModule *module;
	GstModuleManager *mgr;
	guint32 orig_fg;
	guint32 orig_bg;
	guint32 orig_cursor;
	guint32 orig_one;

	scheme = gst_color_scheme_new("dyncolors-reset");
	mgr = gst_module_manager_get_default();
	gst_module_manager_set_color_scheme(mgr, scheme);
	module = g_object_new(GST_TYPE_DYNCOLORS_MODULE, NULL);

	orig_fg = gst_color_scheme_get_foreground(scheme);
	orig_bg = gst_color_scheme_get_background(scheme);
	orig_cursor = gst_color_scheme_get_cursor_color(scheme);
	orig_one = gst_color_scheme_get_color(scheme, 1);

	/* Nothing was changed, so the resets are consumed and do nothing. */
	g_assert_true(feed(module, "110"));
	g_assert_true(feed(module, "111"));
	g_assert_true(feed(module, "112"));
	g_assert_cmpuint(gst_color_scheme_get_foreground(scheme), ==, orig_fg);
	g_assert_cmpuint(gst_color_scheme_get_background(scheme), ==, orig_bg);
	g_assert_cmpuint(gst_color_scheme_get_cursor_color(scheme), ==,
		orig_cursor);

	g_assert_true(feed(module, "10;#ff0000"));
	g_assert_true(feed(module, "11;#00ff00"));
	g_assert_true(feed(module, "12;#0000ff"));
	g_assert_true(feed(module, "4;1;#112233"));
	g_assert_cmpuint(gst_color_scheme_get_foreground(scheme), ==,
		0xffff0000u);
	g_assert_cmpuint(gst_color_scheme_get_background(scheme), ==,
		0xff00ff00u);
	g_assert_cmpuint(gst_color_scheme_get_cursor_color(scheme), ==,
		0xff0000ffu);

	g_assert_true(feed(module, "110"));
	g_assert_cmpuint(gst_color_scheme_get_foreground(scheme), ==, orig_fg);
	g_assert_cmpuint(gst_color_scheme_get_background(scheme), ==,
		0xff00ff00u);
	g_assert_cmpuint(gst_color_scheme_get_color(scheme, 1), ==,
		0xff112233u);

	g_assert_true(feed(module, "111;"));
	g_assert_cmpuint(gst_color_scheme_get_background(scheme), ==, orig_bg);
	g_assert_cmpuint(gst_color_scheme_get_cursor_color(scheme), ==,
		0xff0000ffu);

	g_assert_true(feed(module, "112"));
	g_assert_cmpuint(gst_color_scheme_get_cursor_color(scheme), ==,
		orig_cursor);

	/* A locked palette still consumes the reset and keeps the new color. */
	g_assert_true(feed(module, "10;#ff0000"));
	module->allow_set = FALSE;
	g_assert_true(feed(module, "110"));
	g_assert_cmpuint(gst_color_scheme_get_foreground(scheme), ==,
		0xffff0000u);
	module->allow_set = TRUE;
	g_assert_true(feed(module, "110"));
	g_assert_cmpuint(gst_color_scheme_get_foreground(scheme), ==, orig_fg);

	/* Empty 104 still restores the palette and the three special colors. */
	g_assert_true(feed(module, "10;#ff0000"));
	g_assert_true(feed(module, "11;#00ff00"));
	g_assert_true(feed(module, "12;#0000ff"));
	g_assert_true(feed(module, "104"));
	g_assert_cmpuint(gst_color_scheme_get_foreground(scheme), ==, orig_fg);
	g_assert_cmpuint(gst_color_scheme_get_background(scheme), ==, orig_bg);
	g_assert_cmpuint(gst_color_scheme_get_cursor_color(scheme), ==,
		orig_cursor);
	g_assert_cmpuint(gst_color_scheme_get_color(scheme, 1), ==, orig_one);

	g_assert_true(feed(module, "10;?"));
	g_assert_false(feed(module, "7;file:///tmp"));

	g_object_unref(module);
	gst_module_manager_set_color_scheme(mgr, NULL);
}

static void
test_no_scheme(void)
{
	GstDyncolorsModule *module;
	GstModuleManager *mgr;

	mgr = gst_module_manager_get_default();
	gst_module_manager_set_color_scheme(mgr, NULL);
	module = g_object_new(GST_TYPE_DYNCOLORS_MODULE, NULL);
	g_assert_false(feed(module, "110"));
	g_assert_false(feed(module, "10;#ff0000"));
	g_object_unref(module);
}

int
main(
	int   argc,
	char **argv
){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/dyncolors/reset-one", test_reset_one_color);
	g_test_add_func("/dyncolors/no-scheme", test_no_scheme);
	return g_test_run();
}
