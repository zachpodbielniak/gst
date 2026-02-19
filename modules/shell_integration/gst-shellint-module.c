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
 * The module stores a dynamic array of semantic zones, adjusts
 * row indices when lines scroll out of the buffer, provides
 * Ctrl+Shift+Up/Down navigation between prompts, and renders
 * small colored markers in the left margin at prompt rows.
 */

#include "gst-shellint-module.h"
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
 * @prompt_row: row where the prompt starts (OSC 133;A), -1 if unknown
 * @command_row: row where the command starts (OSC 133;B), -1 if unknown
 * @output_row: row where command output starts (OSC 133;C), -1 if unknown
 * @end_row: row where the command completed (OSC 133;D), -1 if unknown
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
} GstSemanticZone;

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

/* ===== YAML config helpers ===== */

/*
 * cfg_get_bool:
 *
 * Reads a boolean from a YAML mapping with a default fallback.
 */
static gboolean
cfg_get_bool(YamlMapping *map, const gchar *key, gboolean def)
{
	if (map == NULL || !yaml_mapping_has_member(map, key)) {
		return def;
	}
	return yaml_mapping_get_boolean_member(map, key);
}

/*
 * cfg_get_string:
 *
 * Reads a string from a YAML mapping with a default fallback.
 */
static const gchar *
cfg_get_string(YamlMapping *map, const gchar *key, const gchar *def)
{
	if (map == NULL || !yaml_mapping_has_member(map, key)) {
		return def;
	}
	return yaml_mapping_get_string_member(map, key);
}

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
 * Signal callback for "line-scrolled-out". Decrements all row
 * indices in the zone array by one and removes zones that have
 * scrolled entirely off-screen (all rows < 0).
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

		if (zone->prompt_row >= 0)  zone->prompt_row--;
		if (zone->command_row >= 0) zone->command_row--;
		if (zone->output_row >= 0)  zone->output_row--;
		if (zone->end_row >= 0)     zone->end_row--;
	}

	/*
	 * Remove zones that have completely scrolled off. A zone is
	 * considered gone when all its set rows have gone negative.
	 * Walk backwards to safely remove during iteration.
	 */
	for (i = self->zones->len; i > 0; i--) {
		GstSemanticZone *zone;
		gboolean all_gone;

		zone = &g_array_index(self->zones, GstSemanticZone, i - 1);

		all_gone = TRUE;
		if (zone->prompt_row >= 0)  all_gone = FALSE;
		if (zone->command_row >= 0) all_gone = FALSE;
		if (zone->output_row >= 0)  all_gone = FALSE;
		if (zone->end_row >= 0)     all_gone = FALSE;

		if (all_gone) {
			g_array_remove_index(self->zones, i - 1);
		}
	}
}

/* ===== GstEscapeHandler interface ===== */

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

	(void)terminal;

	self = GST_SHELLINT_MODULE(handler);

	/* Only handle OSC sequences */
	if (str_type != ']') {
		return FALSE;
	}

	if (buf == NULL || len < 4) {
		return FALSE;
	}

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

	cur_row = get_current_cursor_row();

	switch (subcmd) {
	case 'A': {
		/* Prompt start: create a new zone */
		GstSemanticZone zone;

		zone.prompt_row = cur_row;
		zone.command_row = -1;
		zone.output_row = -1;
		zone.end_row = -1;
		zone.exit_code = -1;
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
			exit_code = (gint)strtol(rest + 2, NULL, 10);
		}

		if (self->zones->len > 0) {
			GstSemanticZone *zone;

			zone = &g_array_index(self->zones,
				GstSemanticZone, self->zones->len - 1);
			zone->end_row = cur_row;
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

	best = -1;

	for (i = 0; i < self->zones->len; i++) {
		GstSemanticZone *zone;

		zone = &g_array_index(self->zones, GstSemanticZone, i);
		if (zone->prompt_row >= 0 && zone->prompt_row < current_row) {
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
		if (zone->prompt_row >= 0 && zone->prompt_row > current_row) {
			return zone->prompt_row;
		}
	}

	return -1;
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

	(void)keycode;

	/* Only handle Ctrl+Shift combinations */
	if (!(state & ControlMask) || !(state & ShiftMask)) {
		return FALSE;
	}

	self = GST_SHELLINT_MODULE(handler);

	cur_row = get_current_cursor_row();
	if (cur_row < 0) {
		cur_row = 0;
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

	if (target_row < 0) {
		/* No prompt found in that direction */
		return TRUE;
	}

	/*
	 * Move the cursor to the target prompt row. We set the
	 * cursor position directly so the terminal view follows.
	 * Column 0 puts the cursor at the start of the prompt.
	 */
	{
		GstModuleManager *mgr;
		GstTerminal *term;

		mgr = gst_module_manager_get_default();
		term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
		if (term != NULL) {
			gst_terminal_set_cursor_pos(term, 0, target_row);
			mark_all_dirty();
		}
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
		row = zone->prompt_row;
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
	if (self->zones == NULL) {
		self->zones = g_array_new(FALSE, TRUE,
			sizeof(GstSemanticZone));
	}

	/* Connect to terminal's line-scrolled-out signal */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term != NULL) {
		self->scroll_sig_id = g_signal_connect(term,
			"line-scrolled-out",
			G_CALLBACK(on_line_scrolled_out), self);
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
	YamlMapping *mod_cfg;

	self = GST_SHELLINT_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "shell_integration");
	if (mod_cfg == NULL) {
		g_debug("shell_integration: no config section, "
			"using defaults");
		return;
	}

	self->mark_prompts = cfg_get_bool(mod_cfg,
		"mark_prompts", DEFAULT_MARK_PROMPTS);
	self->show_exit_code = cfg_get_bool(mod_cfg,
		"show_exit_code", DEFAULT_SHOW_EXIT_CODE);

	/* Parse error_color if provided */
	{
		const gchar *color_str;

		color_str = cfg_get_string(mod_cfg,
			"error_color", "#ef2929");
		if (!parse_hex_color(color_str,
			&self->error_r, &self->error_g, &self->error_b))
		{
			g_warning("shell_integration: invalid error_color "
				"'%s', using default", color_str);
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

static void
gst_shellint_module_dispose(GObject *object)
{
	GstShellintModule *self;

	self = GST_SHELLINT_MODULE(object);

	if (self->zones != NULL) {
		g_array_free(self->zones, TRUE);
		self->zones = NULL;
	}

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
}

static void
gst_shellint_module_init(GstShellintModule *self)
{
	self->zones = NULL;
	self->scroll_sig_id = 0;
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
