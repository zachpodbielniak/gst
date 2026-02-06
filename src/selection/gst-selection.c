/*
 * gst-selection.c - GST Text Selection Implementation
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Port of st.c's selection system (selstart, selextend, selnormalize,
 * selsnap, getsel, selscroll, selclear, selected).
 *
 * The selection maintains both original (ob/oe) and normalized (nb/ne)
 * coordinates. Original coords track the actual mouse positions;
 * normalized coords are sorted so nb <= ne and account for snapping.
 */

#include "gst-selection.h"
#include "../core/gst-terminal.h"
#include "../core/gst-line.h"
#include "../boxed/gst-glyph.h"
#include "../util/gst-utf8.h"
#include <string.h>
#include <wchar.h>
#include <wctype.h>

/* Maximum UTF-8 bytes per character */
#define UTF_SIZ (4)

/*
 * Word delimiter check. Characters considered word
 * boundaries for SNAP_WORD selection.
 */
static const wchar_t *worddelimiters = L" ";

#define ISDELIM(u) (wcschr(worddelimiters, (wchar_t)(u)) != NULL)

#define BETWEEN(x, a, b) ((x) >= (a) && (x) <= (b))

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

struct _GstSelection {
	GObject parent_instance;

	GstTerminal *term;       /* terminal we reference (weak, not owned) */

	GstSelectionMode mode;   /* idle, empty, ready */
	GstSelectionType type;   /* regular or rectangular */
	GstSelectionSnap snap;   /* none, word, line */

	/*
	 * ob/oe: original begin/end (as set by mouse)
	 * nb/ne: normalized begin/end (sorted, snapped)
	 */
	struct { gint x, y; } ob, oe, nb, ne;

	gboolean alt;            /* selection was made on alt screen */
};

G_DEFINE_TYPE(GstSelection, gst_selection, G_TYPE_OBJECT)

/* Forward declarations */
static void sel_normalize(GstSelection *sel);
static void sel_snap(GstSelection *sel, gint *x, gint *y, gint direction);

static void
gst_selection_dispose(GObject *object)
{
	GstSelection *sel = GST_SELECTION(object);

	sel->term = NULL;

	G_OBJECT_CLASS(gst_selection_parent_class)->dispose(object);
}

static void
gst_selection_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_selection_parent_class)->finalize(object);
}

static void
gst_selection_class_init(GstSelectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = gst_selection_dispose;
	object_class->finalize = gst_selection_finalize;
}

static void
gst_selection_init(GstSelection *sel)
{
	sel->term = NULL;
	sel->mode = GST_SELECTION_IDLE;
	sel->type = GST_SELECTION_TYPE_REGULAR;
	sel->snap = GST_SELECTION_SNAP_NONE;
	sel->ob.x = -1;
	sel->ob.y = 0;
	sel->oe.x = -1;
	sel->oe.y = 0;
	sel->nb.x = 0;
	sel->nb.y = 0;
	sel->ne.x = 0;
	sel->ne.y = 0;
	sel->alt = FALSE;
}

/**
 * gst_selection_new:
 * @term: a #GstTerminal
 *
 * Creates a new selection bound to the given terminal.
 * The selection does NOT take a reference on the terminal;
 * the terminal is expected to outlive the selection.
 *
 * Returns: (transfer full): a new #GstSelection
 */
GstSelection *
gst_selection_new(GstTerminal *term)
{
	GstSelection *sel;

	g_return_val_if_fail(GST_IS_TERMINAL(term), NULL);

	sel = (GstSelection *)g_object_new(GST_TYPE_SELECTION, NULL);
	sel->term = term;

	return sel;
}

/*
 * sel_normalize:
 *
 * Normalizes selection coordinates so that nb <= ne.
 * For regular multi-line selections, nb.x comes from the
 * endpoint with the smaller y. For single-line or rectangular
 * selections, nb.x/ne.x are min/max of the two x coords.
 *
 * After sorting, applies snap. Then adjusts nb.x/ne.x for
 * actual line lengths. Port of st.c selnormalize().
 */
