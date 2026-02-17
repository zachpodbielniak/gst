/*
 * gst-x11-renderer.c - X11 renderer implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Full X11 rendering engine porting st's xdrawline,
 * xdrawglyphfontspecs, xdrawcursor, xloadcols, and the
 * double-buffered pixmap drawing pipeline.
 *
 * All st globals (dc, xw, win, frc[]) are replaced with
 * GObject instance fields.
 */

#include "gst-x11-renderer.h"
#include "gst-x11-render-context.h"
#include "../core/gst-terminal.h"
#include "../core/gst-line.h"
#include "../boxed/gst-glyph.h"
#include "../boxed/gst-cursor.h"
#include "../selection/gst-selection.h"
#include "../module/gst-module-manager.h"
#include <string.h>
#include <math.h>

/**
 * SECTION:gst-x11-renderer
 * @title: GstX11Renderer
 * @short_description: X11-based terminal renderer
 *
 * #GstX11Renderer implements the #GstRenderer interface for X11
 * display systems, using Xlib, Xft, and XRender for drawing.
 * It maintains a double-buffered pixmap and color palette.
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

struct _GstX11Renderer
{
	GstRenderer parent_instance;

	/* X11 resources */
	Display *display;
	Window xwindow;
	Drawable buf;            /* off-screen pixmap for double buffering */
	Visual *vis;
	Colormap cmap;
	gint screen;
	GC gc;
	XftDraw *draw;

	/* Glyph spec buffer (reused per frame) */
	XftGlyphFontSpec *specbuf;
	gint specbuf_len;

	/* Color palette */
	XftColor *colors;
	gsize num_colors;

	/* Font cache (not owned, caller manages lifetime) */
	GstFontCache *font_cache;

	/* Character cell dimensions (from font cache) */
	gint cw;
	gint ch;

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

	/* Old cursor position for redraw */
	gint ocx;
	gint ocy;

	/* Default color indices */
	gint default_fg;
	gint default_bg;
	gint default_cs;   /* cursor color */
	gint default_rcs;  /* reverse cursor color */

	/* Selection (for checking selected cells) */
	GstSelection *selection;
};

G_DEFINE_TYPE(GstX11Renderer, gst_x11_renderer, GST_TYPE_RENDERER)

/* ===== Static helper functions ===== */

/*
 * sixd_to_16bit:
 * @x: 6-level color component (0-5)
 *
 * Converts a 6-level color component to 16-bit for XRenderColor.
 * Maps: 0->0, 1->0x5f5f, 2->0x8787, 3->0xafaf, 4->0xd7d7, 5->0xffff
 *
 * Returns: 16-bit color value
 */
static guint16
sixd_to_16bit(gint x)
{
	return (guint16)(x == 0 ? 0 : 0x3737 + 0x2828 * x);
}

/*
 * load_single_color:
 * @self: the renderer
 * @i: color index
 * @name: color name (or NULL to generate from index)
 * @ncolor: (out): allocated XftColor
 *
 * Loads a single color by index or name. Handles the 6x6x6 cube
 * and grayscale ramp for indices 16-255.
 *
 * Returns: TRUE on success
 */
static gboolean
load_single_color(
	GstX11Renderer  *self,
	gint            i,
	const gchar     *name,
	XftColor        *ncolor
){
	XRenderColor color = { .alpha = 0xffff };

	if (name == NULL) {
		if (i >= 16 && i <= 255) {
			/* 6x6x6 RGB cube (indices 16-231) */
			if (i < 6 * 6 * 6 + 16) {
				color.red   = sixd_to_16bit(((i - 16) / 36) % 6);
				color.green = sixd_to_16bit(((i - 16) / 6) % 6);
				color.blue  = sixd_to_16bit(((i - 16) / 1) % 6);
			} else {
				/* Grayscale ramp (indices 232-255) */
				color.red = (guint16)(0x0808 + 0x0a0a * (i - (6 * 6 * 6 + 16)));
				color.green = color.red;
				color.blue = color.red;
			}
			return (gboolean)XftColorAllocValue(self->display, self->vis,
				self->cmap, &color, ncolor);
		} else {
			/* Use default color name table for 0-15 and 256+ */
			if ((gsize)i < G_N_ELEMENTS(default_colorname)
			    && default_colorname[i] != NULL) {
				name = default_colorname[i];
			} else {
				return FALSE;
			}
		}
	}

	return (gboolean)XftColorAllocName(self->display, self->vis,
		self->cmap, name, ncolor);
}

/*
 * x11_clear_rect:
 * @self: the renderer
 * @x1: left edge
 * @y1: top edge
 * @x2: right edge (exclusive)
 * @y2: bottom edge (exclusive)
 *
 * Clears a rectangle on the pixmap with the background color.
 */
static void
x11_clear_rect(
	GstX11Renderer  *self,
	gint            x1,
	gint            y1,
	gint            x2,
	gint            y2
){
	XftDrawRect(self->draw, &self->colors[self->default_bg],
		x1, y1, (guint)(x2 - x1), (guint)(y2 - y1));
}

/*
 * x11_fill_render_context:
 * @self: the renderer
 * @ctx: (out): render context to populate
 *
 * Populates a GstX11RenderContext with current renderer state.
 * Modules use this struct to access X11 drawing resources.
 */
static void
x11_fill_render_context(
	GstX11Renderer       *self,
	GstX11RenderContext   *ctx
){
	/* Initialize vtable and backend type */
	gst_x11_render_context_init_ops(ctx);

	/* Base context fields */
	ctx->base.cw         = self->cw;
	ctx->base.ch         = self->ch;
	ctx->base.borderpx   = self->borderpx;
	ctx->base.win_w      = self->win_w;
	ctx->base.win_h      = self->win_h;
	ctx->base.win_mode   = self->win_mode;
	ctx->base.glyph_attr = 0;

	/* X11-specific fields */
	ctx->display    = self->display;
	ctx->window     = self->xwindow;
	ctx->drawable   = self->buf;
	ctx->gc         = self->gc;
	ctx->xft_draw   = self->draw;
	ctx->visual     = self->vis;
	ctx->colormap   = self->cmap;
	ctx->colors     = self->colors;
	ctx->num_colors = self->num_colors;
	ctx->font_cache = self->font_cache;
	ctx->fg         = NULL;
	ctx->bg         = NULL;
}

