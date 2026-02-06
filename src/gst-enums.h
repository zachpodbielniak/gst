/*
 * gst-enums.h - GST Enumerations
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file contains all enumeration types used throughout GST.
 * All enums are registered as GTypes for introspection support.
 */

#ifndef GST_ENUMS_H
#define GST_ENUMS_H

#include <glib-object.h>

G_BEGIN_DECLS

/*
 * GstHookPoint:
 *
 * Hook points for module extensibility.
 * Modules can register callbacks at these points to intercept
 * and modify terminal behavior.
 */
typedef enum {
    /* Input Hooks - return TRUE to consume event */
    GST_HOOK_KEY_PRESS,
    GST_HOOK_KEY_RELEASE,
    GST_HOOK_BUTTON_PRESS,
    GST_HOOK_BUTTON_RELEASE,
    GST_HOOK_MOTION,
    GST_HOOK_SCROLL,
    GST_HOOK_DRAG_DROP,
    GST_HOOK_IME_PREEDIT,
    GST_HOOK_IME_COMMIT,

    /* Output Hooks */
    GST_HOOK_PRE_OUTPUT,
    GST_HOOK_POST_OUTPUT,
    GST_HOOK_ESCAPE_SEQUENCE,
    GST_HOOK_ESCAPE_CSI,
    GST_HOOK_ESCAPE_OSC,
    GST_HOOK_ESCAPE_DCS,
    GST_HOOK_EXTERNAL_PIPE,

    /* Rendering Hooks */
    GST_HOOK_PRE_RENDER,
    GST_HOOK_POST_RENDER,
    GST_HOOK_RENDER_BACKGROUND,
    GST_HOOK_RENDER_LINE,
    GST_HOOK_RENDER_GLYPH,
    GST_HOOK_RENDER_CURSOR,
    GST_HOOK_RENDER_SELECTION,
    GST_HOOK_RENDER_OVERLAY,
    GST_HOOK_GLYPH_TRANSFORM,
    GST_HOOK_SYNC_FRAME,

    /* Font Hooks */
    GST_HOOK_FONT_LOAD,
    GST_HOOK_FONT_FALLBACK,
    GST_HOOK_FONT_METRICS,

    /* Color Hooks */
    GST_HOOK_COLOR_QUERY,
    GST_HOOK_COLOR_SET,
    GST_HOOK_COLOR_INVERT,

    /* Window Hooks */
    GST_HOOK_WINDOW_CREATE,
    GST_HOOK_WINDOW_GEOMETRY,
    GST_HOOK_WINDOW_PROPERTY,
    GST_HOOK_FOCUS_IN,
    GST_HOOK_FOCUS_OUT,
    GST_HOOK_FULLSCREEN,

    /* Lifecycle Hooks */
    GST_HOOK_STARTUP,
    GST_HOOK_SHUTDOWN,
    GST_HOOK_RESIZE,
    GST_HOOK_BELL,
    GST_HOOK_TITLE_CHANGE,
    GST_HOOK_ICON_CHANGE,
    GST_HOOK_WORKDIR_CHANGE,

    /* Selection Hooks */
    GST_HOOK_SELECTION_START,
    GST_HOOK_SELECTION_CHANGE,
    GST_HOOK_SELECTION_END,
    GST_HOOK_SELECTION_SNAP,
    GST_HOOK_CLIPBOARD_COPY,
    GST_HOOK_CLIPBOARD_PASTE,

    /* URL/Text Hooks */
    GST_HOOK_URL_DETECT,
    GST_HOOK_URL_OPEN,
    GST_HOOK_TEXT_OPEN,

    /* Terminal State Hooks */
    GST_HOOK_MODE_CHANGE,
    GST_HOOK_CURSOR_MOVE,
    GST_HOOK_SCROLL_REGION,
    GST_HOOK_NEWTERM,

    GST_HOOK_LAST
} GstHookPoint;

GType gst_hook_point_get_type(void) G_GNUC_CONST;
#define GST_TYPE_HOOK_POINT (gst_hook_point_get_type())

/*
 * GstCursorShape:
 *
 * Cursor shape styles.
 */
typedef enum {
    GST_CURSOR_SHAPE_BLOCK,
    GST_CURSOR_SHAPE_UNDERLINE,
    GST_CURSOR_SHAPE_BAR
} GstCursorShape;

GType gst_cursor_shape_get_type(void) G_GNUC_CONST;
#define GST_TYPE_CURSOR_SHAPE (gst_cursor_shape_get_type())

