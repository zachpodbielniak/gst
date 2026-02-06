/*
 * gst-boxdraw-module.c - Box-drawing glyph transformer module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Table-driven rendering of Unicode box-drawing characters (U+2500-U+259F)
 * using X11 line/rectangle primitives for pixel-perfect cell alignment.
 * Implements GstGlyphTransformer to intercept box-drawing codepoints
 * before the normal text renderer handles them.
 */

#include "gst-boxdraw-module.h"
#include "../../src/config/gst-config.h"
#include "../../src/rendering/gst-render-context.h"

/**
 * SECTION:gst-boxdraw-module
 * @title: GstBoxdrawModule
 * @short_description: Pixel-perfect box-drawing character renderer
 *
 * #GstBoxdrawModule intercepts Unicode box-drawing characters and renders
 * them using X11 rectangle primitives instead of font glyphs. This produces
 * pixel-perfect alignment between adjacent box characters, avoiding the
 * gap/overlap issues common with font-based rendering.
 */

/* ===== Drawing operation table ===== */

/*
 * BoxDrawOp:
 * @type: 0=horizontal line, 1=vertical line, 2=filled rectangle
 * @x1: start x (normalized 0.0-1.0 within cell)
 * @y1: start y
 * @x2: end x
 * @y2: end y
 *
 * A single drawing primitive for a box-drawing character.
 * Coordinates are normalized to the cell dimensions.
 */
typedef struct
{
	guint8 type;
	gfloat x1, y1;
	gfloat x2, y2;
} BoxDrawOp;

/*
 * BoxDrawEntry:
 * @ops: array of up to 4 drawing operations
 * @nops: number of operations
 *
 * Drawing operations for a single box-drawing codepoint.
 */
typedef struct
{
	BoxDrawOp ops[4];
	guint8    nops;
} BoxDrawEntry;

/* Shorthand macros for building the table */
#define H(y)        { 0, 0.0f, y, 1.0f, y }      /* full horizontal at y */
#define HL(y)       { 0, 0.0f, y, 0.5f, y }      /* left half horizontal */
#define HR(y)       { 0, 0.5f, y, 1.0f, y }      /* right half horizontal */
#define V(x)        { 1, x, 0.0f, x, 1.0f }      /* full vertical at x */
#define VT(x)       { 1, x, 0.0f, x, 0.5f }      /* top half vertical */
#define VB(x)       { 1, x, 0.5f, x, 1.0f }      /* bottom half vertical */
#define FILL(x1,y1,x2,y2) { 2, x1, y1, x2, y2 } /* filled rectangle */

/*
 * Box-drawing character table: U+2500 to U+257F (128 entries).
 * Each entry contains 1-4 drawing ops for that codepoint.
 * Entries with nops=0 are not handled (fall through to font).
 */