/*
 * x11_make_glyph_specs:
 * @self: the renderer
 * @specs: (out): array of glyph specs to fill
 * @glyphs: array of GstGlyph to render
 * @len: number of glyphs
 * @x: starting column
 * @y: row
 *
 * Generates XftGlyphFontSpec entries for a run of glyphs,
 * looking up fonts via the font cache. Ports st's xmakeglyphfontspecs().
 *
 * Returns: number of specs generated
 */
static gint
x11_make_glyph_specs(
	GstX11Renderer      *self,
	XftGlyphFontSpec    *specs,
	GstLine             *line,
	gint                len,
	gint                x,
	gint                y
){
	gfloat winx;
	gfloat winy;
	gfloat xp;
	gfloat yp;
	gfloat runewidth;
	guint16 prevmode;
	GstFontVariant *fv;
	GstFontStyle fstyle;
	GstGlyph *g;
	GstRune rune;
	guint16 mode;
	XftFont *font_out;
	FT_UInt glyph_out;
	gint i;
	gint numspecs;

	winx = (gfloat)(self->borderpx + x * self->cw);
	winy = (gfloat)(self->borderpx + y * self->ch);
	fv = gst_font_cache_get_font(self->font_cache, GST_FONT_STYLE_NORMAL);
	fstyle = GST_FONT_STYLE_NORMAL;
	runewidth = (gfloat)self->cw;
	prevmode = (guint16)0xFFFF;
	numspecs = 0;

	for (i = 0, xp = winx, yp = winy + (gfloat)fv->ascent; i < len; i++) {
		g = gst_line_get_glyph(line, x + i);
		if (g == NULL) {
			continue;
		}

		rune = g->rune;
		mode = (guint16)g->attr;

		/* Skip wide character dummy cells */
		if (mode & GST_GLYPH_ATTR_WDUMMY) {
			continue;
		}

		/* Determine font variant from attributes */
		if (prevmode != mode) {
			prevmode = mode;
			fstyle = GST_FONT_STYLE_NORMAL;
			runewidth = (gfloat)self->cw;

			if ((mode & GST_GLYPH_ATTR_WIDE) != 0) {
				runewidth = (gfloat)(self->cw * 2);
			}

			if ((mode & GST_GLYPH_ATTR_ITALIC) && (mode & GST_GLYPH_ATTR_BOLD)) {
				fstyle = GST_FONT_STYLE_BOLD_ITALIC;
			} else if (mode & GST_GLYPH_ATTR_ITALIC) {
				fstyle = GST_FONT_STYLE_ITALIC;
			} else if (mode & GST_GLYPH_ATTR_BOLD) {
				fstyle = GST_FONT_STYLE_BOLD;
			}

			fv = gst_font_cache_get_font(self->font_cache, fstyle);
			yp = winy + (gfloat)fv->ascent;
		}

		/* Look up glyph in font cache (main + fallback ring) */
		gst_font_cache_lookup_glyph(self->font_cache, rune, fstyle,
			&font_out, &glyph_out);

		specs[numspecs].font = font_out;
		specs[numspecs].glyph = glyph_out;
		specs[numspecs].x = (gshort)xp;
		specs[numspecs].y = (gshort)yp;
		xp += runewidth;
		numspecs++;
	}

	return numspecs;
}

/*
 * x11_draw_glyph_specs:
 * @self: the renderer
 * @specs: array of glyph font specs
 * @base: base glyph with attributes for this run
 * @len: number of specs
 * @x: starting column
 * @y: row
 *
 * Renders a run of glyphs with the same attributes.
 * Handles true color, reverse video, bold brightening,
 * faint dimming, blink, invisible, underline, strikethrough.
 * Ports st's xdrawglyphfontspecs().
 */
