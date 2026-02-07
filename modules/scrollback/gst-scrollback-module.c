/*
 * gst-scrollback-module.c - Scrollback buffer module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ring buffer scrollback with keyboard navigation and overlay rendering.
 * Captures lines via the "line-scrolled-out" signal, stores them in a
 * ring buffer, and renders history using GstRenderOverlay when the
 * user scrolls back with Shift+Page_Up/Down.
 */

#include "gst-scrollback-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/boxed/gst-glyph.h"
#include "../../src/rendering/gst-render-context.h"

/* keysym values and modifier masks for scrollback navigation */
#include <X11/keysym.h>
#include <X11/X.h>
#include <string.h>

/**
 * SECTION:gst-scrollback-module
 * @title: GstScrollbackModule
 * @short_description: Scrollback buffer with keyboard navigation
 *
 * #GstScrollbackModule maintains a ring buffer of scrolled-out lines
 * and provides keyboard navigation (Shift+PgUp/PgDn/Home/End) to
 * view history. When scrolled back, the module renders the history
 * lines as an overlay on the terminal surface.
 */

/*
 * ScrollLine:
 * @glyphs: array of glyph data for the line
 * @cols: number of columns in this line
 *
 * A saved scrollback line. Stores a copy of the glyph data.
 */
typedef struct
{
	GstGlyph *glyphs;
	gint      cols;
} ScrollLine;

struct _GstScrollbackModule
{
	GstModule parent_instance;

	ScrollLine *lines;          /* ring buffer of saved lines */
	gint        capacity;       /* max lines (from config) */
	gint        count;          /* lines currently stored */
	gint        head;           /* write position in ring */
	gint        scroll_offset;  /* 0=live, >0=viewing history */
	gint        scroll_lines;   /* lines per mouse scroll step */
	gulong      sig_id;         /* signal handler ID for disconnection */
};

/* Forward declarations */
static void
gst_scrollback_module_input_init(GstInputHandlerInterface *iface);
static void
gst_scrollback_module_overlay_init(GstRenderOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstScrollbackModule, gst_scrollback_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER,
		gst_scrollback_module_input_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_scrollback_module_overlay_init))

/* ===== Internal helpers ===== */

/*
 * on_line_scrolled_out:
 *
 * Signal callback for "line-scrolled-out". Copies glyph data
 * from the scrolling-out line into the ring buffer.
 */
static void
on_line_scrolled_out(
	GstTerminal *term,
	GstLine     *line,
	gint         cols,
	gpointer     user_data
){
	GstScrollbackModule *self;
	ScrollLine *sl;
	gint x;

	self = GST_SCROLLBACK_MODULE(user_data);

	/* Get the slot at the write head */
	sl = &self->lines[self->head];

	/* Free previous data if slot was occupied */
	g_free(sl->glyphs);

	/* Copy glyph data from the line */
	sl->cols = cols;
	sl->glyphs = g_new0(GstGlyph, (gsize)cols);

	for (x = 0; x < cols; x++) {
		GstGlyph *g;

		g = gst_line_get_glyph(line, x);
		if (g != NULL) {
			sl->glyphs[x] = *g;
		}
	}

	/* Advance head in ring buffer */
	self->head = (self->head + 1) % self->capacity;
	if (self->count < self->capacity) {
		self->count++;
	}
}

/*
 * mark_all_dirty:
 *
 * Marks all terminal lines as dirty to force a full redraw.
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

/* ===== GstInputHandler interface ===== */

/*
 * handle_key_event:
 *
 * Handles scrollback navigation keys:
 *  Shift+Page_Up:   scroll up by terminal rows
 *  Shift+Page_Down: scroll down by terminal rows
 *  Shift+Home:      scroll to top of history
 *  Shift+End:       scroll to live view
 */