static const BoxDrawEntry box_table[128] = {
	/* U+2500 ─ */ { { H(0.5f) }, 1 },
	/* U+2501 ━ */ { { H(0.5f) }, 1 },
	/* U+2502 │ */ { { V(0.5f) }, 1 },
	/* U+2503 ┃ */ { { V(0.5f) }, 1 },
	/* U+2504-U+250B: dashed lines (not handled, use font) */
	{ { {0} }, 0 }, { { {0} }, 0 }, { { {0} }, 0 }, { { {0} }, 0 },
	{ { {0} }, 0 }, { { {0} }, 0 }, { { {0} }, 0 }, { { {0} }, 0 },
	/* U+250C ┌ */ { { HR(0.5f), VB(0.5f) }, 2 },
	/* U+250D ┍ */ { { HR(0.5f), VB(0.5f) }, 2 },
	/* U+250E ┎ */ { { HR(0.5f), VB(0.5f) }, 2 },
	/* U+250F ┏ */ { { HR(0.5f), VB(0.5f) }, 2 },
	/* U+2510 ┐ */ { { HL(0.5f), VB(0.5f) }, 2 },
	/* U+2511 ┑ */ { { HL(0.5f), VB(0.5f) }, 2 },
	/* U+2512 ┒ */ { { HL(0.5f), VB(0.5f) }, 2 },
	/* U+2513 ┓ */ { { HL(0.5f), VB(0.5f) }, 2 },
	/* U+2514 └ */ { { HR(0.5f), VT(0.5f) }, 2 },
	/* U+2515 ┕ */ { { HR(0.5f), VT(0.5f) }, 2 },
	/* U+2516 ┖ */ { { HR(0.5f), VT(0.5f) }, 2 },
	/* U+2517 ┗ */ { { HR(0.5f), VT(0.5f) }, 2 },
	/* U+2518 ┘ */ { { HL(0.5f), VT(0.5f) }, 2 },
	/* U+2519 ┙ */ { { HL(0.5f), VT(0.5f) }, 2 },
	/* U+251A ┚ */ { { HL(0.5f), VT(0.5f) }, 2 },
	/* U+251B ┛ */ { { HL(0.5f), VT(0.5f) }, 2 },
	/* U+251C ├ */ { { HR(0.5f), V(0.5f) }, 2 },
	/* U+251D ┝ */ { { HR(0.5f), V(0.5f) }, 2 },
	/* U+251E ┞ */ { { HR(0.5f), V(0.5f) }, 2 },
	/* U+251F ┟ */ { { HR(0.5f), V(0.5f) }, 2 },
	/* U+2520 ┠ */ { { HR(0.5f), V(0.5f) }, 2 },
	/* U+2521 ┡ */ { { HR(0.5f), V(0.5f) }, 2 },
	/* U+2522 ┢ */ { { HR(0.5f), V(0.5f) }, 2 },
	/* U+2523 ┣ */ { { HR(0.5f), V(0.5f) }, 2 },
	/* U+2524 ┤ */ { { HL(0.5f), V(0.5f) }, 2 },
	/* U+2525 ┥ */ { { HL(0.5f), V(0.5f) }, 2 },
	/* U+2526 ┦ */ { { HL(0.5f), V(0.5f) }, 2 },
	/* U+2527 ┧ */ { { HL(0.5f), V(0.5f) }, 2 },
	/* U+2528 ┨ */ { { HL(0.5f), V(0.5f) }, 2 },
	/* U+2529 ┩ */ { { HL(0.5f), V(0.5f) }, 2 },
	/* U+252A ┪ */ { { HL(0.5f), V(0.5f) }, 2 },
	/* U+252B ┫ */ { { HL(0.5f), V(0.5f) }, 2 },
	/* U+252C ┬ */ { { H(0.5f), VB(0.5f) }, 2 },
	/* U+252D ┭ */ { { H(0.5f), VB(0.5f) }, 2 },
	/* U+252E ┮ */ { { H(0.5f), VB(0.5f) }, 2 },
	/* U+252F ┯ */ { { H(0.5f), VB(0.5f) }, 2 },
	/* U+2530 ┰ */ { { H(0.5f), VB(0.5f) }, 2 },
	/* U+2531 ┱ */ { { H(0.5f), VB(0.5f) }, 2 },
	/* U+2532 ┲ */ { { H(0.5f), VB(0.5f) }, 2 },
	/* U+2533 ┳ */ { { H(0.5f), VB(0.5f) }, 2 },
	/* U+2534 ┴ */ { { H(0.5f), VT(0.5f) }, 2 },
	/* U+2535 ┵ */ { { H(0.5f), VT(0.5f) }, 2 },
	/* U+2536 ┶ */ { { H(0.5f), VT(0.5f) }, 2 },
	/* U+2537 ┷ */ { { H(0.5f), VT(0.5f) }, 2 },
	/* U+2538 ┸ */ { { H(0.5f), VT(0.5f) }, 2 },
	/* U+2539 ┹ */ { { H(0.5f), VT(0.5f) }, 2 },
	/* U+253A ┺ */ { { H(0.5f), VT(0.5f) }, 2 },
	/* U+253B ┻ */ { { H(0.5f), VT(0.5f) }, 2 },
	/* U+253C ┼ */ { { H(0.5f), V(0.5f) }, 2 },
	/* U+253D ┽ */ { { H(0.5f), V(0.5f) }, 2 },
	/* U+253E ┾ */ { { H(0.5f), V(0.5f) }, 2 },
	/* U+253F ┿ */ { { H(0.5f), V(0.5f) }, 2 },
	/* U+2540 ╀ */ { { H(0.5f), V(0.5f) }, 2 },
	/* U+2541 ╁ */ { { H(0.5f), V(0.5f) }, 2 },
	/* U+2542 ╂ */ { { H(0.5f), V(0.5f) }, 2 },
	/* U+2543-U+254B: more cross variants */
	{ { H(0.5f), V(0.5f) }, 2 },
	{ { H(0.5f), V(0.5f) }, 2 },
	{ { H(0.5f), V(0.5f) }, 2 },
	{ { H(0.5f), V(0.5f) }, 2 },
	{ { H(0.5f), V(0.5f) }, 2 },
	{ { H(0.5f), V(0.5f) }, 2 },
	{ { H(0.5f), V(0.5f) }, 2 },
	{ { H(0.5f), V(0.5f) }, 2 },
	{ { H(0.5f), V(0.5f) }, 2 },
	/* U+254C-U+254F: dashed lines (not handled) */
	{ { {0} }, 0 }, { { {0} }, 0 }, { { {0} }, 0 }, { { {0} }, 0 },
	/* U+2550 ═ */ { { H(0.35f), H(0.65f) }, 2 },
	/* U+2551 ║ */ { { V(0.35f), V(0.65f) }, 2 },
	/* U+2552 ╒ */ { { HR(0.35f), HR(0.65f), VB(0.5f) }, 3 },
	/* U+2553 ╓ */ { { HR(0.5f), { 1, 0.35f, 0.5f, 0.35f, 1.0f }, { 1, 0.65f, 0.5f, 0.65f, 1.0f } }, 3 },
	/* U+2554 ╔ */ { { HR(0.35f), HR(0.65f), { 1, 0.35f, 0.35f, 0.35f, 1.0f }, { 1, 0.65f, 0.65f, 0.65f, 1.0f } }, 4 },
	/* U+2555 ╕ */ { { HL(0.35f), HL(0.65f), VB(0.5f) }, 3 },
	/* U+2556 ╖ */ { { HL(0.5f), { 1, 0.35f, 0.5f, 0.35f, 1.0f }, { 1, 0.65f, 0.5f, 0.65f, 1.0f } }, 3 },
	/* U+2557 ╗ */ { { HL(0.35f), HL(0.65f), { 1, 0.35f, 0.35f, 0.35f, 1.0f }, { 1, 0.65f, 0.65f, 0.65f, 1.0f } }, 4 },
	/* U+2558 ╘ */ { { HR(0.35f), HR(0.65f), VT(0.5f) }, 3 },
	/* U+2559 ╙ */ { { HR(0.5f), { 1, 0.35f, 0.0f, 0.35f, 0.5f }, { 1, 0.65f, 0.0f, 0.65f, 0.5f } }, 3 },
	/* U+255A ╚ */ { { HR(0.35f), HR(0.65f), { 1, 0.35f, 0.0f, 0.35f, 0.65f }, { 1, 0.65f, 0.0f, 0.65f, 0.35f } }, 4 },
	/* U+255B ╛ */ { { HL(0.35f), HL(0.65f), VT(0.5f) }, 3 },
	/* U+255C ╜ */ { { HL(0.5f), { 1, 0.35f, 0.0f, 0.35f, 0.5f }, { 1, 0.65f, 0.0f, 0.65f, 0.5f } }, 3 },
	/* U+255D ╝ */ { { HL(0.35f), HL(0.65f), { 1, 0.35f, 0.0f, 0.35f, 0.65f }, { 1, 0.65f, 0.0f, 0.65f, 0.35f } }, 4 },
	/* U+255E ╞ */ { { HR(0.5f), V(0.35f), V(0.65f) }, 3 },
	/* U+255F ╟ */ { { HR(0.35f), HR(0.65f), V(0.5f) }, 3 },
	/* U+2560 ╠ */ { { HR(0.35f), HR(0.65f), V(0.35f), V(0.65f) }, 4 },
	/* U+2561 ╡ */ { { HL(0.5f), V(0.35f), V(0.65f) }, 3 },
	/* U+2562 ╢ */ { { HL(0.35f), HL(0.65f), V(0.5f) }, 3 },
	/* U+2563 ╣ */ { { HL(0.35f), HL(0.65f), V(0.35f), V(0.65f) }, 4 },
	/* U+2564 ╤ */ { { H(0.35f), H(0.65f), VB(0.5f) }, 3 },
	/* U+2565 ╥ */ { { H(0.5f), { 1, 0.35f, 0.5f, 0.35f, 1.0f }, { 1, 0.65f, 0.5f, 0.65f, 1.0f } }, 3 },
	/* U+2566 ╦ */ { { H(0.35f), H(0.65f), { 1, 0.35f, 0.65f, 0.35f, 1.0f }, { 1, 0.65f, 0.65f, 0.65f, 1.0f } }, 4 },
	/* U+2567 ╧ */ { { H(0.35f), H(0.65f), VT(0.5f) }, 3 },
	/* U+2568 ╨ */ { { H(0.5f), { 1, 0.35f, 0.0f, 0.35f, 0.5f }, { 1, 0.65f, 0.0f, 0.65f, 0.5f } }, 3 },
	/* U+2569 ╩ */ { { H(0.35f), H(0.65f), { 1, 0.35f, 0.0f, 0.35f, 0.35f }, { 1, 0.65f, 0.0f, 0.65f, 0.35f } }, 4 },
	/* U+256A ╪ */ { { H(0.35f), H(0.65f), V(0.5f) }, 3 },
	/* U+256B ╫ */ { { H(0.5f), V(0.35f), V(0.65f) }, 3 },
	/* U+256C ╬ */ { { H(0.35f), H(0.65f), V(0.35f), V(0.65f) }, 4 },
	/* U+256D-U+2570: rounded corners (not handled, use font) */
	{ { {0} }, 0 }, { { {0} }, 0 }, { { {0} }, 0 }, { { {0} }, 0 },
	/* U+2571 ╱ */ { { {0} }, 0 },
	/* U+2572 ╲ */ { { {0} }, 0 },
	/* U+2573 ╳ */ { { {0} }, 0 },
	/* U+2574 ╴ */ { { HL(0.5f) }, 1 },
	/* U+2575 ╵ */ { { VT(0.5f) }, 1 },
	/* U+2576 ╶ */ { { HR(0.5f) }, 1 },
	/* U+2577 ╷ */ { { VB(0.5f) }, 1 },
	/* U+2578 ╸ */ { { HL(0.5f) }, 1 },
	/* U+2579 ╹ */ { { VT(0.5f) }, 1 },
	/* U+257A ╺ */ { { HR(0.5f) }, 1 },
	/* U+257B ╻ */ { { VB(0.5f) }, 1 },
	/* U+257C ╼ */ { { H(0.5f) }, 1 },
	/* U+257D ╽ */ { { V(0.5f) }, 1 },
	/* U+257E ╾ */ { { H(0.5f) }, 1 },
	/* U+257F ╿ */ { { V(0.5f) }, 1 },
};

