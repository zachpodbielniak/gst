/*
 * main.c - GST (GObject Simple Terminal) Entry Point
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Full windowed terminal emulator entry point.
 * Creates a terminal, window, renderer, and PTY,
 * wires all signals together, and runs the GLib main loop.
 *
 * Draw timing uses an adaptive latency model ported from st:
 * coalesce rapid PTY writes into single frames, bounded by
 * minlatency and maxlatency thresholds.
 *
 * Configuration is loaded from YAML files via GstConfig.
 * CLI options override config values where applicable.
 *
 * Supports both X11 and Wayland backends. The backend is
 * auto-detected at runtime (WAYLAND_DISPLAY check), or
 * forced via --x11 / --wayland CLI flags.
 */

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fontconfig/fontconfig.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>

#include "gst.h"
#include "gst-enums.h"
#include "core/gst-terminal.h"
#include "core/gst-pty.h"
#include "rendering/gst-renderer.h"
#include "rendering/gst-font-cache.h"
#include "rendering/gst-x11-renderer.h"
#include "window/gst-window.h"
#include "window/gst-x11-window.h"
#include "selection/gst-selection.h"
#include "config/gst-config.h"
#include "config/gst-keybind.h"
#include "module/gst-module-manager.h"

#ifdef GST_HAVE_WAYLAND
#include "rendering/gst-cairo-font-cache.h"
#include "rendering/gst-wayland-renderer.h"
#include "window/gst-wayland-window.h"
#endif

/* ===== Constants ===== */

/* Keyboard modifier mask for forced mouse reporting */
#define GST_FORCE_MOUSE_MOD (ShiftMask)

/* Mouse reporting mode check */
#define IS_MOUSE_MODE(m) \
	(gst_terminal_has_mode(terminal, GST_MODE_MOUSE_X10) || \
	 gst_terminal_has_mode(terminal, GST_MODE_MOUSE_BTN) || \
	 gst_terminal_has_mode(terminal, GST_MODE_MOUSE_MOTION) || \
	 gst_terminal_has_mode(terminal, GST_MODE_MOUSE_MANY))

/* ===== Command line options ===== */

static gchar *opt_config = NULL;
static gchar *opt_title = NULL;
static gchar *opt_geometry = NULL;
static gchar *opt_font = NULL;
static gchar *opt_name = NULL;
static gchar *opt_windowid = NULL;
static gchar *opt_execute = NULL;
static gboolean opt_line = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_license = FALSE;
static gboolean opt_x11 = FALSE;
static gboolean opt_wayland = FALSE;

static GOptionEntry entries[] = {
	{ "config", 'c', 0, G_OPTION_ARG_FILENAME, &opt_config,
	  "Use specified config file", "PATH" },
	{ "title", 't', 0, G_OPTION_ARG_STRING, &opt_title,
	  "Window title", "TITLE" },
	{ "geometry", 'g', 0, G_OPTION_ARG_STRING, &opt_geometry,
	  "Window geometry (COLSxROWS)", "GEOMETRY" },
	{ "font", 'f', 0, G_OPTION_ARG_STRING, &opt_font,
	  "Font specification", "FONT" },
	{ "name", 'n', 0, G_OPTION_ARG_STRING, &opt_name,
	  "Window name", "NAME" },
	{ "windid", 'w', 0, G_OPTION_ARG_STRING, &opt_windowid,
	  "Embed in window ID", "ID" },
	{ "exec", 'e', 0, G_OPTION_ARG_STRING, &opt_execute,
	  "Execute command instead of shell", "CMD" },
	{ "line", 'l', 0, G_OPTION_ARG_NONE, &opt_line,
	  "Read from stdin", NULL },
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version,
	  "Show version", NULL },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license,
	  "Show license (AGPLv3)", NULL },
	{ "x11", 0, 0, G_OPTION_ARG_NONE, &opt_x11,
	  "Force X11 backend", NULL },
	{ "wayland", 0, 0, G_OPTION_ARG_NONE, &opt_wayland,
	  "Force Wayland backend", NULL },
	{ NULL }
};

static const gchar *license_text =
	"GST - GObject Simple Terminal\n"
	"Copyright (C) 2026 Zach Podbielniak\n"
	"\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n"
	"\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n"
	"\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* ===== Application state ===== */

static GMainLoop *main_loop = NULL;
static GstTerminal *terminal = NULL;
static GstSelection *selection = NULL;
static GstPty *pty = NULL;
static GstWindow *window = NULL;
static GstRenderer *renderer = NULL;
static GstBackendType backend = GST_BACKEND_X11;

/* X11-specific state (only valid when backend == GST_BACKEND_X11) */
static GstFontCache *font_cache = NULL;

#ifdef GST_HAVE_WAYLAND
/* Wayland-specific state (only valid when backend == GST_BACKEND_WAYLAND) */
static GstCairoFontCache *cairo_font_cache = NULL;
#endif

/* Window mode flags (shared between backends) */
static GstWinMode win_mode = GST_WIN_MODE_NUMLOCK;

/* Draw timing state */
static guint draw_timeout_id = 0;
static gint64 draw_trigger_time = 0;
static gboolean drawing = FALSE;

/* Runtime config values (populated from GstConfig on startup) */
static guint cfg_border_px = 2;
static guint cfg_min_latency = 8;
static guint cfg_max_latency = 33;

/* Character cell dimensions (set during font loading) */
static gint cell_w = 0;
static gint cell_h = 0;

/* Mouse reporting deduplication: last reported cell coordinates */
static gint last_mouse_col = -1;
static gint last_mouse_row = -1;

/* ===== Helper functions ===== */

static gboolean
parse_geometry(
	const gchar *geometry,
	gint        *cols,
	gint        *rows
){
	gint c;
	gint r;
	gchar x;

	if (geometry == NULL) {
		return FALSE;
	}

	if (sscanf(geometry, "%d%c%d", &c, &x, &r) != 3) {
		return FALSE;
	}

	if (x != 'x' && x != 'X') {
		return FALSE;
	}

	if (c < 1 || c > GST_MAX_COLS || r < 1 || r > GST_MAX_ROWS) {
		return FALSE;
	}

	*cols = c;
	*rows = r;
	return TRUE;
}

