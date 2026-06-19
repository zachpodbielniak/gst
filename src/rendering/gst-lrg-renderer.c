/*
 * gst-lrg-renderer.c - libregnum (LRG) renderer implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Draws the terminal grid in immediate mode into a raylib window via
 * graylib. Implements the GstRenderer abstract interface. raylib clears
 * the framebuffer every frame, so the whole grid is redrawn each pass;
 * start_draw begins the frame (clear), finish_draw swaps buffers.
 */

#include "gst-lrg-renderer.h"
#include "gst-lrg-render-context.h"
#include "../core/gst-terminal.h"
#include "../core/gst-line.h"
#include "../boxed/gst-glyph.h"
#include "../boxed/gst-cursor.h"
#include "../selection/gst-selection.h"
#include "../module/gst-module-manager.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/**
 * SECTION:gst-lrg-renderer
 * @title: GstLrgRenderer
 * @short_description: libregnum/graylib-based terminal renderer
 *
 * #GstLrgRenderer implements the #GstRenderer interface using graylib
 * (the raylib GObject wrapper) for immediate-mode 2D drawing.
 */

/* Default color names for the 16 ANSI colors (from st config.def.h) */
static const gchar *default_colorname[] = {
	/* 8 normal colors */
	"black", "red3", "green3", "yellow3",
	"blue2", "#c000c0", "cyan3", "gray90",
	/* 8 bright colors */
	"gray50", "red", "green", "yellow",
	"#5c5cff", "magenta", "cyan", "white",
	[255] = 0,
	/* special colors */
	"#cccccc",   /* 256: default foreground */
	"#000000",   /* 257: default background */
	"#555555",   /* 258: cursor foreground (unused usually) */
	"#cccccc",   /* 259: cursor background */
	"#000000",   /* 260: reverse cursor foreground */
	"#cccccc",   /* 261: reverse cursor background */
};

/* Cursor thickness in pixels */
#define GST_CURSOR_THICKNESS (2)

/* Attribute comparison: TRUE if fg/bg/mode differ */
#define ATTRCMP(a, b) ((a).attr != (b).attr || (a).fg != (b).fg || (a).bg != (b).bg)

/* Extract 8-bit RGB from truecolor-encoded fg/bg value */
#define TC_RED(x)   (guint8)(((x) >> 16) & 0xFF)
#define TC_GREEN(x) (guint8)(((x) >>  8) & 0xFF)
#define TC_BLUE(x)  (guint8)(((x)      ) & 0xFF)

struct _GstLrgRenderer
{
	GstRenderer parent_instance;

	/* graylib window (not owned; owned by GstLrgWindow) */
	GstLrgWindow        *lrg_window;
	GrlWindow           *win;

	/* Color palette (262 entries as GstColor RGBA) */
	GstColor            *colors;
	gsize                num_colors;

	/* Font cache (not owned, caller manages lifetime) */
	GstGrlFontCache     *font_cache;

	/* Character cell dimensions (from font cache) */
	gint cw;
	gint ch;
	gint ascent;
	gdouble font_size;

	/* Terminal pixel area (cols*cw, rows*ch) */
	gint tw;
	gint th;

	/* Window pixel dimensions */
	gint win_w;
	gint win_h;

	/* Border padding */
	gint borderpx;

	/* Window mode flags */
	GstWinMode win_mode;

	/* Cursor position (current) */
	gint ocx;
	gint ocy;

	/* Default color indices */
	gint default_fg;
	gint default_bg;
	gint default_cs;
	gint default_rcs;

	/* Selection (for checking selected cells) */
	GstSelection *selection;

	/* Wallpaper state (set during RENDER_BACKGROUND dispatch) */
	gboolean has_wallpaper;
	gdouble wallpaper_bg_alpha;
};

G_DEFINE_TYPE(GstLrgRenderer, gst_lrg_renderer, GST_TYPE_RENDERER)

/* ===== Color helpers ===== */

/*
 * make_grl:
 * @c: GstColor (0xRRGGBBAA)
 * @a: alpha override (0-255)
 *
 * Returns: (transfer full): a new #GrlColor
 */
static GrlColor *
make_grl(GstColor c, guint8 a)
{
	return grl_color_new((guint8)GST_COLOR_R(c), (guint8)GST_COLOR_G(c),
		(guint8)GST_COLOR_B(c), a);
}

/*
 * sixd_to_8bit:
 * @x: 6-level color component (0-5)
 *
 * Returns: 8-bit color value
 */