/*
 * Block elements table: U+2580-U+259F (32 entries).
 * Each is a filled rectangle covering a portion of the cell.
 * Stored as { x1, y1, x2, y2 } in normalized coords.
 */
static const gfloat block_table[32][4] = {
	/* U+2580 ▀ */ { 0.0f, 0.0f, 1.0f, 0.5f },
	/* U+2581 ▁ */ { 0.0f, 0.875f, 1.0f, 1.0f },
	/* U+2582 ▂ */ { 0.0f, 0.75f, 1.0f, 1.0f },
	/* U+2583 ▃ */ { 0.0f, 0.625f, 1.0f, 1.0f },
	/* U+2584 ▄ */ { 0.0f, 0.5f, 1.0f, 1.0f },
	/* U+2585 ▅ */ { 0.0f, 0.375f, 1.0f, 1.0f },
	/* U+2586 ▆ */ { 0.0f, 0.25f, 1.0f, 1.0f },
	/* U+2587 ▇ */ { 0.0f, 0.125f, 1.0f, 1.0f },
	/* U+2588 █ */ { 0.0f, 0.0f, 1.0f, 1.0f },
	/* U+2589 ▉ */ { 0.0f, 0.0f, 0.875f, 1.0f },
	/* U+258A ▊ */ { 0.0f, 0.0f, 0.75f, 1.0f },
	/* U+258B ▋ */ { 0.0f, 0.0f, 0.625f, 1.0f },
	/* U+258C ▌ */ { 0.0f, 0.0f, 0.5f, 1.0f },
	/* U+258D ▍ */ { 0.0f, 0.0f, 0.375f, 1.0f },
	/* U+258E ▎ */ { 0.0f, 0.0f, 0.25f, 1.0f },
	/* U+258F ▏ */ { 0.0f, 0.0f, 0.125f, 1.0f },
	/* U+2590 ▐ */ { 0.5f, 0.0f, 1.0f, 1.0f },
	/* U+2591 ░ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* shade - not handled */
	/* U+2592 ▒ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* shade - not handled */
	/* U+2593 ▓ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* shade - not handled */
	/* U+2594 ▔ */ { 0.0f, 0.0f, 1.0f, 0.125f },
	/* U+2595 ▕ */ { 0.875f, 0.0f, 1.0f, 1.0f },
	/* U+2596 ▖ */ { 0.0f, 0.5f, 0.5f, 1.0f },
	/* U+2597 ▗ */ { 0.5f, 0.5f, 1.0f, 1.0f },
	/* U+2598 ▘ */ { 0.0f, 0.0f, 0.5f, 0.5f },
	/* U+2599 ▙ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* complex - not handled */
	/* U+259A ▚ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* complex - not handled */
	/* U+259B ▛ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* complex - not handled */
	/* U+259C ▜ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* complex - not handled */
	/* U+259D ▝ */ { 0.5f, 0.0f, 1.0f, 0.5f },
	/* U+259E ▞ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* complex - not handled */
	/* U+259F ▟ */ { 0.0f, 0.0f, 0.0f, 0.0f }, /* complex - not handled */
};