/*
 * find_default_config:
 *
 * Search the XDG config path and system paths for config.yaml.
 * Returns the first path that exists, or NULL if none found.
 *
 * Search order:
 *  1. $XDG_CONFIG_HOME/gst/config.yaml (~/.config/gst/config.yaml)
 *  2. /etc/gst/config.yaml
 *  3. /usr/share/gst/config.yaml
 */
static gchar *
find_default_config(void)
{
	const gchar *xdg_config;
	g_autofree gchar *user_path = NULL;
	g_autofree gchar *sysconfdir_path = NULL;
	g_autofree gchar *datadir_path = NULL;

	/* XDG user config (~/.config/gst/config.yaml) */
	xdg_config = g_get_user_config_dir();
	if (xdg_config != NULL) {
		user_path = g_build_filename(xdg_config, "gst",
			"config.yaml", NULL);
		if (g_file_test(user_path, G_FILE_TEST_IS_REGULAR)) {
			return g_steal_pointer(&user_path);
		}
	}

	/* System config (compile-time SYSCONFDIR, e.g. /etc) */
	sysconfdir_path = g_build_filename(
		GST_SYSCONFDIR, "gst", "config.yaml", NULL);
	if (g_file_test(sysconfdir_path, G_FILE_TEST_IS_REGULAR)) {
		return g_steal_pointer(&sysconfdir_path);
	}

	/* Data directory (compile-time DATADIR, e.g. /usr/local/share) */
	datadir_path = g_build_filename(
		GST_DATADIR, "gst", "config.yaml", NULL);
	if (g_file_test(datadir_path, G_FILE_TEST_IS_REGULAR)) {
		return g_steal_pointer(&datadir_path);
	}

	/* Development fallback: data/ in source tree (cwd) */
	if (g_file_test("data/config.yaml", G_FILE_TEST_IS_REGULAR)) {
		return g_strdup("data/config.yaml");
	}

	return NULL;
}

/*
 * Convert pixel coordinates to terminal column/row,
 * accounting for border padding.
 */
static gint
pixel_to_col(gint px)
{
	gint cols;
	gint x;

	cols = gst_terminal_get_cols(terminal);
	x = (px - (gint)cfg_border_px) / cell_w;
	if (x < 0) x = 0;
	if (x >= cols) x = cols - 1;
	return x;
}

static gint
pixel_to_row(gint py)
{
	gint rows;
	gint y;

	rows = gst_terminal_get_rows(terminal);
	y = (py - (gint)cfg_border_px) / cell_h;
	if (y < 0) y = 0;
	if (y >= rows) y = rows - 1;
	return y;
}

/*
 * mouse_report:
 * @button: button code (0=left, 1=mid, 2=right, 3=release, 64=scrollup, 65=scrolldown)
 * @col: terminal column (0-based)
 * @row: terminal row (0-based)
 * @release: TRUE if this is a button release event
 * @motion: TRUE if this is a motion event
 * @state: keyboard modifier mask
 *
 * Encodes a mouse event as an escape sequence and writes
 * it to the PTY. Supports X10 classic encoding and SGR
 * extended encoding formats.
 */
static void
mouse_report(
	gint        button,
	gint        col,
	gint        row,
	gboolean    release,
	gboolean    motion,
	guint       state
){
	gchar buf[64];
	gint len;
	gint cb;

	/* Build the button code with modifier bits */
	cb = button;

	/* Motion events add 32 to the button code */
	if (motion) {
		cb += 32;
	}

	/* Encode modifier keys into the button code */
	if (state & ShiftMask)   cb += 4;
	if (state & Mod1Mask)    cb += 8;
	if (state & ControlMask) cb += 16;

	if (gst_terminal_has_mode(terminal, GST_MODE_MOUSE_SGR)) {
		/*
		 * SGR extended mode: ESC [ < Cb ; Cx ; Cy M/m
		 * Coordinates are 1-based decimal, no offset.
		 * 'M' for press/motion, 'm' for release.
		 */
		len = g_snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
			cb, col + 1, row + 1, release ? 'm' : 'M');
	} else {
		/*
		 * Classic X10/normal mode: ESC [ M Cb Cx Cy
		 * Cb has +32 offset, coordinates have +33 offset.
		 * Coordinates are clamped to 223 (255 - 32) max.
		 */
		if (col > 222) col = 222;
		if (row > 222) row = 222;
		buf[0] = '\033';
		buf[1] = '[';
		buf[2] = 'M';
		buf[3] = (gchar)(32 + cb);
		buf[4] = (gchar)(33 + col);
		buf[5] = (gchar)(33 + row);
		len = 6;
	}

	gst_pty_write(pty, buf, (gssize)len);
}

/*
 * set_win_mode:
 * @mode: new window mode flags
 *
 * Sets the window mode flags on both the local state and
 * the backend-specific renderer.
 */
static void
set_win_mode(GstWinMode mode)
{
	win_mode = mode;

	if (backend == GST_BACKEND_X11) {
		gst_x11_renderer_set_win_mode(
			GST_X11_RENDERER(renderer), mode);
	}
#ifdef GST_HAVE_WAYLAND
	else if (backend == GST_BACKEND_WAYLAND) {
		gst_wayland_renderer_set_win_mode(
			GST_WAYLAND_RENDERER(renderer), mode);
	}
#endif
}

/*
 * detect_backend:
 *
 * Auto-detect the display backend based on environment.
 * CLI flags --x11 / --wayland override auto-detection.
 *
 * Returns: the detected GstBackendType
 */
static GstBackendType
detect_backend(void)
{
#ifdef GST_HAVE_WAYLAND
	if (opt_wayland) {
		return GST_BACKEND_WAYLAND;
	}
#endif
	if (opt_x11) {
		return GST_BACKEND_X11;
	}
#ifdef GST_HAVE_WAYLAND
	if (g_getenv("WAYLAND_DISPLAY") != NULL) {
		return GST_BACKEND_WAYLAND;
	}
#endif
	return GST_BACKEND_X11;
}

