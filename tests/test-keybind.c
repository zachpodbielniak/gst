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
	g_assert_cmpint(gst_action_from_string("copy-command-output"),
		==, GST_ACTION_COPY_COMMAND_OUTPUT);
	g_assert_cmpint(gst_action_from_string("export-command-output"),
		==, GST_ACTION_EXPORT_COMMAND_OUTPUT);
	g_assert_cmpstr(gst_action_to_string(GST_ACTION_COPY_COMMAND_OUTPUT),
		==, "copy-command-output");
	g_assert_cmpstr(gst_action_to_string(GST_ACTION_EXPORT_COMMAND_OUTPUT),
		==, "export-command-output");
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
		GST_ACTION_COPY_COMMAND_OUTPUT,
		GST_ACTION_EXPORT_COMMAND_OUTPUT,
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

/* ===== Test: CapsLock preserves Ctrl+letter binding identity ===== */

static void
test_keybind_lookup_capslock_ctrl(void)
{
	GArray *bindings;
	GstKeybind kb;
	GstAction action;

	bindings = g_array_new(FALSE, TRUE, sizeof(GstKeybind));
	g_assert_true(gst_keybind_parse("Ctrl+c", "clipboard_copy", &kb));
	g_array_append_val(bindings, kb);

	action = gst_keybind_lookup(bindings, XK_C, ControlMask | LockMask);
	g_assert_cmpint(action, ==, GST_ACTION_CLIPBOARD_COPY);

	action = gst_keybind_lookup(bindings, XK_D, ControlMask | LockMask);
	g_assert_cmpint(action, ==, GST_ACTION_NONE);

	g_array_unref(bindings);
}

/* ===== Test: CapsLock reverses Shift's letter case, not its modifier ===== */

