/*
 * gst-keybind.c - Configurable key and mouse bindings
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Parses key binding strings like "Ctrl+Shift+c" into keysym +
 * modifier pairs. Provides lookup functions to translate X11 key
 * events into GstAction values using a configured binding array.
 *
 * Modifier parsing is case-insensitive. Key names are resolved
 * via XStringToKeysym(). When Shift is a modifier and the key is
 * a lowercase letter (a-z), the keysym is normalized to uppercase
 * to match what X11 reports when Shift is held.
 *
 * Lock bits (NumLock, CapsLock, ScrollLock) are stripped from the
 * X11 state before comparison, so bindings work regardless of
 * lock key state.
 */

#include "gst-keybind.h"

#include <string.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

/* ===== Action string table ===== */

/*
 * Mapping between action name strings and GstAction enum values.
 * Used for YAML config parsing and round-tripping.
 */
static const struct {
	const gchar *name;
	GstAction    action;
} action_table[] = {
	{ "clipboard_copy",    GST_ACTION_CLIPBOARD_COPY },
	{ "clipboard_paste",   GST_ACTION_CLIPBOARD_PASTE },
	{ "paste_primary",     GST_ACTION_PASTE_PRIMARY },
	{ "scroll_up",         GST_ACTION_SCROLL_UP },
	{ "scroll_down",       GST_ACTION_SCROLL_DOWN },
	{ "scroll_top",        GST_ACTION_SCROLL_TOP },
	{ "scroll_bottom",     GST_ACTION_SCROLL_BOTTOM },
	{ "scroll_up_fast",    GST_ACTION_SCROLL_UP_FAST },
	{ "scroll_down_fast",  GST_ACTION_SCROLL_DOWN_FAST },
	{ "zoom_in",           GST_ACTION_ZOOM_IN },
	{ "zoom_out",          GST_ACTION_ZOOM_OUT },
	{ "zoom_reset",        GST_ACTION_ZOOM_RESET },
};

#define N_ACTIONS (sizeof(action_table) / sizeof(action_table[0]))

/* ===== Action string conversion ===== */

/**
 * gst_action_from_string:
 * @str: Action name string
 *
 * Looks up an action by name (case-insensitive).
 *
 * Returns: The matching #GstAction, or %GST_ACTION_NONE
 */
GstAction
gst_action_from_string(const gchar *str)
{
	guint i;

	if (str == NULL) {
		return GST_ACTION_NONE;
	}

	for (i = 0; i < N_ACTIONS; i++) {
		if (g_ascii_strcasecmp(str, action_table[i].name) == 0) {
			return action_table[i].action;
		}
	}

	return GST_ACTION_NONE;
}

/**
 * gst_action_to_string:
 * @action: A #GstAction value
 *
 * Converts an action enum value to its canonical string name.
 *
 * Returns: (transfer none): The action name, or "none"
 */
const gchar *
gst_action_to_string(GstAction action)
{
	guint i;

	for (i = 0; i < N_ACTIONS; i++) {
		if (action_table[i].action == action) {
			return action_table[i].name;
		}
	}

	return "none";
}

/* ===== Modifier parsing ===== */

/*
 * parse_modifier_token:
 *
 * Converts a single modifier name to GstKeyMod flags.
 * Case-insensitive. Returns GST_KEY_MOD_NONE if unknown.
 */
static GstKeyMod
parse_modifier_token(const gchar *token)
{
	if (g_ascii_strcasecmp(token, "Ctrl") == 0 ||
	    g_ascii_strcasecmp(token, "Control") == 0)
	{
		return GST_KEY_MOD_CTRL;
	}

	if (g_ascii_strcasecmp(token, "Shift") == 0) {
		return GST_KEY_MOD_SHIFT;
	}

	if (g_ascii_strcasecmp(token, "Alt") == 0 ||
	    g_ascii_strcasecmp(token, "Mod1") == 0)
	{
		return GST_KEY_MOD_ALT;
	}

	if (g_ascii_strcasecmp(token, "Super") == 0 ||
	    g_ascii_strcasecmp(token, "Mod4") == 0)
	{
		return GST_KEY_MOD_SUPER;
	}

	return GST_KEY_MOD_NONE;
}

