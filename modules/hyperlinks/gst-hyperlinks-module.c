/*
 * gst-hyperlinks-module.c - OSC 8 explicit hyperlink module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements OSC 8 explicit hyperlinks per the spec:
 *   https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda
 *
 * OSC 8 open: ESC ] 8 ; params ; uri ST  -- sets active URI
 * OSC 8 close: ESC ] 8 ; ; ST            -- clears active URI
 *
 * The raw OSC buffer arrives as "8;params;uri" (open) or "8;;" (close).
 *
 * Spans are tracked as (start_row, start_col, end_row, end_col, uri_idx)
 * so Ctrl+click can look up which URI the user clicked on. The module
 * also renders an underline overlay on the hovered URI span.
 *
 * Implements:
 *  - GstEscapeHandler: intercept OSC 8 sequences
 *  - GstInputHandler:  Ctrl+click to open the URI under the mouse
 *  - GstRenderOverlay: underline the hovered URI span
 */

#include "gst-hyperlinks-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/boxed/gst-cursor.h"
#include "../../src/rendering/gst-render-context.h"

#include <X11/X.h>
#include <string.h>

/**
 * SECTION:gst-hyperlinks-module
 * @title: GstHyperlinksModule
 * @short_description: OSC 8 explicit hyperlink support
 *
 * #GstHyperlinksModule handles OSC 8 escape sequences to provide
 * clickable hyperlinks in the terminal. URIs are tracked per-span
 * and can be opened with Ctrl+click. Hovered spans are underlined
 * via the render overlay interface.
 */

/* Maximum number of URI spans to prevent unbounded memory growth */
#define MAX_SPANS (10000)

/* Maximum number of unique URIs to store */
#define MAX_URIS (5000)

/*
 * HyperlinkSpan:
 * @start_row: row where the link text begins
 * @start_col: column where the link text begins
 * @end_row: row where the link text ends
 * @end_col: column where the link text ends (exclusive)
 * @uri_idx: index into the URI table (GPtrArray)
 *
 * Tracks a contiguous region of text associated with a single URI.
 * A new span is created each time OSC 8 opens a URI and closed
 * when OSC 8 clears it.
 */
typedef struct
{
	gint start_row;
	gint start_col;
	gint end_row;
	gint end_col;
	guint uri_idx;
} HyperlinkSpan;

struct _GstHyperlinksModule
{
	GstModule parent_instance;

	gchar      *opener;             /* opener command (default: "xdg-open") */
	guint       modifier_mask;      /* modifier for click-to-open (default: ControlMask) */
	gboolean    underline_hover;    /* whether to underline hovered span */

	GPtrArray  *uris;               /* URI string table (gchar*), deduplicated */
	GArray     *spans;              /* array of HyperlinkSpan */

	/* Active span state: set when OSC 8 opens a URI, cleared on close */
	gboolean    span_open;          /* TRUE if a span is currently being built */
	guint       active_uri_idx;     /* URI index of the active (open) span */

	/* Hover state for underline rendering */
	gint        hover_col;          /* mouse column position (-1 = unknown) */
	gint        hover_row;          /* mouse row position (-1 = unknown) */

	/* Signal handler IDs for disconnection on deactivate */
	gulong      sig_scrolled;       /* "line-scrolled-out" handler */
};

/* Forward declarations for interface init functions */
static void
gst_hyperlinks_module_escape_init(GstEscapeHandlerInterface *iface);
static void
gst_hyperlinks_module_input_init(GstInputHandlerInterface *iface);
static void
gst_hyperlinks_module_overlay_init(GstRenderOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstHyperlinksModule, gst_hyperlinks_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_hyperlinks_module_escape_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER,
		gst_hyperlinks_module_input_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_hyperlinks_module_overlay_init))

/* ===== Internal helpers ===== */

/*
 * find_or_add_uri:
 * @self: the hyperlinks module
 * @uri: the URI string to look up or add
 *
 * Searches the URI table for an existing entry matching @uri.
 * If found, returns its index. Otherwise adds @uri to the table
 * and returns the new index. Enforces MAX_URIS limit by refusing
 * new entries once full (returns G_MAXUINT on overflow).
 *
 * Returns: index into self->uris, or G_MAXUINT on failure
 */
