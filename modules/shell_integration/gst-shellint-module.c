/*
 * gst-shellint-module.c - Shell integration via OSC 133 semantic zones
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements semantic prompt zones using the OSC 133 protocol.
 * The shell emits OSC 133 sequences to mark prompt, command,
 * output, and completion boundaries:
 *
 *   OSC 133;A  - prompt start   (record prompt_row)
 *   OSC 133;B  - command start  (record command_row)
 *   OSC 133;C  - output start   (record output_row)
 *   OSC 133;D;N - command done  (record end_row, exit code N)
 *
 * The module stores a bounded array of semantic zones, adjusts
 * signed row indices through retained scrollback, provides
 * Ctrl+Shift+Up/Down navigation between prompts, and renders
 * small colored markers in the left margin at prompt rows.
 */

#include "gst-shellint-module.h"
#include "../scrollback/gst-history.h"
#include "../../src/window/gst-window.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/rendering/gst-render-context.h"

/* keysym values and modifier masks */
#include <X11/keysym.h>
#include <X11/X.h>
#include <string.h>
#include <stdlib.h>

/**
 * SECTION:gst-shellint-module
 * @title: GstShellintModule
 * @short_description: Shell integration via OSC 133 semantic zones
 *
 * #GstShellintModule tracks semantic zones emitted by shell
 * integration scripts (bash, zsh, fish) via OSC 133 escape
 * sequences. It provides prompt-to-prompt navigation with
 * Ctrl+Shift+Up/Down and renders visual markers in the left
 * margin to indicate prompt locations and exit code status.
 */

/* ===== Semantic zone structure ===== */

/*
 * GstSemanticZone:
 * @prompt_row: signed prompt row (OSC 133;A), UNKNOWN_ROW if absent
 * @command_row: signed command row (OSC 133;B), UNKNOWN_ROW if absent
 * @output_row: signed output row (OSC 133;C), UNKNOWN_ROW if absent
 * @end_row: signed completion row (OSC 133;D), UNKNOWN_ROW if absent
 * @exit_code: exit code from OSC 133;D;N, -1 if not yet completed
 *
 * Represents one shell prompt/command/output cycle. Rows are
 * terminal-relative (0 = top visible row) and are decremented
 * as lines scroll out of the buffer.
 */
typedef struct
{
	gint prompt_row;
	gint command_row;
	gint output_row;
	gint end_row;
	gint exit_code;
	gint output_col;
	gint end_col;
} GstSemanticZone;

#define UNKNOWN_ROW (G_MININT)
#define MAX_ZONES (4096)

/* ===== Default configuration ===== */

#define DEFAULT_MARK_PROMPTS  (TRUE)
#define DEFAULT_SHOW_EXIT_CODE (TRUE)

/* Default error color: #ef2929 */
#define DEFAULT_ERROR_R (0xef)
#define DEFAULT_ERROR_G (0x29)
#define DEFAULT_ERROR_B (0x29)

/* Prompt marker color: muted green #4e9a06 */
#define DEFAULT_MARKER_R (0x4e)
#define DEFAULT_MARKER_G (0x9a)
#define DEFAULT_MARKER_B (0x06)

/* Marker dimensions in pixels */
#define MARKER_WIDTH  (3)

/* ===== Private data ===== */

struct _GstShellintModule
{
	GstModule parent_instance;

	GArray   *zones;            /* array of GstSemanticZone */
	gulong    scroll_sig_id;    /* signal handler for line-scrolled-out */
	gulong    resize_sig_id;
	gint      retention;
	gint      navigation_row;

	/* Configuration */
	gboolean  mark_prompts;     /* render prompt markers */
	gboolean  show_exit_code;   /* render exit code indicators */
	guint8    error_r;          /* error indicator color components */
	guint8    error_g;
	guint8    error_b;
};

/* Forward declarations for interface implementations */
static void
gst_shellint_module_escape_handler_init(GstEscapeHandlerInterface *iface);
static void
gst_shellint_module_input_handler_init(GstInputHandlerInterface *iface);
static void
gst_shellint_module_render_overlay_init(GstRenderOverlayInterface *iface);

