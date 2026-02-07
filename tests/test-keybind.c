/*
 * test-keybind.c - Tests for GstKeybind parsing, lookup, and config loading
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>

#include "config/gst-keybind.h"
#include "config/gst-config.h"
#include "gst-enums.h"

/* ===== Helper: write YAML to a temp file ===== */

static gchar *
write_temp_yaml(const gchar *yaml_content)
{
	gchar *path;
	GError *error = NULL;
	gint fd;

	fd = g_file_open_tmp("gst-test-kb-XXXXXX.yaml", &path, &error);
	g_assert_no_error(error);
	g_assert_cmpint(fd, >=, 0);

	g_assert_true(g_file_set_contents(path, yaml_content, -1, &error));
	g_assert_no_error(error);
	close(fd);

	return path;
}

/* ===== Test: parse simple key with no modifiers ===== */

static void
test_keybind_parse_simple_key(void)
{
	GstKeybind kb;
	gboolean ok;

	ok = gst_keybind_parse("a", "clipboard_copy", &kb);
	g_assert_true(ok);
	g_assert_cmpuint(kb.keyval, ==, XK_a);
	g_assert_cmpint(kb.mods, ==, GST_KEY_MOD_NONE);
	g_assert_cmpint(kb.action, ==, GST_ACTION_CLIPBOARD_COPY);
}

/* ===== Test: parse Ctrl+Shift+letter ===== */

static void
test_keybind_parse_ctrl_shift_letter(void)
{
	GstKeybind kb;
	gboolean ok;

	/* "Ctrl+Shift+c" should normalize to XK_C (uppercase) */
	ok = gst_keybind_parse("Ctrl+Shift+c", "clipboard_copy", &kb);
	g_assert_true(ok);
	g_assert_cmpuint(kb.keyval, ==, XK_C);
	g_assert_cmpint(kb.mods, ==,
		(GstKeyMod)(GST_KEY_MOD_CTRL | GST_KEY_MOD_SHIFT));
	g_assert_cmpint(kb.action, ==, GST_ACTION_CLIPBOARD_COPY);
}

/* ===== Test: parse Shift+function key ===== */

static void
test_keybind_parse_shift_function_key(void)
{
	GstKeybind kb;
	gboolean ok;

	ok = gst_keybind_parse("Shift+Page_Up", "scroll_up", &kb);
	g_assert_true(ok);
	g_assert_cmpuint(kb.keyval, ==, XK_Page_Up);
	g_assert_cmpint(kb.mods, ==, GST_KEY_MOD_SHIFT);
	g_assert_cmpint(kb.action, ==, GST_ACTION_SCROLL_UP);
}

/* ===== Test: parse Ctrl+Shift+special key ===== */

static void
test_keybind_parse_ctrl_shift_special(void)
{
	GstKeybind kb;
	gboolean ok;

	ok = gst_keybind_parse("Ctrl+Shift+plus", "zoom_in", &kb);
	g_assert_true(ok);
	g_assert_cmpuint(kb.keyval, ==, XK_plus);
	g_assert_cmpint(kb.mods, ==,
		(GstKeyMod)(GST_KEY_MOD_CTRL | GST_KEY_MOD_SHIFT));
	g_assert_cmpint(kb.action, ==, GST_ACTION_ZOOM_IN);
}

/* ===== Test: parse Ctrl+Shift+number ===== */

static void
test_keybind_parse_ctrl_shift_number(void)
{
	GstKeybind kb;
	gboolean ok;

	ok = gst_keybind_parse("Ctrl+Shift+0", "zoom_reset", &kb);
	g_assert_true(ok);
	g_assert_cmpuint(kb.keyval, ==, XK_0);
	g_assert_cmpint(kb.mods, ==,
		(GstKeyMod)(GST_KEY_MOD_CTRL | GST_KEY_MOD_SHIFT));
	g_assert_cmpint(kb.action, ==, GST_ACTION_ZOOM_RESET);
}

/* ===== Test: parse Shift+Insert ===== */

static void
test_keybind_parse_shift_insert(void)
{
	GstKeybind kb;
	gboolean ok;

	ok = gst_keybind_parse("Shift+Insert", "paste_primary", &kb);
	g_assert_true(ok);
	g_assert_cmpuint(kb.keyval, ==, XK_Insert);
	g_assert_cmpint(kb.mods, ==, GST_KEY_MOD_SHIFT);
	g_assert_cmpint(kb.action, ==, GST_ACTION_PASTE_PRIMARY);
}

/* ===== Test: action from string — valid ===== */