static void
test_keybind_lookup_capslock_ctrl_shift(void)
{
	GArray *bindings;
	GstKeybind kb;
	GstAction action;

	bindings = g_array_new(FALSE, TRUE, sizeof(GstKeybind));
	g_assert_true(gst_keybind_parse("Ctrl+Shift+c", "clipboard_copy", &kb));
	g_array_append_val(bindings, kb);

	action = gst_keybind_lookup(bindings, XK_c,
		ControlMask | ShiftMask | LockMask);
	g_assert_cmpint(action, ==, GST_ACTION_CLIPBOARD_COPY);

	action = gst_keybind_lookup(bindings, XK_d,
		ControlMask | ShiftMask | LockMask);
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

/* Every shipped keyboard binding is exercised with all conventional lock
 * combinations. Expected event symbols are independent of the parser. */
static void
test_keybind_defaults(void)
{
	static const struct {
		guint keyval;
		guint base;
		guint state;
		GstAction action;
	} cases[] = {
		{ XK_C, XK_c, ControlMask | ShiftMask, GST_ACTION_CLIPBOARD_COPY },
		{ XK_V, XK_v, ControlMask | ShiftMask, GST_ACTION_CLIPBOARD_PASTE },
		{ XK_Insert, XK_Insert, ShiftMask, GST_ACTION_PASTE_PRIMARY },
		{ XK_Page_Up, XK_Page_Up, ShiftMask, GST_ACTION_SCROLL_UP },
		{ XK_Page_Down, XK_Page_Down, ShiftMask, GST_ACTION_SCROLL_DOWN },
		{ XK_Page_Up, XK_Page_Up, ControlMask | ShiftMask, GST_ACTION_SCROLL_TOP },
		{ XK_Page_Down, XK_Page_Down, ControlMask | ShiftMask, GST_ACTION_SCROLL_BOTTOM },
		{ XK_Home, XK_Home, ControlMask | ShiftMask, GST_ACTION_SCROLL_TOP },
		{ XK_End, XK_End, ControlMask | ShiftMask, GST_ACTION_SCROLL_BOTTOM },
		{ XK_Home, XK_Home, ShiftMask, GST_ACTION_SCROLL_TOP },
		{ XK_End, XK_End, ShiftMask, GST_ACTION_SCROLL_BOTTOM },
		{ XK_plus, XK_equal, ControlMask | ShiftMask, GST_ACTION_ZOOM_IN },
		{ XK_underscore, XK_minus, ControlMask | ShiftMask, GST_ACTION_ZOOM_OUT },
		{ XK_parenright, XK_0, ControlMask | ShiftMask, GST_ACTION_ZOOM_RESET },
		{ XK_Y, XK_y, ControlMask | ShiftMask, GST_ACTION_COPY_COMMAND_OUTPUT },
		{ XK_O, XK_o, ControlMask | ShiftMask, GST_ACTION_EXPORT_COMMAND_OUTPUT }
	};
	const gchar *paths[] = { NULL, "data/default-config.yaml", "data/all-modules.yaml" };
	guint source;
	guint i;
	guint locks;

	for (source = 0; source < G_N_ELEMENTS(paths); source++) {
		g_autoptr(GstConfig) config = gst_config_new();
		g_autoptr(GError) error = NULL;
		const GArray *bindings;

		if (paths[source] != NULL) {
			g_assert_true(gst_config_load_from_path(config, paths[source], &error));
			g_assert_no_error(error);
		}
		bindings = gst_config_get_keybinds(config);
		g_assert_cmpuint(bindings->len, ==, G_N_ELEMENTS(cases));
		for (i = 0; i < G_N_ELEMENTS(cases); i++) {
			for (locks = 0; locks < 8; locks++) {
				guint state = cases[i].state | ((locks & 1) ? LockMask : 0) |
					((locks & 2) ? Mod2Mask : 0) | ((locks & 4) ? Mod3Mask : 0);
				guint keyval = cases[i].keyval;

				if ((locks & 1) && keyval >= XK_A && keyval <= XK_Z)
					keyval += XK_a - XK_A;
				g_assert_cmpint(gst_keybind_lookup_event(bindings, keyval, cases[i].base, state),
					==, cases[i].action);
				g_assert_cmpint(gst_keybind_lookup_event(bindings, keyval, cases[i].base,
					state | Mod4Mask), ==, GST_ACTION_NONE);
			}
		}
	}
}

/* Cover every ASCII letter, digit and punctuation Shift pair for every
 * supported modifier combination, including negative modifier matches. */
static void
test_keybind_shift_matrix(void)
{
	const gchar *base = "abcdefghijklmnopqrstuvwxyz1234567890-=[]\\;',./`";
	const gchar *shifted = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()_+{}|:\"<>?~";
	guint mods;
	guint i;
	guint locks;

	g_assert_cmpuint(strlen(base), ==, strlen(shifted));
	for (mods = 0; mods < 16; mods++) {
		for (i = 0; base[i] != '\0'; i++) {
			g_autoptr(GArray) bindings = g_array_new(FALSE, FALSE, sizeof(GstKeybind));
			g_autofree gchar *key = g_strdup_printf("%s%s%s%s%s",
				(mods & 1) ? "Ctrl+" : "", (mods & 2) ? "Alt+" : "",
				(mods & 4) ? "Super+" : "", (mods & 8) ? "Shift+" : "",
				XKeysymToString((KeySym)base[i]));
			GstKeybind binding;
			guint state = ((mods & 8) ? ShiftMask : 0) | ((mods & 1) ? ControlMask : 0) |
				((mods & 2) ? Mod1Mask : 0) | ((mods & 4) ? Mod4Mask : 0);

			g_assert_true(gst_keybind_parse(key, "clipboard_copy", &binding));
			g_array_append_val(bindings, binding);
			for (locks = 0; locks < 8; locks++) {
				guint event_state = state | ((locks & 1) ? LockMask : 0) |
					((locks & 2) ? Mod2Mask : 0) | ((locks & 4) ? Mod3Mask : 0);
				guint keyval = (guint)((mods & 8) ? shifted[i] : base[i]);

				if ((locks & 1) && i < 26)
					keyval = (guint)((mods & 8) ? base[i] : shifted[i]);
				g_assert_cmpint(gst_keybind_lookup_event(bindings, keyval, (guint)base[i],
					event_state), ==, GST_ACTION_CLIPBOARD_COPY);
				g_assert_cmpint(gst_keybind_lookup_event(bindings, keyval, (guint)base[i],
					event_state ^ ControlMask), ==, GST_ACTION_NONE);
			}
		}
	}
}

/* Exact shifted bindings win even when a base binding was inserted first;
 * Compose/IME NoSymbol events must never become keyboard shortcuts. */
static void
test_keybind_event_precedence(void)
{
	g_autoptr(GstConfig) config = gst_config_new();
	const GArray *bindings;

	gst_config_clear_keybinds(config);
	gst_config_add_keybind(config, "Ctrl+Shift+1", "clipboard_copy");
	gst_config_add_keybind(config, "Ctrl+Shift+exclam", "clipboard_paste");
	bindings = gst_config_get_keybinds(config);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_exclam, XK_1, ControlMask | ShiftMask),
		==, GST_ACTION_CLIPBOARD_PASTE);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, NoSymbol, XK_1, ControlMask | ShiftMask), ==, GST_ACTION_NONE);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_at, NoSymbol, ControlMask | ShiftMask), ==, GST_ACTION_NONE);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_at, XK_1, ControlMask | ShiftMask), ==, GST_ACTION_CLIPBOARD_COPY);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_at, XK_1, ControlMask), ==, GST_ACTION_NONE);
	/* Shift may turn a letter into punctuation; the fallback must apply
	 * the parser's case normalization to the base symbol too. */
	gst_config_add_keybind(config, "Ctrl+Shift+ssharp", "paste_primary");
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_question, XK_ssharp,
		ControlMask | ShiftMask), ==, GST_ACTION_PASTE_PRIMARY);
}