/* ===== Draw scheduling ===== */

/*
 * schedule_draw:
 *
 * Called when there is new content to render.
 * Implements adaptive latency: waits up to minlatency
 * for more data, draws immediately after maxlatency.
 */
static gboolean
do_draw(gpointer user_data)
{
	draw_timeout_id = 0;
	drawing = FALSE;

	if (!(win_mode & GST_WIN_MODE_VISIBLE)) {
		return G_SOURCE_REMOVE;
	}

	if (!gst_renderer_start_draw(renderer)) {
		return G_SOURCE_REMOVE;
	}

	gst_renderer_render(renderer);
	gst_renderer_finish_draw(renderer);

	return G_SOURCE_REMOVE;
}

static void
schedule_draw(void)
{
	gint64 now;
	gint64 elapsed;
	guint delay;

	now = g_get_monotonic_time();

	if (!drawing) {
		draw_trigger_time = now;
		drawing = TRUE;
	}

	elapsed = (now - draw_trigger_time) / 1000; /* to ms */

	if (elapsed >= (gint64)cfg_max_latency) {
		/* Max latency exceeded: draw immediately */
		if (draw_timeout_id != 0) {
			g_source_remove(draw_timeout_id);
			draw_timeout_id = 0;
		}
		do_draw(NULL);
		return;
	}

	/* Schedule draw at minlatency, adapting to remaining budget */
	delay = (guint)((gint64)cfg_max_latency - elapsed);
	if (delay > cfg_min_latency) {
		delay = cfg_min_latency;
	}

	if (draw_timeout_id == 0) {
		draw_timeout_id = g_timeout_add(delay, do_draw, NULL);
	}
}

/*
 * zoom:
 * @action: GST_ACTION_ZOOM_IN, GST_ACTION_ZOOM_OUT, or GST_ACTION_ZOOM_RESET
 *
 * Adjusts the font size by +/- 1px (or resets to default),
 * reloads the font cache, updates cell dimensions, and
 * triggers a window resize to maintain the same terminal
 * columns and rows at the new cell size.
 */
static void
zoom(GstAction action)
{
	gdouble cur_size;
	gdouble def_size;
	gdouble new_size;
	const gchar *fontstr;
	gint cols;
	gint rows;
	gint new_w;
	gint new_h;

	/* Get current and default font sizes from the active font cache */
	if (backend == GST_BACKEND_X11) {
		cur_size = gst_font_cache_get_font_size(font_cache);
		def_size = gst_font_cache_get_default_font_size(font_cache);
		fontstr = gst_font_cache_get_used_font(font_cache);
	}
#ifdef GST_HAVE_WAYLAND
	else if (backend == GST_BACKEND_WAYLAND) {
		cur_size = gst_cairo_font_cache_get_font_size(cairo_font_cache);
		def_size = gst_cairo_font_cache_get_default_font_size(cairo_font_cache);
		fontstr = gst_cairo_font_cache_get_used_font(cairo_font_cache);
	}
#endif
	else {
		return;
	}

	/* Calculate the new font size */
	switch (action) {
	case GST_ACTION_ZOOM_IN:
		new_size = cur_size + 1.0;
		break;
	case GST_ACTION_ZOOM_OUT:
		new_size = cur_size - 1.0;
		if (new_size < 1.0) new_size = 1.0;
		break;
	case GST_ACTION_ZOOM_RESET:
		new_size = def_size;
		break;
	default:
		return;
	}

	/* No change needed */
	if (new_size == cur_size) {
		return;
	}

	/* Reload fonts at the new size */
	if (backend == GST_BACKEND_X11) {
		GstX11Window *x11_win;
		Display *display;
		gint screen;

		x11_win = GST_X11_WINDOW(window);
		display = gst_x11_window_get_display(x11_win);
		screen = gst_x11_window_get_screen(x11_win);

		gst_font_cache_unload_fonts(font_cache);
		if (!gst_font_cache_load_fonts(font_cache, display, screen,
			fontstr, new_size))
		{
			/* Reload at old size as fallback */
			gst_font_cache_load_fonts(font_cache, display, screen,
				fontstr, cur_size);
			return;
		}

		cell_w = gst_font_cache_get_char_width(font_cache);
		cell_h = gst_font_cache_get_char_height(font_cache);
	}
#ifdef GST_HAVE_WAYLAND
	else if (backend == GST_BACKEND_WAYLAND) {
		gst_cairo_font_cache_unload_fonts(cairo_font_cache);
		if (!gst_cairo_font_cache_load_fonts(cairo_font_cache,
			fontstr, new_size))
		{
			/* Reload at old size as fallback */
			gst_cairo_font_cache_load_fonts(cairo_font_cache,
				fontstr, cur_size);
			return;
		}

		cell_w = gst_cairo_font_cache_get_char_width(cairo_font_cache);
		cell_h = gst_cairo_font_cache_get_char_height(cairo_font_cache);
	}
#endif

	/*
	 * Reload spare fonts: zoom clears the ring cache when unloading
	 * fonts, so spare fonts from the font2 module need reloading.
	 */
	{
		GstConfig *zoom_cfg;
		const gchar *const *fallbacks;

		zoom_cfg = gst_config_get_default();
		fallbacks = gst_config_get_font_fallbacks(zoom_cfg);
		if (fallbacks != NULL && fallbacks[0] != NULL)
		{
			if (backend == GST_BACKEND_X11) {
				gst_font_cache_load_spare_fonts(font_cache,
					fallbacks);
			}
#ifdef GST_HAVE_WAYLAND
			else if (backend == GST_BACKEND_WAYLAND) {
				gst_cairo_font_cache_load_spare_fonts(
					cairo_font_cache, fallbacks);
			}
#endif
		}
	}

	/* Resize window to maintain same cols/rows */
	cols = gst_terminal_get_cols(terminal);
	rows = gst_terminal_get_rows(terminal);
	new_w = cols * cell_w + 2 * (gint)cfg_border_px;
	new_h = rows * cell_h + 2 * (gint)cfg_border_px;

	/* Update WM size hints and request resize */
	gst_window_set_wm_hints(window, cell_w, cell_h, (gint)cfg_border_px);
	gst_window_resize(window, (guint)new_w, (guint)new_h);
}

