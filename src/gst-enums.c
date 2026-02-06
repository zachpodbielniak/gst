/*
 * gst-enums.c - GST Enumeration GType Registration
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file registers all enumeration types with the GObject type system.
 */

#include "gst-enums.h"

/*
 * gst_hook_point_get_type:
 *
 * Registers the GstHookPoint enumeration type.
 *
 * Returns: the GType for GstHookPoint
 */
GType
gst_hook_point_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_HOOK_KEY_PRESS, "GST_HOOK_KEY_PRESS", "key-press" },
            { GST_HOOK_KEY_RELEASE, "GST_HOOK_KEY_RELEASE", "key-release" },
            { GST_HOOK_BUTTON_PRESS, "GST_HOOK_BUTTON_PRESS", "button-press" },
            { GST_HOOK_BUTTON_RELEASE, "GST_HOOK_BUTTON_RELEASE", "button-release" },
            { GST_HOOK_MOTION, "GST_HOOK_MOTION", "motion" },
            { GST_HOOK_SCROLL, "GST_HOOK_SCROLL", "scroll" },
            { GST_HOOK_DRAG_DROP, "GST_HOOK_DRAG_DROP", "drag-drop" },
            { GST_HOOK_IME_PREEDIT, "GST_HOOK_IME_PREEDIT", "ime-preedit" },
            { GST_HOOK_IME_COMMIT, "GST_HOOK_IME_COMMIT", "ime-commit" },
            { GST_HOOK_PRE_OUTPUT, "GST_HOOK_PRE_OUTPUT", "pre-output" },
            { GST_HOOK_POST_OUTPUT, "GST_HOOK_POST_OUTPUT", "post-output" },
            { GST_HOOK_ESCAPE_SEQUENCE, "GST_HOOK_ESCAPE_SEQUENCE", "escape-sequence" },
            { GST_HOOK_ESCAPE_CSI, "GST_HOOK_ESCAPE_CSI", "escape-csi" },
            { GST_HOOK_ESCAPE_OSC, "GST_HOOK_ESCAPE_OSC", "escape-osc" },
            { GST_HOOK_ESCAPE_DCS, "GST_HOOK_ESCAPE_DCS", "escape-dcs" },
            { GST_HOOK_EXTERNAL_PIPE, "GST_HOOK_EXTERNAL_PIPE", "external-pipe" },
            { GST_HOOK_PRE_RENDER, "GST_HOOK_PRE_RENDER", "pre-render" },
            { GST_HOOK_POST_RENDER, "GST_HOOK_POST_RENDER", "post-render" },
            { GST_HOOK_RENDER_BACKGROUND, "GST_HOOK_RENDER_BACKGROUND", "render-background" },
            { GST_HOOK_RENDER_LINE, "GST_HOOK_RENDER_LINE", "render-line" },
            { GST_HOOK_RENDER_GLYPH, "GST_HOOK_RENDER_GLYPH", "render-glyph" },
            { GST_HOOK_RENDER_CURSOR, "GST_HOOK_RENDER_CURSOR", "render-cursor" },
            { GST_HOOK_RENDER_SELECTION, "GST_HOOK_RENDER_SELECTION", "render-selection" },
            { GST_HOOK_RENDER_OVERLAY, "GST_HOOK_RENDER_OVERLAY", "render-overlay" },
            { GST_HOOK_GLYPH_TRANSFORM, "GST_HOOK_GLYPH_TRANSFORM", "glyph-transform" },
            { GST_HOOK_SYNC_FRAME, "GST_HOOK_SYNC_FRAME", "sync-frame" },
            { GST_HOOK_FONT_LOAD, "GST_HOOK_FONT_LOAD", "font-load" },
            { GST_HOOK_FONT_FALLBACK, "GST_HOOK_FONT_FALLBACK", "font-fallback" },
            { GST_HOOK_FONT_METRICS, "GST_HOOK_FONT_METRICS", "font-metrics" },
            { GST_HOOK_COLOR_QUERY, "GST_HOOK_COLOR_QUERY", "color-query" },
            { GST_HOOK_COLOR_SET, "GST_HOOK_COLOR_SET", "color-set" },
            { GST_HOOK_COLOR_INVERT, "GST_HOOK_COLOR_INVERT", "color-invert" },
            { GST_HOOK_WINDOW_CREATE, "GST_HOOK_WINDOW_CREATE", "window-create" },
            { GST_HOOK_WINDOW_GEOMETRY, "GST_HOOK_WINDOW_GEOMETRY", "window-geometry" },
            { GST_HOOK_WINDOW_PROPERTY, "GST_HOOK_WINDOW_PROPERTY", "window-property" },
            { GST_HOOK_FOCUS_IN, "GST_HOOK_FOCUS_IN", "focus-in" },
            { GST_HOOK_FOCUS_OUT, "GST_HOOK_FOCUS_OUT", "focus-out" },
            { GST_HOOK_FULLSCREEN, "GST_HOOK_FULLSCREEN", "fullscreen" },
            { GST_HOOK_STARTUP, "GST_HOOK_STARTUP", "startup" },
            { GST_HOOK_SHUTDOWN, "GST_HOOK_SHUTDOWN", "shutdown" },
            { GST_HOOK_RESIZE, "GST_HOOK_RESIZE", "resize" },
            { GST_HOOK_BELL, "GST_HOOK_BELL", "bell" },
            { GST_HOOK_TITLE_CHANGE, "GST_HOOK_TITLE_CHANGE", "title-change" },
            { GST_HOOK_ICON_CHANGE, "GST_HOOK_ICON_CHANGE", "icon-change" },
            { GST_HOOK_WORKDIR_CHANGE, "GST_HOOK_WORKDIR_CHANGE", "workdir-change" },
            { GST_HOOK_SELECTION_START, "GST_HOOK_SELECTION_START", "selection-start" },
            { GST_HOOK_SELECTION_CHANGE, "GST_HOOK_SELECTION_CHANGE", "selection-change" },
            { GST_HOOK_SELECTION_END, "GST_HOOK_SELECTION_END", "selection-end" },
            { GST_HOOK_SELECTION_SNAP, "GST_HOOK_SELECTION_SNAP", "selection-snap" },
            { GST_HOOK_CLIPBOARD_COPY, "GST_HOOK_CLIPBOARD_COPY", "clipboard-copy" },
            { GST_HOOK_CLIPBOARD_PASTE, "GST_HOOK_CLIPBOARD_PASTE", "clipboard-paste" },
            { GST_HOOK_URL_DETECT, "GST_HOOK_URL_DETECT", "url-detect" },
            { GST_HOOK_URL_OPEN, "GST_HOOK_URL_OPEN", "url-open" },
            { GST_HOOK_TEXT_OPEN, "GST_HOOK_TEXT_OPEN", "text-open" },
            { GST_HOOK_MODE_CHANGE, "GST_HOOK_MODE_CHANGE", "mode-change" },
            { GST_HOOK_CURSOR_MOVE, "GST_HOOK_CURSOR_MOVE", "cursor-move" },
            { GST_HOOK_SCROLL_REGION, "GST_HOOK_SCROLL_REGION", "scroll-region" },
            { GST_HOOK_NEWTERM, "GST_HOOK_NEWTERM", "newterm" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstHookPoint", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_cursor_shape_get_type:
 *
 * Registers the GstCursorShape enumeration type.
 *
 * Returns: the GType for GstCursorShape
 */
GType
gst_cursor_shape_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_CURSOR_SHAPE_BLOCK, "GST_CURSOR_SHAPE_BLOCK", "block" },
            { GST_CURSOR_SHAPE_UNDERLINE, "GST_CURSOR_SHAPE_UNDERLINE", "underline" },
            { GST_CURSOR_SHAPE_BAR, "GST_CURSOR_SHAPE_BAR", "bar" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstCursorShape", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_cursor_state_get_type:
 *
 * Registers the GstCursorState flags type.
 *
 * Returns: the GType for GstCursorState
 */
GType
gst_cursor_state_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GFlagsValue values[] = {
            { GST_CURSOR_STATE_VISIBLE, "GST_CURSOR_STATE_VISIBLE", "visible" },
            { GST_CURSOR_STATE_BLINK, "GST_CURSOR_STATE_BLINK", "blink" },
            { GST_CURSOR_STATE_BLINK_ON, "GST_CURSOR_STATE_BLINK_ON", "blink-on" },
            { GST_CURSOR_STATE_WRAPNEXT, "GST_CURSOR_STATE_WRAPNEXT", "wrapnext" },
            { 0, NULL, NULL }
        };

        GType new_type = g_flags_register_static("GstCursorState", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_glyph_attr_get_type:
 *
 * Registers the GstGlyphAttr flags type.
 *
 * Returns: the GType for GstGlyphAttr
 */
GType
gst_glyph_attr_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GFlagsValue values[] = {
            { GST_GLYPH_ATTR_NONE, "GST_GLYPH_ATTR_NONE", "none" },
            { GST_GLYPH_ATTR_BOLD, "GST_GLYPH_ATTR_BOLD", "bold" },
            { GST_GLYPH_ATTR_FAINT, "GST_GLYPH_ATTR_FAINT", "faint" },
            { GST_GLYPH_ATTR_ITALIC, "GST_GLYPH_ATTR_ITALIC", "italic" },
            { GST_GLYPH_ATTR_UNDERLINE, "GST_GLYPH_ATTR_UNDERLINE", "underline" },
            { GST_GLYPH_ATTR_BLINK, "GST_GLYPH_ATTR_BLINK", "blink" },
            { GST_GLYPH_ATTR_REVERSE, "GST_GLYPH_ATTR_REVERSE", "reverse" },
            { GST_GLYPH_ATTR_INVISIBLE, "GST_GLYPH_ATTR_INVISIBLE", "invisible" },
            { GST_GLYPH_ATTR_STRUCK, "GST_GLYPH_ATTR_STRUCK", "struck" },
            { GST_GLYPH_ATTR_WRAP, "GST_GLYPH_ATTR_WRAP", "wrap" },
            { GST_GLYPH_ATTR_WIDE, "GST_GLYPH_ATTR_WIDE", "wide" },
            { GST_GLYPH_ATTR_WDUMMY, "GST_GLYPH_ATTR_WDUMMY", "wdummy" },
            { GST_GLYPH_ATTR_UNDERCURL, "GST_GLYPH_ATTR_UNDERCURL", "undercurl" },
            { GST_GLYPH_ATTR_DUNDERLINE, "GST_GLYPH_ATTR_DUNDERLINE", "dunderline" },
            { GST_GLYPH_ATTR_OVERLINE, "GST_GLYPH_ATTR_OVERLINE", "overline" },
            { 0, NULL, NULL }
        };

        GType new_type = g_flags_register_static("GstGlyphAttr", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_term_mode_get_type:
 *
 * Registers the GstTermMode flags type.
 *
 * Returns: the GType for GstTermMode
 */
GType
gst_term_mode_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GFlagsValue values[] = {
            { GST_MODE_WRAP, "GST_MODE_WRAP", "wrap" },
            { GST_MODE_INSERT, "GST_MODE_INSERT", "insert" },
            { GST_MODE_ALTSCREEN, "GST_MODE_ALTSCREEN", "altscreen" },
            { GST_MODE_CRLF, "GST_MODE_CRLF", "crlf" },
            { GST_MODE_ECHO, "GST_MODE_ECHO", "echo" },
            { GST_MODE_PRINT, "GST_MODE_PRINT", "print" },
            { GST_MODE_UTF8, "GST_MODE_UTF8", "utf8" },
            { GST_MODE_SIXEL, "GST_MODE_SIXEL", "sixel" },
            { GST_MODE_BRCKTPASTE, "GST_MODE_BRCKTPASTE", "brcktpaste" },
            { GST_MODE_NUMLOCK, "GST_MODE_NUMLOCK", "numlock" },
            { GST_MODE_MOUSE_X10, "GST_MODE_MOUSE_X10", "mouse-x10" },
            { GST_MODE_MOUSE_BTN, "GST_MODE_MOUSE_BTN", "mouse-btn" },
            { GST_MODE_MOUSE_MOTION, "GST_MODE_MOUSE_MOTION", "mouse-motion" },
            { GST_MODE_MOUSE_SGR, "GST_MODE_MOUSE_SGR", "mouse-sgr" },
            { GST_MODE_8BIT, "GST_MODE_8BIT", "8bit" },
            { GST_MODE_APPKEYPAD, "GST_MODE_APPKEYPAD", "appkeypad" },
            { GST_MODE_APPCURSOR, "GST_MODE_APPCURSOR", "appcursor" },
            { GST_MODE_REVERSE, "GST_MODE_REVERSE", "reverse" },
            { GST_MODE_KBDLOCK, "GST_MODE_KBDLOCK", "kbdlock" },
            { GST_MODE_HIDE, "GST_MODE_HIDE", "hide" },
            { GST_MODE_FOCUS, "GST_MODE_FOCUS", "focus" },
            { GST_MODE_MOUSE_MANY, "GST_MODE_MOUSE_MANY", "mouse-many" },
            { GST_MODE_MOUSE_UTF8, "GST_MODE_MOUSE_UTF8", "mouse-utf8" },
            { 0, NULL, NULL }
        };

        GType new_type = g_flags_register_static("GstTermMode", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_selection_mode_get_type:
 *
 * Registers the GstSelectionMode enumeration type.
 *
 * Returns: the GType for GstSelectionMode
 */
GType
gst_selection_mode_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_SELECTION_IDLE, "GST_SELECTION_IDLE", "idle" },
            { GST_SELECTION_EMPTY, "GST_SELECTION_EMPTY", "empty" },
            { GST_SELECTION_READY, "GST_SELECTION_READY", "ready" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstSelectionMode", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_selection_type_get_type:
 *
 * Registers the GstSelectionType enumeration type.
 *
 * Returns: the GType for GstSelectionType
 */
GType
gst_selection_type_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_SELECTION_TYPE_REGULAR, "GST_SELECTION_TYPE_REGULAR", "regular" },
            { GST_SELECTION_TYPE_RECTANGULAR, "GST_SELECTION_TYPE_RECTANGULAR", "rectangular" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstSelectionType", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_selection_snap_get_type:
 *
 * Registers the GstSelectionSnap enumeration type.
 *
 * Returns: the GType for GstSelectionSnap
 */
GType
gst_selection_snap_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_SELECTION_SNAP_NONE, "GST_SELECTION_SNAP_NONE", "none" },
            { GST_SELECTION_SNAP_WORD, "GST_SELECTION_SNAP_WORD", "word" },
            { GST_SELECTION_SNAP_LINE, "GST_SELECTION_SNAP_LINE", "line" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstSelectionSnap", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_escape_state_get_type:
 *
 * Registers the GstEscapeState enumeration type.
 *
 * Returns: the GType for GstEscapeState
 */
GType
gst_escape_state_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_ESC_START, "GST_ESC_START", "start" },
            { GST_ESC_CSI, "GST_ESC_CSI", "csi" },
            { GST_ESC_STR, "GST_ESC_STR", "str" },
            { GST_ESC_ALTCHARSET, "GST_ESC_ALTCHARSET", "altcharset" },
            { GST_ESC_STR_END, "GST_ESC_STR_END", "str-end" },
            { GST_ESC_TEST, "GST_ESC_TEST", "test" },
            { GST_ESC_UTF8, "GST_ESC_UTF8", "utf8" },
            { GST_ESC_DCS, "GST_ESC_DCS", "dcs" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstEscapeState", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_charset_get_type:
 *
 * Registers the GstCharset enumeration type.
 *
 * Returns: the GType for GstCharset
 */
GType
gst_charset_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_CHARSET_GRAPHIC0, "GST_CHARSET_GRAPHIC0", "graphic0" },
            { GST_CHARSET_GRAPHIC1, "GST_CHARSET_GRAPHIC1", "graphic1" },
            { GST_CHARSET_UK, "GST_CHARSET_UK", "uk" },
            { GST_CHARSET_USA, "GST_CHARSET_USA", "usa" },
            { GST_CHARSET_MULTI, "GST_CHARSET_MULTI", "multi" },
            { GST_CHARSET_GER, "GST_CHARSET_GER", "ger" },
            { GST_CHARSET_FIN, "GST_CHARSET_FIN", "fin" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstCharset", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_color_index_get_type:
 *
 * Registers the GstColorIndex enumeration type.
 *
 * Returns: the GType for GstColorIndex
 */
GType
gst_color_index_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_COLOR_BLACK, "GST_COLOR_BLACK", "black" },
            { GST_COLOR_RED, "GST_COLOR_RED", "red" },
            { GST_COLOR_GREEN, "GST_COLOR_GREEN", "green" },
            { GST_COLOR_YELLOW, "GST_COLOR_YELLOW", "yellow" },
            { GST_COLOR_BLUE, "GST_COLOR_BLUE", "blue" },
            { GST_COLOR_MAGENTA, "GST_COLOR_MAGENTA", "magenta" },
            { GST_COLOR_CYAN, "GST_COLOR_CYAN", "cyan" },
            { GST_COLOR_WHITE, "GST_COLOR_WHITE", "white" },
            { GST_COLOR_BRIGHT_BLACK, "GST_COLOR_BRIGHT_BLACK", "bright-black" },
            { GST_COLOR_BRIGHT_RED, "GST_COLOR_BRIGHT_RED", "bright-red" },
            { GST_COLOR_BRIGHT_GREEN, "GST_COLOR_BRIGHT_GREEN", "bright-green" },
            { GST_COLOR_BRIGHT_YELLOW, "GST_COLOR_BRIGHT_YELLOW", "bright-yellow" },
            { GST_COLOR_BRIGHT_BLUE, "GST_COLOR_BRIGHT_BLUE", "bright-blue" },
            { GST_COLOR_BRIGHT_MAGENTA, "GST_COLOR_BRIGHT_MAGENTA", "bright-magenta" },
            { GST_COLOR_BRIGHT_CYAN, "GST_COLOR_BRIGHT_CYAN", "bright-cyan" },
            { GST_COLOR_BRIGHT_WHITE, "GST_COLOR_BRIGHT_WHITE", "bright-white" },
            { GST_COLOR_DEFAULT_FG, "GST_COLOR_DEFAULT_FG", "default-fg" },
            { GST_COLOR_DEFAULT_BG, "GST_COLOR_DEFAULT_BG", "default-bg" },
            { GST_COLOR_CURSOR_FG, "GST_COLOR_CURSOR_FG", "cursor-fg" },
            { GST_COLOR_CURSOR_BG, "GST_COLOR_CURSOR_BG", "cursor-bg" },
            { GST_COLOR_REVERSE_FG, "GST_COLOR_REVERSE_FG", "reverse-fg" },
            { GST_COLOR_REVERSE_BG, "GST_COLOR_REVERSE_BG", "reverse-bg" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstColorIndex", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_module_state_get_type:
 *
 * Registers the GstModuleState enumeration type.
 *
 * Returns: the GType for GstModuleState
 */
GType
gst_module_state_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_MODULE_STATE_UNLOADED, "GST_MODULE_STATE_UNLOADED", "unloaded" },
            { GST_MODULE_STATE_LOADED, "GST_MODULE_STATE_LOADED", "loaded" },
            { GST_MODULE_STATE_INITIALIZED, "GST_MODULE_STATE_INITIALIZED", "initialized" },
            { GST_MODULE_STATE_ENABLED, "GST_MODULE_STATE_ENABLED", "enabled" },
            { GST_MODULE_STATE_DISABLED, "GST_MODULE_STATE_DISABLED", "disabled" },
            { GST_MODULE_STATE_ERROR, "GST_MODULE_STATE_ERROR", "error" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstModuleState", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_module_priority_get_type:
 *
 * Registers the GstModulePriority enumeration type.
 *
 * Returns: the GType for GstModulePriority
 */
GType
gst_module_priority_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_MODULE_PRIORITY_HIGHEST, "GST_MODULE_PRIORITY_HIGHEST", "highest" },
            { GST_MODULE_PRIORITY_HIGH, "GST_MODULE_PRIORITY_HIGH", "high" },
            { GST_MODULE_PRIORITY_NORMAL, "GST_MODULE_PRIORITY_NORMAL", "normal" },
            { GST_MODULE_PRIORITY_LOW, "GST_MODULE_PRIORITY_LOW", "low" },
            { GST_MODULE_PRIORITY_LOWEST, "GST_MODULE_PRIORITY_LOWEST", "lowest" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstModulePriority", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_key_mod_get_type:
 *
 * Registers the GstKeyMod flags type.
 *
 * Returns: the GType for GstKeyMod
 */
GType
gst_key_mod_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GFlagsValue values[] = {
            { GST_KEY_MOD_NONE, "GST_KEY_MOD_NONE", "none" },
            { GST_KEY_MOD_SHIFT, "GST_KEY_MOD_SHIFT", "shift" },
            { GST_KEY_MOD_CTRL, "GST_KEY_MOD_CTRL", "ctrl" },
            { GST_KEY_MOD_ALT, "GST_KEY_MOD_ALT", "alt" },
            { GST_KEY_MOD_SUPER, "GST_KEY_MOD_SUPER", "super" },
            { GST_KEY_MOD_HYPER, "GST_KEY_MOD_HYPER", "hyper" },
            { GST_KEY_MOD_META, "GST_KEY_MOD_META", "meta" },
            { 0, NULL, NULL }
        };

        GType new_type = g_flags_register_static("GstKeyMod", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_mouse_button_get_type:
 *
 * Registers the GstMouseButton enumeration type.
 *
 * Returns: the GType for GstMouseButton
 */
GType
gst_mouse_button_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_MOUSE_BUTTON_LEFT, "GST_MOUSE_BUTTON_LEFT", "left" },
            { GST_MOUSE_BUTTON_MIDDLE, "GST_MOUSE_BUTTON_MIDDLE", "middle" },
            { GST_MOUSE_BUTTON_RIGHT, "GST_MOUSE_BUTTON_RIGHT", "right" },
            { GST_MOUSE_BUTTON_SCROLL_UP, "GST_MOUSE_BUTTON_SCROLL_UP", "scroll-up" },
            { GST_MOUSE_BUTTON_SCROLL_DOWN, "GST_MOUSE_BUTTON_SCROLL_DOWN", "scroll-down" },
            { GST_MOUSE_BUTTON_SCROLL_LEFT, "GST_MOUSE_BUTTON_SCROLL_LEFT", "scroll-left" },
            { GST_MOUSE_BUTTON_SCROLL_RIGHT, "GST_MOUSE_BUTTON_SCROLL_RIGHT", "scroll-right" },
            { GST_MOUSE_BUTTON_8, "GST_MOUSE_BUTTON_8", "button-8" },
            { GST_MOUSE_BUTTON_9, "GST_MOUSE_BUTTON_9", "button-9" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstMouseButton", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_win_mode_get_type:
 *
 * Registers the GstWinMode flags type.
 *
 * Returns: the GType for GstWinMode
 */
GType
gst_win_mode_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GFlagsValue values[] = {
            { GST_WIN_MODE_VISIBLE, "GST_WIN_MODE_VISIBLE", "visible" },
            { GST_WIN_MODE_FOCUSED, "GST_WIN_MODE_FOCUSED", "focused" },
            { GST_WIN_MODE_BLINK, "GST_WIN_MODE_BLINK", "blink" },
            { GST_WIN_MODE_NUMLOCK, "GST_WIN_MODE_NUMLOCK", "numlock" },
            { 0, NULL, NULL }
        };

        GType new_type = g_flags_register_static("GstWinMode", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}

/*
 * gst_font_style_get_type:
 *
 * Registers the GstFontStyle enumeration type.
 *
 * Returns: the GType for GstFontStyle
 */
GType
gst_font_style_get_type(void)
{
    static GType type = 0;

    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            { GST_FONT_STYLE_NORMAL, "GST_FONT_STYLE_NORMAL", "normal" },
            { GST_FONT_STYLE_ITALIC, "GST_FONT_STYLE_ITALIC", "italic" },
            { GST_FONT_STYLE_BOLD, "GST_FONT_STYLE_BOLD", "bold" },
            { GST_FONT_STYLE_BOLD_ITALIC, "GST_FONT_STYLE_BOLD_ITALIC", "bold-italic" },
            { 0, NULL, NULL }
        };

        GType new_type = g_enum_register_static("GstFontStyle", values);
        g_once_init_leave(&type, new_type);
    }

    return type;
}
