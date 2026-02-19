/*
 * gst-sixel-module.c - DEC Sixel graphics protocol module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the DEC Sixel graphics protocol for displaying inline
 * images in the terminal. Intercepts DCS escape sequences via
 * GstEscapeHandler, decodes sixel data into RGBA pixel buffers,
 * and renders placements via GstRenderOverlay.
 *
 * Protocol format:
 *   ESC P Pn ; Pn ; Pn q <sixel-data> ESC \
 *
 * Sixel data characters:
 *   ? (0x3F) through ~ (0x7E): each encodes 6 vertical pixels.
 *     Subtract 0x3F to get the 6-bit pattern. Bit 0 = top pixel.
 *   # <color spec>: color introduction
 *     #idx         - select color index
 *     #idx;2;r;g;b - define color (r,g,b are 0-100 percentages)
 *   !count char    - repeat the sixel char count times
 *   $              - carriage return (move to left edge of current sixel row)
 *   -              - newline (advance 6 pixels down, reset x to 0)
 *
 * The terminal's escape parser receives the full DCS string and
 * dispatches it through the module manager to this module.
 */

#include "gst-sixel-module.h"

#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/boxed/gst-cursor.h"
#include "../../src/rendering/gst-render-context.h"

#include <string.h>
#include <stdlib.h>

/* ===== Constants ===== */

/* Default configuration values */
#define SIXEL_DEFAULT_MAX_WIDTH       (4096)
#define SIXEL_DEFAULT_MAX_HEIGHT      (4096)
#define SIXEL_DEFAULT_MAX_COLORS      (1024)
#define SIXEL_DEFAULT_MAX_RAM_MB      (128)
#define SIXEL_DEFAULT_MAX_PLACEMENTS  (256)

/* Sixel character range: ? (0x3F) through ~ (0x7E) */
#define SIXEL_CHAR_MIN  (0x3F)
#define SIXEL_CHAR_MAX  (0x7E)

/* RGBA bytes per pixel */
#define SIXEL_BPP       (4)

/* Number of vertical pixels per sixel character */
#define SIXEL_BAND_HEIGHT (6)

/* Initial pixel buffer dimensions (grows as needed) */
#define SIXEL_INIT_WIDTH  (256)
#define SIXEL_INIT_HEIGHT (256)

/* ===== Default VGA palette (16 colors) ===== */

/*
 * Standard VGA 16-color palette used as defaults when sixel
 * data doesn't define its own colors via # commands.
 * Format: { R, G, B } with values 0-255.
 */
static const guint8 sixel_default_palette[16][3] = {
	{  0,   0,   0},   /* 0:  black */
	{187,   0,   0},   /* 1:  red */
	{  0, 187,   0},   /* 2:  green */
	{187, 187,   0},   /* 3:  yellow */
	{  0,   0, 187},   /* 4:  blue */
	{187,   0, 187},   /* 5:  magenta */
	{  0, 187, 187},   /* 6:  cyan */
	{187, 187, 187},   /* 7:  white */
	{ 85,  85,  85},   /* 8:  bright black */
	{255,  85,  85},   /* 9:  bright red */
	{ 85, 255,  85},   /* 10: bright green */
	{255, 255,  85},   /* 11: bright yellow */
	{ 85,  85, 255},   /* 12: bright blue */
	{255,  85, 255},   /* 13: bright magenta */
	{ 85, 255, 255},   /* 14: bright cyan */
	{255, 255, 255},   /* 15: bright white */
};

/* ===== Sixel placement structure ===== */

/*
 * SixelPlacement:
 *
 * Represents a decoded sixel image placed on the terminal screen.
 * Stores the RGBA pixel data and its position in terminal coordinates.
 */
typedef struct
{
	guint32  id;         /* auto-incrementing placement ID */
	gint     row;        /* terminal row where the image starts */
	gint     col;        /* terminal column where the image starts */
	gint     width;      /* image width in pixels */
	gint     height;     /* image height in pixels */
	gint     stride;     /* bytes per row (width * SIXEL_BPP) */
	guint8  *data;       /* RGBA pixel data (row-major) */
	gsize    data_size;  /* total allocation size of data in bytes */
} SixelPlacement;

/* ===== Sixel color entry ===== */

/*
 * SixelColor:
 *
 * A single entry in the sixel color palette.
 */
typedef struct
{
	guint8 r;
	guint8 g;
	guint8 b;
} SixelColor;

/* ===== Parser state ===== */

/*
 * SixelParserState:
 *
 * Tracks the state machine while parsing sixel data.
 * The parser operates in a single pass over the DCS content.
 */
typedef enum
{
	SIXEL_STATE_DATA,      /* normal sixel data characters */
	SIXEL_STATE_COLOR,     /* inside a # color command */
	SIXEL_STATE_REPEAT,    /* inside a ! repeat command */
} SixelParserState;