static guint8
sixd_to_8bit(gint x)
{
	if (x == 0) {
		return 0;
	}
	return (guint8)(0x37 + 0x28 * x);
}

/*
 * parse_color_name:
 * @name: color name or hex string
 * @color: (out): resulting GstColor
 *
 * Returns: TRUE on success
 */
static gboolean
parse_color_name(
	const gchar     *name,
	GstColor        *color
){
	guint r;
	guint g;
	guint b;

	if (name == NULL) {
		return FALSE;
	}

	if (name[0] == '#') {
		gsize len;

		len = strlen(name + 1);
		if (len == 6) {
			if (sscanf(name, "#%02x%02x%02x", &r, &g, &b) == 3) {
				*color = GST_COLOR_RGB((guint8)r, (guint8)g, (guint8)b);
				return TRUE;
			}
		} else if (len == 3) {
			if (sscanf(name, "#%1x%1x%1x", &r, &g, &b) == 3) {
				*color = GST_COLOR_RGB(
					(guint8)(r * 17), (guint8)(g * 17), (guint8)(b * 17));
				return TRUE;
			}
		}
		return FALSE;
	}

	if (g_ascii_strcasecmp(name, "black") == 0) {
		*color = GST_COLOR_RGB(0, 0, 0);
	} else if (g_ascii_strcasecmp(name, "red3") == 0) {
		*color = GST_COLOR_RGB(205, 0, 0);
	} else if (g_ascii_strcasecmp(name, "green3") == 0) {
		*color = GST_COLOR_RGB(0, 205, 0);
	} else if (g_ascii_strcasecmp(name, "yellow3") == 0) {
		*color = GST_COLOR_RGB(205, 205, 0);
	} else if (g_ascii_strcasecmp(name, "blue2") == 0) {
		*color = GST_COLOR_RGB(0, 0, 238);
	} else if (g_ascii_strcasecmp(name, "cyan3") == 0) {
		*color = GST_COLOR_RGB(0, 205, 205);
	} else if (g_ascii_strcasecmp(name, "magenta") == 0) {
		*color = GST_COLOR_RGB(255, 0, 255);
	} else if (g_ascii_strcasecmp(name, "cyan") == 0) {
		*color = GST_COLOR_RGB(0, 255, 255);
	} else if (g_ascii_strcasecmp(name, "white") == 0) {
		*color = GST_COLOR_RGB(255, 255, 255);
	} else if (g_ascii_strcasecmp(name, "gray90") == 0) {
		*color = GST_COLOR_RGB(229, 229, 229);
	} else if (g_ascii_strcasecmp(name, "gray50") == 0) {
		*color = GST_COLOR_RGB(127, 127, 127);
	} else if (g_ascii_strcasecmp(name, "red") == 0) {
		*color = GST_COLOR_RGB(255, 0, 0);
	} else if (g_ascii_strcasecmp(name, "green") == 0) {
		*color = GST_COLOR_RGB(0, 255, 0);
	} else if (g_ascii_strcasecmp(name, "yellow") == 0) {
		*color = GST_COLOR_RGB(255, 255, 0);
	} else {
		return FALSE;
	}

	return TRUE;
}

/*
 * resolve_fg_color:
 *
 * Resolves foreground color accounting for truecolor, bold brightening,
 * and faint dimming.
 */
static GstColor
resolve_fg_color(
	GstLrgRenderer  *self,
	guint32         fg_idx,
	guint16         mode
){
	GstColor fg;

	if (GST_IS_TRUECOLOR(fg_idx)) {
		fg = GST_COLOR_RGB(TC_RED(fg_idx), TC_GREEN(fg_idx), TC_BLUE(fg_idx));
	} else if (fg_idx < self->num_colors) {
		fg = self->colors[fg_idx];
	} else {
		fg = self->colors[self->default_fg];
	}

	if ((mode & GST_GLYPH_ATTR_BOLD) && !(mode & GST_GLYPH_ATTR_FAINT)
	    && !GST_IS_TRUECOLOR(fg_idx) && fg_idx <= 7) {
		fg = self->colors[fg_idx + 8];
	}

	if ((mode & GST_GLYPH_ATTR_FAINT) && !(mode & GST_GLYPH_ATTR_BOLD)) {
		fg = GST_COLOR_RGB(
			(guint8)(GST_COLOR_R(fg) / 2),
			(guint8)(GST_COLOR_G(fg) / 2),
			(guint8)(GST_COLOR_B(fg) / 2));
	}

	return fg;
}

/*
 * resolve_bg_color:
 *
 * Resolves background color from index or truecolor value.
 */