/*
 * GstCursorState:
 *
 * Cursor blink/visibility state flags.
 */
typedef enum {
    GST_CURSOR_STATE_VISIBLE  = 1 << 0,
    GST_CURSOR_STATE_BLINK    = 1 << 1,
    GST_CURSOR_STATE_BLINK_ON = 1 << 2,
    GST_CURSOR_STATE_WRAPNEXT = 1 << 3,
    GST_CURSOR_STATE_ORIGIN   = 1 << 4   /* Origin mode (DECOM) */
} GstCursorState;

GType gst_cursor_state_get_type(void) G_GNUC_CONST;
#define GST_TYPE_CURSOR_STATE (gst_cursor_state_get_type())

/*
 * GstGlyphAttr:
 *
 * Glyph attribute flags for text styling.
 */
typedef enum {
    GST_GLYPH_ATTR_NONE       = 0,
    GST_GLYPH_ATTR_BOLD       = 1 << 0,
    GST_GLYPH_ATTR_FAINT      = 1 << 1,
    GST_GLYPH_ATTR_ITALIC     = 1 << 2,
    GST_GLYPH_ATTR_UNDERLINE  = 1 << 3,
    GST_GLYPH_ATTR_BLINK      = 1 << 4,
    GST_GLYPH_ATTR_REVERSE    = 1 << 5,
    GST_GLYPH_ATTR_INVISIBLE  = 1 << 6,
    GST_GLYPH_ATTR_STRUCK     = 1 << 7,
    GST_GLYPH_ATTR_WRAP       = 1 << 8,
    GST_GLYPH_ATTR_WIDE       = 1 << 9,
    GST_GLYPH_ATTR_WDUMMY     = 1 << 10,
    GST_GLYPH_ATTR_UNDERCURL  = 1 << 11,
    GST_GLYPH_ATTR_DUNDERLINE = 1 << 12,
    GST_GLYPH_ATTR_OVERLINE   = 1 << 13
} GstGlyphAttr;

GType gst_glyph_attr_get_type(void) G_GNUC_CONST;
#define GST_TYPE_GLYPH_ATTR (gst_glyph_attr_get_type())

/*
 * GstTermMode:
 *
 * Terminal mode flags (DEC/ANSI modes).
 */
typedef enum {
    GST_MODE_WRAP         = 1 << 0,   /* Auto-wrap mode (DECAWM) */
    GST_MODE_INSERT       = 1 << 1,   /* Insert mode (IRM) */
    GST_MODE_ALTSCREEN    = 1 << 2,   /* Alternate screen buffer */
    GST_MODE_CRLF         = 1 << 3,   /* Carriage return/line feed mode */
    GST_MODE_ECHO         = 1 << 4,   /* Local echo mode */
    GST_MODE_PRINT        = 1 << 5,   /* Print mode */
    GST_MODE_UTF8         = 1 << 6,   /* UTF-8 mode */
    GST_MODE_SIXEL        = 1 << 7,   /* Sixel graphics mode */
    GST_MODE_BRCKTPASTE   = 1 << 8,   /* Bracketed paste mode */
    GST_MODE_NUMLOCK      = 1 << 9,   /* Numlock mode */
    GST_MODE_MOUSE_X10    = 1 << 10,  /* X10 mouse reporting */
    GST_MODE_MOUSE_BTN    = 1 << 11,  /* Button-event mouse tracking */
    GST_MODE_MOUSE_MOTION = 1 << 12,  /* Any-event mouse tracking */
    GST_MODE_MOUSE_SGR    = 1 << 13,  /* SGR extended mouse mode */
    GST_MODE_8BIT         = 1 << 14,  /* 8-bit controls */
    GST_MODE_APPKEYPAD    = 1 << 15,  /* Application keypad mode */
    GST_MODE_APPCURSOR    = 1 << 16,  /* Application cursor keys */
    GST_MODE_REVERSE      = 1 << 17,  /* Reverse video mode */
    GST_MODE_KBDLOCK      = 1 << 18,  /* Keyboard locked */
    GST_MODE_HIDE         = 1 << 19,  /* Cursor hidden */
    GST_MODE_FOCUS        = 1 << 20,  /* Focus reporting mode */
    GST_MODE_MOUSE_MANY   = 1 << 21,  /* Highlight mouse tracking */
    GST_MODE_MOUSE_UTF8   = 1 << 22   /* UTF-8 extended mouse mode */
} GstTermMode;