/* ===== Type definition ===== */

struct _GstSixelModule
{
	GstModule parent_instance;

	/* Placement storage: hash table of id -> SixelPlacement* */
	GHashTable *placements;

	/* Next auto-incrementing placement ID */
	guint32 next_id;

	/* Signal handler IDs */
	gulong sig_scrolled;

	/* Total RAM usage across all placements (bytes) */
	gsize total_ram;

	/* Config values */
	gint max_width;
	gint max_height;
	gint max_colors;
	gint max_ram_mb;
	gint max_placements;
};

/* ===== Interface forward declarations ===== */

static gboolean sixel_handle_escape(GstEscapeHandler *handler,
                                    gchar str_type, const gchar *buf,
                                    gsize len, gpointer terminal);
static void     sixel_render(GstRenderOverlay *overlay,
                             gpointer ctx, gint w, gint h);

static void gst_sixel_escape_handler_init(GstEscapeHandlerInterface *iface);
static void gst_sixel_render_overlay_init(GstRenderOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstSixelModule, gst_sixel_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_sixel_escape_handler_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_sixel_render_overlay_init))

/* ===== Interface init ===== */

static void
gst_sixel_escape_handler_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string = sixel_handle_escape;
}

static void
gst_sixel_render_overlay_init(GstRenderOverlayInterface *iface)
{
	iface->render = sixel_render;
}

/* ===== Placement management ===== */

/*
 * sixel_placement_free:
 *
 * Frees a SixelPlacement and its pixel data.
 */
static void
sixel_placement_free(gpointer data)
{
	SixelPlacement *pl;

	pl = (SixelPlacement *)data;
	if (pl != NULL) {
		g_free(pl->data);
		g_free(pl);
	}
}

/*
 * sixel_evict_oldest:
 * @self: the sixel module
 *
 * Evicts the placement with the lowest ID (oldest) to free RAM.
 * Called when total_ram exceeds the configured budget.
 */
static void
sixel_evict_oldest(GstSixelModule *self)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	guint32 oldest_id;
	SixelPlacement *oldest_pl;

	oldest_id = G_MAXUINT32;
	oldest_pl = NULL;

	g_hash_table_iter_init(&iter, self->placements);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		SixelPlacement *pl;

		pl = (SixelPlacement *)value;
		if (pl->id < oldest_id) {
			oldest_id = pl->id;
			oldest_pl = pl;
		}
	}

	if (oldest_pl != NULL) {
		self->total_ram -= oldest_pl->data_size;
		g_hash_table_remove(self->placements,
			GUINT_TO_POINTER(oldest_id));
	}
}

/*
 * sixel_enforce_limits:
 * @self: the sixel module
 *
 * Enforces max_placements and max_total_ram_mb by evicting
 * oldest placements until the limits are satisfied.
 */
static void
sixel_enforce_limits(GstSixelModule *self)
{
	gsize max_ram_bytes;

	max_ram_bytes = (gsize)self->max_ram_mb * 1024 * 1024;

	/* Evict oldest placements until within limits */
	while ((gint)g_hash_table_size(self->placements) > self->max_placements ||
	       self->total_ram > max_ram_bytes) {
		if (g_hash_table_size(self->placements) == 0) {
			break;
		}
		sixel_evict_oldest(self);
	}
}

/* ===== Sixel parser ===== */

/*
 * sixel_parse_params:
 * @buf: DCS buffer starting after the 'P' type byte
 * @len: length of the buffer
 * @data_start: (out): offset where sixel data begins (after 'q')
 *
 * Skips the DCS numeric parameters (Pn;Pn;Pn) and the 'q'
 * introducer to find the start of actual sixel data.
 *
 * Returns: %TRUE if the 'q' introducer was found
 */
static gboolean
sixel_parse_params(
	const gchar *buf,
	gsize        len,
	gsize       *data_start
){
	gsize i;

	/*
	 * The DCS content starts with optional numeric parameters
	 * separated by semicolons, followed by 'q'. We need to skip
	 * past the 'q' to find where actual sixel data begins.
	 */
	for (i = 0; i < len; i++) {
		if (buf[i] == 'q') {
			*data_start = i + 1;
			return TRUE;
		}

		/* Parameters are digits and semicolons only */
		if (buf[i] != ';' && (buf[i] < '0' || buf[i] > '9') &&
		    buf[i] != ' ') {
			/* Unexpected character before 'q' */
			return FALSE;
		}
	}

	return FALSE;
}

