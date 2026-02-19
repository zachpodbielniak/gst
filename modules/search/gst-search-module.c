/*
 * gst-search-module.c - Interactive scrollback text search module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interactive text search for terminal content. When activated via
 * keybind (default Ctrl+Shift+f), intercepts key input for:
 *  - Printable characters: append to search query
 *  - Backspace: delete last character from query
 *  - Enter: jump to next match
 *  - Shift+Enter: jump to previous match
 *  - Escape: deactivate search mode
 *
 * Matches are found using plain text (g_strstr_len) or GRegex
 * depending on configuration. Results are highlighted as
 * semi-transparent overlays, with the current match shown in a
 * distinct color. A search bar at the bottom displays the query
 * string and match count.
 */

#include "gst-search-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/boxed/gst-glyph.h"
#include "../../src/rendering/gst-render-context.h"

/* keysym values and modifier masks */
#include <X11/keysym.h>
#include <X11/X.h>
#include <string.h>
#include <stdio.h>

/**
 * SECTION:gst-search-module
 * @title: GstSearchModule
 * @short_description: Interactive scrollback text search with highlighting
 *
 * #GstSearchModule provides interactive text search through the visible
 * terminal buffer. When activated, it intercepts keyboard input for
 * building a search query, highlights all matches with semi-transparent
 * overlay rectangles, and allows navigating between matches with
 * Enter / Shift+Enter.
 */

/* Maximum length of the search query string */
#define GST_SEARCH_MAX_QUERY_LEN (256)

/*
 * SearchMatch:
 * @line_idx: terminal row index where the match occurs
 * @col_start: starting column of the match (inclusive)
 * @col_end: ending column of the match (exclusive)
 *
 * Represents a single search match location in the terminal buffer.
 */
typedef struct
{
	gint line_idx;
	gint col_start;
	gint col_end;
} SearchMatch;

struct _GstSearchModule
{
	GstModule parent_instance;

	gboolean  active;             /* whether search mode is on */
	GString  *query;              /* current search query text */
	GArray   *matches;            /* array of SearchMatch results */
	gint      current_match_idx;  /* index of the focused match, -1 = none */

	/* highlight color config (RGBA components) */
	guint8    hl_r;               /* highlight red */
	guint8    hl_g;               /* highlight green */
	guint8    hl_b;               /* highlight blue */
	guint8    hl_a;               /* highlight alpha */

	guint8    cur_r;              /* current match red */
	guint8    cur_g;              /* current match green */
	guint8    cur_b;              /* current match blue */
	guint8    cur_a;              /* current match alpha */

	gboolean  match_case;         /* case-sensitive matching */
	gboolean  use_regex;          /* use GRegex instead of plain text */
};

/* Forward declarations for interface implementations */
static void
gst_search_module_input_init(GstInputHandlerInterface *iface);
static void
gst_search_module_overlay_init(GstRenderOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstSearchModule, gst_search_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_INPUT_HANDLER,
		gst_search_module_input_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_search_module_overlay_init))

/* ===== Internal helpers ===== */

/*
 * mark_all_dirty:
 *
 * Marks all terminal lines as dirty to force a full redraw.
 * Called whenever search state changes that affect rendering.
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
 * parse_hex_color:
 * @hex: color string in "#RRGGBB" format
 * @r: (out): red component
 * @g: (out): green component
 * @b: (out): blue component
 *
 * Parses a hex color string into RGB components.
 * Returns TRUE on success, FALSE if the string is malformed.
 */
static gboolean
parse_hex_color(
	const gchar *hex,
	guint8      *r,
	guint8      *g,
	guint8      *b
){
	guint rv;
	guint gv;
	guint bv;

	if (hex == NULL || hex[0] != '#' || strlen(hex) < 7) {
		return FALSE;
	}

	if (sscanf(hex + 1, "%02x%02x%02x", &rv, &gv, &bv) != 3) {
		return FALSE;
	}

	*r = (guint8)rv;
	*g = (guint8)gv;
	*b = (guint8)bv;
	return TRUE;
}

/*
 * perform_search:
 * @self: the search module
 *
 * Searches all visible terminal lines for the current query string.
 * Populates self->matches with SearchMatch entries for each hit.
 * Supports both plain text (case-insensitive by default) and
 * regex matching modes.
 */