GType gst_term_mode_get_type(void) G_GNUC_CONST;
#define GST_TYPE_TERM_MODE (gst_term_mode_get_type())

/*
 * GstSelectionMode:
 *
 * Selection mode types.
 */
typedef enum {
    GST_SELECTION_IDLE,
    GST_SELECTION_EMPTY,
    GST_SELECTION_READY
} GstSelectionMode;

GType gst_selection_mode_get_type(void) G_GNUC_CONST;
#define GST_TYPE_SELECTION_MODE (gst_selection_mode_get_type())

/*
 * GstSelectionType:
 *
 * Type of selection (character, word, line).
 */
typedef enum {
    GST_SELECTION_TYPE_REGULAR,
    GST_SELECTION_TYPE_RECTANGULAR
} GstSelectionType;

GType gst_selection_type_get_type(void) G_GNUC_CONST;
#define GST_TYPE_SELECTION_TYPE (gst_selection_type_get_type())

/*
 * GstSelectionSnap:
 *
 * Selection snap mode for extending selections.
 */
typedef enum {
    GST_SELECTION_SNAP_NONE,
    GST_SELECTION_SNAP_WORD,
    GST_SELECTION_SNAP_LINE
} GstSelectionSnap;

GType gst_selection_snap_get_type(void) G_GNUC_CONST;
#define GST_TYPE_SELECTION_SNAP (gst_selection_snap_get_type())

/*
 * GstEscapeState:
 *
 * Escape sequence parser state flags (bit field).
 * Multiple flags can be set simultaneously to track
 * the current state of escape sequence parsing.
 */
typedef enum {
    GST_ESC_START      = 1 << 0,   /* ESC received, waiting for command */
    GST_ESC_CSI        = 1 << 1,   /* ESC [ received (CSI) */
    GST_ESC_STR        = 1 << 2,   /* In string (OSC, DCS, APC, PM) */
    GST_ESC_ALTCHARSET = 1 << 3,   /* In charset sequence ESC ( */
    GST_ESC_STR_END    = 1 << 4,   /* String terminator received */
    GST_ESC_TEST       = 1 << 5,   /* In DEC test sequence ESC # */
    GST_ESC_UTF8       = 1 << 6,   /* In UTF-8 mode sequence ESC % */
    GST_ESC_DCS        = 1 << 7    /* Device Control String */
} GstEscapeState;

GType gst_escape_state_get_type(void) G_GNUC_CONST;
#define GST_TYPE_ESCAPE_STATE (gst_escape_state_get_type())

/*
 * GstCharset:
 *
 * Character set designations for G0-G3.
 */
typedef enum {
    GST_CHARSET_GRAPHIC0,    /* DEC Special graphics */
    GST_CHARSET_GRAPHIC1,    /* Alternate character ROM */
    GST_CHARSET_UK,          /* UK character set */
    GST_CHARSET_USA,         /* US ASCII */
    GST_CHARSET_MULTI,       /* Multinational */
    GST_CHARSET_GER,         /* German */
    GST_CHARSET_FIN          /* Finnish */
} GstCharset;

GType gst_charset_get_type(void) G_GNUC_CONST;
#define GST_TYPE_CHARSET (gst_charset_get_type())

/*
 * GstColorIndex:
 *
 * Standard color indices.
 */
typedef enum {
    GST_COLOR_BLACK,
    GST_COLOR_RED,
    GST_COLOR_GREEN,
    GST_COLOR_YELLOW,
    GST_COLOR_BLUE,
    GST_COLOR_MAGENTA,
    GST_COLOR_CYAN,
    GST_COLOR_WHITE,
    /* Bright variants (8-15) */
    GST_COLOR_BRIGHT_BLACK,
    GST_COLOR_BRIGHT_RED,
    GST_COLOR_BRIGHT_GREEN,
    GST_COLOR_BRIGHT_YELLOW,
    GST_COLOR_BRIGHT_BLUE,
    GST_COLOR_BRIGHT_MAGENTA,
    GST_COLOR_BRIGHT_CYAN,
    GST_COLOR_BRIGHT_WHITE,
    /* Extended colors start at 16 */
    GST_COLOR_EXTENDED_START = 16,
    /* 256-color mode: 16-231 are 6x6x6 color cube, 232-255 are grayscale */
    GST_COLOR_DEFAULT_FG = 256,
    GST_COLOR_DEFAULT_BG = 257,
    GST_COLOR_CURSOR_FG = 258,
    GST_COLOR_CURSOR_BG = 259,
    GST_COLOR_REVERSE_FG = 260,
    GST_COLOR_REVERSE_BG = 261,
    GST_COLOR_COUNT = 262
} GstColorIndex;