static GstColor
resolve_bg_color(
	GstLrgRenderer  *self,
	guint32         bg_idx
){
	if (GST_IS_TRUECOLOR(bg_idx)) {
		return GST_COLOR_RGB(TC_RED(bg_idx), TC_GREEN(bg_idx), TC_BLUE(bg_idx));
	} else if (bg_idx < self->num_colors) {
		return self->colors[bg_idx];
	}
	return self->colors[self->default_bg];
}

/*
 * lrg_get_opacity:
 *
 * Returns the window opacity (0.0-1.0), or 1.0 when unavailable.
 */
static gdouble
lrg_get_opacity(GstLrgRenderer *self)
{
	if (self->lrg_window != NULL) {
		return gst_lrg_window_get_opacity(self->lrg_window);
	}
	return 1.0;
}

/*
 * lrg_fill_render_context:
 * @self: the renderer
 * @ctx: (out): render context to populate
 *
 * Populates a GstLrgRenderContext with current renderer state for
 * module dispatch.
 */
static void
lrg_fill_render_context(
	GstLrgRenderer       *self,
	GstLrgRenderContext  *ctx
){
	gst_lrg_render_context_init_ops(ctx);

	ctx->base.cw         = self->cw;
	ctx->base.ch         = self->ch;
	ctx->base.borderpx   = self->borderpx;
	ctx->base.win_w      = self->win_w;
	ctx->base.win_h      = self->win_h;
	ctx->base.win_mode   = self->win_mode;
	ctx->base.glyph_attr = 0;
	ctx->base.opacity    = lrg_get_opacity(self);

	ctx->win         = self->win;
	ctx->font_cache  = self->font_cache;
	ctx->font_size   = self->font_size;
	ctx->colors      = self->colors;
	ctx->num_colors  = self->num_colors;
	ctx->fg          = self->colors[self->default_fg];
	ctx->bg          = self->colors[self->default_bg];
}

/* ===== Glyph run drawing ===== */

/*
 * lrg_draw_glyph_run:
 *
 * Draws the background, glyphs and decorations for a run of cells with
 * the same attributes.
 */
static void
lrg_draw_glyph_run(
	GstLrgRenderer  *self,
	GstGlyph        *base,
	GstLine         *line,
	gint            len,
	gint            x,
	gint            y
){
	guint16 mode;
	GstColor fg;
	GstColor bg;
	GstColor temp;
	gint charlen;
	gint winx;
	gint winy;
	gint width;
	gint i;
	gfloat xp;
	g_autoptr(GrlColor) bgcol = NULL;
	g_autoptr(GrlColor) fgcol = NULL;

	if (self->win == NULL || self->font_cache == NULL) {
		return;
	}

	mode = (guint16)base->attr;
	charlen = len * ((mode & GST_GLYPH_ATTR_WIDE) ? 2 : 1);
	winx = self->borderpx + x * self->cw;
	winy = self->borderpx + y * self->ch;
	width = charlen * self->cw;

	fg = resolve_fg_color(self, base->fg, mode);
	bg = resolve_bg_color(self, base->bg);

	if (mode & GST_GLYPH_ATTR_REVERSE) {
		temp = fg;
		fg = bg;
		bg = temp;
	}

	if ((mode & GST_GLYPH_ATTR_BLINK) && (self->win_mode & GST_WIN_MODE_BLINK)) {
		fg = bg;
	}

	if (mode & GST_GLYPH_ATTR_INVISIBLE) {
		fg = bg;
	}

	/* Fill the cell background (opaque; window-level opacity handles
	 * transparency). */
	bgcol = make_grl(bg, 255);
	grl_draw_rectangle(winx, winy, width, self->ch, bgcol);

	/* Render glyphs */
	fgcol = make_grl(fg, 255);
	xp = (gfloat)winx;

	for (i = 0; i < len; i++) {
		GstGlyph *g;
		GstRune rune;
		GstFontStyle fstyle;
		gfloat runewidth;

		g = gst_line_get_glyph(line, x + i);
		if (g == NULL) {
			xp += (gfloat)self->cw;
			continue;
		}

		rune = g->rune;

		if (g->attr & GST_GLYPH_ATTR_WDUMMY) {
			continue;
		}

		runewidth = (gfloat)self->cw;
		if (g->attr & GST_GLYPH_ATTR_WIDE) {
			runewidth = (gfloat)(self->cw * 2);
		}

		fstyle = GST_FONT_STYLE_NORMAL;
		if ((mode & GST_GLYPH_ATTR_ITALIC) && (mode & GST_GLYPH_ATTR_BOLD)) {
			fstyle = GST_FONT_STYLE_BOLD_ITALIC;
		} else if (mode & GST_GLYPH_ATTR_ITALIC) {
			fstyle = GST_FONT_STYLE_ITALIC;
		} else if (mode & GST_GLYPH_ATTR_BOLD) {
			fstyle = GST_FONT_STYLE_BOLD;
		}

		/* Skip blank cells (space) to save draw calls; otherwise draw the
		 * glyph via the cairo-ft atlas (identical to the other backends). */
		if (rune != 0 && rune != (GstRune)' ') {
			gst_grl_font_cache_draw_glyph(self->font_cache, rune, fstyle,
				(gint)xp, winy, fg);
		}

		xp += runewidth;
	}

	/* Underline decoration */
	if (mode & GST_GLYPH_ATTR_UNDERLINE) {
		grl_draw_rectangle(winx, winy + self->ascent + 1, width, 1, fgcol);
	}

	/* Strikethrough decoration */
	if (mode & GST_GLYPH_ATTR_STRUCK) {
		grl_draw_rectangle(winx, winy + 2 * self->ascent / 3, width, 1, fgcol);
	}

	/* Undercurl decoration (sine wave below baseline) */
	if (mode & GST_GLYPH_ATTR_UNDERCURL) {
		gint uc_x;

		for (uc_x = 0; uc_x < width; uc_x++) {
			gint dy;

			dy = (gint)(sin((double)uc_x * M_PI / ((double)self->cw * 0.5)) * 1.5);
			grl_draw_rectangle(winx + uc_x, winy + self->ascent + 1 + dy,
				1, 1, fgcol);
		}
	}
}