static void
perform_search(GstSearchModule *self)
{
	GstModuleManager *mgr;
	GstTerminal *term;
	gint rows;
	gint cols;
	gint y;
	g_autoptr(GRegex) regex = NULL;

	/* Clear previous results */
	g_array_set_size(self->matches, 0);
	self->current_match_idx = -1;

	/* Nothing to search for */
	if (self->query->len == 0) {
		return;
	}

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return;
	}

	gst_terminal_get_size(term, &cols, &rows);

	/* Compile regex if in regex mode */
	if (self->use_regex) {
		GRegexCompileFlags flags;
		GError *err = NULL;

		flags = G_REGEX_OPTIMIZE;
		if (!self->match_case) {
			flags |= G_REGEX_CASELESS;
		}

		regex = g_regex_new(self->query->str, flags, 0, &err);
		if (regex == NULL) {
			g_debug("search: invalid regex '%s': %s",
				self->query->str,
				err != NULL ? err->message : "unknown");
			g_clear_error(&err);
			return;
		}
	}

	/* Search each visible line */
	for (y = 0; y < rows; y++) {
		g_autofree gchar *text = NULL;
		GstLine *line;
		gint text_len;

		line = gst_terminal_get_line(term, y);
		if (line == NULL) {
			continue;
		}

		text = gst_line_to_string(line);
		if (text == NULL) {
			continue;
		}

		text_len = (gint)strlen(text);
		if (text_len == 0) {
			continue;
		}

		if (self->use_regex && regex != NULL) {
			/*
			 * Regex matching: iterate over all matches in the line.
			 * GMatchInfo provides byte offsets; we convert them to
			 * column positions by counting UTF-8 characters.
			 */
			GMatchInfo *match_info = NULL;

			g_regex_match(regex, text, 0, &match_info);
			while (g_match_info_matches(match_info)) {
				gint start_byte;
				gint end_byte;
				SearchMatch m;

				if (g_match_info_fetch_pos(match_info, 0,
					&start_byte, &end_byte))
				{
					/*
					 * Convert byte offsets to column positions.
					 * Count UTF-8 characters up to each offset.
					 */
					m.line_idx = y;
					m.col_start = (gint)g_utf8_pointer_to_offset(
						text, text + start_byte);
					m.col_end = (gint)g_utf8_pointer_to_offset(
						text, text + end_byte);
					g_array_append_val(self->matches, m);
				}

				g_match_info_next(match_info, NULL);
			}

			g_match_info_free(match_info);
		} else {
			/*
			 * Plain text matching: scan the line for all
			 * occurrences of the query string.
			 */
			const gchar *haystack;
			const gchar *needle;
			g_autofree gchar *lower_text = NULL;
			g_autofree gchar *lower_query = NULL;

			if (self->match_case) {
				haystack = text;
				needle = self->query->str;
			} else {
				lower_text = g_utf8_strdown(text, -1);
				lower_query = g_utf8_strdown(self->query->str, -1);
				haystack = lower_text;
				needle = lower_query;
			}

			{
				const gchar *pos;
				gint needle_len;

				needle_len = (gint)strlen(needle);
				pos = haystack;

				while ((pos = g_strstr_len(pos, -1, needle)) != NULL) {
					SearchMatch m;
					gint byte_start;
					gint byte_end;

					byte_start = (gint)(pos - haystack);
					byte_end = byte_start + needle_len;

					/*
					 * Convert byte offsets to column positions
					 * using the original text for correct UTF-8
					 * character counting.
					 */
					m.line_idx = y;
					m.col_start = (gint)g_utf8_pointer_to_offset(
						haystack, haystack + byte_start);
					m.col_end = (gint)g_utf8_pointer_to_offset(
						haystack, haystack + byte_end);

					g_array_append_val(self->matches, m);

					/* Advance past this match to find the next */
					pos = g_utf8_next_char(pos);
				}
			}
		}
	}

	/* If we found matches, focus on the first one */
	if (self->matches->len > 0) {
		self->current_match_idx = 0;
	}
}

/*
 * navigate_match:
 * @self: the search module
 * @direction: +1 for next match, -1 for previous match
 *
 * Moves the current match index forward or backward, wrapping
 * around at the ends of the match list.
 */
static void
navigate_match(
	GstSearchModule *self,
	gint             direction
){
	gint count;

	count = (gint)self->matches->len;
	if (count == 0) {
		return;
	}

	self->current_match_idx += direction;

	/* Wrap around */
	if (self->current_match_idx >= count) {
		self->current_match_idx = 0;
	} else if (self->current_match_idx < 0) {
		self->current_match_idx = count - 1;
	}

	mark_all_dirty();
}

/* ===== GstInputHandler interface ===== */

/*
 * handle_key_event:
 *
 * Handles keyboard events for the search module.
 *
 * When search mode is inactive:
 *  - Ctrl+Shift+f: activate search mode
 *
 * When search mode is active:
 *  - Printable characters: append to query and re-search
 *  - Backspace: delete last query character and re-search
 *  - Enter: navigate to next match
 *  - Shift+Enter: navigate to previous match
 *  - Escape: deactivate search mode
 */