/* Register the type with all three interfaces */
G_DEFINE_TYPE_WITH_CODE(GstShellintModule, gst_shellint_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_shellint_module_escape_handler_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER,
		gst_shellint_module_input_handler_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_shellint_module_render_overlay_init))

/* ===== Color parsing ===== */

/*
 * parse_hex_color:
 *
 * Parses a "#RRGGBB" hex color string into RGB components.
 * Returns TRUE on success, FALSE on malformed input.
 */
static gboolean
parse_hex_color(
	const gchar *str,
	guint8      *r,
	guint8      *g,
	guint8      *b
){
	guint val;

	if (str == NULL || str[0] != '#' || strlen(str) != 7) {
		return FALSE;
	}

	val = (guint)strtoul(str + 1, NULL, 16);
	*r = (guint8)((val >> 16) & 0xFF);
	*g = (guint8)((val >> 8) & 0xFF);
	*b = (guint8)(val & 0xFF);

	return TRUE;
}

/* ===== Internal helpers ===== */

/*
 * mark_all_dirty:
 *
 * Marks all terminal lines as dirty to force a full redraw.
 * Used after prompt navigation changes the view.
 */
static void
mark_all_dirty(void)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	gint rows;
	gint y;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return;
	}

	rows = gst_terminal_get_rows(term);
	for (y = 0; y < rows; y++) {
		gst_terminal_mark_dirty(term, y);
	}
}

/*
 * get_current_cursor_row:
 *
 * Returns the current cursor row from the terminal, or -1 if
 * the terminal is unavailable.
 */
static gint
get_current_cursor_row(void)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	GstCursor *cursor;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return -1;
	}

	cursor = gst_terminal_get_cursor(term);
	if (cursor == NULL) {
		return -1;
	}

	return cursor->y;
}

/*
 * on_line_scrolled_out:
 *
 * Signal callback for "line-scrolled-out". Shift known boundaries,
 * retaining negative rows until the configured history horizon expires.
 */
static void
on_line_scrolled_out(
	GstTerminal *term,
	GstLine     *line,
	gint         cols,
	gpointer     user_data
){
	GstShellintModule *self;
	guint i;

	(void)term;
	(void)line;
	(void)cols;

	self = GST_SHELLINT_MODULE(user_data);

	/* Decrement all row indices */
	for (i = 0; i < self->zones->len; i++) {
		GstSemanticZone *zone;

		zone = &g_array_index(self->zones, GstSemanticZone, i);

		/* Saturate old boundaries: an open command can run indefinitely. */
		if (zone->prompt_row != UNKNOWN_ROW)
			zone->prompt_row = MAX(zone->prompt_row - 1, -self->retention - 1);
		if (zone->command_row != UNKNOWN_ROW)
			zone->command_row = MAX(zone->command_row - 1, -self->retention - 1);
		if (zone->output_row != UNKNOWN_ROW)
			zone->output_row = MAX(zone->output_row - 1, -self->retention - 1);
		if (zone->end_row != UNKNOWN_ROW)
			zone->end_row = MAX(zone->end_row - 1, -self->retention - 1);
	}
	if (self->navigation_row != UNKNOWN_ROW)
		self->navigation_row = MAX(self->navigation_row - 1, -self->retention - 1);

	/*
	 * Remove zones whose output end (or next prompt for abandoned cycles)
	 * has expired. An open command stays available for a later D marker.
	 * Walk backwards to safely remove during iteration.
	 */
	for (i = self->zones->len; i > 0; i--) {
		GstSemanticZone *zone;
		gboolean all_gone;

		zone = &g_array_index(self->zones, GstSemanticZone, i - 1);

		all_gone = zone->end_row != UNKNOWN_ROW &&
			zone->end_row < -self->retention;
		if (zone->end_row == UNKNOWN_ROW && i < self->zones->len)
			all_gone = g_array_index(self->zones, GstSemanticZone, i).prompt_row < -self->retention;

		if (all_gone) {
			g_array_remove_index(self->zones, i - 1);
		}
	}
}

/* ===== GstEscapeHandler interface ===== */

/* Live reflow changes physical row/column boundaries. Until core supplies
 * an anchor remapping signal, invalidate zones rather than export wrong text. */