/* ===== Signal handlers ===== */

/*
 * PTY data-received: feed through terminal emulator and schedule redraw.
 */
static void
on_pty_data_received(
	GstPty      *pty_obj,
	gpointer    data,
	gulong      len,
	gpointer    user_data
){
	gst_terminal_write(terminal, (const gchar *)data, (gssize)len);
	schedule_draw();
}

/*
 * Terminal response: forward DA/DSR/cursor report to child via PTY.
 */
static void
on_terminal_response(
	GstTerminal *term,
	const gchar *data,
	glong       len,
	gpointer    user_data
){
	gst_pty_write_no_echo(pty, data, (gssize)len);
}

/*
 * Terminal title-changed: update window title.
 */
static void
on_terminal_title_changed(
	GstTerminal *term,
	const gchar *title,
	gpointer    user_data
){
	gst_window_set_title(window, title);
}

/*
 * Terminal bell: dispatch to modules, then trigger bell.
 */
static void
on_terminal_bell(
	GstTerminal *term,
	gpointer    user_data
){
	GstModuleManager *mgr;

	mgr = gst_module_manager_get_default();
	gst_module_manager_dispatch_bell(mgr);

	/* Default bell behavior */
	gst_window_bell(window);
}

/*
 * Terminal escape string: dispatch to modules for APC/DCS/PM handling.
 * Modules like kittygfx intercept APC sequences here.
 */
static void
on_terminal_escape_string(
	GstTerminal *term,
	gchar        str_type,
	const gchar *buf,
	gulong       len,
	gpointer     user_data
){
	GstModuleManager *mgr;

	(void)user_data;

	mgr = gst_module_manager_get_default();
	gst_module_manager_dispatch_escape_string(mgr, str_type, buf,
		(gsize)len, (gpointer)term);
}

/*
 * Child process exited: quit main loop.
 */
static void
on_child_exited(
	GstPty      *pty_obj,
	gint        status,
	gpointer    user_data
){
	if (main_loop != NULL) {
		g_main_loop_quit(main_loop);
	}
}

/*
 * Window key-press: let modules intercept, check shortcuts, then forward to PTY.
 */
static void
on_key_press(
	GstWindow   *win,
	guint       keysym,
	guint       state,
	const gchar *text,
	gint        len,
	gpointer    user_data
){
	GstModuleManager *mgr;
	GstConfig *config;
	GstAction action;
	gchar buf[64];
	gint out_len;

	/* Let modules intercept key events before built-in shortcuts */
	mgr = gst_module_manager_get_default();
	if (gst_module_manager_dispatch_key_event(mgr, keysym, 0, state))
	{
		return;
	}

	/* Look up configured keybind action */
	config = gst_config_get_default();
	action = gst_config_lookup_key_action(config, keysym, state);

	switch (action) {
	case GST_ACTION_CLIPBOARD_COPY:
		{
			gchar *sel_text;

			sel_text = gst_selection_get_text(selection);
			if (sel_text != NULL) {
				gst_window_set_selection(window, sel_text, FALSE);
				gst_window_copy_to_clipboard(window);
				g_free(sel_text);
			}
		}
		return;
	case GST_ACTION_CLIPBOARD_PASTE:
		gst_window_paste_clipboard(window);
		return;
	case GST_ACTION_PASTE_PRIMARY:
		gst_window_paste_primary(window);
		return;
	case GST_ACTION_ZOOM_IN:
	case GST_ACTION_ZOOM_OUT:
	case GST_ACTION_ZOOM_RESET:
		zoom(action);
		return;
	default:
		break;
	}

	/*
	 * Key mapping table takes priority over XLookupString text.
	 * Keys like Backspace, Return, Tab, arrows, F-keys all have
	 * canonical escape sequences that must be sent regardless of
	 * what XLookupString returns (e.g., BS vs DEL for backspace).
	 */
	{
		gint esc_len;

		esc_len = gst_terminal_key_to_escape(
			terminal, keysym, state, buf, sizeof(buf));
		if (esc_len > 0) {
			gst_pty_write(pty, buf, (gssize)esc_len);
			return;
		}
	}

	/* Forward text to PTY (printable characters not in key table) */
	if (len > 0 && text != NULL) {
		out_len = len;

		/* Alt+key: send ESC prefix */
		if (len == 1 && (state & Mod1Mask)) {
			buf[0] = '\033';
			buf[1] = text[0];
			gst_pty_write(pty, buf, 2);
			return;
		}

		gst_pty_write(pty, text, (gssize)out_len);
	}
}

/*
 * Window button-press: handle selection or mouse reporting.
 */
static void
on_button_press(
	GstWindow   *win,
	guint       button,
	guint       state,
	gint        px,
	gint        py,
	gulong      time,
	gpointer    user_data
){
	gint col;
	gint row;

	col = pixel_to_col(px);
	row = pixel_to_row(py);

	/* If mouse reporting is enabled, send to app */
	if (IS_MOUSE_MODE(terminal) && !(state & GST_FORCE_MOUSE_MOD)) {
		gint btn;

		/* Map X11 button numbers to protocol button codes */
		switch (button) {
		case Button1: btn = 0;  break;
		case Button2: btn = 1;  break;
		case Button3: btn = 2;  break;
		case Button4: btn = 64; break; /* scroll up */
		case Button5: btn = 65; break; /* scroll down */
		default:      return;
		}

		/* Reset motion dedup on press */
		last_mouse_col = -1;
		last_mouse_row = -1;

		mouse_report(btn, col, row, FALSE, FALSE, state);
		return;
	}

	/* Left button starts selection */
	if (button == Button1) {
		gst_selection_start(selection, col, row, GST_SELECTION_SNAP_NONE);
		schedule_draw();
	}

	/* Middle button pastes primary */
	if (button == Button2) {
		gst_window_paste_primary(window);
	}
}

