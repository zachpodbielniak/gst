/*
 * gst-keybind.h - Configurable key and mouse bindings
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Provides structs and functions for parsing key binding strings
 * (e.g. "Ctrl+Shift+c") into keysym + modifier pairs, mapping
 * action strings (e.g. "clipboard_copy") to #GstAction values,
 * and looking up actions from X11 key/button events.
 */

#ifndef GST_KEYBIND_H
#define GST_KEYBIND_H

#include <glib.h>
#include "../gst-enums.h"

G_BEGIN_DECLS

/**
 * GstKeybind:
 * @keyval: X11 keysym value (e.g. XK_C, XK_Page_Up)
 * @mods: Modifier flags (#GstKeyMod)
 * @action: The action to trigger (#GstAction)
 *
 * A single keyboard binding mapping a key + modifiers to an action.
 */
typedef struct {
	guint     keyval;
	GstKeyMod mods;
	GstAction action;
} GstKeybind;

/**
 * GstMousebind:
 * @button: Mouse button (#GstMouseButton)
 * @mods: Modifier flags (#GstKeyMod)
 * @action: The action to trigger (#GstAction)
 *
 * A single mouse binding mapping a button + modifiers to an action.
 */
typedef struct {
	GstMouseButton button;
	GstKeyMod      mods;
	GstAction      action;
} GstMousebind;

/**
 * gst_keybind_parse:
 * @key_str: Key binding string (e.g. "Ctrl+Shift+c")
 * @action_str: Action name string (e.g. "clipboard_copy")
 * @out: (out): Location to store the parsed binding
 *
 * Parses a key binding string and action name into a #GstKeybind.
 * The key string is split on '+'; all tokens except the last are
 * treated as modifiers, and the last token is converted to a keysym
 * via XStringToKeysym(). If Shift is present and the key is a
 * lowercase letter (a-z), the keysym is normalized to uppercase.
 *
 * Returns: %TRUE on success, %FALSE if parsing fails
 */
gboolean
gst_keybind_parse(
	const gchar *key_str,
	const gchar *action_str,
	GstKeybind  *out
);

/**
 * gst_mousebind_parse:
 * @key_str: Mouse binding string (e.g. "Shift+Button4")
 * @action_str: Action name string (e.g. "scroll_up_fast")
 * @out: (out): Location to store the parsed binding
 *
 * Parses a mouse binding string and action name into a #GstMousebind.
 * The key string is split on '+'; all tokens except the last are
 * treated as modifiers, and the last token must match "Button[1-9]".
 *
 * Returns: %TRUE on success, %FALSE if parsing fails
 */
gboolean
gst_mousebind_parse(
	const gchar  *key_str,
	const gchar  *action_str,
	GstMousebind *out
);

/**
 * gst_action_from_string:
 * @str: Action name string (e.g. "clipboard_copy")
 *
 * Converts an action name to a #GstAction enum value.
 * The comparison is case-insensitive.
 *
 * Returns: The matching #GstAction, or %GST_ACTION_NONE if unknown
 */
GstAction
gst_action_from_string(const gchar *str);

/**
 * gst_action_to_string:
 * @action: A #GstAction value
 *
 * Converts a #GstAction enum value to its canonical string name.
 *
 * Returns: (transfer none): The action name, or "none" for unknown values
 */
const gchar *
gst_action_to_string(GstAction action);

/**
 * gst_key_mod_from_x11_state:
 * @state: X11 modifier state bitmask
 *
 * Converts an X11 modifier state to #GstKeyMod flags.
 * Strips NumLock (Mod2Mask), CapsLock (LockMask), and
 * ScrollLock (Mod3Mask) so bindings work regardless of lock state.
 *
 * Returns: The converted #GstKeyMod flags
 */
GstKeyMod
gst_key_mod_from_x11_state(guint state);

/**
 * gst_keybind_lookup:
 * @bindings: (element-type GstKeybind): Array of key bindings
 * @keyval: X11 keysym to look up
 * @x11_state: Raw X11 modifier state (lock bits are stripped internally)
 *
 * Searches the binding array for a match. The X11 state is converted
 * to #GstKeyMod (stripping lock bits) before comparison.
 *
 * Returns: The matching #GstAction, or %GST_ACTION_NONE if no match
 */
GstAction
gst_keybind_lookup(
	const GArray *bindings,
	guint        keyval,
	guint        x11_state
);

/**
 * gst_mousebind_lookup:
 * @bindings: (element-type GstMousebind): Array of mouse bindings
 * @button: Mouse button number (X11 Button1-Button9)
 * @x11_state: Raw X11 modifier state (lock bits are stripped internally)
 *
 * Searches the binding array for a match.
 *
 * Returns: The matching #GstAction, or %GST_ACTION_NONE if no match
 */
GstAction
gst_mousebind_lookup(
	const GArray *bindings,
	guint        button,
	guint        x11_state
);

G_END_DECLS

#endif /* GST_KEYBIND_H */