/* ===== Module private data ===== */

struct _GstBoxdrawModule
{
	GstModule parent_instance;
	gint      bold_offset;
};

/* Forward declarations */
static void
gst_boxdraw_module_transformer_init(GstGlyphTransformerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstBoxdrawModule, gst_boxdraw_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_GLYPH_TRANSFORMER,
		gst_boxdraw_module_transformer_init))

/* ===== Internal helpers ===== */

/*
 * draw_box_op:
 *
 * Draws a single BoxDrawOp scaled to pixel coordinates.
 * type 0 = horizontal line (1px thick), type 1 = vertical line,
 * type 2 = filled rectangle.
 */
static void
draw_box_op(
	GstX11RenderContext *ctx,
	const BoxDrawOp     *op,
	gint                px,
	gint                py,
	gint                cw,
	gint                ch,
	gint                bold
){
	gint x1;
	gint y1;
	gint x2;
	gint y2;
	gint thickness;

	x1 = px + (gint)(op->x1 * (gfloat)cw);
	y1 = py + (gint)(op->y1 * (gfloat)ch);
	x2 = px + (gint)(op->x2 * (gfloat)cw);
	y2 = py + (gint)(op->y2 * (gfloat)ch);
	thickness = 1 + bold;

	switch (op->type) {
	case 0: /* horizontal line */
		XftDrawRect(ctx->xft_draw, ctx->fg,
			x1, y1, (guint)(x2 - x1), (guint)thickness);
		break;
	case 1: /* vertical line */
		XftDrawRect(ctx->xft_draw, ctx->fg,
			x1, y1, (guint)thickness, (guint)(y2 - y1));
		break;
	case 2: /* filled rectangle */
		XftDrawRect(ctx->xft_draw, ctx->fg,
			x1, y1, (guint)(x2 - x1), (guint)(y2 - y1));
		break;
	}
}