static void
x11_draw_glyph_specs(
	GstX11Renderer          *self,
	const XftGlyphFontSpec  *specs,
	GstGlyph                *base,
	gint                    len,
	gint                    x,
	gint                    y
){
	guint16 mode;
	guint32 fg_idx;
	guint32 bg_idx;
	gint charlen;
	gint winx;
	gint winy;
	gint width;
	XftColor *fg;
	XftColor *bg;
	XftColor *temp;
	XftColor truefg;
	XftColor truebg;
	XftColor revfg;
	XRenderColor colfg;
	XRenderColor colbg;
	XRectangle r;
	GstFontVariant *fv;
	gboolean truefg_alloc;
	gboolean truebg_alloc;

	mode = (guint16)base->attr;
	fg_idx = base->fg;
	bg_idx = base->bg;
	charlen = len * ((mode & GST_GLYPH_ATTR_WIDE) ? 2 : 1);
	winx = self->borderpx + x * self->cw;
	winy = self->borderpx + y * self->ch;
	width = charlen * self->cw;
	truefg_alloc = FALSE;
	truebg_alloc = FALSE;

	/* Determine foreground color */
	if (GST_IS_TRUECOLOR(fg_idx)) {
		colfg.alpha = 0xffff;
		colfg.red = (guint16)GST_TRUERED(fg_idx);
		colfg.green = (guint16)GST_TRUEGREEN(fg_idx);
		colfg.blue = (guint16)GST_TRUEBLUE(fg_idx);
		XftColorAllocValue(self->display, self->vis, self->cmap, &colfg, &truefg);
		fg = &truefg;
		truefg_alloc = TRUE;
	} else {
		fg = &self->colors[fg_idx];
	}

	/* Determine background color */
	if (GST_IS_TRUECOLOR(bg_idx)) {
		colbg.alpha = 0xffff;
		colbg.red = (guint16)GST_TRUERED(bg_idx);
		colbg.green = (guint16)GST_TRUEGREEN(bg_idx);
		colbg.blue = (guint16)GST_TRUEBLUE(bg_idx);
		XftColorAllocValue(self->display, self->vis, self->cmap, &colbg, &truebg);
		bg = &truebg;
		truebg_alloc = TRUE;
	} else {
		bg = &self->colors[bg_idx];
	}

	/* Bold brightening: shift colors 0-7 to 8-15 */
	if ((mode & GST_GLYPH_ATTR_BOLD) && !(mode & GST_GLYPH_ATTR_FAINT)
	    && fg_idx <= 7) {
		fg = &self->colors[fg_idx + 8];
	}

	/* Faint dimming: halve foreground RGB */
	if ((mode & GST_GLYPH_ATTR_FAINT) && !(mode & GST_GLYPH_ATTR_BOLD)) {
		colfg.red = fg->color.red / 2;
		colfg.green = fg->color.green / 2;
		colfg.blue = fg->color.blue / 2;
		colfg.alpha = fg->color.alpha;
		XftColorAllocValue(self->display, self->vis, self->cmap, &colfg, &revfg);
		fg = &revfg;
	}

	/* Per-glyph reverse: swap fg and bg */
	if (mode & GST_GLYPH_ATTR_REVERSE) {
		temp = fg;
		fg = bg;
		bg = temp;
	}

	/* Blink: invisible during off phase */
	if ((mode & GST_GLYPH_ATTR_BLINK) && (self->win_mode & GST_WIN_MODE_BLINK)) {
		fg = bg;
	}

	/* Invisible attribute */
	if (mode & GST_GLYPH_ATTR_INVISIBLE) {
		fg = bg;
	}

	/* Clear border regions around this cell run */
	if (x == 0) {
		x11_clear_rect(self, 0, (y == 0) ? 0 : winy,
			self->borderpx,
			winy + self->ch + ((winy + self->ch >= self->borderpx + self->th)
				? self->win_h : 0));
	}
	if (winx + width >= self->borderpx + self->tw) {
		x11_clear_rect(self, winx + width, (y == 0) ? 0 : winy,
			self->win_w,
			(winy + self->ch >= self->borderpx + self->th)
				? self->win_h : (winy + self->ch));
	}
	if (y == 0) {
		x11_clear_rect(self, winx, 0, winx + width, self->borderpx);
	}
	if (winy + self->ch >= self->borderpx + self->th) {
		x11_clear_rect(self, winx, winy + self->ch, winx + width, self->win_h);
	}

	/* Fill background */
	XftDrawRect(self->draw, bg, winx, winy, (guint)width, (guint)self->ch);

	/* Set clipping for glyph rendering */
	r.x = 0;
	r.y = 0;
	r.height = (gushort)self->ch;
	r.width = (gushort)width;
	XftDrawSetClipRectangles(self->draw, winx, winy, &r, 1);

	/* Render glyphs */
	if (len > 0) {
		XftDrawGlyphFontSpec(self->draw, fg, specs, len);
	}

	/* Underline decoration */
	if (mode & GST_GLYPH_ATTR_UNDERLINE) {
		fv = gst_font_cache_get_font(self->font_cache, GST_FONT_STYLE_NORMAL);
		XftDrawRect(self->draw, fg, winx, winy + fv->ascent + 1,
			(guint)width, 1);
	}

	/* Strikethrough decoration */
	if (mode & GST_GLYPH_ATTR_STRUCK) {
		fv = gst_font_cache_get_font(self->font_cache, GST_FONT_STYLE_NORMAL);
		XftDrawRect(self->draw, fg, winx, winy + 2 * fv->ascent / 3,
			(guint)width, 1);
	}

	/* Undercurl decoration (sine wave below baseline) */
	if (mode & GST_GLYPH_ATTR_UNDERCURL) {
		gint uc_x;

		fv = gst_font_cache_get_font(self->font_cache, GST_FONT_STYLE_NORMAL);
		for (uc_x = 0; uc_x < width; uc_x++) {
			gint dy;

			dy = (gint)(sin((double)uc_x * M_PI / ((double)self->cw * 0.5)) * 1.5);
			XftDrawRect(self->draw, fg,
				winx + uc_x, winy + fv->ascent + 1 + dy, 1, 1);
		}
	}

	/* Reset clipping */
	XftDrawSetClip(self->draw, 0);

	/* Free allocated true colors */
	if (truefg_alloc) {
		XftColorFree(self->display, self->vis, self->cmap, &truefg);
	}
	if (truebg_alloc) {
		XftColorFree(self->display, self->vis, self->cmap, &truebg);
	}
}

/* ===== Virtual method implementations ===== */

/*
 * x11_renderer_draw_line_impl:
 * @renderer: the GstRenderer
 * @row: row index
 * @x1: start column
 * @x2: end column (exclusive)
 *
 * Draws a single line, grouping glyphs by attributes for
 * efficient batch rendering. Ports st's xdrawline().
 */