static guint
find_or_add_uri(
	GstHyperlinksModule *self,
	const gchar         *uri
){
	guint i;

	/* Deduplicate: check if URI already exists */
	for (i = 0; i < self->uris->len; i++) {
		const gchar *existing;

		existing = (const gchar *)g_ptr_array_index(self->uris, i);
		if (g_strcmp0(existing, uri) == 0) {
			return i;
		}
	}

	/* Refuse new entries if at capacity */
	if (self->uris->len >= MAX_URIS) {
		g_warning("hyperlinks: URI table full (%d), ignoring new URI",
			MAX_URIS);
		return G_MAXUINT;
	}

	g_ptr_array_add(self->uris, g_strdup(uri));
	return self->uris->len - 1;
}

/*
 * close_active_span:
 * @self: the hyperlinks module
 * @term: the terminal (used to read cursor position for span end)
 *
 * Closes the currently active hyperlink span by recording the
 * current cursor position as the span's end point. If no span
 * is open, this is a no-op.
 */
static void
close_active_span(
	GstHyperlinksModule *self,
	GstTerminal         *term
){
	HyperlinkSpan *span;
	GstCursor *cursor;

	if (!self->span_open) {
		return;
	}

	self->span_open = FALSE;

	if (self->spans->len == 0) {
		return;
	}

	/* Finalize the last span's end position */
	span = &g_array_index(self->spans, HyperlinkSpan,
		self->spans->len - 1);
	cursor = gst_terminal_get_cursor(term);
	if (cursor != NULL) {
		span->end_row = cursor->y;
		span->end_col = cursor->x;
	}
}

/*
 * evict_oldest_spans:
 * @self: the hyperlinks module
 *
 * When the span array exceeds MAX_SPANS, removes the oldest half
 * to keep memory usage bounded.
 */
static void
evict_oldest_spans(GstHyperlinksModule *self)
{
	guint to_remove;

	if (self->spans->len <= MAX_SPANS) {
		return;
	}

	to_remove = self->spans->len / 2;
	g_array_remove_range(self->spans, 0, to_remove);
	g_debug("hyperlinks: evicted %u oldest spans", to_remove);
}

/*
 * find_span_at:
 * @self: the hyperlinks module
 * @row: terminal row to check
 * @col: terminal column to check
 *
 * Searches for a hyperlink span containing the given (row, col)
 * position. Spans can be multi-row. A position is "inside" a span
 * if it falls between (start_row, start_col) and (end_row, end_col).
 *
 * Returns: pointer to the matching HyperlinkSpan, or NULL
 */
static HyperlinkSpan *
find_span_at(
	GstHyperlinksModule *self,
	gint                 row,
	gint                 col
){
	guint i;

	/* Search backwards (most recent spans first = most likely hit) */
	for (i = self->spans->len; i > 0; i--) {
		HyperlinkSpan *sp;

		sp = &g_array_index(self->spans, HyperlinkSpan, i - 1);

		/* Single-row span */
		if (sp->start_row == sp->end_row) {
			if (row == sp->start_row &&
			    col >= sp->start_col &&
			    col < sp->end_col)
			{
				return sp;
			}
			continue;
		}

		/* Multi-row span: check if position is within range */
		if (row < sp->start_row || row > sp->end_row) {
			continue;
		}

		/* First row: must be at or after start_col */
		if (row == sp->start_row && col >= sp->start_col) {
			return sp;
		}

		/* Last row: must be before end_col */
		if (row == sp->end_row && col < sp->end_col) {
			return sp;
		}

		/* Middle rows: always inside */
		if (row > sp->start_row && row < sp->end_row) {
			return sp;
		}
	}

	return NULL;
}

/*
 * on_line_scrolled_out:
 *
 * Signal callback for "line-scrolled-out". Adjusts all span row
 * positions downward by one and removes spans that have scrolled
 * entirely off the screen (end_row < 0).
 */