/*
 * sixel_ensure_buffer:
 * @pixels: pointer to the pixel buffer (may be reallocated)
 * @buf_w: pointer to current buffer width (may be increased)
 * @buf_h: pointer to current buffer height (may be increased)
 * @need_w: required width in pixels
 * @need_h: required height in pixels
 * @max_w: maximum allowed width
 * @max_h: maximum allowed height
 *
 * Grows the pixel buffer if needed to accommodate the required
 * dimensions. Doubles the size each time for amortized O(1) growth.
 * New pixels are zero-initialized (transparent black).
 *
 * Returns: %TRUE if the buffer is large enough (possibly after resize)
 */
static gboolean
sixel_ensure_buffer(
	guint8 **pixels,
	gint    *buf_w,
	gint    *buf_h,
	gint     need_w,
	gint     need_h,
	gint     max_w,
	gint     max_h
){
	gint new_w;
	gint new_h;
	guint8 *new_buf;
	gint old_stride;
	gint new_stride;
	gint y;

	if (need_w <= *buf_w && need_h <= *buf_h) {
		return TRUE;
	}

	/* Clamp to max dimensions */
	if (need_w > max_w || need_h > max_h) {
		return FALSE;
	}

	/* Double the size, but at least accommodate the needed dims */
	new_w = *buf_w;
	while (new_w < need_w) {
		new_w *= 2;
	}
	if (new_w > max_w) {
		new_w = max_w;
	}

	new_h = *buf_h;
	while (new_h < need_h) {
		new_h *= 2;
	}
	if (new_h > max_h) {
		new_h = max_h;
	}

	/* Allocate new buffer (zero-initialized = transparent) */
	new_stride = new_w * SIXEL_BPP;
	new_buf = (guint8 *)g_malloc0((gsize)new_stride * (gsize)new_h);

	/* Copy existing pixel data row by row */
	if (*pixels != NULL) {
		old_stride = *buf_w * SIXEL_BPP;
		for (y = 0; y < *buf_h; y++) {
			memcpy(new_buf + (gsize)y * (gsize)new_stride,
			       *pixels + (gsize)y * (gsize)old_stride,
			       (gsize)old_stride);
		}
		g_free(*pixels);
	}

	*pixels = new_buf;
	*buf_w = new_w;
	*buf_h = new_h;

	return TRUE;
}

/*
 * sixel_put_pixel:
 * @pixels: RGBA pixel buffer
 * @buf_w: buffer width
 * @buf_h: buffer height
 * @x: pixel x coordinate
 * @y: pixel y coordinate
 * @color: the color to write
 *
 * Writes a single RGBA pixel into the buffer at (x, y).
 * Silently ignores out-of-bounds coordinates.
 */
static inline void
sixel_put_pixel(
	guint8          *pixels,
	gint             buf_w,
	gint             buf_h,
	gint             x,
	gint             y,
	const SixelColor *color
){
	gsize offset;

	if (x < 0 || x >= buf_w || y < 0 || y >= buf_h) {
		return;
	}

	offset = ((gsize)y * (gsize)buf_w + (gsize)x) * SIXEL_BPP;
	pixels[offset + 0] = color->r;
	pixels[offset + 1] = color->g;
	pixels[offset + 2] = color->b;
	pixels[offset + 3] = 255; /* fully opaque */
}

/*
 * sixel_decode:
 * @data: sixel data bytes (after the 'q')
 * @data_len: length of sixel data
 * @out_pixels: (out): decoded RGBA pixel buffer
 * @out_width: (out): image width in pixels
 * @out_height: (out): image height in pixels
 * @max_w: maximum allowed width
 * @max_h: maximum allowed height
 * @max_colors: maximum palette size
 *
 * Parses the sixel data stream and produces an RGBA pixel buffer.
 * Implements a state machine that handles sixel data characters,
 * color commands (#), repeat commands (!), CR ($), and NL (-).
 *
 * Returns: %TRUE if decoding succeeded
 */
