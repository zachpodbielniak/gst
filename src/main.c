/*
 * main.c - GST (GObject Simple Terminal) Entry Point
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Main entry point for the gst terminal emulator.
 * Currently operates in headless mode (no X11 yet):
 * - Spawns a shell via GstPty
 * - Feeds PTY output through GstTerminal for escape processing
 * - Writes raw PTY output to stdout for display
 * - Reads stdin and forwards to the PTY
 *
 * Once Phase 3 (rendering) is complete, this will be replaced
 * with proper X11 window creation and event handling.
 */

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "gst.h"
#include "core/gst-terminal.h"
#include "core/gst-pty.h"

/* Command line options */
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

/* Application state */
static GMainLoop *main_loop = NULL;
static GstTerminal *terminal = NULL;
static GstPty *pty = NULL;
static struct termios orig_termios;
static gboolean termios_saved = FALSE;

/*
 * parse_geometry:
 * @geometry: geometry string in format "COLSxROWS"
 * @cols: (out): location for columns
 * @rows: (out): location for rows
 *
 * Parses a geometry string.
 *
 * Returns: TRUE if parsing succeeded
 */
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
 * restore_terminal:
 *
 * Restores the host terminal settings saved before entering raw mode.
 */
static void
restore_terminal(void)
{
	if (termios_saved) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
		termios_saved = FALSE;
	}
}

/*
 * set_raw_mode:
 *
 * Puts the host terminal into raw mode so that keypresses are
 * forwarded directly to the child PTY without line buffering
 * or local echo.
 *
 * Returns: TRUE on success
 */
static gboolean
set_raw_mode(void)
{
	struct termios raw;

	if (!isatty(STDIN_FILENO)) {
		return FALSE;
	}

	if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
		return FALSE;
	}
	termios_saved = TRUE;

	raw = orig_termios;
	/* Input: no break, no CR to NL, no parity check, no strip, no flow control */
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	/* Output: disable post-processing */
	raw.c_oflag &= ~(OPOST);
	/* Control: set 8-bit chars */
	raw.c_cflag |= (CS8);
	/* Local: no echo, no canonical, no extended, no signal chars */
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	/* Return as soon as any input is available */
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
		return FALSE;
	}

	return TRUE;
}

/*
 * get_host_winsize:
 * @cols: (out): location for columns
 * @rows: (out): location for rows
 *
 * Queries the host terminal for its current window size.
 *
 * Returns: TRUE if the size was obtained
 */
static gboolean
get_host_winsize(
	gint *cols,
	gint *rows
){
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
		return FALSE;
	}

	*cols = (gint)ws.ws_col;
	*rows = (gint)ws.ws_row;
	return TRUE;
}

/*
 * on_pty_data_received:
 * @pty_obj: the GstPty emitting the signal
 * @data: raw data from the child process
 * @len: length of data
 * @user_data: pointer to GstTerminal
 *
 * Signal handler for GstPty::data-received.
 * Feeds data through the terminal emulator for escape processing,
 * and writes raw output to stdout for display.
 */
static void
on_pty_data_received(
	GstPty      *pty_obj,
	gpointer    data,
	gulong      len,
	gpointer    user_data
){
	GstTerminal *term;

	term = GST_TERMINAL(user_data);

	/* Write raw data to stdout for display in headless mode */
	write(STDOUT_FILENO, data, (size_t)len);

	/* Feed data through terminal emulator for state tracking */
	gst_terminal_write(term, (const gchar *)data, (gssize)len);
}

/*
 * on_terminal_response:
 * @term: the GstTerminal emitting the signal
 * @data: response data to send to PTY
 * @len: length of response data
 * @user_data: pointer to GstPty
 *
 * Signal handler for GstTerminal::response.
 * Forwards terminal responses (DA, DSR, cursor reports) back
 * to the child process via the PTY.
 */
static void
on_terminal_response(
	GstTerminal *term,
	const gchar *data,
	glong       len,
	gpointer    user_data
){
	GstPty *pty_obj;

	pty_obj = GST_PTY(user_data);

	gst_pty_write(pty_obj, data, (gssize)len);
}

/*
 * on_child_exited:
 * @pty_obj: the GstPty emitting the signal
 * @status: exit status of the child process
 * @user_data: unused
 *
 * Signal handler for GstPty::child-exited.
 * Quits the main loop when the child shell exits.
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
 * on_stdin_readable:
 * @source: the GIOChannel for stdin
 * @condition: the I/O condition
 * @user_data: pointer to GstPty
 *
 * GIOChannel callback for stdin. Reads input and forwards
 * it to the child process via the PTY.
 *
 * Returns: TRUE to keep the watch active
 */