/* ===== Key binding parsing ===== */

/**
 * gst_keybind_parse:
 * @key_str: Key binding string (e.g. "Ctrl+Shift+c")
 * @action_str: Action name string (e.g. "clipboard_copy")
 * @out: (out): Location to store the parsed binding
 *
 * Parses a key binding. Splits on '+', treats all tokens except
 * the last as modifiers, resolves the last token as a keysym.
 * Normalizes Shift+lowercase to uppercase keysym.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_keybind_parse(
	const gchar *key_str,
	const gchar *action_str,
	GstKeybind  *out
){
	gchar **tokens;
	guint n_tokens;
	guint i;
	GstKeyMod mods;
	KeySym keysym;
	GstAction action;

	g_return_val_if_fail(key_str != NULL, FALSE);
	g_return_val_if_fail(action_str != NULL, FALSE);
	g_return_val_if_fail(out != NULL, FALSE);

	/* Parse the action string */
	action = gst_action_from_string(action_str);
	if (action == GST_ACTION_NONE) {
		g_warning("Unknown action: '%s'", action_str);
		return FALSE;
	}

	/* Split on '+' */
	tokens = g_strsplit(key_str, "+", -1);
	n_tokens = g_strv_length(tokens);

	if (n_tokens == 0) {
		g_strfreev(tokens);
		return FALSE;
	}

	/* All tokens except the last are modifiers */
	mods = GST_KEY_MOD_NONE;
	for (i = 0; i < n_tokens - 1; i++) {
		GstKeyMod mod;

		mod = parse_modifier_token(tokens[i]);
		if (mod == GST_KEY_MOD_NONE) {
			g_warning("Unknown modifier: '%s' in key '%s'",
				tokens[i], key_str);
			g_strfreev(tokens);
			return FALSE;
		}
		mods |= mod;
	}

	/* Last token is the key name — resolve via XStringToKeysym */
	keysym = XStringToKeysym(tokens[n_tokens - 1]);
	if (keysym == NoSymbol) {
		g_warning("Unknown key name: '%s' in key '%s'",
			tokens[n_tokens - 1], key_str);
		g_strfreev(tokens);
		return FALSE;
	}

	/*
	 * Shift + lowercase letter normalization:
	 * When Shift is held, X11 reports the uppercase keysym (XK_A-XK_Z).
	 * Store the uppercase version so lookup matches correctly.
	 */
	if ((mods & GST_KEY_MOD_SHIFT) &&
	    keysym >= XK_a && keysym <= XK_z)
	{
		keysym = keysym - XK_a + XK_A;
	}

	out->keyval = (guint)keysym;
	out->mods = mods;
	out->action = action;

	g_strfreev(tokens);
	return TRUE;
}

/* ===== Mouse binding parsing ===== */

/**
 * gst_mousebind_parse:
 * @key_str: Mouse binding string (e.g. "Shift+Button4")
 * @action_str: Action name string
 * @out: (out): Location to store the parsed binding
 *
 * Parses a mouse binding. The last token must be "Button[1-9]".
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mousebind_parse(
	const gchar  *key_str,
	const gchar  *action_str,
	GstMousebind *out
){
	gchar **tokens;
	guint n_tokens;
	guint i;
	GstKeyMod mods;
	GstAction action;
	const gchar *btn_str;
	gint btn_num;

	g_return_val_if_fail(key_str != NULL, FALSE);
	g_return_val_if_fail(action_str != NULL, FALSE);
	g_return_val_if_fail(out != NULL, FALSE);

	/* Parse the action string */
	action = gst_action_from_string(action_str);
	if (action == GST_ACTION_NONE) {
		g_warning("Unknown action: '%s'", action_str);
		return FALSE;
	}

	/* Split on '+' */
	tokens = g_strsplit(key_str, "+", -1);
	n_tokens = g_strv_length(tokens);

	if (n_tokens == 0) {
		g_strfreev(tokens);
		return FALSE;
	}

	/* All tokens except the last are modifiers */
	mods = GST_KEY_MOD_NONE;
	for (i = 0; i < n_tokens - 1; i++) {
		GstKeyMod mod;

		mod = parse_modifier_token(tokens[i]);
		if (mod == GST_KEY_MOD_NONE) {
			g_warning("Unknown modifier: '%s' in binding '%s'",
				tokens[i], key_str);
			g_strfreev(tokens);
			return FALSE;
		}
		mods |= mod;
	}

	/* Last token must be "Button[1-9]" */
	btn_str = tokens[n_tokens - 1];
	if (g_ascii_strncasecmp(btn_str, "Button", 6) != 0 ||
	    strlen(btn_str) != 7)
	{
		g_warning("Invalid button: '%s' in binding '%s'",
			btn_str, key_str);
		g_strfreev(tokens);
		return FALSE;
	}

	btn_num = btn_str[6] - '0';
	if (btn_num < 1 || btn_num > 9) {
		g_warning("Invalid button number: '%s' in binding '%s'",
			btn_str, key_str);
		g_strfreev(tokens);
		return FALSE;
	}

	out->button = (GstMouseButton)btn_num;
	out->mods = mods;
	out->action = action;

	g_strfreev(tokens);
	return TRUE;
}

