/*
 * gst-module-configs.h - Typed module configuration structs
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Defines a plain-C struct for each built-in module's configuration.
 * These are embedded directly in GstConfig as the `modules` field,
 * giving callers direct struct access:
 *
 *   GstConfig *cfg = gst_config_get_default();
 *   cfg->modules.sixel.enabled     = TRUE;
 *   cfg->modules.sixel.max_width   = 4096;
 *   cfg->modules.scrollback.lines  = 5000;
 *
 * String fields (gchar *) are heap-allocated and owned by GstConfig.
 * Use GST_CONFIG_SET_STRING() for safe assignment:
 *
 *   GST_CONFIG_SET_STRING(cfg->modules.urlclick.opener, "xdg-open");
 *
 * String-array fields (gchar **) are NULL-terminated strv, also owned
 * by GstConfig. Use g_strfreev() + g_strdupv() to replace them.
 */

#ifndef GST_MODULE_CONFIGS_H
#define GST_MODULE_CONFIGS_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * GST_CONFIG_SET_STRING:
 * @field: An lvalue of type gchar *
 * @val: (nullable): The new string value (will be g_strdup'd)
 *
 * Safely replaces a heap-allocated string field in a module config.
 * Frees the old value and stores a copy of @val.
 */
#define GST_CONFIG_SET_STRING(field, val) \
	do { g_free(field); (field) = g_strdup(val); } while (0)

/* ===== Per-module config structs ===== */

/**
 * GstScrollbackConfig:
 * @enabled: whether the scrollback module is active
 * @lines: scrollback ring buffer capacity
 * @mouse_scroll_lines: lines scrolled per mouse wheel tick
 */
typedef struct _GstScrollbackConfig
{
	gboolean enabled;
	gint     lines;
	gint     mouse_scroll_lines;
} GstScrollbackConfig;

/**
 * GstTransparencyConfig:
 * @enabled: whether the transparency module is active
 * @opacity: static opacity value (0.0-1.0)
 * @focus_opacity: opacity when window is focused (0.0-1.0)
 * @unfocus_opacity: opacity when window loses focus (0.0-1.0)
 */
typedef struct _GstTransparencyConfig
{
	gboolean enabled;
	gdouble  opacity;
	gdouble  focus_opacity;
	gdouble  unfocus_opacity;
} GstTransparencyConfig;

/**
 * GstUrlclickConfig:
 * @enabled: whether the URL click module is active
 * @opener: command to open URLs (e.g. "xdg-open")
 * @regex: URL matching regular expression
 * @modifiers: modifier key string required for click (e.g. "Ctrl")
 */
typedef struct _GstUrlclickConfig
{
	gboolean  enabled;
	gchar    *opener;
	gchar    *regex;
	gchar    *modifiers;
} GstUrlclickConfig;

/**
 * GstExternalpipeConfig:
 * @enabled: whether the external pipe module is active
 * @command: shell command to pipe terminal content to
 * @key: key binding string (e.g. "Ctrl+Shift+e")
 */
typedef struct _GstExternalpipeConfig
{
	gboolean  enabled;
	gchar    *command;
	gchar    *key;
} GstExternalpipeConfig;

/**
 * GstBoxdrawConfig:
 * @enabled: whether the box-drawing module is active
 * @bold_offset: pixel offset for bold box-drawing characters
 */
typedef struct _GstBoxdrawConfig
{
	gboolean enabled;
	gint     bold_offset;
} GstBoxdrawConfig;

/**
 * GstVisualbellConfig:
 * @enabled: whether the visual bell module is active
 * @duration: flash duration in milliseconds
 */
typedef struct _GstVisualbellConfig
{
	gboolean enabled;
	gint     duration;
} GstVisualbellConfig;

/**
 * GstUndercurlConfig:
 * @enabled: whether the undercurl rendering module is active
 */
typedef struct _GstUndercurlConfig
{
	gboolean enabled;
} GstUndercurlConfig;

/**
 * GstClipboardConfig:
 * @enabled: whether the clipboard module is active
 */
typedef struct _GstClipboardConfig
{
	gboolean enabled;
} GstClipboardConfig;

/**
 * GstFont2Config:
 * @enabled: whether the secondary font module is active
 * @fonts: NULL-terminated array of fallback font strings
 */
typedef struct _GstFont2Config
{
	gboolean   enabled;
	gchar    **fonts;
} GstFont2Config;

/**
 * GstKeyboardSelectConfig:
 * @enabled: whether the keyboard selection module is active
 * @key: activation key binding string
 * @show_crosshair: show crosshair cursor during selection
 * @highlight_color: hex color for selection highlight
 * @highlight_alpha: opacity for selection highlight (0-255)
 * @search_color: hex color for search matches
 * @search_alpha: opacity for search matches (0-255)
 */
typedef struct _GstKeyboardSelectConfig
{
	gboolean  enabled;
	gchar    *key;
	gboolean  show_crosshair;
	gchar    *highlight_color;
	gint      highlight_alpha;
	gchar    *search_color;
	gint      search_alpha;
} GstKeyboardSelectConfig;

/**
 * GstKittygfxConfig:
 * @enabled: whether the Kitty graphics protocol module is active
 * @max_total_ram_mb: maximum total RAM for all images (MB)
 * @max_single_image_mb: maximum RAM for a single image (MB)
 * @max_placements: maximum number of image placements
 * @allow_file_transfer: allow file:// URI image loading
 * @allow_shm_transfer: allow shared memory image transfer
 */
typedef struct _GstKittygfxConfig
{
	gboolean enabled;
	gint     max_total_ram_mb;
	gint     max_single_image_mb;
	gint     max_placements;
	gboolean allow_file_transfer;
	gboolean allow_shm_transfer;
} GstKittygfxConfig;

/**
 * GstWebviewConfig:
 * @enabled: whether the webview module is active
 * @host: bind address for HTTP server
 * @port: HTTP port number
 * @read_only: if TRUE, web clients cannot send keyboard input
 * @auth: authentication mode ("none", "token", "password")
 * @token: token string when auth is "token"
 * @password: password string when auth is "password"
 * @update_interval: minimum ms between WebSocket screen pushes
 * @max_clients: maximum simultaneous WebSocket connections
 */
typedef struct _GstWebviewConfig
{
	gboolean  enabled;
	gchar    *host;
	gint      port;
	gboolean  read_only;
	gchar    *auth;
	gchar    *token;
	gchar    *password;
	gint      update_interval;
	gint      max_clients;
} GstWebviewConfig;

/**
 * GstMcpToolsConfig:
 *
 * Per-tool enable/disable flags for the MCP module.
 * All default to FALSE for safety.
 */
typedef struct _GstMcpToolsConfig
{
	gboolean read_screen;
	gboolean read_scrollback;
	gboolean search_scrollback;
	gboolean get_cursor_position;
	gboolean get_cell_attributes;
	gboolean get_foreground_process;
	gboolean get_working_directory;
	gboolean is_shell_idle;
	gboolean get_pty_info;
	gboolean list_detected_urls;
	gboolean get_config;
	gboolean list_modules;
	gboolean set_config;
	gboolean toggle_module;
	gboolean get_window_info;
	gboolean set_window_title;
	gboolean send_text;
	gboolean send_keys;
	gboolean screenshot;
	gboolean save_screenshot;
} GstMcpToolsConfig;

/**
 * GstMcpConfig:
 * @enabled: whether the MCP module is active
 * @transport: transport type ("unix-socket", "http", "stdio")
 * @socket_name: custom socket name (NULL for PID-based default)
 * @port: HTTP port (only used with transport "http")
 * @host: HTTP bind address (only used with transport "http")
 * @tools: per-tool enable/disable flags
 */
typedef struct _GstMcpConfig
{
	gboolean          enabled;
	gchar            *transport;
	gchar            *socket_name;
	gint              port;
	gchar            *host;
	GstMcpToolsConfig tools;
} GstMcpConfig;

/**
 * GstNotifyConfig:
 * @enabled: whether the notification module is active
 * @show_title: include window title in notifications
 * @urgency: notification urgency ("low", "normal", "critical")
 * @timeout: notification timeout (-1 for system default, else seconds)
 * @suppress_focused: suppress notifications when window is focused
 */
typedef struct _GstNotifyConfig
{
	gboolean  enabled;
	gboolean  show_title;
	gchar    *urgency;
	gint      timeout;
	gboolean  suppress_focused;
} GstNotifyConfig;

/**
 * GstDynamicColorsConfig:
 * @enabled: whether the dynamic colors module is active
 * @allow_query: respond to OSC 10/11/12 color queries
 * @allow_set: allow OSC 10/11/12 color changes
 */
typedef struct _GstDynamicColorsConfig
{
	gboolean enabled;
	gboolean allow_query;
	gboolean allow_set;
} GstDynamicColorsConfig;

/**
 * GstOsc52Config:
 * @enabled: whether the OSC 52 clipboard module is active
 * @allow_read: allow apps to read clipboard (security risk)
 * @allow_write: allow apps to write clipboard
 * @max_bytes: maximum decoded payload size in bytes
 */
typedef struct _GstOsc52Config
{
	gboolean enabled;
	gboolean allow_read;
	gboolean allow_write;
	gint     max_bytes;
} GstOsc52Config;

/**
 * GstSyncUpdateConfig:
 * @enabled: whether the synchronized update module is active
 * @timeout: maximum ms to wait for sync end marker
 */
typedef struct _GstSyncUpdateConfig
{
	gboolean enabled;
	gint     timeout;
} GstSyncUpdateConfig;

/**
 * GstShellIntegrationConfig:
 * @enabled: whether the shell integration module is active
 * @mark_prompts: render prompt markers in left margin
 * @show_exit_code: red marker for non-zero exit codes
 * @error_color: hex color for error indicators
 */
typedef struct _GstShellIntegrationConfig
{
	gboolean  enabled;
	gboolean  mark_prompts;
	gboolean  show_exit_code;
	gchar    *error_color;
} GstShellIntegrationConfig;

/**
 * GstHyperlinksConfig:
 * @enabled: whether the hyperlinks module is active
 * @opener: command to open hyperlinks
 * @modifier: modifier key string required for click
 * @underline_hover: underline hovered URI spans
 */
typedef struct _GstHyperlinksConfig
{
	gboolean  enabled;
	gchar    *opener;
	gchar    *modifier;
	gboolean  underline_hover;
} GstHyperlinksConfig;

/**
 * GstSearchConfig:
 * @enabled: whether the search module is active
 * @highlight_color: hex color for match highlights
 * @highlight_alpha: opacity for match highlights (0-255)
 * @current_color: hex color for current match
 * @current_alpha: opacity for current match (0-255)
 * @match_case: case-sensitive search
 * @regex: use regex matching
 */
typedef struct _GstSearchConfig
{
	gboolean  enabled;
	gchar    *highlight_color;
	gint      highlight_alpha;
	gchar    *current_color;
	gint      current_alpha;
	gboolean  match_case;
	gboolean  regex;
} GstSearchConfig;

/**
 * GstSixelConfig:
 * @enabled: whether the Sixel graphics module is active
 * @max_width: maximum image width in pixels
 * @max_height: maximum image height in pixels
 * @max_colors: maximum color palette entries
 * @max_total_ram_mb: maximum total RAM for all images (MB)
 * @max_placements: maximum number of image placements
 */
typedef struct _GstSixelConfig
{
	gboolean enabled;
	gint     max_width;
	gint     max_height;
	gint     max_colors;
	gint     max_total_ram_mb;
	gint     max_placements;
} GstSixelConfig;

/**
 * GstLigaturesConfig:
 * @enabled: whether the ligature rendering module is active
 * @features: NULL-terminated array of OpenType feature tags
 * @cache_size: ligature lookup cache size
 */
typedef struct _GstLigaturesConfig
{
	gboolean   enabled;
	gchar    **features;
	gint       cache_size;
} GstLigaturesConfig;

/**
 * GstWallpaperConfig:
 * @enabled: whether the wallpaper module is active
 * @image_path: filesystem path to the background image (PNG or JPEG)
 * @scale_mode: scaling mode string ("fill", "fit", "stretch", "center")
 * @bg_alpha: alpha for default-background cells over the wallpaper
 *            (0.0 = fully transparent to wallpaper, 1.0 = fully opaque)
 */
typedef struct _GstWallpaperConfig
{
	gboolean  enabled;
	gchar    *image_path;
	gchar    *scale_mode;
	gdouble   bg_alpha;
} GstWallpaperConfig;

/* ===== Container struct ===== */

/**
 * GstModuleConfigs:
 *
 * Aggregate struct holding configuration for all built-in modules.
 * Embedded directly in #GstConfig as the `modules` field.
 */
typedef struct _GstModuleConfigs
{
	GstScrollbackConfig       scrollback;
	GstTransparencyConfig     transparency;
	GstUrlclickConfig         urlclick;
	GstExternalpipeConfig     externalpipe;
	GstBoxdrawConfig          boxdraw;
	GstVisualbellConfig       visualbell;
	GstUndercurlConfig        undercurl;
	GstClipboardConfig        clipboard;
	GstFont2Config            font2;
	GstKeyboardSelectConfig   keyboard_select;
	GstKittygfxConfig         kittygfx;
	GstWebviewConfig          webview;
	GstMcpConfig              mcp;
	GstNotifyConfig           notify;
	GstDynamicColorsConfig    dynamic_colors;
	GstOsc52Config            osc52;
	GstSyncUpdateConfig       sync_update;
	GstShellIntegrationConfig shell_integration;
	GstHyperlinksConfig       hyperlinks;
	GstSearchConfig           search;
	GstSixelConfig            sixel;
	GstLigaturesConfig        ligatures;
	GstWallpaperConfig        wallpaper;
} GstModuleConfigs;

G_END_DECLS

#endif /* GST_MODULE_CONFIGS_H */
