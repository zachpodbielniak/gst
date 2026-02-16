/*
 * gst-wayland-renderer.c - Wayland renderer implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Wayland rendering engine using Cairo for drawing and wl_shm
 * for shared-memory double-buffered surface rendering.
 * Implements the GstRenderer abstract interface.
 *
 * All drawing is done to a Cairo image surface backed by
 * shared memory. Buffers are attached to the Wayland surface
 * and committed for display.
 */

#include "gst-wayland-renderer.h"
#include "gst-wayland-render-context.h"
#include "../core/gst-terminal.h"
#include "../core/gst-line.h"
#include "../boxed/gst-glyph.h"
#include "../boxed/gst-cursor.h"
#include "../selection/gst-selection.h"
#include "../module/gst-module-manager.h"
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

/**
 * SECTION:gst-wayland-renderer
 * @title: GstWaylandRenderer
 * @short_description: Wayland-based terminal renderer
 *
 * #GstWaylandRenderer implements the #GstRenderer interface for Wayland
 * display systems, using Cairo for drawing and wl_shm for
 * shared-memory double-buffered rendering.
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

/* Size of wl_shm stride: 4 bytes per pixel (ARGB8888) */
#define BYTES_PER_PIXEL (4)

struct _GstWaylandRenderer
{
	GstRenderer parent_instance;

	/* Wayland resources */
	GstWaylandWindow    *wl_window;
	struct wl_display   *wl_display;
	struct wl_surface   *wl_surface;
	struct wl_shm       *wl_shm;

	/* Shared memory buffer */
	gint shm_fd;
	guint8 *shm_data;
	gsize shm_size;
	struct wl_shm_pool *shm_pool;
	struct wl_buffer *buffer;

	/* Cairo drawing surface */
	cairo_surface_t *cairo_surface;
	cairo_t *cr;

	/* Color palette (262 entries as GstColor RGBA) */
	GstColor *colors;
	gsize num_colors;

	/* Font cache (not owned, caller manages lifetime) */
	GstCairoFontCache *font_cache;

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

	/* Last applied opacity (used to detect changes and trigger full repaint) */
	gdouble last_opacity;
};

G_DEFINE_TYPE(GstWaylandRenderer, gst_wayland_renderer, GST_TYPE_RENDERER)

/* ===== Shared memory buffer management ===== */

/*
 * create_shm_file:
 * @size: requested size in bytes
 *
 * Creates an anonymous shared memory file descriptor.
 * Uses memfd_create if available, falls back to shm_open.
 *
 * Returns: file descriptor, or -1 on error
 */
static gint
create_shm_file(gsize size)
{
	gint fd;

#ifdef __NR_memfd_create
	fd = (gint)syscall(__NR_memfd_create, "gst-shm", 0);
#else
	{
		gchar name[64];

		g_snprintf(name, sizeof(name), "/gst-shm-%d-%d",
			(gint)getpid(), g_random_int());
		fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
		}
	}
#endif

	if (fd < 0) {
		return -1;
	}

	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * wl_create_buffer:
 * @self: the renderer
 * @width: buffer width in pixels
 * @height: buffer height in pixels
 *
 * Creates a wl_shm buffer backed by shared memory, and
 * creates a Cairo image surface over that memory.
 *
 * Returns: TRUE on success
 */
static gboolean
wl_create_buffer(
	GstWaylandRenderer  *self,
	gint                width,
	gint                height
){
	gint stride;
	gsize size;

	stride = width * BYTES_PER_PIXEL;
	size = (gsize)(stride * height);

	/* Free old buffer resources */
	if (self->cr != NULL) {
		cairo_destroy(self->cr);
		self->cr = NULL;
	}
	if (self->cairo_surface != NULL) {
		cairo_surface_destroy(self->cairo_surface);
		self->cairo_surface = NULL;
	}
	if (self->buffer != NULL) {
		wl_buffer_destroy(self->buffer);
		self->buffer = NULL;
	}
	if (self->shm_pool != NULL) {
		wl_shm_pool_destroy(self->shm_pool);
		self->shm_pool = NULL;
	}
	if (self->shm_data != NULL) {
		munmap(self->shm_data, self->shm_size);
		self->shm_data = NULL;
	}
	if (self->shm_fd >= 0) {
		close(self->shm_fd);
		self->shm_fd = -1;
	}

	if (width <= 0 || height <= 0) {
		return FALSE;
	}

	/* Create shared memory */
	self->shm_fd = create_shm_file(size);
	if (self->shm_fd < 0) {
		g_warning("gst_wayland_renderer: failed to create shm file: %s",
			g_strerror(errno));
		return FALSE;
	}

	self->shm_data = (guint8 *)mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_SHARED, self->shm_fd, 0);
	if (self->shm_data == MAP_FAILED) {
		g_warning("gst_wayland_renderer: mmap failed: %s",
			g_strerror(errno));
		self->shm_data = NULL;
		close(self->shm_fd);
		self->shm_fd = -1;
		return FALSE;
	}
	self->shm_size = size;

	/* Create wl_shm_pool and wl_buffer */
	self->shm_pool = wl_shm_create_pool(self->wl_shm,
		self->shm_fd, (gint32)size);
	self->buffer = wl_shm_pool_create_buffer(self->shm_pool,
		0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

	/* Create cairo image surface backed by the shared memory */
	self->cairo_surface = cairo_image_surface_create_for_data(
		self->shm_data, CAIRO_FORMAT_ARGB32,
		width, height, stride);

	self->cr = cairo_create(self->cairo_surface);

	return TRUE;
}