static gboolean
gst_search_module_handle_key_event(
	GstInputHandler *handler,
	guint            keyval,
	guint            keycode,
	guint            state
){
	GstSearchModule *self;
	guint clean_state;

	(void)keycode;

	self = GST_SEARCH_MODULE(handler);

	/*
	 * Strip lock bits (Caps/Num/Scroll Lock) for reliable matching.
	 * Only consider Shift, Control, and Mod1 (Alt).
	 */
	clean_state = state & (ShiftMask | ControlMask | Mod1Mask);

	/* Toggle activation: Ctrl+Shift+f */
	if (!self->active) {
		if (keyval == XK_f &&
			(clean_state & (ControlMask | ShiftMask)) ==
			(ControlMask | ShiftMask))
		{
			self->active = TRUE;
			g_string_truncate(self->query, 0);
			g_array_set_size(self->matches, 0);
			self->current_match_idx = -1;
			mark_all_dirty();
			g_debug("search: activated");
			return TRUE;
		}
		return FALSE;
	}

	/*
	 * Search mode is active - intercept all key events.
	 */

	/* Escape: deactivate search */
	if (keyval == XK_Escape) {
		self->active = FALSE;
		g_string_truncate(self->query, 0);
		g_array_set_size(self->matches, 0);
		self->current_match_idx = -1;
		mark_all_dirty();
		g_debug("search: deactivated");
		return TRUE;
	}

	/* Enter: navigate matches */
	if (keyval == XK_Return || keyval == XK_KP_Enter) {
		if (clean_state & ShiftMask) {
			navigate_match(self, -1);
		} else {
			navigate_match(self, 1);
		}
		return TRUE;
	}

	/* Backspace: delete last character from query */
	if (keyval == XK_BackSpace) {
		if (self->query->len > 0) {
			/*
			 * Remove the last UTF-8 character. Find the
			 * previous character boundary and truncate.
			 */
			gchar *prev;

			prev = g_utf8_find_prev_char(
				self->query->str,
				self->query->str + self->query->len);

			if (prev != NULL) {
				g_string_truncate(self->query,
					(gsize)(prev - self->query->str));
			} else {
				g_string_truncate(self->query, 0);
			}

			perform_search(self);
			mark_all_dirty();
		}
		return TRUE;
	}

	/* Printable characters: append to query */
	{
		gunichar uc;

		uc = 0;

		/*
		 * Convert X11 keysyms to Unicode codepoints.
		 *
		 * XK_ keysyms in the range 0x0020..0x007E map directly to
		 * ASCII. Latin-1 supplement keysyms 0x00A0..0x00FF also map
		 * directly. Control and Alt modifiers suppress character input.
		 */
		if (!(clean_state & ControlMask) && !(clean_state & Mod1Mask)) {
			if (keyval >= 0x20 && keyval <= 0x7E) {
				uc = (gunichar)keyval;
			} else if (keyval >= 0x00A0 && keyval <= 0x00FF) {
				uc = (gunichar)keyval;
			} else if ((keyval & 0xFF000000) == 0x01000000) {
				/*
				 * XKB Unicode keysyms: keysym = 0x01000000 + UCS.
				 * Strip the prefix to get the Unicode codepoint.
				 */
				uc = (gunichar)(keyval & 0x00FFFFFF);
			}
		}

		if (uc != 0 && g_unichar_isprint(uc) &&
			self->query->len < GST_SEARCH_MAX_QUERY_LEN)
		{
			gchar utf8_buf[6];
			gint utf8_len;

			utf8_len = g_unichar_to_utf8(uc, utf8_buf);
			g_string_append_len(self->query, utf8_buf, utf8_len);

			perform_search(self);
			mark_all_dirty();
		}
	}

	/* Always consume key events while search mode is active */
	return TRUE;
}

/*
 * handle_mouse_event:
 *
 * Mouse events are not used by the search module. Returns FALSE
 * to pass them through to other handlers.
 */
