/*
 * test-notify-osc9.c - OSC 9;4 is not a desktop notification
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <string.h>

#include "../modules/notify/gst-notify-module.c"

static gboolean
feed(
	GstNotifyModule *module,
	const gchar     *buf
){
	return gst_notify_module_handle_escape_string(
		GST_ESCAPE_HANDLER(module), ']', buf, strlen(buf), NULL);
}

static void
test_progress_is_not_a_notification(void)
{
	GstNotifyModule *module;

	module = g_object_new(GST_TYPE_NOTIFY_MODULE, NULL);

	/* Focused windows already skip every OSC before the number switch. */
	g_assert_true(module->suppress_focused);
	g_assert_true(module->is_focused);
	g_assert_false(feed(module, "9;4;1;10"));

	/* Unfocused, a real OSC 9;4 body must not reach notify-send. */
	module->is_focused = FALSE;
	g_assert_false(feed(module, "9;4;1;10"));
	g_assert_false(feed(module, "9;4"));
	g_assert_false(feed(module, "9;4;"));
	g_assert_false(feed(module, "9;4;9"));

	g_object_unref(module);
}

int
main(
	int   argc,
	char **argv
){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/notify/osc9-progress",
		test_progress_is_not_a_notification);
	return g_test_run();
}