/* ===== GstGlyphTransformer interface ===== */

/*
 * transform_glyph:
 *
 * Checks if codepoint is in the box-drawing or block element range.
 * If so, draws it using primitives and returns TRUE.
 */
static gboolean
gst_boxdraw_module_transform_glyph(
	GstGlyphTransformer *transformer,
	gunichar             codepoint,
	gpointer             render_context,
	gint                 x,
	gint                 y,
	gint                 width,
	gint                 height
){
	GstBoxdrawModule *self;
	GstX11RenderContext *ctx;
	guint idx;
	guint i;

	self = GST_BOXDRAW_MODULE(transformer);
	ctx = (GstX11RenderContext *)render_context;

	/* Box-drawing characters: U+2500-U+257F */
	if (codepoint >= 0x2500 && codepoint <= 0x257F) {
		const BoxDrawEntry *entry;

		idx = codepoint - 0x2500;
		entry = &box_table[idx];

		if (entry->nops == 0) {
			return FALSE;
		}

		/* Clear background first */
		XftDrawRect(ctx->xft_draw, ctx->bg, x, y,
			(guint)width, (guint)height);

		/* Draw each operation */
		for (i = 0; i < entry->nops; i++) {
			draw_box_op(ctx, &entry->ops[i],
				x, y, width, height, self->bold_offset);
		}

		return TRUE;
	}

	/* Block elements: U+2580-U+259F */
	if (codepoint >= 0x2580 && codepoint <= 0x259F) {
		const gfloat *b;
		gint bx;
		gint by;
		gint bw;
		gint bh;

		idx = codepoint - 0x2580;
		b = block_table[idx];

		/* Skip entries with zero dimensions (shade/complex patterns) */
		if (b[2] - b[0] < 0.01f && b[3] - b[1] < 0.01f) {
			return FALSE;
		}

		/* Clear background */
		XftDrawRect(ctx->xft_draw, ctx->bg, x, y,
			(guint)width, (guint)height);

		/* Draw filled block */
		bx = x + (gint)(b[0] * (gfloat)width);
		by = y + (gint)(b[1] * (gfloat)height);
		bw = (gint)((b[2] - b[0]) * (gfloat)width);
		bh = (gint)((b[3] - b[1]) * (gfloat)height);

		if (bw > 0 && bh > 0) {
			XftDrawRect(ctx->xft_draw, ctx->fg, bx, by,
				(guint)bw, (guint)bh);
		}

		return TRUE;
	}

	return FALSE;
}