static void
sel_normalize(GstSelection *sel)
{
	gint linelen;
	gint cols;

	if (sel->term == NULL) {
		return;
	}

	cols = gst_terminal_get_cols(sel->term);

	/* Sort coordinates: nb gets smaller, ne gets larger */
	if (sel->type == GST_SELECTION_TYPE_REGULAR &&
	    sel->ob.y != sel->oe.y) {
		/* Multi-line regular: x of earlier line goes to nb.x */
		sel->nb.x = sel->ob.y < sel->oe.y ? sel->ob.x : sel->oe.x;
		sel->ne.x = sel->ob.y < sel->oe.y ? sel->oe.x : sel->ob.x;
	} else {
		sel->nb.x = MIN(sel->ob.x, sel->oe.x);
		sel->ne.x = MAX(sel->ob.x, sel->oe.x);
	}
	sel->nb.y = MIN(sel->ob.y, sel->oe.y);
	sel->ne.y = MAX(sel->ob.y, sel->oe.y);

	/* Apply snapping to both endpoints */
	sel_snap(sel, &sel->nb.x, &sel->nb.y, -1);
	sel_snap(sel, &sel->ne.x, &sel->ne.y, +1);

	/* Adjust for actual line lengths (regular mode only) */
	if (sel->type == GST_SELECTION_TYPE_RECTANGULAR) {
		return;
	}

	linelen = gst_terminal_line_len(sel->term, sel->nb.y);
	if (linelen < sel->nb.x) {
		sel->nb.x = linelen;
	}

	linelen = gst_terminal_line_len(sel->term, sel->ne.y);
	if (linelen <= sel->ne.x) {
		sel->ne.x = cols - 1;
	}
}

/*
 * sel_snap:
 *
 * Snaps a position to word or line boundaries.
 * For word snap, walks in the given direction until a
 * delimiter change or unwrapped line break is found.
 * For line snap, expands to the start/end of the
 * logical (wrapped) line.
 *
 * Port of st.c selsnap().
 */
static void
sel_snap(
	GstSelection *sel,
	gint         *x,
	gint         *y,
	gint         direction
){
	gint newx;
	gint newy;
	gint xt;
	gint yt;
	gint delim;
	gint prevdelim;
	gint cols;
	gint rows;
	const GstGlyph *gp;
	const GstGlyph *prevgp;

	if (sel->term == NULL) {
		return;
	}

	cols = gst_terminal_get_cols(sel->term);
	rows = gst_terminal_get_rows(sel->term);

	switch (sel->snap) {
	case GST_SELECTION_SNAP_WORD:
		/*
		 * Walk in direction until we hit a delimiter boundary
		 * or a line break that isn't wrapped.
		 */
		prevgp = (const GstGlyph *)gst_terminal_get_glyph(
			sel->term, *x, *y);
		if (prevgp == NULL) {
			return;
		}
		prevdelim = ISDELIM(prevgp->rune);

		for (;;) {
			newx = *x + direction;
			newy = *y;

			/* Handle line-end wrapping */
			if (!BETWEEN(newx, 0, cols - 1)) {
				newy += direction;
				newx = (newx + cols) % cols;
				if (!BETWEEN(newy, 0, rows - 1)) {
					break;
				}

				/* Check wrap attribute at line end */
				if (direction > 0) {
					yt = *y;
					xt = *x;
				} else {
					yt = newy;
					xt = newx;
				}

				gp = (const GstGlyph *)gst_terminal_get_glyph(
					sel->term, xt, yt);
				if (gp == NULL ||
				    !(gp->attr & GST_GLYPH_ATTR_WRAP)) {
					break;
				}
			}

			if (newx >= gst_terminal_line_len(sel->term, newy)) {
				break;
			}

			gp = (const GstGlyph *)gst_terminal_get_glyph(
				sel->term, newx, newy);
			if (gp == NULL) {
				break;
			}

			delim = ISDELIM(gp->rune);
			if (!(gp->attr & GST_GLYPH_ATTR_WDUMMY) &&
			    (delim != prevdelim ||
			     (delim && gp->rune != prevgp->rune))) {
				break;
			}

			*x = newx;
			*y = newy;
			prevgp = gp;
			prevdelim = delim;
		}
		break;

	case GST_SELECTION_SNAP_LINE:
		/*
		 * Snap to start/end of the logical line,
		 * following WRAP attributes across rows.
		 */
		*x = (direction < 0) ? 0 : cols - 1;

		if (direction < 0) {
			for (; *y > 0; *y += direction) {
				gp = (const GstGlyph *)gst_terminal_get_glyph(
					sel->term, cols - 1, *y - 1);
				if (gp == NULL ||
				    !(gp->attr & GST_GLYPH_ATTR_WRAP)) {
					break;
				}
			}
		} else if (direction > 0) {
			for (; *y < rows - 1; *y += direction) {
				gp = (const GstGlyph *)gst_terminal_get_glyph(
					sel->term, cols - 1, *y);
				if (gp == NULL ||
				    !(gp->attr & GST_GLYPH_ATTR_WRAP)) {
					break;
				}
			}
		}
		break;

	case GST_SELECTION_SNAP_NONE:
	default:
		break;
	}
}

/**
 * gst_selection_start:
 * @sel: a #GstSelection
 * @col: starting column (0-based)
 * @row: starting row (0-based)
 * @snap: snap mode
 *
 * Begins a new selection. Port of st.c selstart().
 */