static void
test_action_from_string_valid(void)
{
	g_assert_cmpint(gst_action_from_string("clipboard_copy"),
		==, GST_ACTION_CLIPBOARD_COPY);
	g_assert_cmpint(gst_action_from_string("clipboard_paste"),
		==, GST_ACTION_CLIPBOARD_PASTE);
	g_assert_cmpint(gst_action_from_string("paste_primary"),
		==, GST_ACTION_PASTE_PRIMARY);
	g_assert_cmpint(gst_action_from_string("scroll_up"),
		==, GST_ACTION_SCROLL_UP);
	g_assert_cmpint(gst_action_from_string("scroll_down"),
		==, GST_ACTION_SCROLL_DOWN);
	g_assert_cmpint(gst_action_from_string("zoom_in"),
		==, GST_ACTION_ZOOM_IN);
	g_assert_cmpint(gst_action_from_string("zoom_out"),
		==, GST_ACTION_ZOOM_OUT);
	g_assert_cmpint(gst_action_from_string("zoom_reset"),
		==, GST_ACTION_ZOOM_RESET);
}

/* ===== Test: action from string — invalid ===== */

static void
test_action_from_string_invalid(void)
{
	g_assert_cmpint(gst_action_from_string("nonexistent"),
		==, GST_ACTION_NONE);
	g_assert_cmpint(gst_action_from_string(""),
		==, GST_ACTION_NONE);
	g_assert_cmpint(gst_action_from_string(NULL),
		==, GST_ACTION_NONE);
}

/* ===== Test: action round-trip (to_string / from_string) ===== */

static void
test_action_roundtrip(void)
{
	GstAction actions[] = {
		GST_ACTION_CLIPBOARD_COPY,
		GST_ACTION_CLIPBOARD_PASTE,
		GST_ACTION_PASTE_PRIMARY,
		GST_ACTION_SCROLL_UP,
		GST_ACTION_SCROLL_DOWN,
		GST_ACTION_SCROLL_TOP,
		GST_ACTION_SCROLL_BOTTOM,
		GST_ACTION_SCROLL_UP_FAST,
		GST_ACTION_SCROLL_DOWN_FAST,
		GST_ACTION_ZOOM_IN,
		GST_ACTION_ZOOM_OUT,
		GST_ACTION_ZOOM_RESET,
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS(actions); i++) {
		const gchar *name;
		GstAction roundtrip;

		name = gst_action_to_string(actions[i]);
		g_assert_cmpstr(name, !=, "none");

		roundtrip = gst_action_from_string(name);
		g_assert_cmpint(roundtrip, ==, actions[i]);
	}
}

/* ===== Test: mouse binding parse ===== */

static void
test_mousebind_parse(void)
{
	GstMousebind mb;
	gboolean ok;

	ok = gst_mousebind_parse("Shift+Button4", "scroll_up_fast", &mb);
	g_assert_true(ok);
	g_assert_cmpint(mb.button, ==, GST_MOUSE_BUTTON_SCROLL_UP);
	g_assert_cmpint(mb.mods, ==, GST_KEY_MOD_SHIFT);
	g_assert_cmpint(mb.action, ==, GST_ACTION_SCROLL_UP_FAST);

	/* No modifiers */
	ok = gst_mousebind_parse("Button5", "scroll_down", &mb);
	g_assert_true(ok);
	g_assert_cmpint(mb.button, ==, GST_MOUSE_BUTTON_SCROLL_DOWN);
	g_assert_cmpint(mb.mods, ==, GST_KEY_MOD_NONE);
	g_assert_cmpint(mb.action, ==, GST_ACTION_SCROLL_DOWN);
}

/* ===== Test: keybind lookup match ===== */

static void
test_keybind_lookup_match(void)
{
	GArray *bindings;
	GstKeybind kb;
	GstAction action;

	bindings = g_array_new(FALSE, TRUE, sizeof(GstKeybind));

	/* Add Ctrl+Shift+C -> clipboard_copy */
	gst_keybind_parse("Ctrl+Shift+c", "clipboard_copy", &kb);
	g_array_append_val(bindings, kb);

	/* Add Shift+Insert -> paste_primary */
	gst_keybind_parse("Shift+Insert", "paste_primary", &kb);
	g_array_append_val(bindings, kb);

	/*
	 * Lookup with X11 state: ControlMask | ShiftMask and keysym XK_C.
	 * X11 reports uppercase keysym when Shift is held.
	 */
	action = gst_keybind_lookup(bindings, XK_C,
		ControlMask | ShiftMask);
	g_assert_cmpint(action, ==, GST_ACTION_CLIPBOARD_COPY);

	/* Shift+Insert */
	action = gst_keybind_lookup(bindings, XK_Insert, ShiftMask);
	g_assert_cmpint(action, ==, GST_ACTION_PASTE_PRIMARY);

	/* With NumLock (Mod2Mask) — should still match */
	action = gst_keybind_lookup(bindings, XK_C,
		ControlMask | ShiftMask | Mod2Mask);
	g_assert_cmpint(action, ==, GST_ACTION_CLIPBOARD_COPY);

	g_array_unref(bindings);
}