static void
x11_renderer_draw_line_impl(
	GstRenderer     *renderer,
	gint            row,
	gint            x1,
	gint            x2
){
	GstX11Renderer *self;
	GstTerminal *term;
	GstLine *line;
	XftGlyphFontSpec *specs;
	gint numspecs;
	gint i;
	gint x;
	gint ox;
	GstGlyph *new_glyph;
	GstGlyph base;
	GstGlyph cur;
	guint16 new_mode;
	GstModuleManager *mgr;
	gboolean has_glyph_transformers;
	GstX11RenderContext gt_ctx;

	self = GST_X11_RENDERER(renderer);
	term = gst_renderer_get_terminal(renderer);
	if (term == NULL) {
		return;
	}

	line = gst_terminal_get_line(term, row);
	if (line == NULL) {
		return;
	}

	/* Check if any glyph transformers are registered (fast path) */
	mgr = gst_module_manager_get_default();
	has_glyph_transformers = (mgr != NULL);
	if (has_glyph_transformers) {
		x11_fill_render_context(self, &gt_ctx);
	}

	/* Generate all glyph specs for this line segment */
	specs = self->specbuf;
	numspecs = x11_make_glyph_specs(self, specs, line, x2 - x1, x1, row);

	i = 0;
	ox = 0;
	memset(&base, 0, sizeof(GstGlyph));

	/* Iterate and group by matching attributes */
	for (x = x1; x < x2 && i < numspecs; x++) {
		new_glyph = gst_line_get_glyph(line, x);
		if (new_glyph == NULL) {
			continue;
		}

		new_mode = (guint16)new_glyph->attr;

		/* Skip wide char dummies */
		if (new_mode & GST_GLYPH_ATTR_WDUMMY) {
			continue;
		}

		/* Copy glyph to local for modification */
		cur = *new_glyph;

		/* Toggle reverse if cell is selected */
		if (self->selection != NULL && gst_selection_selected(self->selection, x, row)) {
			cur.attr ^= GST_GLYPH_ATTR_REVERSE;
		}

		/* Let glyph transformers handle non-ASCII codepoints */
		if (has_glyph_transformers && cur.rune > 0x7F) {
			gint pixel_x;
			gint pixel_y;
			XftColor *gt_fg;
			XftColor *gt_bg;
			XftColor *gt_temp;
			XftColor gt_truefg;
			XftColor gt_truebg;
			XftColor gt_dimfg;
			XRenderColor gt_colfg;
			XRenderColor gt_colbg;
			gboolean gt_truefg_alloc;
			gboolean gt_truebg_alloc;
			guint16 gt_mode;
			guint32 gt_fg_idx;
			guint32 gt_bg_idx;

			pixel_x = self->borderpx + x * self->cw;
			pixel_y = self->borderpx + row * self->ch;

			/* Resolve per-glyph fg/bg colors for the render context */
			gt_mode = (guint16)cur.attr;
			gt_fg_idx = cur.fg;
			gt_bg_idx = cur.bg;
			gt_truefg_alloc = FALSE;
			gt_truebg_alloc = FALSE;

			if (GST_IS_TRUECOLOR(gt_fg_idx)) {
				gt_colfg.alpha = 0xffff;
				gt_colfg.red = (guint16)GST_TRUERED(gt_fg_idx);
				gt_colfg.green = (guint16)GST_TRUEGREEN(gt_fg_idx);
				gt_colfg.blue = (guint16)GST_TRUEBLUE(gt_fg_idx);
				XftColorAllocValue(self->display, self->vis,
					self->cmap, &gt_colfg, &gt_truefg);
				gt_fg = &gt_truefg;
				gt_truefg_alloc = TRUE;
			} else {
				gt_fg = &self->colors[gt_fg_idx];
			}

			if (GST_IS_TRUECOLOR(gt_bg_idx)) {
				gt_colbg.alpha = 0xffff;
				gt_colbg.red = (guint16)GST_TRUERED(gt_bg_idx);
				gt_colbg.green = (guint16)GST_TRUEGREEN(gt_bg_idx);
				gt_colbg.blue = (guint16)GST_TRUEBLUE(gt_bg_idx);
				XftColorAllocValue(self->display, self->vis,
					self->cmap, &gt_colbg, &gt_truebg);
				gt_bg = &gt_truebg;
				gt_truebg_alloc = TRUE;
			} else {
				gt_bg = &self->colors[gt_bg_idx];
			}

			/* Bold brightening */
			if ((gt_mode & GST_GLYPH_ATTR_BOLD)
			    && !(gt_mode & GST_GLYPH_ATTR_FAINT)
			    && !GST_IS_TRUECOLOR(gt_fg_idx) && gt_fg_idx <= 7) {
				gt_fg = &self->colors[gt_fg_idx + 8];
			}

			/* Faint dimming */
			if ((gt_mode & GST_GLYPH_ATTR_FAINT)
			    && !(gt_mode & GST_GLYPH_ATTR_BOLD)) {
				gt_colfg.red = gt_fg->color.red / 2;
				gt_colfg.green = gt_fg->color.green / 2;
				gt_colfg.blue = gt_fg->color.blue / 2;
				gt_colfg.alpha = gt_fg->color.alpha;
				XftColorAllocValue(self->display, self->vis,
					self->cmap, &gt_colfg, &gt_dimfg);
				gt_fg = &gt_dimfg;
			}

			/* Reverse video */
			if (gt_mode & GST_GLYPH_ATTR_REVERSE) {
				gt_temp = gt_fg;
				gt_fg = gt_bg;
				gt_bg = gt_temp;
			}

			/* Blink: invisible during off phase */
			if ((gt_mode & GST_GLYPH_ATTR_BLINK)
			    && (self->win_mode & GST_WIN_MODE_BLINK)) {
				gt_fg = gt_bg;
			}

			/* Invisible attribute */
			if (gt_mode & GST_GLYPH_ATTR_INVISIBLE) {
				gt_fg = gt_bg;
			}

			gt_ctx.fg = gt_fg;
			gt_ctx.bg = gt_bg;
			gt_ctx.base.glyph_attr = gt_mode;

			if (gst_module_manager_dispatch_glyph_transform(
				mgr, cur.rune, &gt_ctx.base,
				pixel_x, pixel_y, self->cw, self->ch))
			{
				/* Free truecolor resources */
				if (gt_truefg_alloc) {
					XftColorFree(self->display, self->vis,
						self->cmap, &gt_truefg);
				}
				if (gt_truebg_alloc) {
					XftColorFree(self->display, self->vis,
						self->cmap, &gt_truebg);
				}

				/* Flush accumulated run before skipping */
				if (i > 0) {
					x11_draw_glyph_specs(self, specs, &base, i, ox, row);
					specs += i;
					numspecs -= i;
					i = 0;
				}
				continue;
			}

			/* Free truecolor resources if transformer didn't handle it */
			if (gt_truefg_alloc) {
				XftColorFree(self->display, self->vis,
					self->cmap, &gt_truefg);
			}
			if (gt_truebg_alloc) {
				XftColorFree(self->display, self->vis,
					self->cmap, &gt_truebg);
			}
		}

		/* If attributes changed, flush the accumulated run */
		if (i > 0 && ATTRCMP(base, cur)) {
			x11_draw_glyph_specs(self, specs, &base, i, ox, row);
			specs += i;
			numspecs -= i;
			i = 0;
		}

		/* Start new run */
		if (i == 0) {
			ox = x;
			base = cur;
		}
		i++;
	}

	/* Flush final run */
	if (i > 0) {
		x11_draw_glyph_specs(self, specs, &base, i, ox, row);
	}
}