/* YAML persistence must preserve base and explicit shifted bindings, including
 * precedence and modifier aliases; malformed bindings fail without mutation. */
static void
test_keybind_config_roundtrip(void)
{
	g_autofree gchar *path = write_temp_yaml(
		"keybinds:\n"
		"  'control+shift+1': clipboard_copy\n"
		"  'Ctrl+Shift+exclam': clipboard_paste\n"
		"  'Mod1+Shift+bracketleft': paste_primary\n"
		"  'Mod4+F12': scroll_top\n");
	g_autoptr(GstConfig) config = gst_config_new();
	g_autoptr(GstConfig) restored = gst_config_new();
	g_autoptr(GFile) file = g_file_new_for_path(path);
	g_autoptr(GError) error = NULL;
	const GArray *bindings;
	GstKeybind binding = { 0 };
	const gchar *invalid[] = { "Hyper+a", "Meta+a", "Ctrl++", "Ctrl+NotAKeysym" };
	guint i;

	g_assert_true(gst_config_load_from_path(config, path, &error));
	g_assert_no_error(error);
	g_assert_true(gst_config_save_to_file(config, file, &error));
	g_assert_no_error(error);
	g_assert_true(gst_config_load_from_path(restored, path, &error));
	g_assert_no_error(error);
	bindings = gst_config_get_keybinds(restored);
	g_assert_cmpuint(bindings->len, ==, 4);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_exclam, XK_1, ControlMask | ShiftMask),
		==, GST_ACTION_CLIPBOARD_PASTE);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_braceleft, XK_bracketleft, Mod1Mask | ShiftMask),
		==, GST_ACTION_PASTE_PRIMARY);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_F12, XK_F12, Mod4Mask),
		==, GST_ACTION_SCROLL_TOP);
	g_assert_cmpint(gst_keybind_lookup_event(bindings, XK_C, XK_c, ControlMask | ShiftMask),
		==, GST_ACTION_NONE);
	for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
		g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "Unknown *");
		g_assert_false(gst_keybind_parse(invalid[i], "clipboard_copy", &binding));
		g_test_assert_expected_messages();
		g_assert_cmpuint(binding.keyval, ==, 0);
		g_assert_cmpint(binding.action, ==, GST_ACTION_NONE);
	}
	g_unlink(path);
}

/* CapsLock must not break accented, Greek or Cyrillic letter shortcuts. */
static void
test_keybind_international_case(void)
{
	static const struct { guint lower; guint upper; } cases[] = {
		{ XK_eacute, XK_Eacute }, { XK_udiaeresis, XK_Udiaeresis },
		{ XK_Greek_alpha, XK_Greek_ALPHA }, { XK_Cyrillic_a, XK_Cyrillic_A }
	};
	guint i;
	guint shift;
	guint lock;

	for (i = 0; i < G_N_ELEMENTS(cases); i++) {
		for (shift = 0; shift < 2; shift++) {
			GstKeybind binding;
			GArray bindings = { (gchar *)&binding, 1 };
			g_autofree gchar *key = g_strdup_printf("Ctrl+%s%s", shift ? "Shift+" : "",
				XKeysymToString((KeySym)cases[i].lower));
			g_assert_true(gst_keybind_parse(key, "clipboard_copy", &binding));
			for (lock = 0; lock < 2; lock++) {
				guint symbol = (shift != lock) ? cases[i].upper : cases[i].lower;
				guint state = ControlMask | (shift ? ShiftMask : 0) | (lock ? LockMask : 0);
				g_assert_cmpint(gst_keybind_lookup_event(&bindings, symbol, cases[i].lower, state),
					==, GST_ACTION_CLIPBOARD_COPY);
			}
		}
	}
}

/* ===== Main ===== */

int
main(
	int     argc,
	char    **argv
){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/keybind/default-matrix", test_keybind_defaults);
	g_test_add_func("/keybind/shift-matrix", test_keybind_shift_matrix);
	g_test_add_func("/keybind/event-precedence", test_keybind_event_precedence);
	g_test_add_func("/keybind/config-roundtrip", test_keybind_config_roundtrip);
	g_test_add_func("/keybind/international-case", test_keybind_international_case);

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
	g_test_add_func("/keybind/lookup-capslock-ctrl",
		test_keybind_lookup_capslock_ctrl);
	g_test_add_func("/keybind/lookup-capslock-ctrl-shift",
		test_keybind_lookup_capslock_ctrl_shift);

	/* Config integration test */
	g_test_add_func("/keybind/config-load-keybinds",
		test_config_load_keybinds);

	return g_test_run();
}