static gboolean
on_stdin_readable(
	GIOChannel      *source,
	GIOCondition    condition,
	gpointer        user_data
){
	GstPty *pty_obj;
	gchar buf[4096];
	gssize n;

	pty_obj = GST_PTY(user_data);

	if (condition & (G_IO_HUP | G_IO_ERR)) {
		return FALSE;
	}

	if (condition & G_IO_IN) {
		n = read(STDIN_FILENO, buf, sizeof(buf));
		if (n > 0) {
			gst_pty_write(pty_obj, buf, n);
		} else if (n <= 0) {
			return FALSE;
		}
	}

	return TRUE;
}

/*
 * on_sigwinch:
 * @user_data: unused
 *
 * Signal handler for SIGWINCH. Resizes both the terminal
 * emulator and PTY to match the host terminal's new size.
 *
 * Returns: G_SOURCE_CONTINUE to keep the handler active
 */
static gboolean
on_sigwinch(gpointer user_data)
{
	gint cols;
	gint rows;

	if (get_host_winsize(&cols, &rows)) {
		gst_terminal_resize(terminal, cols, rows);
		gst_pty_resize(pty, cols, rows);
	}

	return G_SOURCE_CONTINUE;
}

/*
 * on_sigterm:
 * @user_data: unused
 *
 * Signal handler for SIGTERM/SIGINT. Quits the main loop
 * for clean shutdown.
 *
 * Returns: G_SOURCE_REMOVE
 */
static gboolean
on_sigterm(gpointer user_data)
{
	if (main_loop != NULL) {
		g_main_loop_quit(main_loop);
	}

	return G_SOURCE_REMOVE;
}

int
main(
	int     argc,
	char    **argv
){
	GOptionContext *context;
	GError *error = NULL;
	GIOChannel *stdin_channel;
	guint stdin_watch_id;
	gint cols;
	gint rows;

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

	/* Determine terminal dimensions */
	cols = GST_DEFAULT_COLS;
	rows = GST_DEFAULT_ROWS;

	/* Parse explicit geometry if given */
	if (opt_geometry != NULL) {
		if (!parse_geometry(opt_geometry, &cols, &rows)) {
			g_printerr("Invalid geometry: %s\n", opt_geometry);
			g_printerr("Expected format: COLSxROWS (e.g., 80x24)\n");
			return EXIT_FAILURE;
		}
	} else {
		/* Otherwise inherit size from host terminal */
		get_host_winsize(&cols, &rows);
	}

	/* Create the terminal emulator */
	terminal = gst_terminal_new(cols, rows);

	/* Create the PTY */
	pty = gst_pty_new();

	/* Connect PTY data-received -> terminal write + stdout */
	g_signal_connect(pty, "data-received",
	                 G_CALLBACK(on_pty_data_received), terminal);

	/* Connect terminal response -> PTY write (for DA, DSR replies) */
	g_signal_connect(terminal, "response",
	                 G_CALLBACK(on_terminal_response), pty);

	/* Connect child-exited -> quit main loop */
	g_signal_connect(pty, "child-exited",
	                 G_CALLBACK(on_child_exited), NULL);

	/* Set host terminal to raw mode */
	if (!set_raw_mode()) {
		g_printerr("Warning: could not set raw mode on stdin\n");
	}

	/* Ensure terminal is restored on exit */
	atexit(restore_terminal);

	/* Spawn the shell */
	if (!gst_pty_spawn(pty, opt_execute, NULL, &error)) {
		restore_terminal();
		g_printerr("Failed to spawn shell: %s\n", error->message);
		g_error_free(error);
		g_object_unref(terminal);
		g_object_unref(pty);
		return EXIT_FAILURE;
	}

	/* Set up stdin reading via GIOChannel */
	stdin_channel = g_io_channel_unix_new(STDIN_FILENO);
	g_io_channel_set_encoding(stdin_channel, NULL, NULL);
	g_io_channel_set_buffered(stdin_channel, FALSE);
	g_io_channel_set_flags(stdin_channel,
		g_io_channel_get_flags(stdin_channel) | G_IO_FLAG_NONBLOCK,
		NULL);

	stdin_watch_id = g_io_add_watch(stdin_channel,
		G_IO_IN | G_IO_ERR | G_IO_HUP,
		on_stdin_readable, pty);

	/* Set up SIGWINCH handler for terminal resize */
	g_unix_signal_add(SIGWINCH, on_sigwinch, NULL);

	/* Set up SIGTERM/SIGINT for clean shutdown */
	g_unix_signal_add(SIGTERM, on_sigterm, NULL);
	g_unix_signal_add(SIGINT, on_sigterm, NULL);

	/* Create and run main loop */
	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	/* Cleanup */
	restore_terminal();

	if (g_main_context_find_source_by_id(NULL, stdin_watch_id) != NULL) {
		g_source_remove(stdin_watch_id);
	}
	g_io_channel_unref(stdin_channel);
	g_main_loop_unref(main_loop);
	g_object_unref(pty);
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