/*
 * x11_renderer_draw_cursor_impl:
 * @renderer: the GstRenderer
 * @cx: current cursor column
 * @cy: current cursor row
 * @ox: old cursor column
 * @oy: old cursor row
 *
 * Draws the cursor, first erasing the old one. Supports block,
 * underline, and bar cursor styles, plus hollow box when unfocused.
 * Ports st's xdrawcursor().
 */
static void
x11_renderer_draw_cursor_impl(
	GstRenderer     *renderer,
	gint            cx,
	gint            cy,
	gint            ox,
	gint            oy
){
	GstX11Renderer *self;
	GstTerminal *term;
	GstGlyph *g;
	GstCursor *cursor;
	XftColor drawcol;
	gint winx;
	gint winy;
	guint16 gmode;

	self = GST_X11_RENDERER(renderer);
	term = gst_renderer_get_terminal(renderer);
	if (term == NULL) {
		return;
	}

	/* Erase old cursor by redrawing that cell */
	x11_renderer_draw_line_impl(renderer, oy, ox, ox + 1);

	/* Check if cursor is hidden */
	if (gst_terminal_has_mode(term, GST_MODE_HIDE)) {
		return;
	}

	g = gst_terminal_get_glyph(term, cx, cy);
	if (g == NULL) {
		return;
	}

	cursor = gst_terminal_get_cursor(term);
	drawcol = self->colors[self->default_cs];
	gmode = (guint16)g->attr;

	winx = self->borderpx + cx * self->cw;
	winy = self->borderpx + cy * self->ch;

	if (self->win_mode & GST_WIN_MODE_FOCUSED) {
		/* Focused: draw filled cursor based on shape */
		switch (cursor->shape) {
		case GST_CURSOR_SHAPE_BLOCK:
			/* Draw the glyph with inverted colors */
			{
				GstGlyph block_g = *g;
				block_g.fg = (guint32)self->default_bg;
				block_g.bg = (guint32)self->default_cs;
				block_g.attr = gmode & (GST_GLYPH_ATTR_BOLD | GST_GLYPH_ATTR_ITALIC
					| GST_GLYPH_ATTR_UNDERLINE | GST_GLYPH_ATTR_STRUCK
					| GST_GLYPH_ATTR_WIDE);

				/* Generate and draw single glyph spec */
				{
					XftGlyphFontSpec spec;
					GstLine *cursor_line;
					gint ns;

					cursor_line = gst_terminal_get_line(term, cy);
					if (cursor_line != NULL) {
						ns = x11_make_glyph_specs(self, &spec, cursor_line, 1, cx, cy);
						x11_draw_glyph_specs(self, &spec, &block_g, ns, cx, cy);
					}
				}
			}
			break;
		case GST_CURSOR_SHAPE_UNDERLINE:
			XftDrawRect(self->draw, &drawcol,
				winx,
				winy + self->ch - GST_CURSOR_THICKNESS,
				(guint)self->cw, GST_CURSOR_THICKNESS);
			break;
		case GST_CURSOR_SHAPE_BAR:
			XftDrawRect(self->draw, &drawcol,
				winx,
				winy,
				GST_CURSOR_THICKNESS, (guint)self->ch);
			break;
		}
	} else {
		/* Unfocused: hollow box cursor */
		XftDrawRect(self->draw, &drawcol,
			winx, winy, (guint)self->cw - 1, 1);
		XftDrawRect(self->draw, &drawcol,
			winx, winy, 1, (guint)self->ch - 1);
		XftDrawRect(self->draw, &drawcol,
			winx + self->cw - 1, winy, 1, (guint)self->ch - 1);
		XftDrawRect(self->draw, &drawcol,
			winx, winy + self->ch - 1, (guint)self->cw, 1);
	}
}

/*
 * x11_renderer_render_impl:
 * @renderer: the GstRenderer
 *
 * Full render pass: iterate dirty lines, draw cursor,
 * copy pixmap to window. Uses dirty-line optimization.
 */
static void
x11_renderer_render_impl(GstRenderer *renderer)
{
	GstX11Renderer *self;
	GstTerminal *term;
	GstCursor *cursor;
	gint rows;
	gint cols;
	gint y;
	gint cx;
	gint cy;

	self = GST_X11_RENDERER(renderer);
	term = gst_renderer_get_terminal(renderer);
	if (term == NULL) {
		return;
	}

	gst_terminal_get_size(term, &cols, &rows);
	cursor = gst_terminal_get_cursor(term);
	cx = cursor->x;
	cy = cursor->y;

	/* Draw dirty lines */
	for (y = 0; y < rows; y++) {
		GstLine *line;

		line = gst_terminal_get_line(term, y);
		if (line == NULL) {
			continue;
		}

		if (gst_line_is_dirty(line)) {
			x11_renderer_draw_line_impl(renderer, y, 0, cols);
		}
	}

	/* Draw cursor */
	x11_renderer_draw_cursor_impl(renderer, cx, cy, self->ocx, self->ocy);
	self->ocx = cx;
	self->ocy = cy;

	/* Dispatch render overlays to modules */
	{
		GstModuleManager *mgr;
		GstX11RenderContext ctx;

		mgr = gst_module_manager_get_default();
		x11_fill_render_context(self, &ctx);
		gst_module_manager_dispatch_render_overlay(
			mgr, &ctx.base, self->win_w, self->win_h);
	}

	/* Copy pixmap to window (double-buffer flip) */
	XCopyArea(self->display, self->buf, self->xwindow, self->gc,
		0, 0, (guint)self->win_w, (guint)self->win_h, 0, 0);
	XSetForeground(self->display, self->gc,
		self->colors[self->default_bg].pixel);

	/* Clear terminal dirty flags */
	gst_terminal_clear_dirty(term);
}

