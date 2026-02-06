/*
 * main.c - GST (GObject Simple Terminal) Entry Point
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Full windowed terminal emulator entry point.
 * Creates a terminal, X11 window, renderer, and PTY,
 * wires all signals together, and runs the GLib main loop.
 *
 * Draw timing uses an adaptive latency model ported from st:
 * coalesce rapid PTY writes into single frames, bounded by
 * minlatency and maxlatency thresholds.
 *
 * Configuration is loaded from YAML files via GstConfig.
 * CLI options override config values where applicable.
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
#include "core/gst-terminal.h"
#include "core/gst-pty.h"
#include "rendering/gst-font-cache.h"
#include "rendering/gst-x11-renderer.h"
#include "window/gst-x11-window.h"
#include "selection/gst-selection.h"
#include "config/gst-config.h"
#include "config/gst-color-scheme.h"

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
	{ NULL }
};

static const gchar *license_text =
	"GST - GObject Simple Terminal\n"
	"Copyright (C) 2024 Zach Podbielniak\n"
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
static GstFontCache *font_cache = NULL;
static GstX11Window *window = NULL;
static GstX11Renderer *renderer = NULL;

/* Draw timing state */
static guint draw_timeout_id = 0;
static gint64 draw_trigger_time = 0;
static gboolean drawing = FALSE;

/* Runtime config values (populated from GstConfig on startup) */
static guint cfg_border_px = 2;
static guint cfg_min_latency = 8;
static guint cfg_max_latency = 33;

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
	const gchar *system_paths[] = {
		"/etc/gst/config.yaml",
		"/usr/share/gst/config.yaml",
		NULL
	};
	guint i;

	/* XDG user config */
	xdg_config = g_get_user_config_dir();
	if (xdg_config != NULL) {
		user_path = g_build_filename(xdg_config, "gst",
			"config.yaml", NULL);
		if (g_file_test(user_path, G_FILE_TEST_IS_REGULAR)) {
			return g_steal_pointer(&user_path);
		}
	}

	/* System config paths */
	for (i = 0; system_paths[i] != NULL; i++) {
		if (g_file_test(system_paths[i], G_FILE_TEST_IS_REGULAR)) {
			return g_strdup(system_paths[i]);
		}
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
	gint cw;
	gint cols;
	gint x;

	cw = gst_font_cache_get_char_width(font_cache);
	cols = gst_terminal_get_cols(terminal);
	x = (px - (gint)cfg_border_px) / cw;
	if (x < 0) x = 0;
	if (x >= cols) x = cols - 1;
	return x;
}

static gint
pixel_to_row(gint py)
{
	gint ch;
	gint rows;
	gint y;

	ch = gst_font_cache_get_char_height(font_cache);
	rows = gst_terminal_get_rows(terminal);
	y = (py - (gint)cfg_border_px) / ch;
	if (y < 0) y = 0;
	if (y >= rows) y = rows - 1;
	return y;
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
	GstWinMode wm;

	draw_timeout_id = 0;
	drawing = FALSE;

	wm = gst_x11_renderer_get_win_mode(renderer);
	if (!(wm & GST_WIN_MODE_VISIBLE)) {
		return G_SOURCE_REMOVE;
	}

	if (!gst_renderer_start_draw(GST_RENDERER(renderer))) {
		return G_SOURCE_REMOVE;
	}

	gst_renderer_render(GST_RENDERER(renderer));
	gst_renderer_finish_draw(GST_RENDERER(renderer));

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
	gst_pty_write(pty, data, (gssize)len);
}

/*
 * Terminal title-changed: update X11 window title.
 */
static void
on_terminal_title_changed(
	GstTerminal *term,
	const gchar *title,
	gpointer    user_data
){
	gst_x11_window_set_title_x11(window, title);
}

/*
 * Terminal bell: flash urgency hint.
 */