static void
on_history_resize(GstTerminal *term, gint cols, gint rows, gpointer data)
{
	GstShellintModule *self = (GstShellintModule *)data;
	(void)term;
	(void)cols;
	(void)rows;
	if (self->zones != NULL)
		g_array_set_size(self->zones, 0);
	self->navigation_row = UNKNOWN_ROW;
}

/*
 * handle_escape_string:
 *
 * Handles OSC 133 semantic zone sequences. The raw buffer
 * contains the full OSC content with semicolons intact, e.g.:
 *   "133;A"     - prompt start
 *   "133;B"     - command start
 *   "133;C"     - output start
 *   "133;D;0"   - command complete with exit code 0
 *   "133;D"     - command complete with no exit code
 *
 * Parses the OSC number, checks for 133, then dispatches on
 * the subcommand character after the first semicolon.
 */
static gboolean
gst_shellint_module_handle_escape_string(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstShellintModule *self;
	gint osc_num;
	gchar *endptr;
	const gchar *rest;
	gchar subcmd;
	gint cur_row;
	gint cur_col;
	GstTerminal *term;
	g_autofree gchar *sequence = NULL;

	self = GST_SHELLINT_MODULE(handler);

	/* Only handle OSC sequences */
	if (str_type != ']') {
		return FALSE;
	}

	if (buf == NULL || len < 4) {
		return FALSE;
	}
	/* Escape handlers receive bounded buffers, not necessarily C strings. */
	sequence = g_strndup(buf, len);
	buf = sequence;

	/* Parse the OSC number */
	osc_num = (gint)strtol(buf, &endptr, 10);
	if (endptr == buf || osc_num != 133) {
		return FALSE;
	}

	/* Skip semicolon after "133" */
	if (*endptr != ';') {
		return FALSE;
	}
	rest = endptr + 1;

	/* Extract subcommand character */
	if (*rest == '\0') {
		return FALSE;
	}
	subcmd = *rest;
	if (rest[1] != '\0' && rest[1] != ';')
		return FALSE;

	term = (GstTerminal *)terminal;
	if (term == NULL || self->zones == NULL || gst_terminal_is_altscreen(term))
		return FALSE;
	cur_row = gst_terminal_get_cursor(term)->y;
	cur_col = gst_terminal_get_cursor(term)->x;
	if (gst_terminal_get_cursor(term)->state & GST_CURSOR_STATE_WRAPNEXT)
		cur_col++;

	switch (subcmd) {
	case 'A': {
		/* Prompt start: create a new zone */
		GstSemanticZone zone;

		zone.prompt_row = cur_row;
		zone.command_row = UNKNOWN_ROW;
		zone.output_row = UNKNOWN_ROW;
		zone.end_row = UNKNOWN_ROW;
		zone.exit_code = -1;
		zone.output_col = 0;
		zone.end_col = 0;
		if (self->zones->len == MAX_ZONES)
			g_array_remove_index(self->zones, 0);
		g_array_append_val(self->zones, zone);

		g_debug("shell_integration: prompt start at row %d", cur_row);
		break;
	}

	case 'B': {
		/* Command start: update the most recent zone */
		if (self->zones->len > 0) {
			GstSemanticZone *zone;

			zone = &g_array_index(self->zones,
				GstSemanticZone, self->zones->len - 1);
			zone->command_row = cur_row;

			g_debug("shell_integration: command start at row %d",
				cur_row);
		}
		break;
	}

	case 'C': {
		/* Output start: update the most recent zone */
		if (self->zones->len > 0) {
			GstSemanticZone *zone;

			zone = &g_array_index(self->zones,
				GstSemanticZone, self->zones->len - 1);
			zone->output_row = cur_row;
			zone->output_col = cur_col;

			g_debug("shell_integration: output start at row %d",
				cur_row);
		}
		break;
	}

	case 'D': {
		/* Command complete: parse optional exit code */
		gint exit_code;

		exit_code = 0;

		/* Check for ";N" after the D */
		if (*(rest + 1) == ';' && *(rest + 2) != '\0') {
			gchar *status_end;
			gint64 status = g_ascii_strtoll(rest + 2, &status_end, 10);
			if (status_end == rest + 2 || (*status_end != '\0' && *status_end != ';') ||
				status < 0 || status > G_MAXINT)
				return FALSE;
			exit_code = (gint)status;
		}

		if (self->zones->len > 0) {
			GstSemanticZone *zone;

			zone = &g_array_index(self->zones,
				GstSemanticZone, self->zones->len - 1);
			zone->end_row = cur_row;
			zone->end_col = cur_col;
			zone->exit_code = exit_code;

			g_debug("shell_integration: command done at row %d, "
				"exit=%d", cur_row, exit_code);
		}

		/* Mark dirty to update exit code indicator */
		mark_all_dirty();
		break;
	}

	default:
		/* Unknown subcommand, don't consume */
		return FALSE;
	}

	return TRUE;
}