/*
 * x11_renderer_resize_impl:
 * @renderer: the GstRenderer
 * @width: new window width in pixels
 * @height: new window height in pixels
 *
 * Handles window resize: recreates the pixmap and XftDraw,
 * updates terminal pixel dimensions.
 */
static void
x11_renderer_resize_impl(
	GstRenderer     *renderer,
	guint           width,
	guint           height
){
	GstX11Renderer *self;
	GstTerminal *term;
	gint cols;
	gint rows;

	self = GST_X11_RENDERER(renderer);
	self->win_w = (gint)width;
	self->win_h = (gint)height;

	/* Refresh cell dimensions from font cache (picks up zoom changes) */
	if (self->font_cache != NULL) {
		self->cw = gst_font_cache_get_char_width(self->font_cache);
		self->ch = gst_font_cache_get_char_height(self->font_cache);
	}

	term = gst_renderer_get_terminal(renderer);
	if (term != NULL) {
		gst_terminal_get_size(term, &cols, &rows);
		self->tw = cols * self->cw;
		self->th = rows * self->ch;
	}

	/* Recreate pixmap */
	if (self->buf != 0) {
		XFreePixmap(self->display, self->buf);
	}
	self->buf = XCreatePixmap(self->display, self->xwindow,
		(guint)self->win_w, (guint)self->win_h,
		(guint)DefaultDepth(self->display, self->screen));

	/* Recreate XftDraw */
	if (self->draw != NULL) {
		XftDrawDestroy(self->draw);
	}
	self->draw = XftDrawCreate(self->display, self->buf,
		self->vis, self->cmap);

	/* Fill with background */
	XSetForeground(self->display, self->gc,
		self->colors[self->default_bg].pixel);
	XFillRectangle(self->display, self->buf, self->gc,
		0, 0, (guint)self->win_w, (guint)self->win_h);

	/* Reallocate glyph spec buffer */
	if (term != NULL) {
		gst_terminal_get_size(term, &cols, &rows);
		g_free(self->specbuf);
		self->specbuf_len = cols;
		self->specbuf = g_new(XftGlyphFontSpec, (gsize)cols);
	}
}

/*
 * x11_renderer_clear_impl:
 * @renderer: the GstRenderer
 *
 * Clears the entire render surface to background color.
 */
static void
x11_renderer_clear_impl(GstRenderer *renderer)
{
	GstX11Renderer *self;

	self = GST_X11_RENDERER(renderer);

	if (self->draw != NULL) {
		XftDrawRect(self->draw, &self->colors[self->default_bg],
			0, 0, (guint)self->win_w, (guint)self->win_h);
	}
}

/*
 * x11_renderer_start_draw_impl:
 * @renderer: the GstRenderer
 *
 * Checks if Xft drawing is ready.
 *
 * Returns: TRUE if drawing can proceed
 */
static gboolean
x11_renderer_start_draw_impl(GstRenderer *renderer)
{
	GstX11Renderer *self;

	self = GST_X11_RENDERER(renderer);

	if (!(self->win_mode & GST_WIN_MODE_VISIBLE)) {
		return FALSE;
	}

	return (self->draw != NULL);
}

/*
 * x11_renderer_finish_draw_impl:
 * @renderer: the GstRenderer
 *
 * Copies the pixmap to the window and flushes.
 */
static void
x11_renderer_finish_draw_impl(GstRenderer *renderer)
{
	GstX11Renderer *self;

	self = GST_X11_RENDERER(renderer);

	XCopyArea(self->display, self->buf, self->xwindow, self->gc,
		0, 0, (guint)self->win_w, (guint)self->win_h, 0, 0);
	XFlush(self->display);
}

/* ===== GObject lifecycle ===== */

static void
gst_x11_renderer_dispose(GObject *object)
{
	GstX11Renderer *self;

	self = GST_X11_RENDERER(object);

	/* Free colors */
	if (self->colors != NULL && self->display != NULL) {
		gsize i;

		for (i = 0; i < self->num_colors; i++) {
			XftColorFree(self->display, self->vis, self->cmap, &self->colors[i]);
		}
		g_free(self->colors);
		self->colors = NULL;
		self->num_colors = 0;
	}

	/* Free glyph spec buffer */
	g_clear_pointer(&self->specbuf, g_free);

	/* Free XftDraw */
	if (self->draw != NULL) {
		XftDrawDestroy(self->draw);
		self->draw = NULL;
	}

	/* Free pixmap */
	if (self->buf != 0 && self->display != NULL) {
		XFreePixmap(self->display, self->buf);
		self->buf = 0;
	}

	/* Free GC */
	if (self->gc != NULL && self->display != NULL) {
		XFreeGC(self->display, self->gc);
		self->gc = NULL;
	}

	g_clear_object(&self->selection);

	G_OBJECT_CLASS(gst_x11_renderer_parent_class)->dispose(object);
}

/*
 * x11_renderer_capture_screenshot_impl:
 *
 * Captures the off-screen pixmap (double buffer) as RGBA pixel data.
 * XGetImage returns pixels in the Visual's native format (typically
 * BGRA on little-endian x86). We convert per-pixel to RGBA order.
 */