static gboolean
sixel_decode(
	const gchar *data,
	gsize        data_len,
	guint8     **out_pixels,
	gint        *out_width,
	gint        *out_height,
	gint         max_w,
	gint         max_h,
	gint         max_colors
){
	guint8 *pixels;
	gint buf_w;
	gint buf_h;
	SixelColor *palette;
	gint palette_size;
	gint cur_color;
	gint cursor_x;
	gint cursor_y;
	gint max_x;
	gint max_y;
	SixelParserState state;
	gsize i;

	/* Accumulator for numeric parameters */
	gint num_acc;
	gint color_params[5];
	gint color_param_count;
	gint repeat_count;

	/* Initialize pixel buffer */
	buf_w = SIXEL_INIT_WIDTH;
	buf_h = SIXEL_INIT_HEIGHT;
	if (buf_w > max_w) buf_w = max_w;
	if (buf_h > max_h) buf_h = max_h;
	pixels = (guint8 *)g_malloc0(
		(gsize)buf_w * (gsize)buf_h * SIXEL_BPP);

	/* Initialize palette with default VGA colors */
	palette_size = max_colors;
	palette = (SixelColor *)g_malloc0(
		(gsize)palette_size * sizeof(SixelColor));
	for (i = 0; i < 16 && (gint)i < palette_size; i++) {
		palette[i].r = sixel_default_palette[i][0];
		palette[i].g = sixel_default_palette[i][1];
		palette[i].b = sixel_default_palette[i][2];
	}

	cur_color = 0;
	cursor_x = 0;
	cursor_y = 0;
	max_x = 0;
	max_y = 0;
	state = SIXEL_STATE_DATA;
	num_acc = 0;
	color_param_count = 0;
	repeat_count = 0;
	memset(color_params, 0, sizeof(color_params));

	for (i = 0; i < data_len; i++) {
		guchar ch;

		ch = (guchar)data[i];

		switch (state) {

		case SIXEL_STATE_COLOR:
			/*
			 * Color command parsing:
			 *   #idx          - select color
			 *   #idx;2;r;g;b  - define and select color (RGB percentages)
			 *
			 * We accumulate digits into num_acc. Semicolons separate
			 * parameters into color_params[]. When we hit a non-digit,
			 * non-semicolon character, the color command is complete.
			 */
			if (ch >= '0' && ch <= '9') {
				num_acc = num_acc * 10 + (gint)(ch - '0');
				continue;
			}

			if (ch == ';') {
				if (color_param_count < 5) {
					color_params[color_param_count] = num_acc;
					color_param_count++;
				}
				num_acc = 0;
				continue;
			}

			/* End of color command - store last param */
			if (color_param_count < 5) {
				color_params[color_param_count] = num_acc;
				color_param_count++;
			}

			if (color_param_count == 1) {
				/* #idx - just select the color */
				cur_color = color_params[0];
				if (cur_color >= palette_size) {
					cur_color = 0;
				}
			} else if (color_param_count >= 5 &&
			           color_params[1] == 2) {
				/*
				 * #idx;2;r;g;b - define color using RGB
				 * percentages (0-100). Convert to 0-255.
				 */
				gint idx;
				gint r;
				gint g;
				gint b;

				idx = color_params[0];
				r = color_params[2];
				g = color_params[3];
				b = color_params[4];

				/* Clamp percentages to 0-100 */
				if (r > 100) r = 100;
				if (g > 100) g = 100;
				if (b > 100) b = 100;
				if (r < 0) r = 0;
				if (g < 0) g = 0;
				if (b < 0) b = 0;

				if (idx >= 0 && idx < palette_size) {
					palette[idx].r = (guint8)((r * 255) / 100);
					palette[idx].g = (guint8)((g * 255) / 100);
					palette[idx].b = (guint8)((b * 255) / 100);
					cur_color = idx;
				}
			} else if (color_param_count >= 5 &&
			           color_params[1] == 1) {
				/*
				 * #idx;1;h;l;s - define color using HLS
				 * (Hue 0-360, Lightness 0-100, Saturation 0-100)
				 *
				 * Convert HLS to RGB. This is the VT340 native
				 * color coordinate system.
				 */
				gint idx;
				gint h;
				gint l;
				gint s;
				gdouble hf;
				gdouble lf;
				gdouble sf;
				gdouble c;
				gdouble x_val;
				gdouble m;
				gdouble r1;
				gdouble g1;
				gdouble b1;

				idx = color_params[0];
				h = color_params[2];
				l = color_params[3];
				s = color_params[4];

				/* Clamp values */
				if (h > 360) h = 360;
				if (l > 100) l = 100;
				if (s > 100) s = 100;
				if (h < 0) h = 0;
				if (l < 0) l = 0;
				if (s < 0) s = 0;

				/* Convert to 0.0-1.0 range */
				hf = (gdouble)h / 360.0;
				lf = (gdouble)l / 100.0;
				sf = (gdouble)s / 100.0;

				/* HSL to RGB conversion */
				if (sf == 0.0) {
					r1 = lf;
					g1 = lf;
					b1 = lf;
				} else {
					gdouble hue_sector;
					gint hi;
					gdouble f;

					c = (1.0 - ((2.0 * lf - 1.0) < 0 ?
						-(2.0 * lf - 1.0) :
						(2.0 * lf - 1.0))) * sf;
					hue_sector = hf * 6.0;
					hi = (gint)hue_sector % 6;
					f = hue_sector - (gint)hue_sector;
					x_val = c * (1.0 - ((f - (gdouble)(hi % 2 == 0)) < 0 ?
						-(f - (gdouble)(hi % 2 == 0)) :
						(f - (gdouble)(hi % 2 == 0))));
					m = lf - c / 2.0;

					switch (hi) {
					case 0: r1 = c + m; g1 = x_val + m; b1 = m; break;
					case 1: r1 = x_val + m; g1 = c + m; b1 = m; break;
					case 2: r1 = m; g1 = c + m; b1 = x_val + m; break;
					case 3: r1 = m; g1 = x_val + m; b1 = c + m; break;
					case 4: r1 = x_val + m; g1 = m; b1 = c + m; break;
					default: r1 = c + m; g1 = m; b1 = x_val + m; break;
					}
				}

				if (idx >= 0 && idx < palette_size) {
					palette[idx].r = (guint8)(r1 * 255.0 + 0.5);
					palette[idx].g = (guint8)(g1 * 255.0 + 0.5);
					palette[idx].b = (guint8)(b1 * 255.0 + 0.5);
					cur_color = idx;
				}
			}

			state = SIXEL_STATE_DATA;

			/*
			 * The character that ended the color command is NOT
			 * consumed; it needs to be re-processed as data.
			 * Fall through to SIXEL_STATE_DATA below by
			 * decrementing i so the for-loop re-visits this char.
			 */
			i--;
			continue;

		case SIXEL_STATE_REPEAT:
			/*
			 * Repeat command: !<count><sixel-char>
			 * Accumulate digits until we see the sixel character.
			 */
			if (ch >= '0' && ch <= '9') {
				repeat_count = repeat_count * 10 + (gint)(ch - '0');
				continue;
			}

			/*
			 * The next character should be a sixel data char.
			 * Draw it repeat_count times. If it's not a valid
			 * sixel char, just abandon the repeat.
			 */
			if (ch >= SIXEL_CHAR_MIN && ch <= SIXEL_CHAR_MAX) {
				guint8 sixel_val;
				gint bit;
				gint rep;
				const SixelColor *col;
				gint need_x;
				gint need_y;

				sixel_val = (guint8)(ch - SIXEL_CHAR_MIN);

				/* Validate color index */
				if (cur_color >= 0 && cur_color < palette_size) {
					col = &palette[cur_color];
				} else {
					col = &palette[0];
				}

				/* Ensure buffer can hold the repeated pixels */
				need_x = cursor_x + repeat_count;
				need_y = cursor_y + SIXEL_BAND_HEIGHT;
				if (!sixel_ensure_buffer(&pixels, &buf_w, &buf_h,
				                         need_x, need_y,
				                         max_w, max_h)) {
					/* Hit dimension limit; truncate */
					if (repeat_count > max_w - cursor_x) {
						repeat_count = max_w - cursor_x;
					}
				}

				/* Draw the repeated sixel character */
				for (rep = 0; rep < repeat_count; rep++) {
					if (cursor_x >= buf_w) {
						break;
					}
					for (bit = 0; bit < SIXEL_BAND_HEIGHT; bit++) {
						if (sixel_val & (1 << bit)) {
							sixel_put_pixel(pixels, buf_w, buf_h,
								cursor_x, cursor_y + bit, col);
						}
					}
					if (cursor_x > max_x) max_x = cursor_x;
					if (cursor_y + SIXEL_BAND_HEIGHT - 1 > max_y) {
						max_y = cursor_y + SIXEL_BAND_HEIGHT - 1;
					}
					cursor_x++;
				}
			}

			state = SIXEL_STATE_DATA;
			continue;

		case SIXEL_STATE_DATA:
			/* Falls through to handling below */
			break;
		}

		/* ===== SIXEL_STATE_DATA handling ===== */

		if (ch >= SIXEL_CHAR_MIN && ch <= SIXEL_CHAR_MAX) {
			/*
			 * Sixel data character. Each character encodes 6
			 * vertical pixels. Subtract 0x3F to get the bit
			 * pattern. Bit 0 = top pixel, bit 5 = bottom pixel.
			 */
			guint8 sixel_val;
			gint bit;
			const SixelColor *col;

			sixel_val = (guint8)(ch - SIXEL_CHAR_MIN);

			/* Validate color index */
			if (cur_color >= 0 && cur_color < palette_size) {
				col = &palette[cur_color];
			} else {
				col = &palette[0];
			}

			/* Ensure buffer can hold this column */
			if (!sixel_ensure_buffer(&pixels, &buf_w, &buf_h,
			                         cursor_x + 1,
			                         cursor_y + SIXEL_BAND_HEIGHT,
			                         max_w, max_h)) {
				/* Exceeded max dimensions; skip this pixel */
				continue;
			}

			/* Set the 6 vertical pixels */
			for (bit = 0; bit < SIXEL_BAND_HEIGHT; bit++) {
				if (sixel_val & (1 << bit)) {
					sixel_put_pixel(pixels, buf_w, buf_h,
						cursor_x, cursor_y + bit, col);
				}
			}

			if (cursor_x > max_x) max_x = cursor_x;
			if (cursor_y + SIXEL_BAND_HEIGHT - 1 > max_y) {
				max_y = cursor_y + SIXEL_BAND_HEIGHT - 1;
			}

			cursor_x++;

		} else if (ch == '#') {
			/* Begin color command */
			state = SIXEL_STATE_COLOR;
			num_acc = 0;
			color_param_count = 0;
			memset(color_params, 0, sizeof(color_params));

		} else if (ch == '!') {
			/* Begin repeat command */
			state = SIXEL_STATE_REPEAT;
			repeat_count = 0;

		} else if (ch == '$') {
			/*
			 * Carriage return: move cursor back to left edge
			 * of the current sixel band. This allows overprinting
			 * with a different color.
			 */
			cursor_x = 0;

		} else if (ch == '-') {
			/*
			 * Newline: advance to the next sixel band (6 pixels
			 * down) and reset x to the left edge.
			 */
			cursor_y += SIXEL_BAND_HEIGHT;
			cursor_x = 0;

		}
		/* Ignore any other characters (including control chars) */
	}

	/*
	 * Handle case where parser ended in a non-data state.
	 * If we were mid-color-command, finalize it. For repeat,
	 * there's nothing to do since no sixel char was provided.
	 */
	if (state == SIXEL_STATE_COLOR) {
		if (color_param_count < 5) {
			color_params[color_param_count] = num_acc;
			color_param_count++;
		}
		if (color_param_count == 1) {
			cur_color = color_params[0];
			if (cur_color >= palette_size) {
				cur_color = 0;
			}
		}
		/* RGB/HLS definitions at stream end are unlikely but harmless */
	}

	g_free(palette);

	/*
	 * Calculate actual image dimensions from the max pixel
	 * positions written. Add 1 because positions are 0-based.
	 */
	if (max_x < 0 || max_y < 0) {
		/* No pixels were written */
		g_free(pixels);
		*out_pixels = NULL;
		*out_width = 0;
		*out_height = 0;
		return FALSE;
	}

	*out_pixels = pixels;
	*out_width = buf_w;
	*out_height = buf_h;

	/*
	 * Trim the output to actual content size. The buffer may be
	 * larger than needed due to power-of-two growth. We store the
	 * full buffer but record the actual content dimensions.
	 */
	*out_width = max_x + 1;
	*out_height = max_y + 1;

	return TRUE;
}