void
gst_selection_start(
	GstSelection     *sel,
	gint             col,
	gint             row,
	GstSelectionSnap snap
){
	g_return_if_fail(GST_IS_SELECTION(sel));

	gst_selection_clear(sel);

	sel->mode = GST_SELECTION_EMPTY;
	sel->type = GST_SELECTION_TYPE_REGULAR;
	sel->snap = snap;
	sel->alt = (sel->term != NULL) ?
		gst_terminal_is_altscreen(sel->term) : FALSE;
	sel->oe.x = sel->ob.x = col;
	sel->oe.y = sel->ob.y = row;

	sel_normalize(sel);

	if (sel->snap != GST_SELECTION_SNAP_NONE) {
		sel->mode = GST_SELECTION_READY;
	}
}

/**
 * gst_selection_extend:
 * @sel: a #GstSelection
 * @col: current column
 * @row: current row
 * @type: selection type
 * @done: %TRUE if finalized
 *
 * Extends the selection. Port of st.c selextend().
 */
void
gst_selection_extend(
	GstSelection     *sel,
	gint             col,
	gint             row,
	GstSelectionType type,
	gboolean         done
){
	g_return_if_fail(GST_IS_SELECTION(sel));

	if (sel->mode == GST_SELECTION_IDLE) {
		return;
	}

	if (done && sel->mode == GST_SELECTION_EMPTY) {
		gst_selection_clear(sel);
		return;
	}

	sel->oe.x = col;
	sel->oe.y = row;
	sel_normalize(sel);
	sel->type = type;

	sel->mode = done ? GST_SELECTION_IDLE : GST_SELECTION_READY;
}

/**
 * gst_selection_clear:
 * @sel: a #GstSelection
 *
 * Clears the selection. Port of st.c selclear().
 */
void
gst_selection_clear(GstSelection *sel)
{
	g_return_if_fail(GST_IS_SELECTION(sel));

	if (sel->ob.x == -1) {
		return;
	}

	sel->mode = GST_SELECTION_IDLE;
	sel->ob.x = -1;
}

/**
 * gst_selection_scroll:
 * @sel: a #GstSelection
 * @orig: origin row of scroll
 * @n: lines scrolled (positive=up)
 *
 * Adjusts selection when terminal scrolls.
 * Port of st.c selscroll().
 */
void
gst_selection_scroll(
	GstSelection *sel,
	gint         orig,
	gint         n
){
	gint bot;

	g_return_if_fail(GST_IS_SELECTION(sel));

	if (sel->ob.x == -1 || sel->term == NULL) {
		return;
	}

	gst_terminal_get_scroll_region(sel->term, NULL, &bot);

	/*
	 * If the selection straddles the scroll boundary
	 * (one end inside, one outside), clear it.
	 */
	if (BETWEEN(sel->nb.y, orig, bot) !=
	    BETWEEN(sel->ne.y, orig, bot)) {
		gst_selection_clear(sel);
	} else if (BETWEEN(sel->nb.y, orig, bot)) {
		sel->ob.y += n;
		sel->oe.y += n;

		if (sel->ob.y < orig || sel->ob.y > bot ||
		    sel->oe.y < orig || sel->oe.y > bot) {
			gst_selection_clear(sel);
		} else {
			sel_normalize(sel);
		}
	}
}

/**
 * gst_selection_selected:
 * @sel: a #GstSelection
 * @col: column to check
 * @row: row to check
 *
 * Checks if a cell is selected. Port of st.c selected().
 *
 * Returns: %TRUE if the cell is within the selection
 */
gboolean
gst_selection_selected(
	GstSelection *sel,
	gint         col,
	gint         row
){
	g_return_val_if_fail(GST_IS_SELECTION(sel), FALSE);

	if (sel->mode == GST_SELECTION_EMPTY || sel->ob.x == -1) {
		return FALSE;
	}

	/* Selection only valid on the screen where it was made */
	if (sel->term != NULL &&
	    sel->alt != gst_terminal_is_altscreen(sel->term)) {
		return FALSE;
	}

	if (sel->type == GST_SELECTION_TYPE_RECTANGULAR) {
		return BETWEEN(row, sel->nb.y, sel->ne.y) &&
		       BETWEEN(col, sel->nb.x, sel->ne.x);
	}

	return BETWEEN(row, sel->nb.y, sel->ne.y) &&
	       (row != sel->nb.y || col >= sel->nb.x) &&
	       (row != sel->ne.y || col <= sel->ne.x);
}

/**
 * gst_selection_is_empty:
 * @sel: a #GstSelection
 *
 * Checks if the selection is empty.
 *
 * Returns: %TRUE if no text is selected
 */