static void
gst_shellint_module_escape_handler_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string =
		gst_shellint_module_handle_escape_string;
}

/* ===== GstInputHandler interface ===== */

/*
 * find_prev_prompt:
 *
 * Finds the prompt row of the zone before the given row.
 * Searches backwards through the zone array for the closest
 * prompt_row that is strictly less than @current_row.
 *
 * Returns: the prompt row, or -1 if none found
 */
static gint
find_prev_prompt(GstShellintModule *self, gint current_row)
{
	gint best;
	guint i;

	best = UNKNOWN_ROW;

	for (i = 0; i < self->zones->len; i++) {
		GstSemanticZone *zone;

		zone = &g_array_index(self->zones, GstSemanticZone, i);
		if (zone->prompt_row != UNKNOWN_ROW && zone->prompt_row < current_row &&
			zone->prompt_row >= -self->retention) {
			best = zone->prompt_row;
		}
	}

	return best;
}

/*
 * find_next_prompt:
 *
 * Finds the prompt row of the zone after the given row.
 * Searches forward through the zone array for the closest
 * prompt_row that is strictly greater than @current_row.
 *
 * Returns: the prompt row, or -1 if none found
 */
static gint
find_next_prompt(GstShellintModule *self, gint current_row)
{
	guint i;

	for (i = 0; i < self->zones->len; i++) {
		GstSemanticZone *zone;

		zone = &g_array_index(self->zones, GstSemanticZone, i);
		if (zone->prompt_row != UNKNOWN_ROW && zone->prompt_row > current_row) {
			return zone->prompt_row;
		}
	}

	return UNKNOWN_ROW;
}

/*
 * handle_key_event:
 *
 * Handles prompt navigation keys:
 *   Ctrl+Shift+Up:   jump to previous prompt
 *   Ctrl+Shift+Down: jump to next prompt
 *
 * Navigation works by scrolling the terminal view so the target
 * prompt row is visible. Uses the scrollback module's scroll
 * offset if the prompts are in history, or marks dirty for
 * on-screen prompts.
 */
static gboolean
gst_shellint_module_handle_key_event(
	GstInputHandler *handler,
	guint            keyval,
	guint            keycode,
	guint            state
){
	GstShellintModule *self;
	gint cur_row;
	gint target_row;
	GstTerminal *term;

	(void)keycode;
	self = GST_SHELLINT_MODULE(handler);
	term = (GstTerminal *)gst_module_manager_get_terminal(gst_module_manager_get_default());
	if (term == NULL || gst_terminal_is_altscreen(term) || self->zones == NULL)
		return FALSE;

	/* Only handle Ctrl+Shift combinations */
	if (!(state & ControlMask) || !(state & ShiftMask)) {
		self->navigation_row = UNKNOWN_ROW;
		return FALSE;
	}

	cur_row = self->navigation_row;
	if (cur_row == UNKNOWN_ROW) {
		GstModule *history;
		const GstHistoryApi *api = gst_history_lookup(&history);
		cur_row = get_current_cursor_row() + 1;
		if (api != NULL && api->offset(history) > 0)
			cur_row = -api->offset(history);
	}

	switch (keyval) {
	case XK_Up:
		target_row = find_prev_prompt(self, cur_row);
		break;
	case XK_Down:
		target_row = find_next_prompt(self, cur_row);
		break;
	default:
		return FALSE;
	}

	if (target_row == UNKNOWN_ROW) {
		/* No prompt found in that direction */
		return TRUE;
	}

	/*
	 * Change only the history viewport, never the application's cursor.
	 */
	{
		GstModule *history;
		const GstHistoryApi *api;

		api = gst_history_lookup(&history);
		if (api != NULL)
			api->set_offset(history, -target_row);
		self->navigation_row = target_row;
		mark_all_dirty();
	}

	return TRUE;
}