static void
on_terminal_bell(
	GstTerminal *term,
	gpointer    user_data
){
	gst_x11_window_bell(window);
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
 * Window key-press: check shortcuts, then forward to PTY.
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
	gchar buf[64];
	gint out_len;

	/* Ctrl+Shift+C: copy to clipboard */
	if (keysym == XK_C && (state & ControlMask) && (state & ShiftMask)) {
		gchar *sel_text;

		sel_text = gst_selection_get_text(selection);
		if (sel_text != NULL) {
			gst_x11_window_set_selection(window, sel_text, FALSE);
			gst_x11_window_copy_to_clipboard(window);
			g_free(sel_text);
		}
		return;
	}

	/* Ctrl+Shift+V: paste from clipboard */
	if (keysym == XK_V && (state & ControlMask) && (state & ShiftMask)) {
		gst_x11_window_paste_clipboard(window);
		return;
	}

	/* Shift+Insert: paste primary */
	if (keysym == XK_Insert && (state & ShiftMask)) {
		gst_x11_window_paste_primary(window);
		return;
	}

	/* Forward text to PTY */
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
		/* TODO: mouse reporting protocol (SGR, X10, etc.) */
		return;
	}

	/* Left button starts selection */
	if (button == Button1) {
		gst_selection_start(selection, col, row, GST_SELECTION_SNAP_NONE);
		schedule_draw();
	}

	/* Middle button pastes primary */
	if (button == Button2) {
		gst_x11_window_paste_primary(window);
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
			gst_x11_window_set_selection(window, sel_text, FALSE);
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

	wm = gst_x11_renderer_get_win_mode(renderer);
	if (focused) {
		wm |= GST_WIN_MODE_FOCUSED;
	} else {
		wm &= ~GST_WIN_MODE_FOCUSED;
	}
	gst_x11_renderer_set_win_mode(renderer, wm);

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
	gint cw;
	gint ch;
	gint cols;
	gint rows;

	cw = gst_font_cache_get_char_width(font_cache);
	ch = gst_font_cache_get_char_height(font_cache);

	cols = ((gint)width - 2 * (gint)cfg_border_px) / cw;
	rows = ((gint)height - 2 * (gint)cfg_border_px) / ch;

	if (cols < 1) cols = 1;
	if (rows < 1) rows = 1;

	gst_terminal_resize(terminal, cols, rows);
	gst_renderer_resize(GST_RENDERER(renderer), width, height);
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

	wm = gst_x11_renderer_get_win_mode(renderer);
	if (visible) {
		wm |= GST_WIN_MODE_VISIBLE;
	} else {
		wm &= ~GST_WIN_MODE_VISIBLE;
	}
	gst_x11_renderer_set_win_mode(renderer, wm);
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

/* ===== Main ===== */

int
main(
	int     argc,
	char    **argv
){
	GOptionContext *context;
	GError *error = NULL;
	GstConfig *config;
	GstColorScheme *scheme;
	gint cols;
	gint rows;
	gint cw;
	gint ch;
	const gchar *fontstr;
	const gchar *shell_cmd;
	gulong embed_id;
	Display *display;
	Window xid;
	Visual *visual;
	Colormap colormap;
	gint screen;

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
		"  4. /usr/share/gst/modules/");

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

	/* Step 1: Create terminal */
	terminal = gst_terminal_new(cols, rows);

	/* Step 2: Create selection */
	selection = gst_selection_new(terminal);

	/* Step 3: Initialize fontconfig and load fonts */
	if (!FcInit()) {
		g_printerr("Could not initialize fontconfig\n");
		g_object_unref(terminal);
		return EXIT_FAILURE;
	}

	font_cache = gst_font_cache_new();

	/*
	 * We need an X display to load fonts. Create the window first,
	 * then use its display for font loading and renderer creation.
	 */

	/* Step 4: Create X11 window with estimated dimensions */
	embed_id = 0;
	if (opt_windowid != NULL) {
		embed_id = strtoul(opt_windowid, NULL, 0);
	}

	/*
	 * Bootstrap: we need cw/ch from fonts to size the window,
	 * but we need a display to load fonts. Open a temporary display
	 * connection for font loading, then use window's display.
	 */
	{
		Display *tmp_dpy;
		gint tmp_screen;

		tmp_dpy = XOpenDisplay(NULL);
		if (tmp_dpy == NULL) {
			g_printerr("Cannot open X11 display\n");
			g_object_unref(font_cache);
			g_object_unref(selection);
			g_object_unref(terminal);
			return EXIT_FAILURE;
		}
		tmp_screen = XDefaultScreen(tmp_dpy);

		if (!gst_font_cache_load_fonts(font_cache, tmp_dpy, tmp_screen, fontstr, 0)) {
			g_printerr("Cannot load font: %s\n", fontstr);
			XCloseDisplay(tmp_dpy);
			g_object_unref(font_cache);
			g_object_unref(selection);
			g_object_unref(terminal);
			return EXIT_FAILURE;
		}

		/* Font cache now holds references to tmp_dpy's resources,
		 * but we'll use the window's display instead. Unload and
		 * reload after window creation.
		 */
		cw = gst_font_cache_get_char_width(font_cache);
		ch = gst_font_cache_get_char_height(font_cache);

		/* Unload from tmp display, we'll reload on window display */
		gst_font_cache_unload_fonts(font_cache);
		XCloseDisplay(tmp_dpy);
	}

	/* Step 5: Create X11 window with proper dimensions */
	window = gst_x11_window_new(cols, rows, cw, ch,
		(gint)cfg_border_px, embed_id);
	if (window == NULL) {
		g_printerr("Cannot create X11 window\n");
		g_object_unref(font_cache);
		g_object_unref(selection);
		g_object_unref(terminal);
		return EXIT_FAILURE;
	}

	/* Step 6: Reload fonts on window's display */
	display = gst_x11_window_get_display(window);
	screen = gst_x11_window_get_screen(window);

	if (!gst_font_cache_load_fonts(font_cache, display, screen, fontstr, 0)) {
		g_printerr("Cannot load font on window display: %s\n", fontstr);
		g_object_unref(window);
		g_object_unref(font_cache);
		g_object_unref(selection);
		g_object_unref(terminal);
		return EXIT_FAILURE;
	}

	cw = gst_font_cache_get_char_width(font_cache);
	ch = gst_font_cache_get_char_height(font_cache);

	/* Step 7: Show window and set WM hints */
	gst_x11_window_set_wm_hints(window, cw, ch, (gint)cfg_border_px);
	gst_window_show(GST_WINDOW(window));

	/* Apply title: CLI --title overrides config */
	if (opt_title != NULL) {
		gst_x11_window_set_title_x11(window, opt_title);
	} else {
		gst_x11_window_set_title_x11(window,
			gst_config_get_title(config));
	}

	/* Step 8: Create X11 renderer */
	xid = gst_x11_window_get_xid(window);
	visual = gst_x11_window_get_visual(window);
	colormap = gst_x11_window_get_colormap(window);

	renderer = gst_x11_renderer_new(
		terminal, display, xid, visual, colormap,
		screen, font_cache, (gint)cfg_border_px);

	/* Step 9: Load colors from config */
	scheme = gst_color_scheme_new("config");
	gst_color_scheme_load_from_config(scheme, config);

	if (!gst_x11_renderer_load_colors(renderer)) {
		g_printerr("Cannot load colors\n");
	}

	g_object_unref(scheme);

	/* Set initial win_mode */
	gst_x11_renderer_set_win_mode(renderer,
		GST_WIN_MODE_VISIBLE | GST_WIN_MODE_FOCUSED | GST_WIN_MODE_NUMLOCK);

	/* Step 10: Create PTY and spawn shell */
	pty = gst_pty_new();

	if (!gst_pty_spawn(pty, shell_cmd, NULL, &error)) {
		g_printerr("Failed to spawn shell: %s\n", error->message);
		g_error_free(error);
		g_object_unref(renderer);
		g_object_unref(window);
		g_object_unref(font_cache);
		g_object_unref(pty);
		g_object_unref(selection);
		g_object_unref(terminal);
		return EXIT_FAILURE;
	}

	/* Step 11: Connect all signals */

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

	/* Step 12: Start X11 event watch */
	gst_x11_window_start_event_watch(window);

	/* Set up SIGTERM/SIGINT for clean shutdown */
	g_unix_signal_add(SIGTERM, on_sigterm, NULL);
	g_unix_signal_add(SIGINT, on_sigterm, NULL);

	/* Step 13: Run main loop */
	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	/* Cleanup */
	if (draw_timeout_id != 0) {
		g_source_remove(draw_timeout_id);
	}

	g_main_loop_unref(main_loop);
	g_object_unref(renderer);
	g_object_unref(pty);
	g_object_unref(window);
	gst_font_cache_unload_fonts(font_cache);
	g_object_unref(font_cache);
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