static gboolean
gst_scrollback_module_handle_key_event(
	GstInputHandler *handler,
	guint            keyval,
	guint            keycode,
	guint            state
){
	GstScrollbackModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;
	gint rows;
	gint old_offset;

	/* Only handle Shift+key combinations */
	if (!(state & ShiftMask)) {
		return FALSE;
	}

	self = GST_SCROLLBACK_MODULE(handler);
	old_offset = self->scroll_offset;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	rows = (term != NULL) ? gst_terminal_get_rows(term) : 24;

	switch (keyval) {
	case XK_Page_Up:
		self->scroll_offset += rows;
		break;
	case XK_Page_Down:
		self->scroll_offset -= rows;
		break;
	case XK_Home:
		self->scroll_offset = self->count;
		break;
	case XK_End:
		self->scroll_offset = 0;
		break;
	default:
		return FALSE;
	}

	/* Clamp scroll offset */
	if (self->scroll_offset < 0) {
		self->scroll_offset = 0;
	}
	if (self->scroll_offset > self->count) {
		self->scroll_offset = self->count;
	}

	/* If offset changed, trigger redraw */
	if (self->scroll_offset != old_offset) {
		mark_all_dirty();
	}

	return TRUE;
}

static void
gst_scrollback_module_input_init(GstInputHandlerInterface *iface)
{
	iface->handle_key_event = gst_scrollback_module_handle_key_event;
}

/* ===== GstRenderOverlay interface ===== */

/*
 * render:
 *
 * When scroll_offset > 0, renders scrollback history lines
 * as an overlay. Draws stored glyph data using the font cache
 * and XftDraw from the render context.
 */
static void
gst_scrollback_module_render(
	GstRenderOverlay *overlay,
	gpointer          render_context,
	gint              width,
	gint              height
){
	GstScrollbackModule *self;
	GstRenderContext *ctx;
	GstModuleManager *mgr;
	GstTerminal *term;
	gint rows;
	gint cols;
	gint y;
	gchar indicator[64];
	gint ind_len;

	self = GST_SCROLLBACK_MODULE(overlay);

	if (self->scroll_offset <= 0) {
		return;
	}

	ctx = (GstRenderContext *)render_context;
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return;
	}

	gst_terminal_get_size(term, &cols, &rows);

	/* Clear the drawable with background color (index 257 = default bg) */
	gst_render_context_fill_rect(ctx, 0, 0, width, height, 257);

	/* Render scrollback lines */
	for (y = 0; y < rows; y++) {
		gint ring_idx;
		ScrollLine *sl;
		gint x;
		gint pixel_y;

		/*
		 * Map visible row y to ring buffer index.
		 * Row 0 = oldest visible, row (rows-1) = newest visible.
		 * The line at head-scroll_offset+y wraps around.
		 */
		ring_idx = (self->head - self->scroll_offset + y + self->capacity)
			% self->capacity;

		/* Check if this index is valid (within stored lines) */
		if (y >= (gint)self->scroll_offset) {
			/* This row is in the live terminal area, skip overlay */
			break;
		}

		sl = &self->lines[ring_idx];
		if (sl->glyphs == NULL) {
			continue;
		}

		pixel_y = ctx->borderpx + y * ctx->ch;

		/* Draw each glyph in the line */
		for (x = 0; x < sl->cols && x < cols; x++) {
			GstGlyph *g;
			gint pixel_x;

			g = &sl->glyphs[x];
			if (g->rune == 0) {
				continue;
			}

			if (g->attr & GST_GLYPH_ATTR_WDUMMY) {
				continue;
			}

			pixel_x = ctx->borderpx + x * ctx->cw;

			/* Draw background */
			gst_render_context_fill_rect(ctx,
				pixel_x, pixel_y,
				ctx->cw, ctx->ch, g->bg);

			/* Draw glyph via abstract dispatch */
			gst_render_context_draw_glyph(ctx,
				g->rune, GST_FONT_STYLE_NORMAL,
				pixel_x, pixel_y,
				g->fg, g->bg, g->attr);
		}
	}

	/* Draw scroll indicator at top-right */
	ind_len = g_snprintf(indicator, sizeof(indicator),
		"[%d/%d]", self->scroll_offset, self->count);
	if (ind_len > 0) {
		gint ind_x;
		gint ind_y;
		gint i;

		ind_x = width - ind_len * ctx->cw - ctx->borderpx;
		ind_y = ctx->borderpx;

		for (i = 0; i < ind_len; i++) {
			gst_render_context_draw_glyph(ctx,
				(GstRune)indicator[i], GST_FONT_STYLE_NORMAL,
				ind_x + i * ctx->cw, ind_y,
				256, 257, 0);
		}
	}
}