/* ===== Virtual method implementations ===== */

/*
 * lrg_renderer_draw_line_impl:
 *
 * Draws a single line, grouping glyphs by attributes.
 */
static void
lrg_renderer_draw_line_impl(
	GstRenderer     *renderer,
	gint            row,
	gint            x1,
	gint            x2
){
	GstLrgRenderer *self;
	GstTerminal *term;
	GstLine *line;
	gint i;
	gint x;
	gint ox;
	GstGlyph *new_glyph;
	GstGlyph base;
	GstGlyph cur;
	guint16 new_mode;
	GstModuleManager *mgr;
	gboolean has_glyph_transformers;
	GstLrgRenderContext gt_ctx;

	self = GST_LRG_RENDERER(renderer);
	term = gst_renderer_get_terminal(renderer);
	if (term == NULL) {
		return;
	}

	line = gst_terminal_get_line(term, row);
	if (line == NULL) {
		return;
	}

	mgr = gst_module_manager_get_default();
	has_glyph_transformers = (mgr != NULL);
	if (has_glyph_transformers) {
		lrg_fill_render_context(self, &gt_ctx);
	}

	i = 0;
	ox = x1;
	memset(&base, 0, sizeof(GstGlyph));

	for (x = x1; x < x2; x++) {
		new_glyph = gst_line_get_glyph(line, x);
		if (new_glyph == NULL) {
			continue;
		}

		new_mode = (guint16)new_glyph->attr;

		if (new_mode & GST_GLYPH_ATTR_WDUMMY) {
			continue;
		}

		cur = *new_glyph;

		if (self->selection != NULL
		    && gst_selection_selected(self->selection, x, row)) {
			cur.attr ^= GST_GLYPH_ATTR_REVERSE;
		}

		/* Let glyph transformers handle non-ASCII codepoints */
		if (has_glyph_transformers && cur.rune > 0x7F) {
			gint pixel_x;
			gint pixel_y;
			GstColor gt_fg_c;
			GstColor gt_bg_c;
			GstColor gt_tmp_c;
			guint16 gt_mode;

			pixel_x = self->borderpx + x * self->cw;
			pixel_y = self->borderpx + row * self->ch;

			gt_mode = (guint16)cur.attr;
			gt_fg_c = resolve_fg_color(self, cur.fg, gt_mode);
			gt_bg_c = resolve_bg_color(self, cur.bg);

			if (gt_mode & GST_GLYPH_ATTR_REVERSE) {
				gt_tmp_c = gt_fg_c;
				gt_fg_c = gt_bg_c;
				gt_bg_c = gt_tmp_c;
			}

			if ((gt_mode & GST_GLYPH_ATTR_BLINK)
			    && (self->win_mode & GST_WIN_MODE_BLINK)) {
				gt_fg_c = gt_bg_c;
			}

			if (gt_mode & GST_GLYPH_ATTR_INVISIBLE) {
				gt_fg_c = gt_bg_c;
			}

			gt_ctx.fg = gt_fg_c;
			gt_ctx.bg = gt_bg_c;
			gt_ctx.base.glyph_attr = gt_mode;
			gt_ctx.base.current_line = line;
			gt_ctx.base.current_col = x;
			gt_ctx.base.current_cols = x2;

			if (gst_module_manager_dispatch_glyph_transform(
				mgr, cur.rune, &gt_ctx.base,
				pixel_x, pixel_y, self->cw, self->ch))
			{
				if (i > 0) {
					lrg_draw_glyph_run(self, &base, line, i, ox, row);
					i = 0;
				}
				ox = x + 1;
				continue;
			}
		}

		if (i > 0 && ATTRCMP(base, cur)) {
			lrg_draw_glyph_run(self, &base, line, i, ox, row);
			i = 0;
		}

		if (i == 0) {
			ox = x;
			base = cur;
		}
		i++;
	}

	if (i > 0) {
		lrg_draw_glyph_run(self, &base, line, i, ox, row);
	}
}