static GBytes *
x11_renderer_capture_screenshot_impl(
	GstRenderer *renderer,
	gint        *out_width,
	gint        *out_height,
	gint        *out_stride
){
	GstX11Renderer *self;
	XImage *img;
	gint w, h, stride;
	guint8 *rgba;
	gint x, y;

	self = GST_X11_RENDERER(renderer);

	w = self->win_w;
	h = self->win_h;

	if (w <= 0 || h <= 0 || self->buf == 0) {
		return NULL;
	}

	/* Capture the off-screen pixmap */
	img = XGetImage(self->display, self->buf, 0, 0,
		(guint)w, (guint)h, AllPlanes, ZPixmap);
	if (img == NULL) {
		return NULL;
	}

	/* Allocate RGBA output buffer (4 bytes per pixel) */
	stride = w * 4;
	rgba = (guint8 *)g_malloc((gsize)stride * (gsize)h);

	/*
	 * Convert from X11 native format to RGBA.
	 * XImage with ZPixmap and 32-bit visual stores pixels as
	 * 0xAARRGGBB in native byte order, which on little-endian
	 * is byte order: BB GG RR AA. We extract via XGetPixel
	 * for portability across visuals.
	 */
	for (y = 0; y < h; y++) {
		guint8 *row;

		row = rgba + y * stride;
		for (x = 0; x < w; x++) {
			gulong pixel;
			guint8 *dst;

			pixel = XGetPixel(img, x, y);
			dst = row + x * 4;
			dst[0] = (guint8)((pixel >> 16) & 0xFF); /* R */
			dst[1] = (guint8)((pixel >>  8) & 0xFF); /* G */
			dst[2] = (guint8)((pixel      ) & 0xFF); /* B */
			dst[3] = 0xFF;                            /* A (opaque) */
		}
	}

	XDestroyImage(img);

	if (out_width != NULL)  *out_width  = w;
	if (out_height != NULL) *out_height = h;
	if (out_stride != NULL) *out_stride = stride;

	return g_bytes_new_take(rgba, (gsize)stride * (gsize)h);
}

static void
gst_x11_renderer_class_init(GstX11RendererClass *klass)
{
	GObjectClass *object_class;
	GstRendererClass *renderer_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_x11_renderer_dispose;

	renderer_class = GST_RENDERER_CLASS(klass);
	renderer_class->render = x11_renderer_render_impl;
	renderer_class->resize = x11_renderer_resize_impl;
	renderer_class->clear = x11_renderer_clear_impl;
	renderer_class->draw_line = x11_renderer_draw_line_impl;
	renderer_class->draw_cursor = x11_renderer_draw_cursor_impl;
	renderer_class->start_draw = x11_renderer_start_draw_impl;
	renderer_class->finish_draw = x11_renderer_finish_draw_impl;
	renderer_class->capture_screenshot = x11_renderer_capture_screenshot_impl;
}

static void
gst_x11_renderer_init(GstX11Renderer *self)
{
	self->display = NULL;
	self->xwindow = 0;
	self->buf = 0;
	self->vis = NULL;
	self->cmap = 0;
	self->screen = 0;
	self->gc = NULL;
	self->draw = NULL;
	self->specbuf = NULL;
	self->specbuf_len = 0;
	self->colors = NULL;
	self->num_colors = 0;
	self->font_cache = NULL;
	self->cw = 0;
	self->ch = 0;
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
}

/* ===== Public API ===== */

/**
 * gst_x11_renderer_new:
 * @terminal: the terminal to render
 * @display: X11 display
 * @xwindow: X11 window
 * @visual: X11 visual
 * @colormap: X11 colormap
 * @screen: X11 screen number
 * @font_cache: the font cache
 * @borderpx: border padding in pixels
 *
 * Creates a new X11 renderer, setting up the drawing context,
 * pixmap double buffer, and GC.
 *
 * Returns: (transfer full): A new #GstX11Renderer
 */
GstX11Renderer *
gst_x11_renderer_new(
	GstTerminal     *terminal,
	Display         *display,
	Window          xwindow,
	Visual          *visual,
	Colormap        colormap,
	gint            screen,
	GstFontCache    *font_cache,
	gint            borderpx
){
	GstX11Renderer *self;
	XGCValues gcvalues;
	gint cols;
	gint rows;

	self = (GstX11Renderer *)g_object_new(GST_TYPE_X11_RENDERER,
		"terminal", terminal,
		NULL);

	self->display = display;
	self->xwindow = xwindow;
	self->vis = visual;
	self->cmap = colormap;
	self->screen = screen;
	self->font_cache = font_cache;
	self->borderpx = borderpx;

	/* Get cell dimensions from font cache */
	self->cw = gst_font_cache_get_char_width(font_cache);
	self->ch = gst_font_cache_get_char_height(font_cache);

	/* Calculate pixel dimensions */
	gst_terminal_get_size(terminal, &cols, &rows);
	self->tw = cols * self->cw;
	self->th = rows * self->ch;
	self->win_w = 2 * borderpx + self->tw;
	self->win_h = 2 * borderpx + self->th;

	/* Create GC for pixmap operations */
	memset(&gcvalues, 0, sizeof(gcvalues));
	gcvalues.graphics_exposures = False;
	self->gc = XCreateGC(display, xwindow, GCGraphicsExposures, &gcvalues);

	/* Create pixmap for double buffering */
	self->buf = XCreatePixmap(display, xwindow,
		(guint)self->win_w, (guint)self->win_h,
		(guint)DefaultDepth(display, screen));

	/* Create Xft draw context on pixmap */
	self->draw = XftDrawCreate(display, self->buf, visual, colormap);

	/* Allocate glyph spec buffer */
	self->specbuf_len = cols;
	self->specbuf = g_new(XftGlyphFontSpec, (gsize)cols);

	return self;
}

/*
 * x11_override_color:
 *
 * Replaces a single color slot with a named color (e.g. "#RRGGBB").
 * Frees the old XftColor and allocates a new one.
 */
static void
x11_override_color(
	GstX11Renderer  *self,
	gsize            index,
	const gchar     *name
){
	XftColorFree(self->display, self->vis, self->cmap,
		&self->colors[index]);
	if (!load_single_color(self, (gint)index, name,
	    &self->colors[index])) {
		g_warning("x11_override_color: failed for index %zu: %s",
			index, name);
	}
}

/**
 * gst_x11_renderer_load_colors:
 * @self: A #GstX11Renderer
 * @config: (nullable): A #GstConfig for palette and color overrides
 *
 * Loads the full color palette (262 colors) from defaults,
 * then applies any overrides from @config.
 *
 * Returns: TRUE on success
 */