/* ===== Signal callbacks ===== */

/*
 * on_line_scrolled_out:
 *
 * Signal callback for "line-scrolled-out". Adjusts all placement
 * row positions upward by one and removes placements that have
 * scrolled entirely off the screen (bottom edge above row 0).
 */
static void
on_line_scrolled_out(
	GstTerminal *term,
	gpointer     line,
	gint         cols,
	gpointer     user_data
){
	GstSixelModule *self;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	GList *remove_ids;
	GList *l;

	(void)term;
	(void)line;
	(void)cols;

	self = GST_SIXEL_MODULE(user_data);
	remove_ids = NULL;

	/* Shift all placement rows up by one, collect expired IDs */
	g_hash_table_iter_init(&iter, self->placements);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		SixelPlacement *pl;
		GstModuleManager *mgr;
		GstTerminal *t;
		gint rows;
		gint cell_height;
		gint img_rows;

		pl = (SixelPlacement *)value;
		pl->row--;

		/*
		 * Calculate how many terminal rows this image spans.
		 * Use the render context cell height if available,
		 * otherwise estimate conservatively with a default
		 * cell height of 16 pixels.
		 */
		mgr = gst_module_manager_get_default();
		t = (GstTerminal *)gst_module_manager_get_terminal(mgr);
		rows = 0;
		cell_height = 16;

		if (t != NULL) {
			rows = gst_terminal_get_rows(t);
		}
		(void)rows;

		img_rows = (pl->height + cell_height - 1) / cell_height;

		/*
		 * Remove if the entire image has scrolled above the
		 * visible area (its bottom row is above row 0).
		 */
		if (pl->row + img_rows < 0) {
			remove_ids = g_list_prepend(remove_ids,
				GUINT_TO_POINTER(pl->id));
		}
	}

	/* Remove expired placements */
	for (l = remove_ids; l != NULL; l = l->next) {
		guint32 id;
		SixelPlacement *pl;

		id = GPOINTER_TO_UINT(l->data);
		pl = (SixelPlacement *)g_hash_table_lookup(
			self->placements, GUINT_TO_POINTER(id));
		if (pl != NULL) {
			self->total_ram -= pl->data_size;
		}
		g_hash_table_remove(self->placements,
			GUINT_TO_POINTER(id));
	}

	g_list_free(remove_ids);
}