/*
 * lrg_renderer_draw_cursor_impl:
 *
 * Draws the cursor. Because the full frame is redrawn each tick, there is
 * no need to erase the old cursor first.
 */
static void
lrg_renderer_draw_cursor_impl(
	GstRenderer     *renderer,
	gint            cx,
	gint            cy,
	gint            ox,
	gint            oy
){
	GstLrgRenderer *self;
	GstTerminal *term;
	GstGlyph *g;
	GstCursor *cursor;
	GstColor drawcol;
	gint winx;
	gint winy;

	(void)ox;
	(void)oy;

	self = GST_LRG_RENDERER(renderer);
	term = gst_renderer_get_terminal(renderer);
	if (term == NULL || self->win == NULL) {
		return;
	}

	if (gst_terminal_has_mode(term, GST_MODE_HIDE)) {
		return;
	}

	g = gst_terminal_get_glyph(term, cx, cy);
	if (g == NULL) {
		return;
	}

	cursor = gst_terminal_get_cursor(term);
	drawcol = self->colors[self->default_cs];

	winx = self->borderpx + cx * self->cw;
	winy = self->borderpx + cy * self->ch;

	if (self->win_mode & GST_WIN_MODE_FOCUSED) {
		switch (cursor->shape) {
		case GST_CURSOR_SHAPE_BLOCK:
			{
				GstGlyph block_g;
				GstLine *cursor_line;

				block_g = *g;
				block_g.fg = (guint32)self->default_bg;
				block_g.bg = (guint32)self->default_cs;
				block_g.attr = g->attr & (GST_GLYPH_ATTR_BOLD
					| GST_GLYPH_ATTR_ITALIC | GST_GLYPH_ATTR_UNDERLINE
					| GST_GLYPH_ATTR_STRUCK | GST_GLYPH_ATTR_WIDE);

				cursor_line = gst_terminal_get_line(term, cy);
				if (cursor_line != NULL) {
					lrg_draw_glyph_run(self, &block_g, cursor_line, 1, cx, cy);
				}
			}
			break;
		case GST_CURSOR_SHAPE_UNDERLINE:
			{
				g_autoptr(GrlColor) c = make_grl(drawcol, 255);
				grl_draw_rectangle(winx,
					winy + self->ch - GST_CURSOR_THICKNESS,
					self->cw, GST_CURSOR_THICKNESS, c);
			}
			break;
		case GST_CURSOR_SHAPE_BAR:
			{
				g_autoptr(GrlColor) c = make_grl(drawcol, 255);
				grl_draw_rectangle(winx, winy,
					GST_CURSOR_THICKNESS, self->ch, c);
			}
			break;
		}
	} else {
		/* Unfocused: hollow box cursor */
		g_autoptr(GrlColor) c = make_grl(drawcol, 255);

		grl_draw_rectangle(winx, winy, self->cw - 1, 1, c);
		grl_draw_rectangle(winx, winy, 1, self->ch - 1, c);
		grl_draw_rectangle(winx + self->cw - 1, winy, 1, self->ch - 1, c);
		grl_draw_rectangle(winx, winy + self->ch - 1, self->cw, 1, c);
	}
}

/*
 * lrg_renderer_render_impl:
 *
 * Full render pass: redraw every line, the cursor and module overlays.
 * The whole grid is redrawn (no dirty tracking) because raylib cleared
 * the framebuffer in start_draw.
 */