GType gst_color_index_get_type(void) G_GNUC_CONST;
#define GST_TYPE_COLOR_INDEX (gst_color_index_get_type())

/*
 * GstModuleState:
 *
 * Module lifecycle state.
 */
typedef enum {
    GST_MODULE_STATE_UNLOADED,
    GST_MODULE_STATE_LOADED,
    GST_MODULE_STATE_INITIALIZED,
    GST_MODULE_STATE_ENABLED,
    GST_MODULE_STATE_DISABLED,
    GST_MODULE_STATE_ERROR
} GstModuleState;

GType gst_module_state_get_type(void) G_GNUC_CONST;
#define GST_TYPE_MODULE_STATE (gst_module_state_get_type())

/*
 * GstModulePriority:
 *
 * Module priority for hook ordering.
 * Lower values run first.
 */
typedef enum {
    GST_MODULE_PRIORITY_HIGHEST = -1000,
    GST_MODULE_PRIORITY_HIGH    = -100,
    GST_MODULE_PRIORITY_NORMAL  = 0,
    GST_MODULE_PRIORITY_LOW     = 100,
    GST_MODULE_PRIORITY_LOWEST  = 1000
} GstModulePriority;

GType gst_module_priority_get_type(void) G_GNUC_CONST;
#define GST_TYPE_MODULE_PRIORITY (gst_module_priority_get_type())

/*
 * GstKeyMod:
 *
 * Keyboard modifier flags.
 */
typedef enum {
    GST_KEY_MOD_NONE  = 0,
    GST_KEY_MOD_SHIFT = 1 << 0,
    GST_KEY_MOD_CTRL  = 1 << 1,
    GST_KEY_MOD_ALT   = 1 << 2,
    GST_KEY_MOD_SUPER = 1 << 3,
    GST_KEY_MOD_HYPER = 1 << 4,
    GST_KEY_MOD_META  = 1 << 5
} GstKeyMod;

GType gst_key_mod_get_type(void) G_GNUC_CONST;
#define GST_TYPE_KEY_MOD (gst_key_mod_get_type())

/*
 * GstMouseButton:
 *
 * Mouse button identifiers.
 */
typedef enum {
    GST_MOUSE_BUTTON_LEFT = 1,
    GST_MOUSE_BUTTON_MIDDLE = 2,
    GST_MOUSE_BUTTON_RIGHT = 3,
    GST_MOUSE_BUTTON_SCROLL_UP = 4,
    GST_MOUSE_BUTTON_SCROLL_DOWN = 5,
    GST_MOUSE_BUTTON_SCROLL_LEFT = 6,
    GST_MOUSE_BUTTON_SCROLL_RIGHT = 7,
    GST_MOUSE_BUTTON_8 = 8,
    GST_MOUSE_BUTTON_9 = 9
} GstMouseButton;

GType gst_mouse_button_get_type(void) G_GNUC_CONST;
#define GST_TYPE_MOUSE_BUTTON (gst_mouse_button_get_type())

/*
 * GstWinMode:
 *
 * Window mode flags for rendering state.
 * Tracks visibility, focus, blink phase, and numlock state.
 */
typedef enum {
    GST_WIN_MODE_VISIBLE  = 1 << 0,
    GST_WIN_MODE_FOCUSED  = 1 << 1,
    GST_WIN_MODE_BLINK    = 1 << 2,
    GST_WIN_MODE_NUMLOCK  = 1 << 3
} GstWinMode;

GType gst_win_mode_get_type(void) G_GNUC_CONST;
#define GST_TYPE_WIN_MODE (gst_win_mode_get_type())

/*
 * GstFontStyle:
 *
 * Font style variants for rendering.
 * Used to select regular, italic, bold, or bold+italic fonts.
 */
typedef enum {
    GST_FONT_STYLE_NORMAL,
    GST_FONT_STYLE_ITALIC,
    GST_FONT_STYLE_BOLD,
    GST_FONT_STYLE_BOLD_ITALIC
} GstFontStyle;

GType gst_font_style_get_type(void) G_GNUC_CONST;
#define GST_TYPE_FONT_STYLE (gst_font_style_get_type())

G_END_DECLS

#endif /* GST_ENUMS_H */