/*
 * handle_mouse_event:
 *
 * This module does not handle mouse events. Return FALSE
 * to pass through to the next handler.
 */
static gboolean
gst_shellint_module_handle_mouse_event(
	GstInputHandler *handler,
	guint            button,
	guint            state,
	gint             col,
	gint             row
){
	(void)handler;
	(void)button;
	(void)state;
	(void)col;
	(void)row;

	return FALSE;
}

static void
gst_shellint_module_input_handler_init(GstInputHandlerInterface *iface)
{
	iface->handle_key_event = gst_shellint_module_handle_key_event;
	iface->handle_mouse_event = gst_shellint_module_handle_mouse_event;
}

/* ===== GstRenderOverlay interface ===== */

/*
 * render:
 *
 * Renders prompt markers and exit code indicators as an overlay.
 *
 * For each zone whose prompt_row is visible on screen:
 *  - A small colored rectangle is drawn in the left margin
 *  - Green marker for successful commands (exit code 0) or
 *    incomplete zones
 *  - Red marker for non-zero exit codes
 *
 * The marker is drawn at the leftmost pixels of the prompt row,
 * within the border padding area.
 */
static void
gst_shellint_module_render(
	GstRenderOverlay *overlay,
	gpointer          render_context,
	gint              width,
	gint              height
){
	GstShellintModule *self;
	GstRenderContext *ctx;
	GstModuleManager *mgr;
	GstTerminal *term;
	gint rows;
	guint i;

	(void)width;
	(void)height;

	self = GST_SHELLINT_MODULE(overlay);
	ctx = (GstRenderContext *)render_context;

	if (!self->mark_prompts && !self->show_exit_code) {
		return;
	}

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return;
	}

	if (gst_terminal_is_altscreen(term) || self->zones == NULL)
		return;
	rows = gst_terminal_get_rows(term);

	for (i = 0; i < self->zones->len; i++) {
		GstSemanticZone *zone;
		gint row;
		gint pixel_y;
		guint8 r;
		guint8 g;
		guint8 b;
		gboolean is_error;

		zone = &g_array_index(self->zones, GstSemanticZone, i);

		/* Only render if the prompt row is visible */
		{
			GstModule *history;
			const GstHistoryApi *api = gst_history_lookup(&history);
			row = zone->prompt_row + (api != NULL ? api->offset(history) : 0);
		}
		if (row < 0 || row >= rows) {
			continue;
		}

		/* Determine marker color based on exit code */
		is_error = (zone->exit_code > 0);

		if (is_error && self->show_exit_code) {
			/* Red marker for non-zero exit code */
			r = self->error_r;
			g = self->error_g;
			b = self->error_b;
		} else if (self->mark_prompts) {
			/* Green marker for prompts */
			r = DEFAULT_MARKER_R;
			g = DEFAULT_MARKER_G;
			b = DEFAULT_MARKER_B;
		} else {
			continue;
		}

		/*
		 * Draw the marker in the left border area. Position it
		 * at x=1 to leave a 1px gap from the window edge.
		 */
		pixel_y = ctx->borderpx + row * ctx->ch;

		gst_render_context_fill_rect_rgba(ctx,
			1, pixel_y,
			MARKER_WIDTH, ctx->ch,
			r, g, b, 255);
	}
}

static void
gst_shellint_module_render_overlay_init(GstRenderOverlayInterface *iface)
{
	iface->render = gst_shellint_module_render;
}