static void
lrg_renderer_render_impl(GstRenderer *renderer)
{
	GstLrgRenderer *self;
	GstTerminal *term;
	GstCursor *cursor;
	gint rows;
	gint cols;
	gint y;
	gint cx;
	gint cy;

	self = GST_LRG_RENDERER(renderer);
	term = gst_renderer_get_terminal(renderer);
	if (term == NULL || self->win == NULL) {
		return;
	}

	gst_terminal_get_size(term, &cols, &rows);
	cursor = gst_terminal_get_cursor(term);
	cx = cursor->x;
	cy = cursor->y;

	/* Dispatch render background to modules (modules using fill_rect; the
	 * 2D LRG backend has no image-based wallpaper). */
	{
		GstModuleManager *mgr;
		GstLrgRenderContext bg_ctx;

		mgr = gst_module_manager_get_default();
		lrg_fill_render_context(self, &bg_ctx);
		bg_ctx.base.has_wallpaper = FALSE;
		bg_ctx.base.wallpaper_bg_alpha = 1.0;
		gst_module_manager_dispatch_render_background(
			mgr, &bg_ctx.base, self->win_w, self->win_h);
		self->has_wallpaper = bg_ctx.base.has_wallpaper;
		self->wallpaper_bg_alpha = bg_ctx.base.wallpaper_bg_alpha;
	}

	/* Draw every line (full redraw each frame) */
	for (y = 0; y < rows; y++) {
		GstLine *line;

		line = gst_terminal_get_line(term, y);
		if (line == NULL) {
			continue;
		}
		lrg_renderer_draw_line_impl(renderer, y, 0, cols);
	}

	/* Draw cursor */
	lrg_renderer_draw_cursor_impl(renderer, cx, cy, self->ocx, self->ocy);
	self->ocx = cx;
	self->ocy = cy;

	/* Dispatch render overlays to modules */
	{
		GstModuleManager *mgr;
		GstLrgRenderContext ctx;

		mgr = gst_module_manager_get_default();
		lrg_fill_render_context(self, &ctx);
		gst_module_manager_dispatch_render_overlay(
			mgr, &ctx.base, self->win_w, self->win_h);
	}

	gst_terminal_clear_dirty(term);
}

/*
 * lrg_renderer_resize_impl:
 *
 * Updates pixel dimensions and refreshes cell metrics on window resize.
 */
static void
lrg_renderer_resize_impl(
	GstRenderer     *renderer,
	guint           width,
	guint           height
){
	GstLrgRenderer *self;
	GstTerminal *term;
	gint cols;
	gint rows;

	self = GST_LRG_RENDERER(renderer);
	self->win_w = (gint)width;
	self->win_h = (gint)height;

	if (self->font_cache != NULL) {
		self->cw = gst_grl_font_cache_get_char_width(self->font_cache);
		self->ch = gst_grl_font_cache_get_char_height(self->font_cache);
		self->ascent = gst_grl_font_cache_get_ascent(self->font_cache);
		self->font_size = gst_grl_font_cache_get_font_size(self->font_cache);
	}

	term = gst_renderer_get_terminal(renderer);
	if (term != NULL) {
		gst_terminal_get_size(term, &cols, &rows);
		self->tw = cols * self->cw;
		self->th = rows * self->ch;
	}
}

/*
 * lrg_renderer_clear_impl:
 *
 * Clears the window to the background color. Only valid inside a frame
 * (between begin_drawing and swap_buffers).
 */
static void
lrg_renderer_clear_impl(GstRenderer *renderer)
{
	GstLrgRenderer *self;
	g_autoptr(GrlColor) bg = NULL;
	guint8 a;

	self = GST_LRG_RENDERER(renderer);
	if (self->win == NULL || self->colors == NULL) {
		return;
	}

	a = (guint8)(lrg_get_opacity(self) * 255.0 + 0.5);
	bg = make_grl(self->colors[self->default_bg], a);
	grl_window_clear_background(self->win, bg);
}

/*
 * lrg_renderer_start_draw_impl:
 *
 * Begins a frame: begin_drawing + clear to background.
 *
 * Returns: TRUE if drawing can proceed
 */
static gboolean
lrg_renderer_start_draw_impl(GstRenderer *renderer)
{
	GstLrgRenderer *self;
	g_autoptr(GrlColor) bg = NULL;
	guint8 a;

	self = GST_LRG_RENDERER(renderer);

	if (!(self->win_mode & GST_WIN_MODE_VISIBLE) || self->win == NULL) {
		return FALSE;
	}

	grl_window_begin_drawing(self->win);

	a = (guint8)(lrg_get_opacity(self) * 255.0 + 0.5);
	if (self->colors != NULL) {
		bg = make_grl(self->colors[self->default_bg], a);
	} else {
		bg = grl_color_new(0, 0, 0, a);
	}
	grl_window_clear_background(self->win, bg);

	return TRUE;
}