/*
 * Window button-release: finalize selection.
 */
static void
on_button_release(
	GstWindow   *win,
	guint       button,
	guint       state,
	gint        px,
	gint        py,
	gulong      time,
	gpointer    user_data
){
	gint col;
	gint row;
	gchar *sel_text;

	if (IS_MOUSE_MODE(terminal) && !(state & GST_FORCE_MOUSE_MOD)) {
		gint btn;

		col = pixel_to_col(px);
		row = pixel_to_row(py);

		/* X10 mode does not report release events */
		if (gst_terminal_has_mode(terminal, GST_MODE_MOUSE_X10)) {
			return;
		}

		/* Map X11 button numbers to protocol button codes */
		switch (button) {
		case Button1: btn = 0; break;
		case Button2: btn = 1; break;
		case Button3: btn = 2; break;
		default:      return;
		}

		/* Reset motion dedup on release */
		last_mouse_col = -1;
		last_mouse_row = -1;

		/*
		 * SGR mode preserves the actual button in release.
		 * Classic mode sends button=3 (generic release).
		 */
		if (!gst_terminal_has_mode(terminal, GST_MODE_MOUSE_SGR)) {
			btn = 3;
		}

		mouse_report(btn, col, row, TRUE, FALSE, state);
		return;
	}

	col = pixel_to_col(px);
	row = pixel_to_row(py);

	if (button == Button1) {
		gst_selection_extend(selection, col, row,
			GST_SELECTION_TYPE_REGULAR, TRUE);

		/* Set primary selection */
		sel_text = gst_selection_get_text(selection);
		if (sel_text != NULL) {
			gst_window_set_selection(window, sel_text, FALSE);
			g_free(sel_text);
		}
		schedule_draw();
	}
}

/*
 * Window motion-notify: extend selection.
 */
static void
on_motion_notify(
	GstWindow   *win,
	guint       state,
	gint        px,
	gint        py,
	gpointer    user_data
){
	gint col;
	gint row;

	if (IS_MOUSE_MODE(terminal) && !(state & GST_FORCE_MOUSE_MOD)) {
		gint btn;

		col = pixel_to_col(px);
		row = pixel_to_row(py);

		/* Deduplicate: skip if same cell as last report */
		if (col == last_mouse_col && row == last_mouse_row) {
			return;
		}
		last_mouse_col = col;
		last_mouse_row = row;

		/*
		 * GST_MODE_MOUSE_MANY (1003): report all motion.
		 * GST_MODE_MOUSE_BTN (1002) / GST_MODE_MOUSE_MOTION:
		 *   only report motion while a button is held.
		 */
		if (!gst_terminal_has_mode(terminal, GST_MODE_MOUSE_MANY)) {
			if (!(state & (Button1Mask | Button2Mask | Button3Mask))) {
				return;
			}
		}

		/* Determine held button for the motion report */
		if (state & Button1Mask)      btn = 0;
		else if (state & Button2Mask) btn = 1;
		else if (state & Button3Mask) btn = 2;
		else                          btn = 0;

		mouse_report(btn, col, row, FALSE, TRUE, state);
		return;
	}

	col = pixel_to_col(px);
	row = pixel_to_row(py);

	gst_selection_extend(selection, col, row,
		GST_SELECTION_TYPE_REGULAR, FALSE);
	schedule_draw();
}

/*
 * Window focus-change: update renderer mode, send focus escapes.
 */
static void
on_focus_change(
	GstWindow   *win,
	gboolean    focused,
	gpointer    user_data
){
	GstWinMode wm;

	wm = win_mode;
	if (focused) {
		wm |= GST_WIN_MODE_FOCUSED;
	} else {
		wm &= ~GST_WIN_MODE_FOCUSED;
	}
	set_win_mode(wm);

	/* Send focus escape sequences if enabled */
	if (gst_terminal_has_mode(terminal, GST_MODE_FOCUS)) {
		if (focused) {
			gst_pty_write(pty, "\033[I", 3);
		} else {
			gst_pty_write(pty, "\033[O", 3);
		}
	}

	schedule_draw();
}

/*
 * Window configure: resize terminal, renderer, and PTY.
 */
static void
on_configure(
	GstWindow   *win,
	guint       width,
	guint       height,
	gpointer    user_data
){
	gint cols;
	gint rows;

	cols = ((gint)width - 2 * (gint)cfg_border_px) / cell_w;
	rows = ((gint)height - 2 * (gint)cfg_border_px) / cell_h;

	if (cols < 1) cols = 1;
	if (rows < 1) rows = 1;

	gst_terminal_resize(terminal, cols, rows);
	gst_renderer_resize(renderer, width, height);
	gst_pty_resize(pty, cols, rows);

	schedule_draw();
}

/*
 * Window expose: mark all lines dirty and redraw.
 */
static void
on_expose(
	GstWindow   *win,
	gpointer    user_data
){
	gint rows;
	gint y;

	rows = gst_terminal_get_rows(terminal);
	for (y = 0; y < rows; y++) {
		gst_terminal_mark_dirty(terminal, y);
	}

	schedule_draw();
}

/*
 * Window visibility: update renderer win_mode.
 */
static void
on_visibility(
	GstWindow   *win,
	gboolean    visible,
	gpointer    user_data
){
	GstWinMode wm;

	wm = win_mode;
	if (visible) {
		wm |= GST_WIN_MODE_VISIBLE;
	} else {
		wm &= ~GST_WIN_MODE_VISIBLE;
	}
	set_win_mode(wm);
}

/*
 * Window close-request: quit main loop.
 */
static void
on_close_request(
	GstWindow   *win,
	gpointer    user_data
){
	if (main_loop != NULL) {
		g_main_loop_quit(main_loop);
	}
}

/*
 * Window selection-notify: paste data to PTY.
 */