/* ===== GstModule vfuncs ===== */

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 */
static const gchar *
gst_shellint_module_get_name(GstModule *module)
{
	(void)module;
	return "shell_integration";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
gst_shellint_module_get_description(GstModule *module)
{
	(void)module;
	return "Shell integration via OSC 133 semantic zones";
}

/*
 * activate:
 *
 * Activates the shell integration module. Allocates the zone
 * array and connects to the terminal's "line-scrolled-out"
 * signal to track row adjustments.
 */
static gboolean
gst_shellint_module_activate(GstModule *module)
{
	GstShellintModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;

	self = GST_SHELLINT_MODULE(module);

	/* Initialize zone array if not already present */
	self->navigation_row = UNKNOWN_ROW;
	if (self->zones == NULL) {
		self->zones = g_array_new(FALSE, TRUE,
			sizeof(GstSemanticZone));
	}

	/* Connect to terminal's line-scrolled-out signal */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term != NULL) {
		self->scroll_sig_id = g_signal_connect_object(term,
			"line-scrolled-out",
			G_CALLBACK(on_line_scrolled_out), self, 0);
		self->resize_sig_id = g_signal_connect_object(term, "resize",
			G_CALLBACK(on_history_resize), self, 0);
	}

	g_debug("shell_integration: activated (mark_prompts=%d, "
		"show_exit_code=%d)",
		self->mark_prompts, self->show_exit_code);
	return TRUE;
}

/*
 * deactivate:
 *
 * Deactivates the shell integration module. Disconnects from
 * the line-scrolled-out signal and frees the zone array.
 */
static void
gst_shellint_module_deactivate(GstModule *module)
{
	GstShellintModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;

	self = GST_SHELLINT_MODULE(module);

	/* Disconnect signal */
	if (self->scroll_sig_id != 0) {
		mgr = gst_module_manager_get_default();
		term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
		if (term != NULL) {
			g_signal_handler_disconnect(term,
				self->scroll_sig_id);
		}
		self->scroll_sig_id = 0;
	}

	/* Free zone array */
	if (self->resize_sig_id != 0) {
		mgr = gst_module_manager_get_default();
		term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
		if (term != NULL)
			g_signal_handler_disconnect(term, self->resize_sig_id);
		self->resize_sig_id = 0;
	}
	if (self->zones != NULL) {
		g_array_free(self->zones, TRUE);
		self->zones = NULL;
	}

	g_debug("shell_integration: deactivated");
}

/*
 * configure:
 *
 * Reads shell integration configuration from the YAML config:
 *  - mark_prompts: whether to render prompt markers (default: true)
 *  - show_exit_code: whether to show exit code indicators (default: true)
 *  - error_color: hex color for error indicators (default: "#ef2929")
 */
static void
gst_shellint_module_configure(GstModule *module, gpointer config)
{
	GstShellintModule *self;
	GstConfig *cfg;

	self = GST_SHELLINT_MODULE(module);
	cfg = (GstConfig *)config;

	self->mark_prompts = cfg->modules.shell_integration.mark_prompts;
	self->show_exit_code = cfg->modules.shell_integration.show_exit_code;
	self->retention = CLAMP(cfg->modules.scrollback.lines, 1, 1000000);

	/* Parse error_color if provided */
	{
		const gchar *color_str;

		color_str = cfg->modules.shell_integration.error_color;
		if (color_str == NULL ||
		    !parse_hex_color(color_str,
			&self->error_r, &self->error_g, &self->error_b))
		{
			g_warning("shell_integration: invalid error_color "
				"'%s', using default",
				color_str != NULL ? color_str : "(null)");
			self->error_r = DEFAULT_ERROR_R;
			self->error_g = DEFAULT_ERROR_G;
			self->error_b = DEFAULT_ERROR_B;
		}
	}

	g_debug("shell_integration: configured (mark_prompts=%d, "
		"show_exit_code=%d, error_color=#%02x%02x%02x)",
		self->mark_prompts, self->show_exit_code,
		self->error_r, self->error_g, self->error_b);
}

/* ===== GObject lifecycle ===== */

/**
 * gst_shellint_module_dup_output:
 * @self: a shell integration module
 * @error: (out) (optional): error return
 *
 * Extracts the navigated command's completed output, or the most recent
 * completed command when no prompt has been selected. Evicted prefixes are
 * rejected rather than silently exporting incomplete output. OSC C/D cell
 * boundaries exclude the command and following prompt.
 *
 * Returns: (transfer full) (nullable): UTF-8 output, or %NULL on failure
 */