static void
on_line_scrolled_out(
	GstTerminal *term,
	gpointer     line,
	gint         cols,
	gpointer     user_data
){
	GstHyperlinksModule *self;
	guint i;
	guint write_idx;

	(void)term;
	(void)line;
	(void)cols;

	self = GST_HYPERLINKS_MODULE(user_data);

	/* Shift all span rows up by one */
	write_idx = 0;
	for (i = 0; i < self->spans->len; i++) {
		HyperlinkSpan *sp;

		sp = &g_array_index(self->spans, HyperlinkSpan, i);
		sp->start_row--;
		sp->end_row--;

		/* Keep spans that are still (partially) visible */
		if (sp->end_row >= 0) {
			if (write_idx != i) {
				g_array_index(self->spans, HyperlinkSpan,
					write_idx) = *sp;
			}
			write_idx++;
		}
	}

	/* Truncate removed entries */
	if (write_idx < self->spans->len) {
		g_array_set_size(self->spans, write_idx);
	}
}

/* ===== GstEscapeHandler interface ===== */

/*
 * handle_escape_string:
 *
 * Intercepts OSC sequences. OSC 8 arrives as:
 *   buf = "8;params;uri"  (open) or "8;;"  (close)
 *   str_type = ']' (OSC)
 *
 * On open: records the current cursor position as span start,
 *          stores the URI in the dedup table.
 * On close: records the current cursor position as span end.
 */
static gboolean
gst_hyperlinks_module_handle_escape_string(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstHyperlinksModule *self;
	GstTerminal *term;
	const gchar *first_semi;
	const gchar *second_semi;
	const gchar *uri;
	gsize uri_len;
	guint uri_idx;
	GstCursor *cursor;
	HyperlinkSpan span;

	self = GST_HYPERLINKS_MODULE(handler);

	/* Only handle OSC sequences */
	if (str_type != ']') {
		return FALSE;
	}

	/*
	 * Check that the buffer starts with "8;" which identifies OSC 8.
	 * Minimum buffer: "8;;" (3 bytes for close) or "8;;x" (4+ for open).
	 */
	if (len < 3 || buf[0] != '8' || buf[1] != ';') {
		return FALSE;
	}

	term = (GstTerminal *)terminal;

	/*
	 * Parse the OSC 8 payload: "8;params;uri"
	 * The first semicolon is at buf[1]. Find the second semicolon
	 * which separates params from URI.
	 */
	first_semi = buf + 1;
	second_semi = memchr(first_semi + 1, ';', len - 2);

	if (second_semi == NULL) {
		/* Malformed: no second semicolon. Ignore. */
		g_debug("hyperlinks: malformed OSC 8 (no second semicolon)");
		return TRUE;
	}

	uri = second_semi + 1;
	uri_len = len - (gsize)(uri - buf);

	/*
	 * Close case: URI is empty (buf = "8;;")
	 * Just close the active span.
	 */
	if (uri_len == 0 || uri[0] == '\0') {
		close_active_span(self, term);
		return TRUE;
	}

	/*
	 * Open case: URI is non-empty.
	 * If a span is already open, close it first.
	 */
	close_active_span(self, term);

	/* Add or find the URI in the dedup table */
	{
		g_autofree gchar *uri_str = NULL;

		uri_str = g_strndup(uri, uri_len);
		uri_idx = find_or_add_uri(self, uri_str);
	}

	if (uri_idx == G_MAXUINT) {
		return TRUE;
	}

	/* Record the span start at the current cursor position */
	cursor = gst_terminal_get_cursor(term);
	if (cursor == NULL) {
		return TRUE;
	}

	memset(&span, 0, sizeof(span));
	span.start_row = cursor->y;
	span.start_col = cursor->x;
	span.end_row = cursor->y;
	span.end_col = cursor->x;
	span.uri_idx = uri_idx;

	g_array_append_val(self->spans, span);
	self->span_open = TRUE;
	self->active_uri_idx = uri_idx;

	/* Evict old spans if over the limit */
	evict_oldest_spans(self);

	return TRUE;
}