static void
on_selection_notify(
	GstWindow   *win,
	const gchar *data,
	gint        len,
	gpointer    user_data
){
	if (data == NULL || len <= 0) {
		return;
	}

	/* Wrap in bracketed paste if enabled */
	if (gst_terminal_has_mode(terminal, GST_MODE_BRCKTPASTE)) {
		gst_pty_write(pty, "\033[200~", 6);
	}

	gst_pty_write(pty, data, (gssize)len);

	if (gst_terminal_has_mode(terminal, GST_MODE_BRCKTPASTE)) {
		gst_pty_write(pty, "\033[201~", 6);
	}
}

/*
 * SIGTERM/SIGINT handler: clean shutdown.
 */
static gboolean
on_sigterm(gpointer user_data)
{
	if (main_loop != NULL) {
		g_main_loop_quit(main_loop);
	}

	return G_SOURCE_REMOVE;
}

/* ===== Backend initialization ===== */

/*
 * init_x11_backend:
 * @cols: terminal columns
 * @rows: terminal rows
 * @fontstr: font specification
 * @config: configuration object
 *
 * Initializes X11 backend: loads fonts, creates window and renderer.
 *
 * Returns: TRUE on success
 */
static gboolean
init_x11_backend(
	gint            cols,
	gint            rows,
	const gchar     *fontstr,
	GstConfig       *config
){
	gulong embed_id;
	Display *display;
	Window xid;
	Visual *visual;
	Colormap colormap;
	gint screen;
	GstX11Window *x11_win;
	GstX11Renderer *x11_renderer;

	/* Initialize fontconfig */
	if (!FcInit()) {
		g_printerr("Could not initialize fontconfig\n");
		return FALSE;
	}

	font_cache = gst_font_cache_new();

	/* Bootstrap: open temp display for font metrics */
	{
		Display *tmp_dpy;
		gint tmp_screen;

		tmp_dpy = XOpenDisplay(NULL);
		if (tmp_dpy == NULL) {
			g_printerr("Cannot open X11 display\n");
			g_object_unref(font_cache);
			font_cache = NULL;
			return FALSE;
		}
		tmp_screen = XDefaultScreen(tmp_dpy);

		if (!gst_font_cache_load_fonts(font_cache, tmp_dpy,
			tmp_screen, fontstr, 0))
		{
			g_printerr("Cannot load font: %s\n", fontstr);
			XCloseDisplay(tmp_dpy);
			g_object_unref(font_cache);
			font_cache = NULL;
			return FALSE;
		}

		cell_w = gst_font_cache_get_char_width(font_cache);
		cell_h = gst_font_cache_get_char_height(font_cache);

		gst_font_cache_unload_fonts(font_cache);
		XCloseDisplay(tmp_dpy);
	}

	/* Create X11 window */
	embed_id = 0;
	if (opt_windowid != NULL) {
		embed_id = strtoul(opt_windowid, NULL, 0);
	}

	x11_win = gst_x11_window_new(cols, rows, cell_w, cell_h,
		(gint)cfg_border_px, embed_id);
	if (x11_win == NULL) {
		g_printerr("Cannot create X11 window\n");
		g_object_unref(font_cache);
		font_cache = NULL;
		return FALSE;
	}
	window = GST_WINDOW(x11_win);

	/* Reload fonts on window's display */
	display = gst_x11_window_get_display(x11_win);
	screen = gst_x11_window_get_screen(x11_win);

	if (!gst_font_cache_load_fonts(font_cache, display, screen, fontstr, 0)) {
		g_printerr("Cannot load font on window display: %s\n", fontstr);
		g_object_unref(window);
		window = NULL;
		g_object_unref(font_cache);
		font_cache = NULL;
		return FALSE;
	}

	cell_w = gst_font_cache_get_char_width(font_cache);
	cell_h = gst_font_cache_get_char_height(font_cache);

	/* Show window and set WM hints */
	gst_window_set_wm_hints(window, cell_w, cell_h, (gint)cfg_border_px);
	gst_window_show(window);

	/* Create X11 renderer */
	xid = gst_x11_window_get_xid(x11_win);
	visual = gst_x11_window_get_visual(x11_win);
	colormap = gst_x11_window_get_colormap(x11_win);

	x11_renderer = gst_x11_renderer_new(
		terminal, display, xid, visual, colormap,
		screen, font_cache, (gint)cfg_border_px);
	renderer = GST_RENDERER(x11_renderer);

	/* Load colors from config */
	if (!gst_x11_renderer_load_colors(x11_renderer, config)) {
		g_printerr("Cannot load colors\n");
	}

	/* Set initial win_mode */
	set_win_mode(GST_WIN_MODE_VISIBLE | GST_WIN_MODE_FOCUSED
		| GST_WIN_MODE_NUMLOCK);

	return TRUE;
}

#ifdef GST_HAVE_WAYLAND
/*
 * init_wayland_backend:
 * @cols: terminal columns
 * @rows: terminal rows
 * @fontstr: font specification
 * @config: configuration object
 *
 * Initializes Wayland backend: loads fonts via Cairo,
 * creates Wayland window and renderer.
 *
 * Returns: TRUE on success
 */
static gboolean
init_wayland_backend(
	gint            cols,
	gint            rows,
	const gchar     *fontstr,
	GstConfig       *config
){
	GstWaylandWindow *wl_win;
	GstWaylandRenderer *wl_renderer;

	/* Initialize fontconfig */
	if (!FcInit()) {
		g_printerr("Could not initialize fontconfig\n");
		return FALSE;
	}

	/* Load fonts via Cairo font cache (no Display needed) */
	cairo_font_cache = gst_cairo_font_cache_new();

	if (!gst_cairo_font_cache_load_fonts(cairo_font_cache, fontstr, 0)) {
		g_printerr("Cannot load font: %s\n", fontstr);
		g_object_unref(cairo_font_cache);
		cairo_font_cache = NULL;
		return FALSE;
	}

	cell_w = gst_cairo_font_cache_get_char_width(cairo_font_cache);
	cell_h = gst_cairo_font_cache_get_char_height(cairo_font_cache);

	/* Create Wayland window */
	wl_win = gst_wayland_window_new(cols, rows, cell_w, cell_h,
		(gint)cfg_border_px);
	if (wl_win == NULL) {
		g_printerr("Cannot create Wayland window\n");
		g_object_unref(cairo_font_cache);
		cairo_font_cache = NULL;
		return FALSE;
	}
	window = GST_WINDOW(wl_win);

	/* Show window and set WM hints */
	gst_window_set_wm_hints(window, cell_w, cell_h, (gint)cfg_border_px);
	gst_window_show(window);

	/* Create Wayland renderer (extracts display/surface/shm internally) */
	wl_renderer = gst_wayland_renderer_new(
		terminal, wl_win,
		cairo_font_cache, (gint)cfg_border_px);
	renderer = GST_RENDERER(wl_renderer);

	/* Load colors from config */
	if (!gst_wayland_renderer_load_colors(wl_renderer, config)) {
		g_printerr("Cannot load colors\n");
	}

	/* Set initial win_mode */
	set_win_mode(GST_WIN_MODE_VISIBLE | GST_WIN_MODE_FOCUSED
		| GST_WIN_MODE_NUMLOCK);

	return TRUE;
}
#endif /* GST_HAVE_WAYLAND */