/* ===== GstModule vfuncs ===== */

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 * Must match the config key under modules: { sixel: ... }.
 */
static const gchar *
sixel_get_name(GstModule *module)
{
	(void)module;
	return "sixel";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
sixel_get_description(GstModule *module)
{
	(void)module;
	return "DEC Sixel graphics protocol for inline images";
}

/*
 * configure:
 *
 * Read module config from YAML.
 * Keys: max_width, max_height, max_colors, max_total_ram_mb,
 *       max_placements.
 */
static void
sixel_configure(
	GstModule *base,
	gpointer   config
){
	GstSixelModule *self;
	GstConfig *cfg;

	self = GST_SIXEL_MODULE(base);
	cfg = (GstConfig *)config;

	self->max_width = cfg->modules.sixel.max_width;
	self->max_height = cfg->modules.sixel.max_height;
	self->max_colors = cfg->modules.sixel.max_colors;
	self->max_ram_mb = cfg->modules.sixel.max_total_ram_mb;
	self->max_placements = cfg->modules.sixel.max_placements;

	g_debug("sixel: configured (max_w=%d, max_h=%d, colors=%d, "
		"ram=%dMB, placements=%d)",
		self->max_width, self->max_height, self->max_colors,
		self->max_ram_mb, self->max_placements);
}

/*
 * activate:
 *
 * Creates the placement hash table and connects to the terminal's
 * "line-scrolled-out" signal for scroll management.
 */
static gboolean
sixel_activate(GstModule *base)
{
	GstSixelModule *self;
	GstModuleManager *mgr;
	GstTerminal *term;

	self = GST_SIXEL_MODULE(base);

	/* Create placement storage if not already present */
	if (self->placements == NULL) {
		self->placements = g_hash_table_new_full(
			g_direct_hash, g_direct_equal,
			NULL, sixel_placement_free);
	}

	/* Connect to terminal's line-scrolled-out signal */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term != NULL) {
		self->sig_scrolled = g_signal_connect(term,
			"line-scrolled-out",
			G_CALLBACK(on_line_scrolled_out), self);
	}

	g_debug("sixel: activated");
	return TRUE;
}