/*
 * lrg_renderer_finish_draw_impl:
 *
 * Ends a frame by swapping buffers. Deliberately does NOT call
 * grl_window_end_drawing(): its internal WaitTime would block the GLib
 * main loop (see cmacs lrgterm / gsurf LRG window).
 */
static void
lrg_renderer_finish_draw_impl(GstRenderer *renderer)
{
	GstLrgRenderer *self;

	self = GST_LRG_RENDERER(renderer);
	if (self->win == NULL) {
		return;
	}

	grl_window_swap_buffers(self->win);
}

/*
 * lrg_renderer_capture_screenshot_impl:
 *
 * Screenshot capture is not yet implemented for the LRG backend.
 */
static GBytes *
lrg_renderer_capture_screenshot_impl(
	GstRenderer *renderer,
	gint        *out_width,
	gint        *out_height,
	gint        *out_stride
){
	(void)renderer;
	(void)out_width;
	(void)out_height;
	(void)out_stride;
	return NULL;
}

/* ===== GObject lifecycle ===== */

static void
gst_lrg_renderer_dispose(GObject *object)
{
	GstLrgRenderer *self;

	self = GST_LRG_RENDERER(object);

	g_clear_pointer(&self->colors, g_free);
	self->num_colors = 0;

	g_clear_object(&self->selection);

	G_OBJECT_CLASS(gst_lrg_renderer_parent_class)->dispose(object);
}

static void
gst_lrg_renderer_class_init(GstLrgRendererClass *klass)
{
	GObjectClass *object_class;
	GstRendererClass *renderer_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_lrg_renderer_dispose;

	renderer_class = GST_RENDERER_CLASS(klass);
	renderer_class->render = lrg_renderer_render_impl;
	renderer_class->resize = lrg_renderer_resize_impl;
	renderer_class->clear = lrg_renderer_clear_impl;
	renderer_class->draw_line = lrg_renderer_draw_line_impl;
	renderer_class->draw_cursor = lrg_renderer_draw_cursor_impl;
	renderer_class->start_draw = lrg_renderer_start_draw_impl;
	renderer_class->finish_draw = lrg_renderer_finish_draw_impl;
	renderer_class->capture_screenshot = lrg_renderer_capture_screenshot_impl;
}

static void
gst_lrg_renderer_init(GstLrgRenderer *self)
{
	self->lrg_window = NULL;
	self->win = NULL;
	self->colors = NULL;
	self->num_colors = 0;
	self->font_cache = NULL;
	self->cw = 0;
	self->ch = 0;
	self->ascent = 0;
	self->font_size = 0.0;
	self->tw = 0;
	self->th = 0;
	self->win_w = 0;
	self->win_h = 0;
	self->borderpx = 0;
	self->win_mode = GST_WIN_MODE_NUMLOCK;
	self->ocx = 0;
	self->ocy = 0;
	self->default_fg = GST_COLOR_DEFAULT_FG;
	self->default_bg = GST_COLOR_DEFAULT_BG;
	self->default_cs = GST_COLOR_CURSOR_BG;
	self->default_rcs = GST_COLOR_REVERSE_BG;
	self->selection = NULL;
	self->has_wallpaper = FALSE;
	self->wallpaper_bg_alpha = 1.0;
}

/* ===== Public API ===== */

GstLrgRenderer *
gst_lrg_renderer_new(
	GstTerminal         *terminal,
	GstLrgWindow        *lrg_window,
	GstGrlFontCache     *font_cache,
	gint                borderpx
){
	GstLrgRenderer *self;
	gint cols;
	gint rows;

	self = (GstLrgRenderer *)g_object_new(GST_TYPE_LRG_RENDERER,
		"terminal", terminal,
		NULL);

	self->lrg_window = lrg_window;
	self->win = gst_lrg_window_get_grl_window(lrg_window);
	self->font_cache = font_cache;
	self->borderpx = borderpx;

	self->cw = gst_grl_font_cache_get_char_width(font_cache);
	self->ch = gst_grl_font_cache_get_char_height(font_cache);
	self->ascent = gst_grl_font_cache_get_ascent(font_cache);
	self->font_size = gst_grl_font_cache_get_font_size(font_cache);

	gst_terminal_get_size(terminal, &cols, &rows);
	self->tw = cols * self->cw;
	self->th = rows * self->ch;
	self->win_w = 2 * borderpx + self->tw;
	self->win_h = 2 * borderpx + self->th;

	return self;
}