/* ===== Main ===== */

int
main(
	int     argc,
	char    **argv
){
	GOptionContext *context;
	GError *error = NULL;
	GstConfig *config;
	gint cols;
	gint rows;
	const gchar *fontstr;
	const gchar *shell_cmd;

	/* Set locale for proper UTF-8 handling */
	setlocale(LC_ALL, "");

	/* Parse command line options */
	context = g_option_context_new("[-e command [args]]");
	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_set_summary(context,
		"GST - GObject Simple Terminal\n"
		"A GObject-based terminal emulator with modular extensibility.");
	g_option_context_set_description(context,
		"GST is a lightweight terminal emulator based on st (suckless terminal)\n"
		"reimplemented using GObject for clean architecture and modular plugins.\n"
		"\n"
		"Configuration files are searched in this order:\n"
		"  1. --config PATH (command line override)\n"
		"  2. ~/.config/gst/config.yaml\n"
		"  3. /etc/gst/config.yaml\n"
		"  4. /usr/share/gst/config.yaml\n"
		"\n"
		"Modules are loaded from:\n"
		"  1. $GST_MODULE_PATH (colon-separated)\n"
		"  2. ~/.config/gst/modules/\n"
		"  3. /etc/gst/modules/\n"
		"  4. /usr/share/gst/modules/\n"
		"\n"
		"Backend selection:\n"
		"  Auto-detected (Wayland if $WAYLAND_DISPLAY is set, X11 otherwise).\n"
		"  Use --x11 or --wayland to force a specific backend.");

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Error parsing options: %s\n", error->message);
		g_error_free(error);
		g_option_context_free(context);
		return EXIT_FAILURE;
	}
	g_option_context_free(context);

	/* Handle --version */
	if (opt_version) {
		g_print("gst %s\n", GST_VERSION_STRING);
		return EXIT_SUCCESS;
	}

	/* Handle --license */
	if (opt_license) {
		g_print("%s", license_text);
		return EXIT_SUCCESS;
	}

	/* Validate mutually exclusive flags */
	if (opt_x11 && opt_wayland) {
		g_printerr("Cannot specify both --x11 and --wayland\n");
		return EXIT_FAILURE;
	}

	/* Detect backend */
	backend = detect_backend();

#ifndef GST_HAVE_WAYLAND
	if (backend == GST_BACKEND_WAYLAND) {
		g_printerr("Wayland support not compiled in. "
			"Rebuild with BUILD_WAYLAND=1\n");
		return EXIT_FAILURE;
	}