gboolean
gst_x11_renderer_load_colors(
	GstX11Renderer  *self,
	GstConfig       *config
){
	gsize i;
	gsize count;

	g_return_val_if_fail(GST_IS_X11_RENDERER(self), FALSE);

	/* Free old colors if any */
	if (self->colors != NULL) {
		for (i = 0; i < self->num_colors; i++) {
			XftColorFree(self->display, self->vis, self->cmap, &self->colors[i]);
		}
		g_free(self->colors);
	}

	/* Step 1: Load all 262 colors from hardcoded defaults */
	count = (gsize)GST_COLOR_COUNT;
	self->colors = g_new0(XftColor, count);
	self->num_colors = count;

	for (i = 0; i < count; i++) {
		if (!load_single_color(self, (gint)i, NULL, &self->colors[i])) {
			g_warning("gst_x11_renderer_load_colors: could not allocate color %zu", i);
			return FALSE;
		}
	}

	/* Step 2: Apply config color overrides */
	if (config != NULL) {
		const gchar *const *palette_hex;
		guint n_palette;
		const gchar *hex;

		/* Override palette entries 0-15 */
		palette_hex = gst_config_get_palette_hex(config);
		n_palette = gst_config_get_n_palette(config);

		for (i = 0; i < n_palette && palette_hex != NULL; i++) {
			if (palette_hex[i] != NULL) {
				x11_override_color(self, i, palette_hex[i]);
			}
		}

		/* Override foreground (index 256) */
		hex = gst_config_get_fg_hex(config);
		if (hex != NULL) {
			x11_override_color(self, 256, hex);
		} else if (palette_hex != NULL) {
			guint fg_idx;

			fg_idx = gst_config_get_fg_index(config);
			if (fg_idx < n_palette && palette_hex[fg_idx] != NULL) {
				x11_override_color(self, 256, palette_hex[fg_idx]);
			}
		}

		/* Override background (index 257) */
		hex = gst_config_get_bg_hex(config);
		if (hex != NULL) {
			x11_override_color(self, 257, hex);
		} else if (palette_hex != NULL) {
			guint bg_idx;

			bg_idx = gst_config_get_bg_index(config);
			if (bg_idx < n_palette && palette_hex[bg_idx] != NULL) {
				x11_override_color(self, 257, palette_hex[bg_idx]);
			}
		}

		/* Override cursor foreground (index 258) */
		hex = gst_config_get_cursor_fg_hex(config);
		if (hex != NULL) {
			x11_override_color(self, 258, hex);
		} else if (palette_hex != NULL) {
			guint idx;

			idx = gst_config_get_cursor_fg_index(config);
			if (idx < n_palette && palette_hex[idx] != NULL) {
				x11_override_color(self, 258, palette_hex[idx]);
			}
		}

		/* Override cursor background (index 259) */
		hex = gst_config_get_cursor_bg_hex(config);
		if (hex != NULL) {
			x11_override_color(self, 259, hex);
		} else if (palette_hex != NULL) {
			guint idx;

			idx = gst_config_get_cursor_bg_index(config);
			if (idx < n_palette && palette_hex[idx] != NULL) {
				x11_override_color(self, 259, palette_hex[idx]);
			}
		}
	}

	/* Fill background pixmap with background color */
	if (self->buf != 0) {
		XSetForeground(self->display, self->gc,
			self->colors[self->default_bg].pixel);
		XFillRectangle(self->display, self->buf, self->gc,
			0, 0, (guint)self->win_w, (guint)self->win_h);
	}

	return TRUE;
}

/**
 * gst_x11_renderer_set_color:
 * @self: A #GstX11Renderer
 * @index: color index
 * @name: color name
 *
 * Sets a single color by name.
 *
 * Returns: TRUE on success
 */
gboolean
gst_x11_renderer_set_color(
	GstX11Renderer  *self,
	gint            index,
	const gchar     *name
){
	XftColor ncolor;

	g_return_val_if_fail(GST_IS_X11_RENDERER(self), FALSE);
	g_return_val_if_fail(index >= 0 && (gsize)index < self->num_colors, FALSE);

	if (!load_single_color(self, index, name, &ncolor)) {
		return FALSE;
	}

	XftColorFree(self->display, self->vis, self->cmap, &self->colors[index]);
	self->colors[index] = ncolor;

	return TRUE;
}

/**
 * gst_x11_renderer_get_font_cache:
 * @self: A #GstX11Renderer
 *
 * Returns: (transfer none): the font cache
 */
GstFontCache *
gst_x11_renderer_get_font_cache(GstX11Renderer *self)
{
	g_return_val_if_fail(GST_IS_X11_RENDERER(self), NULL);

	return self->font_cache;
}

/**
 * gst_x11_renderer_set_win_mode:
 * @self: A #GstX11Renderer
 * @mode: new window mode flags
 *
 * Updates the window mode flags.
 */
void
gst_x11_renderer_set_win_mode(
	GstX11Renderer  *self,
	GstWinMode      mode
){
	g_return_if_fail(GST_IS_X11_RENDERER(self));

	self->win_mode = mode;
}

/**
 * gst_x11_renderer_get_win_mode:
 * @self: A #GstX11Renderer
 *
 * Returns: the current window mode flags
 */
GstWinMode
gst_x11_renderer_get_win_mode(GstX11Renderer *self)
{
	g_return_val_if_fail(GST_IS_X11_RENDERER(self), 0);

	return self->win_mode;
}

/**
 * gst_x11_renderer_set_selection:
 * @self: A #GstX11Renderer
 * @selection: (transfer none): A #GstSelection for highlight checks
 *
 * Sets the selection object. The renderer uses this to determine
 * which cells to render with reverse video during draw_line.
 */
void
gst_x11_renderer_set_selection(
	GstX11Renderer  *self,
	GstSelection    *selection
){
	g_return_if_fail(GST_IS_X11_RENDERER(self));

	if (self->selection != NULL) {
		g_object_unref(self->selection);
	}
	self->selection = (selection != NULL) ? g_object_ref(selection) : NULL;
}