gboolean
gst_lrg_renderer_load_colors(
	GstLrgRenderer  *self,
	GstConfig       *config
){
	gsize i;
	gsize count;

	g_return_val_if_fail(GST_IS_LRG_RENDERER(self), FALSE);

	g_clear_pointer(&self->colors, g_free);

	count = (gsize)GST_COLOR_COUNT;
	self->colors = g_new0(GstColor, count);
	self->num_colors = count;

	for (i = 0; i < count; i++) {
		const gchar *name;

		name = NULL;

		if (i >= 16 && i <= 255) {
			if (i < 6 * 6 * 6 + 16) {
				guint8 r;
				guint8 g;
				guint8 b;

				r = sixd_to_8bit(((gint)(i - 16) / 36) % 6);
				g = sixd_to_8bit(((gint)(i - 16) / 6) % 6);
				b = sixd_to_8bit(((gint)(i - 16) / 1) % 6);
				self->colors[i] = GST_COLOR_RGB(r, g, b);
			} else {
				guint8 v;

				v = (guint8)(0x08 + 0x0a * (gint)(i - (6 * 6 * 6 + 16)));
				self->colors[i] = GST_COLOR_RGB(v, v, v);
			}
		} else {
			if (i < G_N_ELEMENTS(default_colorname)
			    && default_colorname[i] != NULL) {
				name = default_colorname[i];
			}

			if (name != NULL) {
				if (!parse_color_name(name, &self->colors[i])) {
					self->colors[i] = GST_COLOR_RGB(0, 0, 0);
				}
			} else {
				self->colors[i] = GST_COLOR_RGB(0, 0, 0);
			}
		}
	}

	if (config != NULL) {
		const gchar *const *palette_hex;
		guint n_palette;
		const gchar *hex;

		palette_hex = gst_config_get_palette_hex(config);
		n_palette = gst_config_get_n_palette(config);

		for (i = 0; i < n_palette && palette_hex != NULL; i++) {
			if (palette_hex[i] != NULL) {
				parse_color_name(palette_hex[i], &self->colors[i]);
			}
		}

		hex = gst_config_get_fg_hex(config);
		if (hex != NULL) {
			parse_color_name(hex, &self->colors[256]);
		} else if (palette_hex != NULL) {
			guint fg_idx;

			fg_idx = gst_config_get_fg_index(config);
			if (fg_idx < n_palette) {
				self->colors[256] = self->colors[fg_idx];
			}
		}

		hex = gst_config_get_bg_hex(config);
		if (hex != NULL) {
			parse_color_name(hex, &self->colors[257]);
		} else if (palette_hex != NULL) {
			guint bg_idx;

			bg_idx = gst_config_get_bg_index(config);
			if (bg_idx < n_palette) {
				self->colors[257] = self->colors[bg_idx];
			}
		}

		hex = gst_config_get_cursor_fg_hex(config);
		if (hex != NULL) {
			parse_color_name(hex, &self->colors[258]);
		} else if (palette_hex != NULL) {
			guint idx;

			idx = gst_config_get_cursor_fg_index(config);
			if (idx < n_palette) {
				self->colors[258] = self->colors[idx];
			}
		}

		hex = gst_config_get_cursor_bg_hex(config);
		if (hex != NULL) {
			parse_color_name(hex, &self->colors[259]);
		} else if (palette_hex != NULL) {
			guint idx;

			idx = gst_config_get_cursor_bg_index(config);
			if (idx < n_palette) {
				self->colors[259] = self->colors[idx];
			}
		}
	}

	return TRUE;
}

void
gst_lrg_renderer_set_win_mode(GstLrgRenderer *self, GstWinMode mode)
{
	g_return_if_fail(GST_IS_LRG_RENDERER(self));
	self->win_mode = mode;
}

GstWinMode
gst_lrg_renderer_get_win_mode(GstLrgRenderer *self)
{
	g_return_val_if_fail(GST_IS_LRG_RENDERER(self), 0);
	return self->win_mode;
}

void
gst_lrg_renderer_set_selection(
	GstLrgRenderer      *self,
	GstSelection        *selection
){
	g_return_if_fail(GST_IS_LRG_RENDERER(self));

	if (self->selection != NULL) {
		g_object_unref(self->selection);
	}
	self->selection = (selection != NULL) ? g_object_ref(selection) : NULL;
}