gchar *
gst_shellint_module_dup_output(GstShellintModule *self, GError **error)
{
	GstTerminal *term;
	GstSemanticZone *zone;
	GString *output;
	gint i;
	gint row;

	g_return_val_if_fail(GST_IS_SHELLINT_MODULE(self), NULL);
	term = (GstTerminal *)gst_module_manager_get_terminal(gst_module_manager_get_default());
	zone = NULL;
	if (self->navigation_row != UNKNOWN_ROW && self->navigation_row < -self->retention) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"The selected command has been evicted from scrollback");
		return NULL;
	}
	if (self->zones != NULL) {
		for (i = (gint)self->zones->len - 1; i >= 0; i--) {
			GstSemanticZone *candidate = &g_array_index(self->zones, GstSemanticZone, i);
			if (self->navigation_row != UNKNOWN_ROW &&
				candidate->prompt_row != self->navigation_row)
				continue;
			if (candidate->output_row != UNKNOWN_ROW && candidate->end_row != UNKNOWN_ROW) {
				zone = candidate;
				break;
			}
		}
	}
	if (term == NULL || zone == NULL || gst_terminal_is_altscreen(term)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			"No completed command output is available on the primary screen");
		return NULL;
	}
	if (zone->end_row < zone->output_row ||
		(zone->end_row == zone->output_row && zone->end_col < zone->output_col)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			"Command output boundaries were overwritten by cursor movement");
		return NULL;
	}
	output = g_string_new(NULL);
	for (row = zone->output_row; row <= zone->end_row; row++) {
		const GstLine *line;
		g_autofree gchar *text = NULL;
		gint start;
		gint end;
		gboolean wrapped;

		line = gst_history_line(term, row);
		if (line == NULL) {
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				"Command output has been evicted from scrollback");
			g_string_free(output, TRUE);
			return NULL;
		}
		start = row == zone->output_row ? zone->output_col : 0;
		end = row == zone->end_row ? zone->end_col : line->len;
		wrapped = line->len > 0 &&
			(line->glyphs[line->len - 1].attr & GST_GLYPH_ATTR_WRAP) != 0;
		text = gst_line_to_string_range(line, start, end);
		/* Preserve spaces at soft wraps and exact final-cell boundaries. */
		if (row != zone->end_row && !wrapped) {
			gsize length = strlen(text);
			while (length > 0 && text[length - 1] == ' ')
				text[--length] = '\0';
		}
		if (output->len + strlen(text) + 1 > 16 * 1024 * 1024) {
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
				"Command output exceeds the 16 MiB export limit");
			g_string_free(output, TRUE);
			return NULL;
		}
		g_string_append(output, text);
		if (row != zone->end_row && !wrapped)
			g_string_append_c(output, '\n');
	}
	return g_string_free(output, FALSE);
}

/**
 * gst_shellint_module_copy_output:
 * @self: a shell integration module
 * @error: (out) (optional): error return
 *
 * Copies completed output to the window's CLIPBOARD selection.
 * Returns: %TRUE when output was handed to the window
 */
gboolean
gst_shellint_module_copy_output(GstShellintModule *self, GError **error)
{
	GstWindow *window;
	g_autofree gchar *text = NULL;

	text = gst_shellint_module_dup_output(self, error);
	if (text == NULL)
		return FALSE;
	window = (GstWindow *)gst_module_manager_get_window(gst_module_manager_get_default());
	if (window == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "No terminal window is available");
		return FALSE;
	}
	gst_window_set_selection(window, text, TRUE);
	return TRUE;
}

/* The child owns only its private filename, never a module/terminal pointer. */
static void
editor_finished(GObject *source, GAsyncResult *result, gpointer data)
{
	g_autoptr(GError) error = NULL;
	gchar *path = (gchar *)data;

	if (!g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error))
		g_warning("shell_integration: editor: %s", error->message);
	if (g_unlink(path) != 0)
		g_warning("shell_integration: could not remove output file %s", path);
	g_free(path);
}