static void
gst_hyperlinks_module_escape_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string =
		gst_hyperlinks_module_handle_escape_string;
}

/* ===== GstInputHandler interface ===== */

/*
 * handle_key_event:
 *
 * Hyperlinks module does not consume any key events.
 * Required by the GstInputHandler interface contract.
 */
static gboolean
gst_hyperlinks_module_handle_key_event(
	GstInputHandler *handler G_GNUC_UNUSED,
	guint            keyval G_GNUC_UNUSED,
	guint            keycode G_GNUC_UNUSED,
	guint            state G_GNUC_UNUSED
){
	return FALSE;
}

/*
 * handle_mouse_event:
 *
 * On button-1 (left click) with the configured modifier held (default:
 * Ctrl), checks if the click position falls inside a hyperlink span.
 * If so, opens the associated URI via the configured opener command.
 *
 * Also tracks mouse position for hover underline rendering on any
 * mouse motion (button 0 = motion event dispatched by module manager).
 */
static gboolean
gst_hyperlinks_module_handle_mouse_event(
	GstInputHandler *handler,
	guint            button,
	guint            state,
	gint             col,
	gint             row
){
	GstHyperlinksModule *self;
	HyperlinkSpan *sp;
	const gchar *uri;
	g_autofree gchar *cmd = NULL;
	GError *error = NULL;

	self = GST_HYPERLINKS_MODULE(handler);

	/*
	 * Track mouse position for hover underlining.
	 * Button 0 is used as a sentinel for motion events in some
	 * dispatch paths. Always update hover state regardless.
	 */
	if (self->hover_col != col || self->hover_row != row) {
		self->hover_col = col;
		self->hover_row = row;
	}

	/* Only handle left-click (button 1) */
	if (button != 1) {
		return FALSE;
	}

	/* Check that the required modifier is held */
	if ((state & self->modifier_mask) != self->modifier_mask) {
		return FALSE;
	}

	/* Look up the span at the click position */
	sp = find_span_at(self, row, col);
	if (sp == NULL) {
		return FALSE;
	}

	/* Resolve the URI from the index */
	if (sp->uri_idx >= self->uris->len) {
		return FALSE;
	}

	uri = (const gchar *)g_ptr_array_index(self->uris, sp->uri_idx);
	if (uri == NULL || uri[0] == '\0') {
		return FALSE;
	}

	/* Open the URI with the configured opener */
	if (self->opener == NULL || self->opener[0] == '\0') {
		g_warning("hyperlinks: no opener command configured");
		return TRUE;
	}

	cmd = g_strdup_printf("%s '%s'", self->opener, uri);
	g_message("hyperlinks: opening URI: %s", uri);

	if (!g_spawn_command_line_async(cmd, &error)) {
		g_warning("hyperlinks: failed to open URI: %s",
			error->message);
		g_error_free(error);
		return TRUE;
	}

	return TRUE;
}

static void
gst_hyperlinks_module_input_init(GstInputHandlerInterface *iface)
{
	iface->handle_key_event =
		gst_hyperlinks_module_handle_key_event;
	iface->handle_mouse_event =
		gst_hyperlinks_module_handle_mouse_event;
}

/* ===== GstRenderOverlay interface ===== */

/*
 * render_span_underline:
 * @ctx: render context
 * @sp: the hyperlink span to underline
 * @cols: terminal column count
 *
 * Draws a 1-pixel underline beneath each cell of the given span.
 * Uses the default foreground color (index 256).
 */