/*
 * deactivate:
 *
 * Disconnects signals and frees placement storage.
 */
static void
sixel_deactivate(GstModule *base)
{
	GstSixelModule *self;

	self = GST_SIXEL_MODULE(base);

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

	/* Free all placements */
	if (self->placements != NULL) {
		g_hash_table_remove_all(self->placements);
		self->total_ram = 0;
	}

	g_debug("sixel: deactivated");
}

/* ===== Escape handler implementation ===== */

/*
 * sixel_handle_escape:
 *
 * Handles DCS escape sequences. Only processes sequences whose
 * str_type is 'P' (DCS) and that contain a 'q' sixel introducer.
 *
 * Flow:
 * 1. Check for DCS type ('P')
 * 2. Find the 'q' introducer to locate sixel data start
 * 3. Decode sixel data into RGBA pixel buffer
 * 4. Create a placement at the current cursor position
 * 5. Enforce RAM and placement count limits
 * 6. Mark terminal dirty for redraw
 */
static gboolean
sixel_handle_escape(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstSixelModule *self;
	GstTerminal *term;
	GstCursor *cursor;
	SixelPlacement *pl;
	gsize data_start;
	guint8 *pixels;
	gint img_w;
	gint img_h;
	gint cur_col;
	gint cur_row;

	self = GST_SIXEL_MODULE(handler);

	/* Only handle DCS sequences (str_type 'P') */
	if (str_type != 'P') {
		return FALSE;
	}

	/* Must have content to parse */
	if (len < 2) {
		return FALSE;
	}

	/* Find the sixel data start (after 'q' introducer) */
	if (!sixel_parse_params(buf, len, &data_start)) {
		/* No 'q' found - not a sixel sequence */
		return FALSE;
	}

	/* Nothing to decode if no data follows 'q' */
	if (data_start >= len) {
		return TRUE;
	}

	/* Decode the sixel data into an RGBA pixel buffer */
	pixels = NULL;
	img_w = 0;
	img_h = 0;

	if (!sixel_decode(buf + data_start, len - data_start,
	                  &pixels, &img_w, &img_h,
	                  self->max_width, self->max_height,
	                  self->max_colors)) {
		return TRUE; /* consumed but no image produced */
	}

	if (pixels == NULL || img_w <= 0 || img_h <= 0) {
		g_free(pixels);
		return TRUE;
	}

	/* Get the cursor position for placement */
	term = (GstTerminal *)terminal;
	cur_col = 0;
	cur_row = 0;
	if (term != NULL) {
		cursor = gst_terminal_get_cursor(term);
		if (cursor != NULL) {
			cur_col = cursor->x;
			cur_row = cursor->y;
		}
	}

	/* Create and store the placement */
	pl = g_new0(SixelPlacement, 1);
	pl->id = self->next_id++;
	pl->row = cur_row;
	pl->col = cur_col;
	pl->width = img_w;
	pl->height = img_h;
	pl->stride = img_w * SIXEL_BPP;
	pl->data = pixels;
	pl->data_size = (gsize)img_w * (gsize)img_h * SIXEL_BPP;

	self->total_ram += pl->data_size;

	g_hash_table_insert(self->placements,
		GUINT_TO_POINTER(pl->id), pl);

	/* Enforce RAM and placement count limits */
	sixel_enforce_limits(self);

	/* Mark terminal dirty for redraw */
	if (term != NULL) {
		gst_terminal_mark_dirty(term, -1);
	}

	g_debug("sixel: placed image #%u at (%d,%d) size %dx%d "
		"(%.1f KB, total %.1f MB)",
		pl->id, cur_col, cur_row, img_w, img_h,
		(gdouble)pl->data_size / 1024.0,
		(gdouble)self->total_ram / (1024.0 * 1024.0));

	return TRUE;
}