/* ===== X11 state conversion ===== */

/**
 * gst_key_mod_from_x11_state:
 * @state: X11 modifier state bitmask
 *
 * Converts X11 modifier state to GstKeyMod, stripping lock bits.
 * NumLock = Mod2Mask, CapsLock = LockMask, ScrollLock = Mod3Mask.
 *
 * Returns: The converted #GstKeyMod flags
 */
GstKeyMod
gst_key_mod_from_x11_state(guint state)
{
	GstKeyMod mods;

	mods = GST_KEY_MOD_NONE;

	if (state & ShiftMask) {
		mods |= GST_KEY_MOD_SHIFT;
	}
	if (state & ControlMask) {
		mods |= GST_KEY_MOD_CTRL;
	}
	if (state & Mod1Mask) {
		mods |= GST_KEY_MOD_ALT;
	}
	if (state & Mod4Mask) {
		mods |= GST_KEY_MOD_SUPER;
	}

	/* Mod2Mask (NumLock), LockMask (CapsLock), Mod3Mask (ScrollLock)
	 * are intentionally NOT mapped — they are stripped. */

	return mods;
}

/* ===== Lookup functions ===== */

/**
 * gst_keybind_lookup:
 * @bindings: (element-type GstKeybind): Array of key bindings
 * @keyval: X11 keysym to look up
 * @x11_state: Raw X11 modifier state
 *
 * Linear scan of the binding array. Converts x11_state to GstKeyMod
 * (stripping lock bits) before comparing.
 *
 * Returns: The matching #GstAction, or %GST_ACTION_NONE
 */
GstAction
gst_keybind_lookup(
	const GArray *bindings,
	guint        keyval,
	guint        x11_state
){
	GstKeyMod mods;
	guint i;

	if (bindings == NULL) {
		return GST_ACTION_NONE;
	}

	mods = gst_key_mod_from_x11_state(x11_state);

	for (i = 0; i < bindings->len; i++) {
		const GstKeybind *kb;

		kb = &g_array_index(bindings, GstKeybind, i);
		if (kb->keyval == keyval && kb->mods == mods) {
			return kb->action;
		}
	}

	return GST_ACTION_NONE;
}

/**
 * gst_mousebind_lookup:
 * @bindings: (element-type GstMousebind): Array of mouse bindings
 * @button: Mouse button number (X11 Button1-Button9)
 * @x11_state: Raw X11 modifier state
 *
 * Linear scan of the binding array.
 *
 * Returns: The matching #GstAction, or %GST_ACTION_NONE
 */
GstAction
gst_mousebind_lookup(
	const GArray *bindings,
	guint        button,
	guint        x11_state
){
	GstKeyMod mods;
	guint i;

	if (bindings == NULL) {
		return GST_ACTION_NONE;
	}

	mods = gst_key_mod_from_x11_state(x11_state);

	for (i = 0; i < bindings->len; i++) {
		const GstMousebind *mb;

		mb = &g_array_index(bindings, GstMousebind, i);
		if ((guint)mb->button == button && mb->mods == mods) {
			return mb->action;
		}
	}

	return GST_ACTION_NONE;
}