static gboolean
gst_search_module_handle_mouse_event(
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
gst_search_module_input_init(GstInputHandlerInterface *iface)
{
	iface->handle_key_event = gst_search_module_handle_key_event;
	iface->handle_mouse_event = gst_search_module_handle_mouse_event;
}

/* ===== GstRenderOverlay interface ===== */

/*
 * render:
 *
 * Renders the search overlay when search mode is active:
 *  1. Semi-transparent highlight rectangles over each match
 *  2. A distinct highlight on the current/focused match
 *  3. A search bar at the bottom with query text and match count
 */
static void
gst_search_module_render(
	GstRenderOverlay *overlay,
	gpointer          render_context,
	gint              width,
	gint              height
){
	GstSearchModule *self;
	GstRenderContext *ctx;
	guint i;
	gchar status[64];
	gint status_len;
	gint bar_y;
	gint bar_h;
	gint text_x;
	gint q_len;
	gint q_idx;

	self = GST_SEARCH_MODULE(overlay);

	if (!self->active) {
		return;
	}

	ctx = (GstRenderContext *)render_context;

	/* ===== Draw match highlight rectangles ===== */

	for (i = 0; i < self->matches->len; i++) {
		SearchMatch *m;
		gint px;
		gint py;
		gint pw;

		m = &g_array_index(self->matches, SearchMatch, i);

		px = ctx->borderpx + m->col_start * ctx->cw;
		py = ctx->borderpx + m->line_idx * ctx->ch;
		pw = (m->col_end - m->col_start) * ctx->cw;

		if ((gint)i == self->current_match_idx) {
			/* Current match: use distinct highlight color */
			gst_render_context_fill_rect_rgba(ctx,
				px, py, pw, ctx->ch,
				self->cur_r, self->cur_g, self->cur_b, self->cur_a);
		} else {
			/* Normal match: use standard highlight color */
			gst_render_context_fill_rect_rgba(ctx,
				px, py, pw, ctx->ch,
				self->hl_r, self->hl_g, self->hl_b, self->hl_a);
		}
	}

	/* ===== Draw search bar at bottom ===== */

	bar_h = ctx->ch + 4;  /* text height + small padding */
	bar_y = height - bar_h;

	/* Semi-transparent dark background for the search bar */
	gst_render_context_fill_rect_rgba(ctx,
		0, bar_y, width, bar_h,
		0x20, 0x20, 0x20, 0xD0);

	/* Draw "Search:" label */
	{
		const gchar *label = "Search:";
		gint label_len;

		label_len = (gint)strlen(label);
		text_x = ctx->borderpx + 2;

		for (i = 0; i < (guint)label_len; i++) {
			gst_render_context_draw_glyph(ctx,
				(GstRune)label[i], GST_FONT_STYLE_BOLD,
				text_x + (gint)i * ctx->cw, bar_y + 2,
				256, 257, 0);
		}

		text_x += label_len * ctx->cw + ctx->cw;
	}

	/* Draw query text */
	q_len = (gint)g_utf8_strlen(self->query->str, -1);
	{
		const gchar *p;

		p = self->query->str;
		for (q_idx = 0; q_idx < q_len; q_idx++) {
			gunichar uc;

			uc = g_utf8_get_char(p);

			gst_render_context_draw_glyph(ctx,
				(GstRune)uc, GST_FONT_STYLE_NORMAL,
				text_x + q_idx * ctx->cw, bar_y + 2,
				256, 257, 0);

			p = g_utf8_next_char(p);
		}

		text_x += q_len * ctx->cw;
	}

	/* Draw blinking cursor indicator after query text */
	gst_render_context_fill_rect_rgba(ctx,
		text_x, bar_y + 2,
		2, ctx->ch,
		0xFF, 0xFF, 0xFF, 0xC0);

	text_x += ctx->cw;

	/* Draw match count status */
	if (self->matches->len > 0) {
		status_len = g_snprintf(status, sizeof(status),
			"[%d/%d]",
			self->current_match_idx + 1,
			(gint)self->matches->len);
	} else if (self->query->len > 0) {
		status_len = g_snprintf(status, sizeof(status), "No matches");
	} else {
		status_len = 0;
	}

	if (status_len > 0) {
		for (i = 0; i < (guint)status_len; i++) {
			gst_render_context_draw_glyph(ctx,
				(GstRune)status[i], GST_FONT_STYLE_NORMAL,
				text_x + (gint)i * ctx->cw, bar_y + 2,
				256, 257, 0);
		}
	}
}

static void
gst_search_module_overlay_init(GstRenderOverlayInterface *iface)
{
	iface->render = gst_search_module_render;
}

/* ===== GstModule vfuncs ===== */

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 */
static const gchar *
gst_search_module_get_name(GstModule *module)
{
	(void)module;
	return "search";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
gst_search_module_get_description(GstModule *module)
{
	(void)module;
	return "Interactive scrollback text search with highlighting";
}

/*
 * activate:
 *
 * Activates the search module. Initializes the match array
 * and resets search state.
 */
static gboolean
gst_search_module_activate(GstModule *module)
{
	GstSearchModule *self;

	self = GST_SEARCH_MODULE(module);

	self->active = FALSE;
	g_string_truncate(self->query, 0);
	g_array_set_size(self->matches, 0);
	self->current_match_idx = -1;

	g_debug("search: activated");
	return TRUE;
}

/*
 * deactivate:
 *
 * Deactivates the search module. Clears all search state.
 */
static void
gst_search_module_deactivate(GstModule *module)
{
	GstSearchModule *self;

	self = GST_SEARCH_MODULE(module);

	self->active = FALSE;
	g_string_truncate(self->query, 0);
	g_array_set_size(self->matches, 0);
	self->current_match_idx = -1;

	g_debug("search: deactivated");
}

/*
 * configure:
 *
 * Reads search configuration from the YAML config:
 *  - highlight_color: hex color for normal match highlights
 *  - highlight_alpha: alpha transparency for normal highlights (0-255)
 *  - current_color: hex color for the focused match highlight
 *  - current_alpha: alpha transparency for the focused highlight (0-255)
 *  - match_case: whether search is case-sensitive
 *  - regex: whether to use regex matching
 */
static void
gst_search_module_configure(GstModule *module, gpointer config)
{
	GstSearchModule *self;
	GstConfig *cfg;

	self = GST_SEARCH_MODULE(module);
	cfg = (GstConfig *)config;

	/* Highlight color */
	if (cfg->modules.search.highlight_color != NULL) {
		guint8 r;
		guint8 g;
		guint8 b;

		if (parse_hex_color(cfg->modules.search.highlight_color,
			&r, &g, &b))
		{
			self->hl_r = r;
			self->hl_g = g;
			self->hl_b = b;
		}
	}

	/* Highlight alpha */
	{
		gint val;

		val = cfg->modules.search.highlight_alpha;
		if (val < 0) val = 0;
		if (val > 255) val = 255;
		self->hl_a = (guint8)val;
	}

	/* Current match color */
	if (cfg->modules.search.current_color != NULL) {
		guint8 r;
		guint8 g;
		guint8 b;

		if (parse_hex_color(cfg->modules.search.current_color,
			&r, &g, &b))
		{
			self->cur_r = r;
			self->cur_g = g;
			self->cur_b = b;
		}
	}

	/* Current match alpha */
	{
		gint val;

		val = cfg->modules.search.current_alpha;
		if (val < 0) val = 0;
		if (val > 255) val = 255;
		self->cur_a = (guint8)val;
	}

	/* Boolean flags */
	self->match_case = cfg->modules.search.match_case;
	self->use_regex = cfg->modules.search.regex;

	g_debug("search: configured (case=%s, regex=%s, "
		"hl=#%02x%02x%02x/%d, cur=#%02x%02x%02x/%d)",
		self->match_case ? "yes" : "no",
		self->use_regex ? "yes" : "no",
		self->hl_r, self->hl_g, self->hl_b, self->hl_a,
		self->cur_r, self->cur_g, self->cur_b, self->cur_a);
}

/* ===== GObject lifecycle ===== */

static void
gst_search_module_dispose(GObject *object)
{
	GstSearchModule *self;

	self = GST_SEARCH_MODULE(object);

	if (self->query != NULL) {
		g_string_free(self->query, TRUE);
		self->query = NULL;
	}

	if (self->matches != NULL) {
		g_array_free(self->matches, TRUE);
		self->matches = NULL;
	}

	G_OBJECT_CLASS(gst_search_module_parent_class)->dispose(object);
}

static void
gst_search_module_class_init(GstSearchModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_search_module_dispose;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_search_module_get_name;
	module_class->get_description = gst_search_module_get_description;
	module_class->activate = gst_search_module_activate;
	module_class->deactivate = gst_search_module_deactivate;
	module_class->configure = gst_search_module_configure;
}

static void
gst_search_module_init(GstSearchModule *self)
{
	self->active = FALSE;
	self->query = g_string_new(NULL);
	self->matches = g_array_new(FALSE, FALSE, sizeof(SearchMatch));
	self->current_match_idx = -1;

	/* Default highlight color: yellow (#ffff00) alpha 100 */
	self->hl_r = 0xFF;
	self->hl_g = 0xFF;
	self->hl_b = 0x00;
	self->hl_a = 100;

	/* Default current match color: orange (#ff8800) alpha 150 */
	self->cur_r = 0xFF;
	self->cur_g = 0x88;
	self->cur_b = 0x00;
	self->cur_a = 150;

	self->match_case = FALSE;
	self->use_regex = FALSE;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 * Returns the GType so the manager can instantiate the module.
 *
 * Returns: The #GType for #GstSearchModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_SEARCH_MODULE;
}