static void
render_span_underline(
	GstRenderContext *ctx,
	HyperlinkSpan   *sp,
	gint             cols
){
	gint y;
	gint underline_h;

	/* Underline thickness: 1 pixel, positioned at bottom of cell */
	underline_h = 1;

	for (y = sp->start_row; y <= sp->end_row; y++) {
		gint col_start;
		gint col_end;
		gint px;
		gint py;
		gint pw;

		/* Determine column range for this row */
		if (y == sp->start_row) {
			col_start = sp->start_col;
		} else {
			col_start = 0;
		}

		if (y == sp->end_row) {
			col_end = sp->end_col;
		} else {
			col_end = cols;
		}

		if (col_end <= col_start) {
			continue;
		}

		px = ctx->borderpx + col_start * ctx->cw;
		py = ctx->borderpx + y * ctx->ch + ctx->ch - underline_h;
		pw = (col_end - col_start) * ctx->cw;

		/* Draw underline using default foreground (index 256) */
		gst_render_context_fill_rect(ctx, px, py, pw, underline_h, 256);
	}
}

/*
 * render:
 *
 * Called after the main terminal render pass. If underline_hover is
 * enabled and the mouse is hovering over a hyperlink span, draws
 * an underline beneath the span's text.
 */
static void
gst_hyperlinks_module_render(
	GstRenderOverlay *overlay,
	gpointer          render_context,
	gint              width,
	gint              height
){
	GstHyperlinksModule *self;
	GstRenderContext *ctx;
	GstModuleManager *mgr;
	GstTerminal *term;
	HyperlinkSpan *sp;
	gint cols;

	(void)width;
	(void)height;

	self = GST_HYPERLINKS_MODULE(overlay);

	if (!self->underline_hover) {
		return;
	}

	/* No hover position tracked yet */
	if (self->hover_row < 0 || self->hover_col < 0) {
		return;
	}

	/* Find span under hover position */
	sp = find_span_at(self, self->hover_row, self->hover_col);
	if (sp == NULL) {
		return;
	}

	ctx = (GstRenderContext *)render_context;
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return;
	}

	cols = gst_terminal_get_cols(term);

	render_span_underline(ctx, sp, cols);
}

static void
gst_hyperlinks_module_overlay_init(GstRenderOverlayInterface *iface)
{
	iface->render = gst_hyperlinks_module_render;
}

/* ===== GstModule vfuncs ===== */

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 */
static const gchar *
gst_hyperlinks_module_get_name(GstModule *module)
{
	(void)module;
	return "hyperlinks";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
gst_hyperlinks_module_get_description(GstModule *module)
{
	(void)module;
	return "OSC 8 explicit hyperlinks with click-to-open";
}

/*
 * activate:
 *
 * Allocates span and URI storage, connects to the terminal's
 * "line-scrolled-out" signal for scroll management.
 */
static gboolean
gst_hyperlinks_module_activate(GstModule *module)
{
	GstHyperlinksModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;

	self = GST_HYPERLINKS_MODULE(module);

	/* Allocate storage if not already present */
	if (self->uris == NULL) {
		self->uris = g_ptr_array_new_with_free_func(g_free);
	}
	if (self->spans == NULL) {
		self->spans = g_array_new(FALSE, TRUE,
			sizeof(HyperlinkSpan));
	}

	self->span_open = FALSE;
	self->hover_col = -1;
	self->hover_row = -1;

	/* Connect to terminal signals for scroll management */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term != NULL) {
		self->sig_scrolled = g_signal_connect(term,
			"line-scrolled-out",
			G_CALLBACK(on_line_scrolled_out), self);
	}

	g_debug("hyperlinks: activated (opener=%s)", self->opener);
	return TRUE;
}

/*
 * deactivate:
 *
 * Disconnects signals and frees span/URI storage.
 */
static void
gst_hyperlinks_module_deactivate(GstModule *module)
{
	GstHyperlinksModule *self;

	self = GST_HYPERLINKS_MODULE(module);

	/* Disconnect terminal signals */
	if (self->sig_scrolled != 0) {
		GstModuleManager *mgr;
		GstTerminal *term;

		mgr = gst_module_manager_get_default();
		term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
		if (term != NULL) {
			g_signal_handler_disconnect(term, self->sig_scrolled);
		}
		self->sig_scrolled = 0;
	}

	/* Clear span and URI data */
	if (self->spans != NULL) {
		g_array_set_size(self->spans, 0);
	}
	if (self->uris != NULL) {
		g_ptr_array_set_size(self->uris, 0);
	}

	self->span_open = FALSE;
	self->hover_col = -1;
	self->hover_row = -1;

	g_debug("hyperlinks: deactivated");
}