/* ===== Static helper functions ===== */

/*
 * sixd_to_8bit:
 * @x: 6-level color component (0-5)
 *
 * Converts a 6-level color component to 8-bit.
 * Maps: 0->0, 1->0x5f, 2->0x87, 3->0xaf, 4->0xd7, 5->0xff
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
 * Parses a color name to GstColor. Supports hex formats
 * (#RGB, #RRGGBB) and a small set of named colors.
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

	/* Hex color */
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

	/* Named X11 colors (subset used by st) */
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
 * wl_set_source_color:
 * @cr: cairo context
 * @color: GstColor RGBA value
 *
 * Sets the cairo source color from a GstColor.
 */
static void
wl_set_source_color(cairo_t *cr, GstColor color)
{
	cairo_set_source_rgb(cr,
		(gdouble)GST_COLOR_R(color) / 255.0,
		(gdouble)GST_COLOR_G(color) / 255.0,
		(gdouble)GST_COLOR_B(color) / 255.0);
}

/*
 * wl_set_bg_color:
 * @self: the renderer
 * @cr: cairo context
 * @color: GstColor RGBA value
 *
 * Sets the cairo source color for background painting.
 * Applies the window opacity as alpha, so backgrounds
 * become semi-transparent while text stays fully opaque.
 */
static void
wl_set_bg_color(GstWaylandRenderer *self, cairo_t *cr, GstColor color)
{
	gdouble alpha;

	alpha = (self->wl_window != NULL)
		? gst_wayland_window_get_opacity(self->wl_window) : 1.0;

	cairo_set_source_rgba(cr,
		(gdouble)GST_COLOR_R(color) / 255.0,
		(gdouble)GST_COLOR_G(color) / 255.0,
		(gdouble)GST_COLOR_B(color) / 255.0,
		alpha);
}

/*
 * wl_clear_rect:
 * @self: the renderer
 * @x1: left edge
 * @y1: top edge
 * @x2: right edge (exclusive)
 * @y2: bottom edge (exclusive)
 *
 * Clears a rectangle on the surface with the background color.
 */