static void
gst_boxdraw_module_transformer_init(GstGlyphTransformerInterface *iface)
{
	iface->transform_glyph = gst_boxdraw_module_transform_glyph;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_boxdraw_module_get_name(GstModule *module)
{
	(void)module;
	return "boxdraw";
}

static const gchar *
gst_boxdraw_module_get_description(GstModule *module)
{
	(void)module;
	return "Pixel-perfect box-drawing character renderer";
}

static gboolean
gst_boxdraw_module_activate(GstModule *module)
{
	g_debug("boxdraw: activated");
	return TRUE;
}

static void
gst_boxdraw_module_deactivate(GstModule *module)
{
	g_debug("boxdraw: deactivated");
}

/*
 * configure:
 *
 * Reads boxdraw configuration from the YAML config:
 *  - bold_offset: extra pixel offset for bold lines (typically 0 or 1)
 */
static void
gst_boxdraw_module_configure(GstModule *module, gpointer config)
{
	GstBoxdrawModule *self;
	YamlMapping *mod_cfg;

	self = GST_BOXDRAW_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "boxdraw");
	if (mod_cfg == NULL)
	{
		g_debug("boxdraw: no config section, using defaults");
		return;
	}

	if (yaml_mapping_has_member(mod_cfg, "bold_offset"))
	{
		gint64 val;

		val = yaml_mapping_get_int_member(mod_cfg, "bold_offset");
		self->bold_offset = (gint)val;
	}

	g_debug("boxdraw: configured (bold_offset=%d)", self->bold_offset);
}

/* ===== GObject lifecycle ===== */

static void
gst_boxdraw_module_class_init(GstBoxdrawModuleClass *klass)
{
	GstModuleClass *module_class;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_boxdraw_module_get_name;
	module_class->get_description = gst_boxdraw_module_get_description;
	module_class->activate = gst_boxdraw_module_activate;
	module_class->deactivate = gst_boxdraw_module_deactivate;
	module_class->configure = gst_boxdraw_module_configure;
}

static void
gst_boxdraw_module_init(GstBoxdrawModule *self)
{
	self->bold_offset = 1;
}

/* ===== Module entry point ===== */

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_BOXDRAW_MODULE;
}
