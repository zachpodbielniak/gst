/*
 * main.c - GST (GObject Simple Terminal) Entry Point
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Main entry point for the gst terminal emulator.
 */

#include <glib.h>
#include <glib-object.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "gst.h"
#include "core/gst-terminal.h"
#include "core/gst-pty.h"
#include "config/gst-config.h"
#include "window/gst-x11-window.h"
#include "rendering/gst-x11-renderer.h"

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

static void
print_usage_examples(void)
{
    g_print("\n");
    g_print("Examples:\n");
    g_print("  gst                          # Start with default shell\n");
    g_print("  gst -e htop                  # Run htop\n");
    g_print("  gst -g 120x40                # Start with 120 columns, 40 rows\n");
    g_print("  gst -f 'JetBrains Mono:14'   # Use specific font\n");
    g_print("  gst -c ~/.config/gst/my.yaml # Use custom config\n");
}

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

int
main(
    int     argc,
    char    **argv
){
    GOptionContext *context;
    GError *error = NULL;
    gint cols = GST_DEFAULT_COLS;
    gint rows = GST_DEFAULT_ROWS;

    /* Set locale */
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

    /* Parse geometry if specified */
    if (opt_geometry != NULL) {
        if (!parse_geometry(opt_geometry, &cols, &rows)) {
            g_printerr("Invalid geometry: %s\n", opt_geometry);
            g_printerr("Expected format: COLSxROWS (e.g., 80x24)\n");
            return EXIT_FAILURE;
        }
    }

    /* TODO: Initialize the full application */
    g_print("GST %s\n", GST_VERSION_STRING);
    g_print("Terminal: %dx%d\n", cols, rows);

    if (opt_title != NULL) {
        g_print("Title: %s\n", opt_title);
    }

    if (opt_font != NULL) {
        g_print("Font: %s\n", opt_font);
    }

    if (opt_config != NULL) {
        g_print("Config: %s\n", opt_config);
    }

    if (opt_execute != NULL) {
        g_print("Execute: %s\n", opt_execute);
    }

    /*
     * TODO: Full implementation:
     * 1. Load configuration from YAML
     * 2. Initialize X11 display and window
     * 3. Create terminal and PTY
     * 4. Load and initialize modules
     * 5. Enter main event loop
     * 6. Clean up on exit
     */

    g_print("\nGST is under development. Full functionality coming soon.\n");
    print_usage_examples();

    /* Cleanup */
    g_free(opt_config);
    g_free(opt_title);
    g_free(opt_geometry);
    g_free(opt_font);
    g_free(opt_name);
    g_free(opt_windowid);
    g_free(opt_execute);

    return EXIT_SUCCESS;
}