/* ===== Render overlay implementation ===== */

/*
 * sixel_render:
 *
 * Renders all visible sixel placements on the terminal surface.
 * Iterates all placements and draws those within the visible
 * area using gst_render_context_draw_image().
 */
static void
sixel_render(
	GstRenderOverlay *overlay,
	gpointer          render_ctx,
	gint              width,
	gint              height
){
	GstSixelModule *self;
	GstRenderContext *ctx;
	GstModuleManager *mgr;
	GstTerminal *term;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	gint rows;

	self = GST_SIXEL_MODULE(overlay);
	ctx = (GstRenderContext *)render_ctx;

	if (self->placements == NULL || ctx == NULL) {
		return;
	}

	if (g_hash_table_size(self->placements) == 0) {
		return;
	}

	/* Get terminal dimensions for visibility check */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return;
	}

	rows = gst_terminal_get_rows(term);

	/* Iterate all placements and draw visible ones */
	g_hash_table_iter_init(&iter, self->placements);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		SixelPlacement *pl;
		gint px;
		gint py;
		gint dw;
		gint dh;
		gint img_term_rows;

		pl = (SixelPlacement *)value;

		if (pl->data == NULL || pl->width <= 0 || pl->height <= 0) {
			continue;
		}

		/*
		 * Calculate how many terminal rows this image spans.
		 * Skip if the entire image is above or below the
		 * visible area.
		 */
		img_term_rows = (pl->height + ctx->ch - 1) / ctx->ch;
		if (pl->row + img_term_rows < 0 || pl->row >= rows) {
			continue;
		}

		/* Calculate pixel position from terminal coordinates */
		px = ctx->borderpx + pl->col * ctx->cw;
		py = ctx->borderpx + pl->row * ctx->ch;

		/* Use actual image dimensions for destination size */
		dw = pl->width;
		dh = pl->height;

		/* Clip to window bounds */
		if (px >= width || py >= height) {
			continue;
		}
		if (px + dw > width) {
			dw = width - px;
		}
		if (py + dh > height) {
			dh = height - py;
		}

		/* Skip if clipped to nothing */
		if (dw <= 0 || dh <= 0) {
			continue;
		}

		/* Draw the image using the render context vtable */
		gst_render_context_draw_image(ctx,
			pl->data, pl->width, pl->height, pl->stride,
			px, py, dw, dh);
	}
}

/* ===== GObject lifecycle ===== */

static void
gst_sixel_module_finalize(GObject *object)
{
	GstSixelModule *self;

	self = GST_SIXEL_MODULE(object);

	if (self->placements != NULL) {
		g_hash_table_destroy(self->placements);
		self->placements = NULL;
	}

	G_OBJECT_CLASS(gst_sixel_module_parent_class)->finalize(object);
}

static void
gst_sixel_module_class_init(GstSixelModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_sixel_module_finalize;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = sixel_get_name;
	module_class->get_description = sixel_get_description;
	module_class->configure = sixel_configure;
	module_class->activate = sixel_activate;
	module_class->deactivate = sixel_deactivate;
}

static void
gst_sixel_module_init(GstSixelModule *self)
{
	self->placements = NULL;
	self->next_id = 1;
	self->sig_scrolled = 0;
	self->total_ram = 0;

	/* Defaults */
	self->max_width = SIXEL_DEFAULT_MAX_WIDTH;
	self->max_height = SIXEL_DEFAULT_MAX_HEIGHT;
	self->max_colors = SIXEL_DEFAULT_MAX_COLORS;
	self->max_ram_mb = SIXEL_DEFAULT_MAX_RAM_MB;
	self->max_placements = SIXEL_DEFAULT_MAX_PLACEMENTS;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType so the module manager
 * can instantiate this module.
 *
 * Returns: The #GType for #GstSixelModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_SIXEL_MODULE;
}