/* ===== Test: keybind lookup no match ===== */

static void
test_keybind_lookup_no_match(void)
{
	GArray *bindings;
	GstAction action;
	GstKeybind kb;

	bindings = g_array_new(FALSE, TRUE, sizeof(GstKeybind));

	gst_keybind_parse("Ctrl+Shift+c", "clipboard_copy", &kb);
	g_array_append_val(bindings, kb);

	/* Just Ctrl+C (no Shift) — should NOT match */
	action = gst_keybind_lookup(bindings, XK_c, ControlMask);
	g_assert_cmpint(action, ==, GST_ACTION_NONE);

	/* Unbound key */
	action = gst_keybind_lookup(bindings, XK_F1, 0);
	g_assert_cmpint(action, ==, GST_ACTION_NONE);

	/* NULL bindings array */
	action = gst_keybind_lookup(NULL, XK_C,
		ControlMask | ShiftMask);
	g_assert_cmpint(action, ==, GST_ACTION_NONE);

	g_array_unref(bindings);
}

/* ===== Test: config loads keybinds from YAML ===== */

static void
test_config_load_keybinds(void)
{
	g_autoptr(GstConfig) config = NULL;
	g_autofree gchar *path = NULL;
	GError *error = NULL;
	const GArray *keybinds;
	const GArray *mousebinds;
	GstAction action;

	/* YAML with custom keybinds that replace defaults */
	path = write_temp_yaml(
		"keybinds:\n"
		"  \"Ctrl+Shift+c\": clipboard_copy\n"
		"  \"Ctrl+Shift+v\": clipboard_paste\n"
		"\n"
		"mousebinds:\n"
		"  \"Button4\": scroll_up\n"
		"  \"Shift+Button5\": scroll_down_fast\n"
	);

	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);

	/* Keybinds section replaces defaults — should have exactly 2 */
	keybinds = gst_config_get_keybinds(config);
	g_assert_nonnull(keybinds);
	g_assert_cmpuint(keybinds->len, ==, 2);

	/* Mousebinds section replaces defaults — should have exactly 2 */
	mousebinds = gst_config_get_mousebinds(config);
	g_assert_nonnull(mousebinds);
	g_assert_cmpuint(mousebinds->len, ==, 2);

	/* Verify lookup works with loaded bindings */
	action = gst_config_lookup_key_action(config, XK_C,
		ControlMask | ShiftMask);
	g_assert_cmpint(action, ==, GST_ACTION_CLIPBOARD_COPY);

	action = gst_config_lookup_key_action(config, XK_V,
		ControlMask | ShiftMask);
	g_assert_cmpint(action, ==, GST_ACTION_CLIPBOARD_PASTE);

	/* Shift+Insert was NOT in our custom config — should be NONE */
	action = gst_config_lookup_key_action(config, XK_Insert,
		ShiftMask);
	g_assert_cmpint(action, ==, GST_ACTION_NONE);

	/* Mouse lookup */
	action = gst_config_lookup_mouse_action(config, 4, 0);
	g_assert_cmpint(action, ==, GST_ACTION_SCROLL_UP);

	action = gst_config_lookup_mouse_action(config, 5, ShiftMask);
	g_assert_cmpint(action, ==, GST_ACTION_SCROLL_DOWN_FAST);

	g_unlink(path);
}

/* ===== Main ===== */

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);

	/* Key binding parse tests */
	g_test_add_func("/keybind/parse-simple-key",
		test_keybind_parse_simple_key);
	g_test_add_func("/keybind/parse-ctrl-shift-letter",
		test_keybind_parse_ctrl_shift_letter);
	g_test_add_func("/keybind/parse-shift-function-key",
		test_keybind_parse_shift_function_key);
	g_test_add_func("/keybind/parse-ctrl-shift-special",
		test_keybind_parse_ctrl_shift_special);
	g_test_add_func("/keybind/parse-ctrl-shift-number",
		test_keybind_parse_ctrl_shift_number);
	g_test_add_func("/keybind/parse-shift-insert",
		test_keybind_parse_shift_insert);

	/* Action string tests */
	g_test_add_func("/keybind/action-from-string-valid",
		test_action_from_string_valid);
	g_test_add_func("/keybind/action-from-string-invalid",
		test_action_from_string_invalid);
	g_test_add_func("/keybind/action-roundtrip",
		test_action_roundtrip);

	/* Mouse binding tests */
	g_test_add_func("/keybind/mousebind-parse",
		test_mousebind_parse);

	/* Lookup tests */
	g_test_add_func("/keybind/lookup-match",
		test_keybind_lookup_match);
	g_test_add_func("/keybind/lookup-no-match",
		test_keybind_lookup_no_match);

	/* Config integration test */
	g_test_add_func("/keybind/config-load-keybinds",
		test_config_load_keybinds);

	return g_test_run();
}