/**
 * gst_shellint_module_export_output:
 * @self: a shell integration module
 * @editor: editor command and arguments, parsed without a shell
 * @error: (out) (optional): error return
 *
 * Opens a private temporary output file in an asynchronous editor process.
 * The editor must wait until the file is no longer needed (emacsclient does
 * so by default). Shell functions and shell metacharacters are not expanded.
 * Returns: %TRUE if the editor was started
 */
gboolean
gst_shellint_module_export_output(GstShellintModule *self,
	const gchar *editor, GError **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *path = NULL;
	g_auto(GStrv) argv = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileIOStream) stream = NULL;
	g_autoptr(GSubprocess) process = NULL;
	gint argc;
	gboolean written;

	text = gst_shellint_module_dup_output(self, error);
	if (text == NULL)
		return FALSE;
	if (editor == NULL || *editor == '\0') {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"Configure an editor executable, for example emacsclient");
		return FALSE;
	}
	if (!g_shell_parse_argv(editor, &argc, &argv, error))
		return FALSE;
	file = g_file_new_tmp("gst-command-XXXXXX", &stream, error);
	if (file == NULL)
		return FALSE;
	path = g_file_get_path(file);
	written = g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(stream)),
		text, strlen(text), NULL, NULL, error);
	if (!g_io_stream_close(G_IO_STREAM(stream), NULL, written ? error : NULL))
		written = FALSE;
	if (written) {
		argv = g_realloc_n(argv, (gsize)argc + 2, sizeof(gchar *));
		argv[argc] = g_strdup(path);
		argv[argc + 1] = NULL;
		process = g_subprocess_newv((const gchar * const *)argv,
			G_SUBPROCESS_FLAGS_NONE, error);
	}
	if (process == NULL) {
		g_unlink(path);
		return FALSE;
	}
	g_subprocess_wait_check_async(process, NULL, editor_finished, g_steal_pointer(&path));
	return TRUE;
}

/* GObject action signals let the driver bind actions without linking an
 * optional module into the executable. Detailed failures go to stderr. */
static gboolean
copy_output_action(GstShellintModule *self)
{
	g_autoptr(GError) error = NULL;
	gboolean ok = gst_shellint_module_copy_output(self, &error);
	if (!ok)
		g_warning("shell_integration: %s", error->message);
	return ok;
}

static gboolean
export_output_action(GstShellintModule *self, const gchar *editor)
{
	g_autoptr(GError) error = NULL;
	gboolean ok = gst_shellint_module_export_output(self, editor, &error);
	if (!ok)
		g_warning("shell_integration: %s", error->message);
	return ok;
}

static void
gst_shellint_module_dispose(GObject *object)
{
	GstShellintModule *self;

	self = GST_SHELLINT_MODULE(object);

	gst_shellint_module_deactivate(GST_MODULE(self));
	G_OBJECT_CLASS(gst_shellint_module_parent_class)->dispose(object);
}

static void
gst_shellint_module_class_init(GstShellintModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_shellint_module_dispose;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_shellint_module_get_name;
	module_class->get_description = gst_shellint_module_get_description;
	module_class->activate = gst_shellint_module_activate;
	module_class->deactivate = gst_shellint_module_deactivate;
	module_class->configure = gst_shellint_module_configure;
	g_signal_new_class_handler("copy-command-output", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK(copy_output_action),
		NULL, NULL, NULL, G_TYPE_BOOLEAN, 0);
	g_signal_new_class_handler("export-command-output", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_CALLBACK(export_output_action),
		NULL, NULL, NULL, G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
}

static void
gst_shellint_module_init(GstShellintModule *self)
{
	self->zones = NULL;
	self->scroll_sig_id = 0;
	self->retention = 10000;
	self->navigation_row = UNKNOWN_ROW;
	self->mark_prompts = DEFAULT_MARK_PROMPTS;
	self->show_exit_code = DEFAULT_SHOW_EXIT_CODE;
	self->error_r = DEFAULT_ERROR_R;
	self->error_g = DEFAULT_ERROR_G;
	self->error_b = DEFAULT_ERROR_B;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 * Returns the GType so the manager can instantiate the module.
 *
 * Returns: The #GType for #GstShellintModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_SHELLINT_MODULE;
}