#endif

	/* Step 0: Load configuration */
	config = gst_config_get_default();

	if (opt_config != NULL) {
		/* Explicit config path from --config */
		if (!gst_config_load_from_path(config, opt_config, &error)) {
			g_printerr("Failed to load config '%s': %s\n",
				opt_config, error->message);
			g_error_free(error);
			return EXIT_FAILURE;
		}
	} else {
		/* Search default paths, silently ignore missing */
		g_autofree gchar *default_path = NULL;

		default_path = find_default_config();
		if (default_path != NULL) {
			if (!gst_config_load_from_path(config, default_path, &error)) {
				g_printerr("Warning: failed to load config '%s': %s\n",
					default_path, error->message);
				g_clear_error(&error);
				/* Continue with defaults */
			}
		}
	}

	/* Cache runtime config values for hot paths */
	cfg_border_px = gst_config_get_border_px(config);
	cfg_min_latency = gst_config_get_min_latency(config);
	cfg_max_latency = gst_config_get_max_latency(config);

	/* Determine terminal dimensions (CLI overrides config) */
	cols = (gint)gst_config_get_cols(config);
	rows = (gint)gst_config_get_rows(config);

	if (opt_geometry != NULL) {
		if (!parse_geometry(opt_geometry, &cols, &rows)) {
			g_printerr("Invalid geometry: %s\n", opt_geometry);
			g_printerr("Expected format: COLSxROWS (e.g., 80x24)\n");
			return EXIT_FAILURE;
		}
	}

	/* Determine font (CLI overrides config) */
	fontstr = (opt_font != NULL) ? opt_font
		: gst_config_get_font_primary(config);

	/* Determine shell (CLI --exec overrides config) */
	shell_cmd = (opt_execute != NULL) ? opt_execute
		: gst_config_get_shell(config);

	/* Step 0.5: Load modules */
	{
		GstModuleManager *mod_mgr;
		const gchar *mod_env;

		mod_mgr = gst_module_manager_get_default();
		gst_module_manager_set_config(mod_mgr, config);

		/* $GST_MODULE_PATH: colon-separated list of directories */
		mod_env = g_getenv("GST_MODULE_PATH");
		if (mod_env != NULL)
		{
			gchar **paths;
			guint pi;

			paths = g_strsplit(mod_env, ":", -1);
			for (pi = 0; paths[pi] != NULL; pi++)
			{
				gst_module_manager_load_from_directory(mod_mgr, paths[pi]);
			}
			g_strfreev(paths);
		}

		/* User module directory: ~/.config/gst/modules/ */
		{
			g_autofree gchar *user_mod_dir = NULL;

			user_mod_dir = g_build_filename(
				g_get_user_config_dir(), "gst", "modules", NULL);
			gst_module_manager_load_from_directory(mod_mgr, user_mod_dir);
		}

		/* System module directory */
		gst_module_manager_load_from_directory(mod_mgr, GST_MODULEDIR);

		/* NOTE: activate_all is deferred to after terminal/window creation
		 * so modules have access to core objects during activation. */
	}

	/* Step 1: Create terminal */
	terminal = gst_terminal_new(cols, rows);

	/* Step 2: Create selection */
	selection = gst_selection_new(terminal);

	/* Step 3: Initialize backend (fonts, window, renderer) */
	{
		gboolean ok;

		ok = FALSE;

		switch (backend) {
		case GST_BACKEND_X11:
			ok = init_x11_backend(cols, rows, fontstr, config);
			break;
#ifdef GST_HAVE_WAYLAND
		case GST_BACKEND_WAYLAND:
			ok = init_wayland_backend(cols, rows, fontstr, config);
			break;
#endif
		default:
			g_printerr("Unknown backend type\n");
			break;
		}

		if (!ok) {
			g_object_unref(selection);
			g_object_unref(terminal);
			return EXIT_FAILURE;
		}
	}

	/* Apply title: CLI --title overrides config */
	if (opt_title != NULL) {
		gst_window_set_title(window, opt_title);
	} else {
		gst_window_set_title(window,
			gst_config_get_title(config));
	}

	/* Step 4: Create PTY and spawn shell */
	pty = gst_pty_new();

	if (!gst_pty_spawn(pty, shell_cmd, NULL, &error)) {
		g_printerr("Failed to spawn shell: %s\n", error->message);
		g_error_free(error);
		g_object_unref(renderer);
		g_object_unref(window);
		if (font_cache != NULL) {
			g_object_unref(font_cache);
		}
#ifdef GST_HAVE_WAYLAND
		if (cairo_font_cache != NULL) {
			g_object_unref(cairo_font_cache);
		}
#endif
		g_object_unref(pty);
		g_object_unref(selection);
		g_object_unref(terminal);
		return EXIT_FAILURE;
	}

	/* Step 5: Connect all signals */

	/* PTY signals */
	g_signal_connect(pty, "data-received",
		G_CALLBACK(on_pty_data_received), NULL);
	g_signal_connect(pty, "child-exited",
		G_CALLBACK(on_child_exited), NULL);

	/* Terminal signals */
	g_signal_connect(terminal, "response",
		G_CALLBACK(on_terminal_response), NULL);
	g_signal_connect(terminal, "title-changed",
		G_CALLBACK(on_terminal_title_changed), NULL);
	g_signal_connect(terminal, "bell",
		G_CALLBACK(on_terminal_bell), NULL);
	g_signal_connect(terminal, "escape-string",
		G_CALLBACK(on_terminal_escape_string), NULL);

	/* Window signals */
	g_signal_connect(window, "key-press",
		G_CALLBACK(on_key_press), NULL);
	g_signal_connect(window, "button-press",
		G_CALLBACK(on_button_press), NULL);
	g_signal_connect(window, "button-release",
		G_CALLBACK(on_button_release), NULL);
	g_signal_connect(window, "motion-notify",
		G_CALLBACK(on_motion_notify), NULL);
	g_signal_connect(window, "focus-change",
		G_CALLBACK(on_focus_change), NULL);
	g_signal_connect(window, "configure",
		G_CALLBACK(on_configure), NULL);
	g_signal_connect(window, "expose",
		G_CALLBACK(on_expose), NULL);
	g_signal_connect(window, "visibility",
		G_CALLBACK(on_visibility), NULL);
	g_signal_connect(window, "close-request",
		G_CALLBACK(on_close_request), NULL);
	g_signal_connect(window, "selection-notify",
		G_CALLBACK(on_selection_notify), NULL);

	/* Step 6: Start event watch */
	gst_window_start_event_watch(window);

	/* Step 7: Provide core objects to module manager and activate */
	{
		GstModuleManager *mod_mgr;

		mod_mgr = gst_module_manager_get_default();
		gst_module_manager_set_terminal(mod_mgr, terminal);
		gst_module_manager_set_window(mod_mgr, window);
		gst_module_manager_set_backend_type(mod_mgr, (gint)backend);

		/* Provide the appropriate font cache for modules (e.g. font2) */
		if (backend == GST_BACKEND_X11) {
			gst_module_manager_set_font_cache(mod_mgr, font_cache);
		}
#ifdef GST_HAVE_WAYLAND
		else if (backend == GST_BACKEND_WAYLAND) {
			gst_module_manager_set_font_cache(mod_mgr, cairo_font_cache);
		}
#endif

		gst_module_manager_activate_all(mod_mgr);
	}

	/* Set up SIGTERM/SIGINT for clean shutdown */
	g_unix_signal_add(SIGTERM, on_sigterm, NULL);
	g_unix_signal_add(SIGINT, on_sigterm, NULL);

	/* Step 8: Run main loop */
	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	/* Cleanup */
	gst_module_manager_deactivate_all(gst_module_manager_get_default());

	if (draw_timeout_id != 0) {
		g_source_remove(draw_timeout_id);
	}

	g_main_loop_unref(main_loop);
	g_object_unref(renderer);
	g_object_unref(pty);
	g_object_unref(window);

	if (font_cache != NULL) {
		gst_font_cache_unload_fonts(font_cache);
		g_object_unref(font_cache);
	}
#ifdef GST_HAVE_WAYLAND
	if (cairo_font_cache != NULL) {
		gst_cairo_font_cache_unload_fonts(cairo_font_cache);
		g_object_unref(cairo_font_cache);
	}
#endif

	g_object_unref(selection);
	g_object_unref(terminal);

	g_free(opt_config);
	g_free(opt_title);
	g_free(opt_geometry);
	g_free(opt_font);
	g_free(opt_name);
	g_free(opt_windowid);
	g_free(opt_execute);

	return EXIT_SUCCESS;
}