gboolean
gst_selection_is_empty(GstSelection *sel)
{
	g_return_val_if_fail(GST_IS_SELECTION(sel), TRUE);

	return (sel->ob.x == -1 ||
	        sel->mode == GST_SELECTION_IDLE ||
	        sel->mode == GST_SELECTION_EMPTY);
}

/**
 * gst_selection_get_text:
 * @sel: a #GstSelection
 *
 * Extracts selected text from the terminal buffer.
 * Port of st.c getsel(). Handles both regular and
 * rectangular selections, trims trailing spaces per line,
 * and encodes each glyph as UTF-8.
 *
 * Returns: (transfer full) (nullable): selected text, or %NULL
 */
gchar *
gst_selection_get_text(GstSelection *sel)
{
	gchar *str;
	gchar *ptr;
	gint y;
	gint lastx;
	gint linelen;
	gint bufsize;
	gint cols;
	gint start_x;
	const GstGlyph *gp;
	const GstGlyph *last;
	const GstGlyph *first;
	GstLine *line;

	g_return_val_if_fail(GST_IS_SELECTION(sel), NULL);

	if (sel->ob.x == -1 || sel->term == NULL) {
		return NULL;
	}

	cols = gst_terminal_get_cols(sel->term);

	/* Allocate worst-case buffer */
	bufsize = (cols + 1) * (sel->ne.y - sel->nb.y + 1) * UTF_SIZ;
	ptr = str = (gchar *)g_malloc((gsize)bufsize + 1);

	for (y = sel->nb.y; y <= sel->ne.y; y++) {
		line = gst_terminal_get_line(sel->term, y);
		if (line == NULL) {
			*ptr++ = '\n';
			continue;
		}

		linelen = gst_terminal_line_len(sel->term, y);
		if (linelen == 0) {
			*ptr++ = '\n';
			continue;
		}

		/* Determine column range for this row */
		if (sel->type == GST_SELECTION_TYPE_RECTANGULAR) {
			start_x = sel->nb.x;
			lastx = sel->ne.x;
		} else {
			start_x = (sel->nb.y == y) ? sel->nb.x : 0;
			lastx = (sel->ne.y == y) ? sel->ne.x : cols - 1;
		}

		first = gst_line_get_glyph_const(line, start_x);
		last = gst_line_get_glyph_const(line, MIN(lastx, linelen - 1));

		if (first == NULL || last == NULL) {
			*ptr++ = '\n';
			continue;
		}

		/* Trim trailing spaces */
		while (last >= first && last->rune == ' ') {
			--last;
		}

		/* Encode each glyph as UTF-8 */
		for (gp = first; gp <= last; ++gp) {
			if (gp->attr & GST_GLYPH_ATTR_WDUMMY) {
				continue;
			}
			ptr += gst_utf8_encode(gp->rune, ptr);
		}

		/*
		 * Add newline between rows. Don't add newline if
		 * the line is wrapped (continuing on next line) in
		 * regular selection mode.
		 */
		if ((y < sel->ne.y || lastx >= linelen) &&
		    (sel->type == GST_SELECTION_TYPE_RECTANGULAR ||
		     last < first ||
		     !((last + 1 <= gst_line_get_glyph_const(line, cols - 1)) &&
		       ((last + 1)->attr & GST_GLYPH_ATTR_WRAP)))) {
			*ptr++ = '\n';
		}
	}

	*ptr = '\0';

	/* Return NULL for empty result */
	if (ptr == str) {
		g_free(str);
		return NULL;
	}

	return str;
}

/**
 * gst_selection_get_mode:
 * @sel: a #GstSelection
 *
 * Gets the current selection mode.
 *
 * Returns: the current #GstSelectionMode
 */
GstSelectionMode
gst_selection_get_mode(GstSelection *sel)
{
	g_return_val_if_fail(GST_IS_SELECTION(sel), GST_SELECTION_IDLE);
	return sel->mode;
}

/**
 * gst_selection_set_range:
 * @sel: a #GstSelection
 * @start_col: starting column
 * @start_row: starting row
 * @end_col: ending column
 * @end_row: ending row
 *
 * Sets the selection range directly. No snapping is applied.
 */
void
gst_selection_set_range(
	GstSelection *sel,
	gint         start_col,
	gint         start_row,
	gint         end_col,
	gint         end_row
){
	g_return_if_fail(GST_IS_SELECTION(sel));

	sel->ob.x = start_col;
	sel->ob.y = start_row;
	sel->oe.x = end_col;
	sel->oe.y = end_row;
	sel->snap = GST_SELECTION_SNAP_NONE;
	sel->mode = GST_SELECTION_READY;

	if (sel->term != NULL) {
		sel->alt = gst_terminal_is_altscreen(sel->term);
	}

	sel_normalize(sel);
}