/*
 * configure:
 *
 * Reads hyperlinks configuration from the YAML config:
 *  - opener: command to open URIs (default: "xdg-open")
 *  - modifier: modifier key name for click-to-open (default: "Ctrl")
 *  - underline_hover: whether to underline hovered spans (default: true)
 */
static void
gst_hyperlinks_module_configure(GstModule *module, gpointer config)
{
	GstHyperlinksModule *self;
	GstConfig *cfg;

	self = GST_HYPERLINKS_MODULE(module);
	cfg = (GstConfig *)config;

	/* Opener command */
	if (cfg->modules.hyperlinks.opener != NULL &&
	    cfg->modules.hyperlinks.opener[0] != '\0')
	{
		g_free(self->opener);
		self->opener = g_strdup(cfg->modules.hyperlinks.opener);
	}

	/* Modifier key */
	if (cfg->modules.hyperlinks.modifier != NULL)
	{
		const gchar *val;

		val = cfg->modules.hyperlinks.modifier;

		/*
		 * Map modifier name to X11 mask value.
		 * ControlMask = 1<<2 = 4, ShiftMask = 1<<0 = 1,
		 * Mod1Mask (Alt) = 1<<3 = 8.
		 */
		if (g_ascii_strcasecmp(val, "Ctrl") == 0 ||
		    g_ascii_strcasecmp(val, "Control") == 0)
		{
			self->modifier_mask = ControlMask;
		}
		else if (g_ascii_strcasecmp(val, "Shift") == 0)
		{
			self->modifier_mask = ShiftMask;
		}
		else if (g_ascii_strcasecmp(val, "Alt") == 0 ||
		         g_ascii_strcasecmp(val, "Mod1") == 0)
		{
			self->modifier_mask = Mod1Mask;
		}
		else
		{
			g_warning("hyperlinks: unknown modifier '%s', "
				"using Ctrl", val);
		}
	}

	/* Underline hover */
	self->underline_hover = cfg->modules.hyperlinks.underline_hover;

	g_debug("hyperlinks: configured (opener=%s, modifier=0x%x, "
		"underline_hover=%d)",
		self->opener, self->modifier_mask, self->underline_hover);
}

/* ===== GObject lifecycle ===== */

static void
gst_hyperlinks_module_dispose(GObject *object)
{
	GstHyperlinksModule *self;

	self = GST_HYPERLINKS_MODULE(object);

	g_clear_pointer(&self->opener, g_free);

	if (self->uris != NULL) {
		g_ptr_array_unref(self->uris);
		self->uris = NULL;
	}

	if (self->spans != NULL) {
		g_array_unref(self->spans);
		self->spans = NULL;
	}

	G_OBJECT_CLASS(gst_hyperlinks_module_parent_class)->dispose(object);
}

static void
gst_hyperlinks_module_class_init(GstHyperlinksModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_hyperlinks_module_dispose;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_hyperlinks_module_get_name;
	module_class->get_description = gst_hyperlinks_module_get_description;
	module_class->activate = gst_hyperlinks_module_activate;
	module_class->deactivate = gst_hyperlinks_module_deactivate;
	module_class->configure = gst_hyperlinks_module_configure;
}

static void
gst_hyperlinks_module_init(GstHyperlinksModule *self)
{
	self->opener = g_strdup("xdg-open");
	self->modifier_mask = ControlMask;
	self->underline_hover = TRUE;

	self->uris = g_ptr_array_new_with_free_func(g_free);
	self->spans = g_array_new(FALSE, TRUE, sizeof(HyperlinkSpan));

	self->span_open = FALSE;
	self->active_uri_idx = 0;
	self->hover_col = -1;
	self->hover_row = -1;
	self->sig_scrolled = 0;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 * Returns the GType so the manager can instantiate the module.
 *
 * Returns: The #GType for #GstHyperlinksModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_HYPERLINKS_MODULE;
}