static void
wl_clear_rect(
	GstWaylandRenderer  *self,
	gint                x1,
	gint                y1,
	gint                x2,
	gint                y2
){
	if (self->cr == NULL) {
		return;
	}

	/* Use OPERATOR_SOURCE to write alpha directly into the buffer,
	 * rather than blending over existing content */
	wl_set_bg_color(self, self->cr, self->colors[self->default_bg]);
	cairo_set_operator(self->cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle(self->cr, (gdouble)x1, (gdouble)y1,
		(gdouble)(x2 - x1), (gdouble)(y2 - y1));
	cairo_fill(self->cr);
	cairo_set_operator(self->cr, CAIRO_OPERATOR_OVER);
}

/*
 * wl_fill_render_context:
 * @self: the renderer
 * @ctx: (out): render context to populate
 *
 * Populates a GstWaylandRenderContext with current renderer state.
 * Modules use this struct to access drawing resources.
 */
static void
wl_fill_render_context(
	GstWaylandRenderer       *self,
	GstWaylandRenderContext   *ctx
){
	/* Initialize vtable and backend type */
	gst_wayland_render_context_init_ops(ctx);

	/* Base context fields */
	ctx->base.cw         = self->cw;
	ctx->base.ch         = self->ch;
	ctx->base.borderpx   = self->borderpx;
	ctx->base.win_w      = self->win_w;
	ctx->base.win_h      = self->win_h;
	ctx->base.win_mode   = self->win_mode;
	ctx->base.glyph_attr = 0;

	/* Wayland/Cairo-specific fields */
	ctx->cr         = self->cr;
	ctx->surface    = self->cairo_surface;
	ctx->font_cache = self->font_cache;
	ctx->colors     = self->colors;
	ctx->num_colors = self->num_colors;
	ctx->fg         = self->colors[self->default_fg];
	ctx->bg         = self->colors[self->default_bg];
}

/*
 * resolve_fg_color:
 * @self: the renderer
 * @fg_idx: foreground color index (may be truecolor)
 * @mode: glyph attribute flags
 *
 * Resolves foreground color accounting for truecolor, bold
 * brightening, faint dimming, reverse, blink, and invisible.
 *
 * Returns: resolved GstColor
 */
static GstColor
resolve_fg_color(
	GstWaylandRenderer  *self,
	guint32             fg_idx,
	guint16             mode
){
	GstColor fg;

	if (GST_IS_TRUECOLOR(fg_idx)) {
		fg = GST_COLOR_RGB(TC_RED(fg_idx), TC_GREEN(fg_idx), TC_BLUE(fg_idx));
	} else if (fg_idx < self->num_colors) {
		fg = self->colors[fg_idx];
	} else {
		fg = self->colors[self->default_fg];
	}

	/* Bold brightening: shift colors 0-7 to 8-15 */
	if ((mode & GST_GLYPH_ATTR_BOLD) && !(mode & GST_GLYPH_ATTR_FAINT)
	    && !GST_IS_TRUECOLOR(fg_idx) && fg_idx <= 7) {
		fg = self->colors[fg_idx + 8];
	}

	/* Faint dimming: halve foreground RGB */
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
 * @self: the renderer
 * @bg_idx: background color index (may be truecolor)
 *
 * Resolves background color from index or truecolor value.
 *
 * Returns: resolved GstColor
 */
static GstColor
resolve_bg_color(
	GstWaylandRenderer  *self,
	guint32             bg_idx
){
	if (GST_IS_TRUECOLOR(bg_idx)) {
		return GST_COLOR_RGB(TC_RED(bg_idx), TC_GREEN(bg_idx), TC_BLUE(bg_idx));
	} else if (bg_idx < self->num_colors) {
		return self->colors[bg_idx];
	}
	return self->colors[self->default_bg];
}

/*
 * wl_draw_glyph_run:
 * @self: the renderer
 * @base: base glyph with attributes for this run
 * @len: number of character cells in the run
 * @x: starting column
 * @y: row
 *
 * Draws the background and decorations for a run of glyphs
 * with the same attributes. The actual glyph rendering is
 * handled per-glyph via the font cache.
 */
static void
wl_draw_glyph_run(
	GstWaylandRenderer      *self,
	GstGlyph                *base,
	GstLine                 *line,
	gint                    len,
	gint                    x,
	gint                    y
){
	guint16 mode;
	GstColor fg;
	GstColor bg;
	GstColor temp;
	gint charlen;
	gint winx;
	gint winy;
	gint width;
	gint ascent;
	gint i;
	cairo_scaled_font_t *scaled_font;
	gulong glyph_index;
	GstGlyph *g;
	GstRune rune;
	GstFontStyle fstyle;
	gfloat xp;

	if (self->cr == NULL) {
		return;
	}

	mode = (guint16)base->attr;
	charlen = len * ((mode & GST_GLYPH_ATTR_WIDE) ? 2 : 1);
	winx = self->borderpx + x * self->cw;
	winy = self->borderpx + y * self->ch;
	width = charlen * self->cw;

	/* Resolve colors */
	fg = resolve_fg_color(self, base->fg, mode);
	bg = resolve_bg_color(self, base->bg);

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
		wl_clear_rect(self, 0, (y == 0) ? 0 : winy,
			self->borderpx,
			winy + self->ch + ((winy + self->ch >= self->borderpx + self->th)
				? self->win_h : 0));
	}
	if (winx + width >= self->borderpx + self->tw) {
		wl_clear_rect(self, winx + width, (y == 0) ? 0 : winy,
			self->win_w,
			(winy + self->ch >= self->borderpx + self->th)
				? self->win_h : (winy + self->ch));
	}
	if (y == 0) {
		wl_clear_rect(self, winx, 0, winx + width, self->borderpx);
	}
	if (winy + self->ch >= self->borderpx + self->th) {
		wl_clear_rect(self, winx, winy + self->ch, winx + width, self->win_h);
	}

	/* Fill background with opacity-aware alpha */
	wl_set_bg_color(self, self->cr, bg);
	cairo_set_operator(self->cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle(self->cr, (gdouble)winx, (gdouble)winy,
		(gdouble)width, (gdouble)self->ch);
	cairo_fill(self->cr);
	cairo_set_operator(self->cr, CAIRO_OPERATOR_OVER);

	/* Set clipping for glyph rendering */
	cairo_save(self->cr);
	cairo_rectangle(self->cr, (gdouble)winx, (gdouble)winy,
		(gdouble)width, (gdouble)self->ch);
	cairo_clip(self->cr);

	/* Render glyphs */
	wl_set_source_color(self->cr, fg);
	ascent = gst_cairo_font_cache_get_ascent(self->font_cache);
	xp = (gfloat)winx;

	for (i = 0; i < len; i++) {
		gfloat runewidth;

		g = gst_line_get_glyph(line, x + i);
		if (g == NULL) {
			xp += (gfloat)self->cw;
			continue;
		}

		rune = g->rune;

		/* Skip wide char dummy cells */
		if (g->attr & GST_GLYPH_ATTR_WDUMMY) {
			continue;
		}

		runewidth = (gfloat)self->cw;
		if (g->attr & GST_GLYPH_ATTR_WIDE) {
			runewidth = (gfloat)(self->cw * 2);
		}

		/* Determine font style */
		fstyle = GST_FONT_STYLE_NORMAL;
		if ((mode & GST_GLYPH_ATTR_ITALIC) && (mode & GST_GLYPH_ATTR_BOLD)) {
			fstyle = GST_FONT_STYLE_BOLD_ITALIC;
		} else if (mode & GST_GLYPH_ATTR_ITALIC) {
			fstyle = GST_FONT_STYLE_ITALIC;
		} else if (mode & GST_GLYPH_ATTR_BOLD) {
			fstyle = GST_FONT_STYLE_BOLD;
		}

		/* Look up and render glyph */
		if (gst_cairo_font_cache_lookup_glyph(self->font_cache,
		    rune, fstyle, &scaled_font, &glyph_index))
		{
			cairo_glyph_t cg;

			cairo_set_scaled_font(self->cr, scaled_font);
			cg.index = glyph_index;
			cg.x = (gdouble)xp;
			cg.y = (gdouble)(winy + ascent);
			cairo_show_glyphs(self->cr, &cg, 1);
		}

		xp += runewidth;
	}

	/* Restore clipping */
	cairo_restore(self->cr);

	/* Underline decoration */
	if (mode & GST_GLYPH_ATTR_UNDERLINE) {
		wl_set_source_color(self->cr, fg);
		cairo_rectangle(self->cr, (gdouble)winx,
			(gdouble)(winy + ascent + 1),
			(gdouble)width, 1.0);
		cairo_fill(self->cr);
	}

	/* Strikethrough decoration */
	if (mode & GST_GLYPH_ATTR_STRUCK) {
		wl_set_source_color(self->cr, fg);
		cairo_rectangle(self->cr, (gdouble)winx,
			(gdouble)(winy + 2 * ascent / 3),
			(gdouble)width, 1.0);
		cairo_fill(self->cr);
	}

	/* Undercurl decoration (sine wave below baseline) */
	if (mode & GST_GLYPH_ATTR_UNDERCURL) {
		gint uc_x;

		wl_set_source_color(self->cr, fg);
		for (uc_x = 0; uc_x < width; uc_x++) {
			gint dy;

			dy = (gint)(sin((double)uc_x * M_PI / ((double)self->cw * 0.5)) * 1.5);
			cairo_rectangle(self->cr,
				(gdouble)(winx + uc_x),
				(gdouble)(winy + ascent + 1 + dy),
				1.0, 1.0);
			cairo_fill(self->cr);
		}
	}
}

/* ===== Virtual method implementations ===== */

/*
 * wl_renderer_draw_line_impl:
 * @renderer: the GstRenderer
 * @row: row index
 * @x1: start column
 * @x2: end column (exclusive)
 *
 * Draws a single line, grouping glyphs by attributes for
 * efficient batch rendering.
 */
static void
wl_renderer_draw_line_impl(
	GstRenderer     *renderer,
	gint            row,
	gint            x1,
	gint            x2
){
	GstWaylandRenderer *self;
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
	GstWaylandRenderContext gt_ctx;

	self = GST_WAYLAND_RENDERER(renderer);
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
		wl_fill_render_context(self, &gt_ctx);
	}

	i = 0;
	ox = x1;
	memset(&base, 0, sizeof(GstGlyph));

	/* Iterate and group by matching attributes */
	for (x = x1; x < x2; x++) {
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

			pixel_x = self->borderpx + x * self->cw;
			pixel_y = self->borderpx + row * self->ch;

			if (gst_module_manager_dispatch_glyph_transform(
				mgr, cur.rune, &gt_ctx.base,
				pixel_x, pixel_y, self->cw, self->ch))
			{
				/* Flush accumulated run before skipping */
				if (i > 0) {
					wl_draw_glyph_run(self, &base, line, i, ox, row);
					i = 0;
				}
				ox = x + 1;
				continue;
			}
		}

		/* If attributes changed, flush the accumulated run */
		if (i > 0 && ATTRCMP(base, cur)) {
			wl_draw_glyph_run(self, &base, line, i, ox, row);
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
		wl_draw_glyph_run(self, &base, line, i, ox, row);
	}
}

/*
 * wl_renderer_draw_cursor_impl:
 * @renderer: the GstRenderer
 * @cx: current cursor column
 * @cy: current cursor row
 * @ox: old cursor column
 * @oy: old cursor row
 *
 * Draws the cursor, first erasing the old one. Supports block,
 * underline, and bar cursor styles, plus hollow box when unfocused.
 */
static void
wl_renderer_draw_cursor_impl(
	GstRenderer     *renderer,
	gint            cx,
	gint            cy,
	gint            ox,
	gint            oy
){
	GstWaylandRenderer *self;
	GstTerminal *term;
	GstGlyph *g;
	GstCursor *cursor;
	GstColor drawcol;
	gint winx;
	gint winy;

	self = GST_WAYLAND_RENDERER(renderer);
	term = gst_renderer_get_terminal(renderer);
	if (term == NULL || self->cr == NULL) {
		return;
	}

	/* Erase old cursor by redrawing that cell */
	wl_renderer_draw_line_impl(renderer, oy, ox, ox + 1);

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

	winx = self->borderpx + cx * self->cw;
	winy = self->borderpx + cy * self->ch;

	if (self->win_mode & GST_WIN_MODE_FOCUSED) {
		/* Focused: draw filled cursor based on shape */
		switch (cursor->shape) {
		case GST_CURSOR_SHAPE_BLOCK:
			/* Draw the glyph with inverted colors */
			{
				GstGlyph block_g;
				GstLine *cursor_line;

				block_g = *g;
				block_g.fg = (guint32)self->default_bg;
				block_g.bg = (guint32)self->default_cs;
				block_g.attr = g->attr & (GST_GLYPH_ATTR_BOLD | GST_GLYPH_ATTR_ITALIC
					| GST_GLYPH_ATTR_UNDERLINE | GST_GLYPH_ATTR_STRUCK
					| GST_GLYPH_ATTR_WIDE);

				cursor_line = gst_terminal_get_line(term, cy);
				if (cursor_line != NULL) {
					wl_draw_glyph_run(self, &block_g, cursor_line, 1, cx, cy);
				}
			}
			break;
		case GST_CURSOR_SHAPE_UNDERLINE:
			wl_set_source_color(self->cr, drawcol);
			cairo_rectangle(self->cr,
				(gdouble)winx,
				(gdouble)(winy + self->ch - GST_CURSOR_THICKNESS),
				(gdouble)self->cw, (gdouble)GST_CURSOR_THICKNESS);
			cairo_fill(self->cr);
			break;
		case GST_CURSOR_SHAPE_BAR:
			wl_set_source_color(self->cr, drawcol);
			cairo_rectangle(self->cr,
				(gdouble)winx,
				(gdouble)winy,
				(gdouble)GST_CURSOR_THICKNESS, (gdouble)self->ch);
			cairo_fill(self->cr);
			break;
		}
	} else {
		/* Unfocused: hollow box cursor */
		wl_set_source_color(self->cr, drawcol);
		/* Top edge */
		cairo_rectangle(self->cr,
			(gdouble)winx, (gdouble)winy,
			(gdouble)(self->cw - 1), 1.0);
		cairo_fill(self->cr);
		/* Left edge */
		cairo_rectangle(self->cr,
			(gdouble)winx, (gdouble)winy,
			1.0, (gdouble)(self->ch - 1));
		cairo_fill(self->cr);
		/* Right edge */
		cairo_rectangle(self->cr,
			(gdouble)(winx + self->cw - 1), (gdouble)winy,
			1.0, (gdouble)(self->ch - 1));
		cairo_fill(self->cr);
		/* Bottom edge */
		cairo_rectangle(self->cr,
			(gdouble)winx, (gdouble)(winy + self->ch - 1),
			(gdouble)self->cw, 1.0);
		cairo_fill(self->cr);
	}
}

/*
 * wl_renderer_render_impl:
 * @renderer: the GstRenderer
 *
 * Full render pass: iterate dirty lines, draw cursor,
 * commit surface. Uses dirty-line optimization.
 */
static void
wl_renderer_render_impl(GstRenderer *renderer)
{
	GstWaylandRenderer *self;
	GstTerminal *term;
	GstCursor *cursor;
	gint rows;
	gint cols;
	gint y;
	gint cx;
	gint cy;

	self = GST_WAYLAND_RENDERER(renderer);
	term = gst_renderer_get_terminal(renderer);
	if (term == NULL || self->cr == NULL) {
		return;
	}

	gst_terminal_get_size(term, &cols, &rows);
	cursor = gst_terminal_get_cursor(term);
	cx = cursor->x;
	cy = cursor->y;

	/*
	 * Detect opacity changes (set by the transparency module via
	 * the render overlay callback on the previous frame). When
	 * opacity changes, repaint the entire surface background and
	 * mark all lines dirty so every cell gets redrawn with the
	 * new alpha value.
	 */
	if (self->wl_window != NULL) {
		gdouble cur_opacity;

		cur_opacity = gst_wayland_window_get_opacity(self->wl_window);
		if (cur_opacity != self->last_opacity) {
			self->last_opacity = cur_opacity;

			/* Repaint full surface background at new opacity */
			if (self->colors != NULL) {
				wl_set_bg_color(self, self->cr,
					self->colors[self->default_bg]);
				cairo_set_operator(self->cr, CAIRO_OPERATOR_SOURCE);
				cairo_paint(self->cr);
				cairo_set_operator(self->cr, CAIRO_OPERATOR_OVER);
			}

			/* Mark all lines dirty for full redraw */
			for (y = 0; y < rows; y++) {
				gst_terminal_mark_dirty(term, y);
			}
		}
	}

	/* Draw dirty lines */
	for (y = 0; y < rows; y++) {
		GstLine *line;

		line = gst_terminal_get_line(term, y);
		if (line == NULL) {
			continue;
		}

		if (gst_line_is_dirty(line)) {
			wl_renderer_draw_line_impl(renderer, y, 0, cols);
		}
	}

	/* Draw cursor */
	wl_renderer_draw_cursor_impl(renderer, cx, cy, self->ocx, self->ocy);
	self->ocx = cx;
	self->ocy = cy;

	/* Dispatch render overlays to modules */
	{
		GstModuleManager *mgr;
		GstWaylandRenderContext ctx;

		mgr = gst_module_manager_get_default();
		wl_fill_render_context(self, &ctx);
		gst_module_manager_dispatch_render_overlay(
			mgr, &ctx.base, self->win_w, self->win_h);
	}

	/* Flush Cairo surface and commit to Wayland */
	cairo_surface_flush(self->cairo_surface);
	wl_surface_attach(self->wl_surface, self->buffer, 0, 0);
	wl_surface_damage_buffer(self->wl_surface, 0, 0,
		self->win_w, self->win_h);
	wl_surface_commit(self->wl_surface);

	/* Clear terminal dirty flags */
	gst_terminal_clear_dirty(term);
}

/*
 * wl_renderer_resize_impl:
 * @renderer: the GstRenderer
 * @width: new window width in pixels
 * @height: new window height in pixels
 *
 * Handles window resize: recreates the shared-memory buffer
 * and Cairo surface at the new size.
 */
static void
wl_renderer_resize_impl(
	GstRenderer     *renderer,
	guint           width,
	guint           height
){
	GstWaylandRenderer *self;
	GstTerminal *term;
	gint cols;
	gint rows;

	self = GST_WAYLAND_RENDERER(renderer);
	self->win_w = (gint)width;
	self->win_h = (gint)height;

	/* Refresh cell dimensions from font cache (picks up zoom changes) */
	if (self->font_cache != NULL) {
		self->cw = gst_cairo_font_cache_get_char_width(self->font_cache);
		self->ch = gst_cairo_font_cache_get_char_height(self->font_cache);
	}

	term = gst_renderer_get_terminal(renderer);
	if (term != NULL) {
		gst_terminal_get_size(term, &cols, &rows);
		self->tw = cols * self->cw;
		self->th = rows * self->ch;
	}

	/* Recreate shm buffer and cairo surface */
	wl_create_buffer(self, self->win_w, self->win_h);

	/* Fill with background color (alpha-aware) */
	if (self->cr != NULL && self->colors != NULL) {
		wl_set_bg_color(self, self->cr, self->colors[self->default_bg]);
		cairo_set_operator(self->cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(self->cr);
		cairo_set_operator(self->cr, CAIRO_OPERATOR_OVER);
	}
}

/*
 * wl_renderer_clear_impl:
 * @renderer: the GstRenderer
 *
 * Clears the entire render surface to background color.
 */
static void
wl_renderer_clear_impl(GstRenderer *renderer)
{
	GstWaylandRenderer *self;

	self = GST_WAYLAND_RENDERER(renderer);

	if (self->cr != NULL && self->colors != NULL) {
		wl_set_bg_color(self, self->cr, self->colors[self->default_bg]);
		cairo_set_operator(self->cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(self->cr);
		cairo_set_operator(self->cr, CAIRO_OPERATOR_OVER);
	}
}

/*
 * wl_renderer_start_draw_impl:
 * @renderer: the GstRenderer
 *
 * Checks if Cairo drawing is ready.
 *
 * Returns: TRUE if drawing can proceed
 */
static gboolean
wl_renderer_start_draw_impl(GstRenderer *renderer)
{
	GstWaylandRenderer *self;

	self = GST_WAYLAND_RENDERER(renderer);

	if (!(self->win_mode & GST_WIN_MODE_VISIBLE)) {
		return FALSE;
	}

	return (self->cr != NULL);
}

/*
 * wl_renderer_finish_draw_impl:
 * @renderer: the GstRenderer
 *
 * Flushes the Cairo surface and commits the buffer
 * to the Wayland surface.
 */
static void
wl_renderer_finish_draw_impl(GstRenderer *renderer)
{
	GstWaylandRenderer *self;

	self = GST_WAYLAND_RENDERER(renderer);

	if (self->cr == NULL || self->cairo_surface == NULL) {
		return;
	}

	cairo_surface_flush(self->cairo_surface);
	wl_surface_attach(self->wl_surface, self->buffer, 0, 0);
	wl_surface_damage_buffer(self->wl_surface, 0, 0,
		self->win_w, self->win_h);
	wl_surface_commit(self->wl_surface);

	if (self->wl_display != NULL) {
		wl_display_flush(self->wl_display);
	}
}

/* ===== GObject lifecycle ===== */

static void
gst_wayland_renderer_dispose(GObject *object)
{
	GstWaylandRenderer *self;

	self = GST_WAYLAND_RENDERER(object);

	/* Free Cairo resources */
	if (self->cr != NULL) {
		cairo_destroy(self->cr);
		self->cr = NULL;
	}
	if (self->cairo_surface != NULL) {
		cairo_surface_destroy(self->cairo_surface);
		self->cairo_surface = NULL;
	}

	/* Free Wayland buffer resources */
	if (self->buffer != NULL) {
		wl_buffer_destroy(self->buffer);
		self->buffer = NULL;
	}
	if (self->shm_pool != NULL) {
		wl_shm_pool_destroy(self->shm_pool);
		self->shm_pool = NULL;
	}

	/* Unmap shared memory */
	if (self->shm_data != NULL) {
		munmap(self->shm_data, self->shm_size);
		self->shm_data = NULL;
	}
	if (self->shm_fd >= 0) {
		close(self->shm_fd);
		self->shm_fd = -1;
	}

	/* Free color palette */
	g_clear_pointer(&self->colors, g_free);
	self->num_colors = 0;

	g_clear_object(&self->selection);

	G_OBJECT_CLASS(gst_wayland_renderer_parent_class)->dispose(object);
}

/*
 * wl_renderer_capture_screenshot_impl:
 *
 * Captures the Cairo image surface as RGBA pixel data.
 * Cairo ARGB32 stores pixels as 0xAARRGGBB in native byte order,
 * which on little-endian is byte order: BB GG RR AA. We convert
 * per-pixel to RGBA order.
 */
static GBytes *
wl_renderer_capture_screenshot_impl(
	GstRenderer *renderer,
	gint        *out_width,
	gint        *out_height,
	gint        *out_stride
){
	GstWaylandRenderer *self;
	guint8 *src_data;
	gint w, h, src_stride, dst_stride;
	guint8 *rgba;
	gint x, y;

	self = GST_WAYLAND_RENDERER(renderer);

	w = self->win_w;
	h = self->win_h;

	if (w <= 0 || h <= 0 || self->cairo_surface == NULL) {
		return NULL;
	}

	/* Flush pending drawing operations to the surface */
	cairo_surface_flush(self->cairo_surface);

	src_data = cairo_image_surface_get_data(self->cairo_surface);
	if (src_data == NULL) {
		return NULL;
	}

	src_stride = cairo_image_surface_get_stride(self->cairo_surface);

	/* Allocate RGBA output buffer (4 bytes per pixel) */
	dst_stride = w * 4;
	rgba = (guint8 *)g_malloc((gsize)dst_stride * (gsize)h);

	/*
	 * Convert from Cairo ARGB32 to RGBA.
	 * Cairo ARGB32 on little-endian: byte order is B, G, R, A.
	 */
	for (y = 0; y < h; y++) {
		guint8 *src_row;
		guint8 *dst_row;

		src_row = src_data + y * src_stride;
		dst_row = rgba + y * dst_stride;
		for (x = 0; x < w; x++) {
			guint8 *src;
			guint8 *dst;

			src = src_row + x * BYTES_PER_PIXEL;
			dst = dst_row + x * 4;
			dst[0] = src[2]; /* R (from Cairo B,G,R,A layout) */
			dst[1] = src[1]; /* G */
			dst[2] = src[0]; /* B */
			dst[3] = src[3]; /* A */
		}
	}

	if (out_width != NULL)  *out_width  = w;
	if (out_height != NULL) *out_height = h;
	if (out_stride != NULL) *out_stride = dst_stride;

	return g_bytes_new_take(rgba, (gsize)dst_stride * (gsize)h);
}

static void
gst_wayland_renderer_class_init(GstWaylandRendererClass *klass)
{
	GObjectClass *object_class;
	GstRendererClass *renderer_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_wayland_renderer_dispose;

	renderer_class = GST_RENDERER_CLASS(klass);
	renderer_class->render = wl_renderer_render_impl;
	renderer_class->resize = wl_renderer_resize_impl;
	renderer_class->clear = wl_renderer_clear_impl;
	renderer_class->draw_line = wl_renderer_draw_line_impl;
	renderer_class->draw_cursor = wl_renderer_draw_cursor_impl;
	renderer_class->start_draw = wl_renderer_start_draw_impl;
	renderer_class->finish_draw = wl_renderer_finish_draw_impl;
	renderer_class->capture_screenshot = wl_renderer_capture_screenshot_impl;
}

static void
gst_wayland_renderer_init(GstWaylandRenderer *self)
{
	self->wl_window = NULL;
	self->wl_display = NULL;
	self->wl_surface = NULL;
	self->wl_shm = NULL;
	self->shm_fd = -1;
	self->shm_data = NULL;
	self->shm_size = 0;
	self->shm_pool = NULL;
	self->buffer = NULL;
	self->cairo_surface = NULL;
	self->cr = NULL;
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
	self->last_opacity = 1.0;
}

/* ===== Public API ===== */

/**
 * gst_wayland_renderer_new:
 * @terminal: the terminal to render
 * @wl_window: Wayland window (display/surface/shm extracted internally)
 * @font_cache: Cairo font cache
 * @borderpx: border padding in pixels
 *
 * Creates a new Wayland renderer with Cairo drawing and
 * wl_shm double-buffered rendering. Stores a reference to the
 * window so the opacity can be read for background alpha painting.
 *
 * Returns: (transfer full): A new #GstWaylandRenderer
 */
GstWaylandRenderer *
gst_wayland_renderer_new(
	GstTerminal         *terminal,
	GstWaylandWindow    *wl_window,
	GstCairoFontCache   *font_cache,
	gint                borderpx
){
	GstWaylandRenderer *self;
	gint cols;
	gint rows;

	self = (GstWaylandRenderer *)g_object_new(GST_TYPE_WAYLAND_RENDERER,
		"terminal", terminal,
		NULL);

	self->wl_window = wl_window;
	self->wl_display = gst_wayland_window_get_display(wl_window);
	self->wl_surface = gst_wayland_window_get_surface(wl_window);
	self->wl_shm = gst_wayland_window_get_shm(wl_window);
	self->font_cache = font_cache;
	self->borderpx = borderpx;

	/* Get cell dimensions from font cache */
	self->cw = gst_cairo_font_cache_get_char_width(font_cache);
	self->ch = gst_cairo_font_cache_get_char_height(font_cache);

	/* Calculate pixel dimensions */
	gst_terminal_get_size(terminal, &cols, &rows);
	self->tw = cols * self->cw;
	self->th = rows * self->ch;
	self->win_w = 2 * borderpx + self->tw;
	self->win_h = 2 * borderpx + self->th;

	/* Create initial shm buffer and cairo surface */
	wl_create_buffer(self, self->win_w, self->win_h);

	return self;
}

/**
 * gst_wayland_renderer_load_colors:
 * @self: A #GstWaylandRenderer
 * @config: (nullable): A #GstConfig for palette and color overrides
 *
 * Loads the full color palette (262 colors) from defaults,
 * then applies any overrides from @config.
 *
 * Returns: TRUE on success
 */
gboolean
gst_wayland_renderer_load_colors(
	GstWaylandRenderer  *self,
	GstConfig           *config
){
	gsize i;
	gsize count;

	g_return_val_if_fail(GST_IS_WAYLAND_RENDERER(self), FALSE);

	/* Free old colors if any */
	g_clear_pointer(&self->colors, g_free);

	/* Step 1: Load all 262 colors from hardcoded defaults */
	count = (gsize)GST_COLOR_COUNT;
	self->colors = g_new0(GstColor, count);
	self->num_colors = count;

	for (i = 0; i < count; i++) {
		const gchar *name;

		name = NULL;

		if (i >= 16 && i <= 255) {
			/* Generate programmatic colors */
			if (i < 6 * 6 * 6 + 16) {
				/* 6x6x6 RGB cube (indices 16-231) */
				guint8 r;
				guint8 g;
				guint8 b;

				r = sixd_to_8bit(((gint)(i - 16) / 36) % 6);
				g = sixd_to_8bit(((gint)(i - 16) / 6) % 6);
				b = sixd_to_8bit(((gint)(i - 16) / 1) % 6);
				self->colors[i] = GST_COLOR_RGB(r, g, b);
			} else {
				/* Grayscale ramp (indices 232-255) */
				guint8 v;

				v = (guint8)(0x08 + 0x0a * (gint)(i - (6 * 6 * 6 + 16)));
				self->colors[i] = GST_COLOR_RGB(v, v, v);
			}
		} else {
			/* Use default color name table */
			if (i < G_N_ELEMENTS(default_colorname)
			    && default_colorname[i] != NULL) {
				name = default_colorname[i];
			}

			if (name != NULL) {
				if (!parse_color_name(name, &self->colors[i])) {
					g_warning("gst_wayland_renderer_load_colors: "
						"could not parse color %zu: %s", i, name);
					self->colors[i] = GST_COLOR_RGB(0, 0, 0);
				}
			} else {
				self->colors[i] = GST_COLOR_RGB(0, 0, 0);
			}
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
				parse_color_name(palette_hex[i], &self->colors[i]);
			}
		}

		/* Override foreground (index 256) */
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

		/* Override background (index 257) */
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

		/* Override cursor foreground (index 258) */
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

		/* Override cursor background (index 259) */
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

	/* Fill with background color (alpha-aware) */
	if (self->cr != NULL) {
		wl_set_bg_color(self, self->cr, self->colors[self->default_bg]);
		cairo_set_operator(self->cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(self->cr);
		cairo_set_operator(self->cr, CAIRO_OPERATOR_OVER);
	}

	return TRUE;
}

/**
 * gst_wayland_renderer_set_win_mode:
 * @self: A #GstWaylandRenderer
 * @mode: new window mode flags
 *
 * Updates the window mode flags.
 */
void
gst_wayland_renderer_set_win_mode(
	GstWaylandRenderer  *self,
	GstWinMode          mode
){
	g_return_if_fail(GST_IS_WAYLAND_RENDERER(self));

	self->win_mode = mode;
}

/**
 * gst_wayland_renderer_get_win_mode:
 * @self: A #GstWaylandRenderer
 *
 * Gets the current window mode flags.
 *
 * Returns: the current GstWinMode flags
 */
GstWinMode
gst_wayland_renderer_get_win_mode(GstWaylandRenderer *self)
{
	g_return_val_if_fail(GST_IS_WAYLAND_RENDERER(self), 0);

	return self->win_mode;
}

/**
 * gst_wayland_renderer_set_selection:
 * @self: A #GstWaylandRenderer
 * @selection: (transfer none): A #GstSelection for highlight checks
 *
 * Sets the selection object. The renderer uses this to determine
 * which cells to render with reverse video during draw_line.
 */
void
gst_wayland_renderer_set_selection(
	GstWaylandRenderer  *self,
	GstSelection        *selection
){
	g_return_if_fail(GST_IS_WAYLAND_RENDERER(self));

	if (self->selection != NULL) {
		g_object_unref(self->selection);
	}
	self->selection = (selection != NULL) ? g_object_ref(selection) : NULL;
}