static void
gst_scrollback_module_overlay_init(GstRenderOverlayInterface *iface)
{
	iface->render = gst_scrollback_module_render;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_scrollback_module_get_name(GstModule *module)
{
	(void)module;
	return "scrollback";
}

static const gchar *
gst_scrollback_module_get_description(GstModule *module)
{
	(void)module;
	return "Scrollback buffer with keyboard navigation";
}

/*
 * activate:
 *
 * Allocates the ring buffer and connects to the terminal's
 * "line-scrolled-out" signal.
 */
static gboolean
gst_scrollback_module_activate(GstModule *module)
{
	GstScrollbackModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;

	self = GST_SCROLLBACK_MODULE(module);

	/* Allocate ring buffer */
	self->lines = g_new0(ScrollLine, (gsize)self->capacity);
	self->count = 0;
	self->head = 0;
	self->scroll_offset = 0;

	/* Connect to terminal's line-scrolled-out signal */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term != NULL) {
		self->sig_id = g_signal_connect(term, "line-scrolled-out",
			G_CALLBACK(on_line_scrolled_out), self);
	}

	g_debug("scrollback: activated (capacity=%d)", self->capacity);
	return TRUE;
}

/*
 * deactivate:
 *
 * Disconnects from the signal and frees the ring buffer.
 */
static void
gst_scrollback_module_deactivate(GstModule *module)
{
	GstScrollbackModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;
	gint i;

	self = GST_SCROLLBACK_MODULE(module);

	/* Disconnect signal */
	if (self->sig_id != 0) {
		mgr = gst_module_manager_get_default();
		term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
		if (term != NULL) {
			g_signal_handler_disconnect(term, self->sig_id);
		}
		self->sig_id = 0;
	}

	/* Free ring buffer */
	if (self->lines != NULL) {
		for (i = 0; i < self->capacity; i++) {
			g_free(self->lines[i].glyphs);
		}
		g_free(self->lines);
		self->lines = NULL;
	}

	self->count = 0;
	self->head = 0;
	self->scroll_offset = 0;

	g_debug("scrollback: deactivated");
}

/*
 * configure:
 *
 * Reads scrollback configuration from the YAML config:
 *  - lines: ring buffer capacity (clamped to 100-1000000)
 *  - mouse_scroll_lines: lines per mouse scroll step (clamped to 1-100)
 */
static void
gst_scrollback_module_configure(GstModule *module, gpointer config)
{
	GstScrollbackModule *self;
	YamlMapping *mod_cfg;

	self = GST_SCROLLBACK_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "scrollback");
	if (mod_cfg == NULL)
	{
		g_debug("scrollback: no config section, using defaults");
		return;
	}

	if (yaml_mapping_has_member(mod_cfg, "lines"))
	{
		gint64 val;

		val = yaml_mapping_get_int_member(mod_cfg, "lines");
		if (val < 100) val = 100;
		if (val > 1000000) val = 1000000;
		self->capacity = (gint)val;
	}

	if (yaml_mapping_has_member(mod_cfg, "mouse_scroll_lines"))
	{
		gint64 val;

		val = yaml_mapping_get_int_member(mod_cfg, "mouse_scroll_lines");
		if (val < 1) val = 1;
		if (val > 100) val = 100;
		self->scroll_lines = (gint)val;
	}

	g_debug("scrollback: configured (capacity=%d, scroll_lines=%d)",
		self->capacity, self->scroll_lines);
}

/* ===== GObject lifecycle ===== */

static void
gst_scrollback_module_dispose(GObject *object)
{
	GstScrollbackModule *self;
	gint i;

	self = GST_SCROLLBACK_MODULE(object);

	if (self->lines != NULL) {
		for (i = 0; i < self->capacity; i++) {
			g_free(self->lines[i].glyphs);
		}
		g_free(self->lines);
		self->lines = NULL;
	}

	G_OBJECT_CLASS(gst_scrollback_module_parent_class)->dispose(object);
}

static void
gst_scrollback_module_class_init(GstScrollbackModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_scrollback_module_dispose;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_scrollback_module_get_name;
	module_class->get_description = gst_scrollback_module_get_description;
	module_class->activate = gst_scrollback_module_activate;
	module_class->deactivate = gst_scrollback_module_deactivate;
	module_class->configure = gst_scrollback_module_configure;
}

static void
gst_scrollback_module_init(GstScrollbackModule *self)
{
	self->lines = NULL;
	self->capacity = 10000;
	self->count = 0;
	self->head = 0;
	self->scroll_offset = 0;
	self->scroll_lines = 3;
	self->sig_id = 0;
}

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_SCROLLBACK_MODULE;
}
