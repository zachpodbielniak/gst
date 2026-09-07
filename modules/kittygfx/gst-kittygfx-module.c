/*
 * gst-kittygfx-module.c - Kitty graphics protocol module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements the Kitty graphics protocol for displaying inline images
 * in the terminal. Intercepts APC escape sequences via GstEscapeHandler,
 * manages an image cache, and renders placements via GstRenderOverlay.
 *
 * Protocol format:
 *   ESC _ G <key>=<val>[,<key>=<val>]... ; <base64_payload> ESC \
 *
 * The terminal's escape parser receives the full APC string and
 * dispatches it through the module manager to this module.
 */

#include "gst-kittygfx-module.h"
#include "gst-kittygfx-parser.h"
#include "gst-kittygfx-image.h"
#include "gst-kittygfx-diacritics.h"

#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/boxed/gst-glyph.h"
#include "../../src/interfaces/gst-glyph-transformer.h"
#include "../../src/boxed/gst-cursor.h"
#include "../../src/rendering/gst-render-context.h"

#include <string.h>

/* ===== Type definition ===== */

/* Maximum number of queued response bodies for echo detection */
#define MAX_SENT_RESPONSES (64)

struct _GstKittygfxModule
{
	GstModule parent_instance;

	GstKittyImageCache *cache;
	GstTerminal *terminal; /* Weak; signal handlers are disconnected on teardown. */

	/*
	 * Queue of APC bodies (gchar*) we have sent as responses.
	 * Used to detect and discard echoed responses that the PTY
	 * line discipline reflects back. Capped at MAX_SENT_RESPONSES.
	 */
	GQueue *sent_responses;

	/* Config */
	gint  max_ram_mb;
	gint  max_single_mb;
	gint  max_placements;
	gboolean allow_file_transfer;
	gboolean allow_shm_transfer;
};

/* ===== Interface implementations ===== */

static gboolean kittygfx_handle_escape(GstEscapeHandler *handler,
                                       gchar str_type, const gchar *buf,
                                       gsize len, gpointer terminal);
static void     kittygfx_render(GstRenderOverlay *overlay,
                                gpointer ctx, gint w, gint h);

static void gst_kittygfx_escape_handler_init(GstEscapeHandlerInterface *iface);
static void gst_kittygfx_render_overlay_init(GstRenderOverlayInterface *iface);
static void gst_kittygfx_glyph_transformer_init(GstGlyphTransformerInterface *iface);

/* Terminal mutation notifications are intentionally distinct from dirty
 * marks: redraws must never destroy image placements. */
static void
kittygfx_region_erased(GstTerminal *term, gint x1, gint y1, gint x2, gint y2,
	GstKittygfxModule *self)
{
	if (self->cache != NULL) {
		gst_kitty_image_cache_erase(self->cache, x1, y1, x2, y2);
		gst_terminal_mark_dirty(term, -1);
	}
}

static void
kittygfx_region_scrolled(GstTerminal *term, gint top, gint bottom, gint amount,
	GstKittygfxModule *self)
{
	if (self->cache != NULL) {
		gst_kitty_image_cache_scroll_region(self->cache, top, bottom, amount);
		gst_terminal_mark_dirty(term, -1);
	}
}

static void
kittygfx_mode_changed(GstTerminal *term, GstTermMode mode, gboolean enabled,
	GstKittygfxModule *self)
{
	(void)enabled;
	if (self->cache != NULL && (mode & GST_MODE_ALTSCREEN)) {
		gst_kitty_image_cache_clear_alt(self->cache);
		gst_terminal_mark_dirty(term, -1);
	}
}

static void
kittygfx_bind_terminal(GstKittygfxModule *self, GstTerminal *term)
{
	if (self->terminal == term) {
		return;
	}
	if (self->terminal != NULL) {
		g_signal_handlers_disconnect_by_data(self->terminal, self);
		g_object_remove_weak_pointer(G_OBJECT(self->terminal),
			(gpointer *)&self->terminal);
	}
	self->terminal = term;
	if (term != NULL) {
		g_object_add_weak_pointer(G_OBJECT(term), (gpointer *)&self->terminal);
		g_signal_connect_object(term, "mode-changed",
			G_CALLBACK(kittygfx_mode_changed), self, 0);
		/* The driver supplies these core signals; do not confuse legacy
		 * contents-changed with an overwrite notification. */
		if (g_signal_lookup("region-erased", GST_TYPE_TERMINAL) != 0) {
			g_signal_connect_object(term, "region-erased",
				G_CALLBACK(kittygfx_region_erased), self, 0);
		}
		if (g_signal_lookup("region-scrolled", GST_TYPE_TERMINAL) != 0) {
			g_signal_connect_object(term, "region-scrolled",
				G_CALLBACK(kittygfx_region_scrolled), self, 0);
		}
	}
}

G_DEFINE_TYPE_WITH_CODE(GstKittygfxModule, gst_kittygfx_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_kittygfx_escape_handler_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_kittygfx_render_overlay_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_GLYPH_TRANSFORMER,
		gst_kittygfx_glyph_transformer_init))

/* Decode one cell without relying on draw order or persistent coordinates.
 * Missing diacritics inherit only from contiguous matching placeholders. */
static gboolean
placeholder_coordinates(GstLine *line, gint col,
	guint32 *image_id, gint *image_row, gint *image_col)
{
	gint start;
	gint i;
	gint row;
	gint column;
	gint high;
	guint32 previous_fg;
	gboolean previous;
	GstGlyph *target;

	target = gst_line_get_glyph(line, col);
	if (target == NULL) {
		return FALSE;
	}
	start = col;
	while (start > 0) {
		GstGlyph *g;

		g = gst_line_get_glyph(line, start - 1);
		if (g == NULL || g->rune != 0x10EEEE || g->fg != target->fg) {
			break;
		}
		start--;
	}
	row = column = high = 0;
	previous_fg = 0;
	previous = FALSE;
	for (i = start; i <= col; i++) {
		GstGlyph *g;
		gchar buffer[7];
		const gchar *text;
		gint values[3];
		gint n;
		gboolean inherit;
		gboolean valid;

		g = gst_line_get_glyph(line, i);
		if (g == NULL || g->rune != 0x10EEEE ||
		    (!GST_IS_TRUECOLOR(g->fg) && g->fg > 255)) {
			return FALSE;
		}
		text = gst_glyph_get_text(g, buffer);
		text = g_utf8_next_char(text);
		n = 0;
		valid = TRUE;
		while (*text != '\0' && n < 3) {
			gunichar rune;
			guint j;

			rune = g_utf8_get_char(text);
			for (j = 0; j < G_N_ELEMENTS(kitty_diacritics); j++) {
				if (kitty_diacritics[j] == rune) {
					break;
				}
			}
			if (j == G_N_ELEMENTS(kitty_diacritics)) {
				valid = FALSE;
				break;
			}
			values[n++] = (gint)j;
			text = g_utf8_next_char(text);
		}
		if (!valid || (n == 3 && values[2] > 255)) {
			if (i == col) {
				return FALSE;
			}
			previous = FALSE;
			continue;
		}
		inherit = previous && previous_fg == g->fg &&
			(n == 0 || values[0] == row);
		if (n >= 2 && values[1] != column + 1) {
			inherit = FALSE;
		}
		row = n >= 1 ? values[0] : (inherit ? row : 0);
		column = n >= 2 ? values[1] : (inherit ? column + 1 : 0);
		high = n >= 3 ? values[2] : (inherit ? high : 0);
		previous = TRUE;
		previous_fg = g->fg;
	}
	*image_id = (previous_fg & 0xFFFFFF) | ((guint32)high << 24);
	*image_row = row;
	*image_col = column;
	return *image_id != 0;
}

/* Render only the portion represented by this text cell. Sampling in virtual
 * placement pixel coordinates avoids seams and works even when a source
 * pixel spans many cells. Text overwrite/scroll/reflow needs no image state. */
static gboolean
kittygfx_transform_glyph(GstGlyphTransformer *transformer, gunichar rune,
	gpointer render_context, gint x, gint y, gint width, gint height)
{
	GstKittygfxModule *self;
	GstRenderContext *ctx;
	GstImagePlacement *pl;
	GstKittyImage *img;
	GList *l;
	guint32 image_id;
	gint row;
	gint col;
	gint sw;
	gint sh;
	gint px;
	gint py;
	gdouble scale;
	g_autofree guint8 *pixels = NULL;

	if (rune != 0x10EEEE) {
		return FALSE;
	}
	self = GST_KITTYGFX_MODULE(transformer);
	ctx = (GstRenderContext *)render_context;
	if (self->cache == NULL || ctx == NULL || ctx->current_line == NULL ||
	    width <= 0 || height <= 0 || width > G_MAXINT / 4) {
		return FALSE;
	}
	gst_render_context_fill_rect_bg(ctx, x, y, width, height);
	if (!placeholder_coordinates((GstLine *)ctx->current_line, ctx->current_col,
	    &image_id, &row, &col)) {
		return TRUE;
	}
	pl = NULL;
	for (l = self->cache->placements; l != NULL; l = l->next) {
		GstImagePlacement *candidate;

		candidate = (GstImagePlacement *)l->data;
		if (candidate->virtual_placement && candidate->image_id == image_id) {
			pl = candidate;
			break;
		}
	}
	img = gst_kitty_image_cache_get_image(self->cache, image_id);
	if (pl == NULL || img == NULL || img->data == NULL ||
	    row >= pl->dst_rows || col >= pl->dst_cols ||
	    pl->src_x < 0 || pl->src_x >= img->width ||
	    pl->src_y < 0 || pl->src_y >= img->height) {
		return TRUE;
	}
	sw = MIN(pl->crop_w > 0 ? pl->crop_w : img->width, img->width - pl->src_x);
	sh = MIN(pl->crop_h > 0 ? pl->crop_h : img->height, img->height - pl->src_y);
	scale = MIN((gdouble)pl->dst_cols * width / sw,
		(gdouble)pl->dst_rows * height / sh);
	pixels = g_try_malloc0_n((gsize)height, (gsize)width * 4);
	if (pixels == NULL) {
		return TRUE;
	}
	for (py = 0; py < height; py++) {
		for (px = 0; px < width; px++) {
			gdouble sx;
			gdouble sy;

			sx = ((gdouble)col * width + px) / scale;
			sy = ((gdouble)row * height + py) / scale;
			if (sx < sw && sy < sh) {
				memcpy(pixels + ((gsize)py * width + px) * 4,
					img->data + (gsize)(pl->src_y + (gint)sy) * img->stride +
					(gsize)(pl->src_x + (gint)sx) * 4, 4);
			}
		}
	}
	gst_render_context_draw_image(ctx, pixels, width, height, width * 4,
		x, y, width, height);
	return TRUE;
}

static void
gst_kittygfx_glyph_transformer_init(GstGlyphTransformerInterface *iface)
{
	iface->transform_glyph = kittygfx_transform_glyph;
}

/* ===== Interface init ===== */

static void
gst_kittygfx_escape_handler_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string = kittygfx_handle_escape;
}

static void
gst_kittygfx_render_overlay_init(GstRenderOverlayInterface *iface)
{
	iface->render = kittygfx_render;
}

/* ===== GstModule vfuncs ===== */

/*
 * configure:
 *
 * Read module config from YAML.
 * Keys: max_total_ram_mb, max_single_image_mb, max_placements,
 *       allow_file_transfer, allow_shm_transfer.
 */
static void
kittygfx_configure(
	GstModule *base,
	gpointer   config
){
	GstKittygfxModule *self;
	GstConfig *cfg;

	self = GST_KITTYGFX_MODULE(base);
	cfg = (GstConfig *)config;

	self->max_ram_mb = cfg->modules.kittygfx.max_total_ram_mb;
	self->max_single_mb = cfg->modules.kittygfx.max_single_image_mb;
	self->max_placements = cfg->modules.kittygfx.max_placements;
	self->allow_file_transfer = cfg->modules.kittygfx.allow_file_transfer;
	self->allow_shm_transfer = cfg->modules.kittygfx.allow_shm_transfer;

	g_debug("kittygfx: configured (ram=%dMB, single=%dMB, "
		"placements=%d, file=%d, shm=%d)",
		self->max_ram_mb, self->max_single_mb,
		self->max_placements, self->allow_file_transfer,
		self->allow_shm_transfer);
}

/*
 * activate:
 *
 * Create the image cache with configured limits.
 */
static gboolean
kittygfx_activate(GstModule *base)
{
	GstKittygfxModule *self;

	self = GST_KITTYGFX_MODULE(base);

	/* Create image cache if not already present */
	if (self->cache == NULL) {
		self->cache = gst_kitty_image_cache_new(
			self->max_ram_mb,
			self->max_single_mb,
			self->max_placements);
	}

	return TRUE;
}

/*
 * deactivate:
 *
 * Free the image cache.
 */
static void
kittygfx_deactivate(GstModule *base)
{
	GstKittygfxModule *self;

	self = GST_KITTYGFX_MODULE(base);
	kittygfx_bind_terminal(self, NULL);

	if (self->cache != NULL) {
		gst_kitty_image_cache_free(self->cache);
		self->cache = NULL;
	}
}

/* ===== Escape handler implementation ===== */

/*
 * kittygfx_handle_escape:
 *
 * Handles APC escape sequences. Only processes sequences that start
 * with 'G' (kitty graphics protocol identifier).
 *
 * Flow:
 * 1. Check for 'G' prefix
 * 2. Parse key=value command
 * 3. Validate transmission type (reject file/shm if not allowed)
 * 4. Process through image cache
 * 5. Set placement position from cursor if needed
 * 6. Send response back via terminal if needed
 */
static gboolean
kittygfx_handle_escape(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstKittygfxModule *self;
	GstGraphicsCommand cmd;
	gchar *response;

	self = GST_KITTYGFX_MODULE(handler);

	/* Only handle APC sequences starting with 'G' */
	if (str_type != '_' || len < 2 || buf[0] != 'G') {
		return FALSE;
	}

	if (self->cache == NULL) {
		return FALSE;
	}
	kittygfx_bind_terminal(self, (GstTerminal *)terminal);

	/*
	 * Detect and discard echoed responses to prevent echo cascade.
	 *
	 * When we write a response (e.g. \033_Gi=31,p=1;OK\033\\) to the PTY,
	 * the line discipline echoes it back if ECHO is on. The echoed
	 * data arrives here as a new APC. Without this check, it would
	 * be parsed as a transmit command (default action='t'), fail to
	 * decode the status text as base64, generate an error response,
	 * which echoes again — creating an infinite cascade whose error
	 * messages contain characters like 'd' that leak to the child
	 * process as keypresses.
	 *
	 * Strategy:
	 * 1. Queue match: compare incoming body against bodies we sent.
	 * 2. Fallback heuristic: detect response-shaped strings that
	 *    start with "i=" and whose payload starts with a status
	 *    word (OK, E...) rather than base64 data.
	 */
	{
		const gchar *after_g;
		gsize after_g_len;
		GList *ql;

		after_g = buf + 1;
		after_g_len = len - 1;

		/* Queue match: exact body comparison against sent responses */
		for (ql = self->sent_responses->head; ql != NULL; ql = ql->next) {
			const gchar *queued;
			gsize queued_len;

			queued = (const gchar *)ql->data;
			queued_len = strlen(queued);
			if (queued_len == after_g_len &&
			    memcmp(queued, after_g, queued_len) == 0) {
				/* Match found - remove from queue and consume */
				g_free(ql->data);
				g_queue_delete_link(self->sent_responses, ql);
				return TRUE;
			}
		}

		/*
		 * Fallback heuristic: response bodies always start with "i="
		 * and the payload after ';' is a status word (OK or E...),
		 * never valid base64 image data. Real transmit commands have
		 * action keys like "a=", "f=", "s=" in addition to "i=".
		 */
		{
			const gchar *semi;

			semi = (const gchar *)memchr(after_g, ';', after_g_len);
			if (semi != NULL && after_g_len >= 3 &&
			    after_g[0] == 'i' && after_g[1] == '=') {
				const gchar *payload;
				gsize payload_len;

				payload = semi + 1;
				payload_len = after_g_len - (gsize)(payload - after_g);

				/*
				 * Status payloads are "OK" or error codes matching
				 * E<UPPERCASE>:<message> (e.g., EINVAL:, ENOENT:).
				 * The colon ':' is NOT in the base64 alphabet, so
				 * checking E + uppercase + colon definitively identifies
				 * error responses vs base64 image data.
				 */
				if ((payload_len >= 2 &&
				     payload[0] == 'O' && payload[1] == 'K') ||
				    (payload_len >= 3 &&
				     payload[0] == 'E' &&
				     payload[1] >= 'A' && payload[1] <= 'Z' &&
				     memchr(payload, ':', payload_len) != NULL)) {
					return TRUE; /* consume echoed response */
				}
			}
		}
	}

	/* Parse the command (skip the leading 'G') */
	if (!gst_gfx_command_parse(buf + 1, len - 1, &cmd)) {
		return FALSE;
	}

	/* Security: reject file and shm transfers unless allowed */
	if (cmd.transmission == 'f' || cmd.transmission == 't') {
		if (!self->allow_file_transfer) {
			return TRUE; /* consume but ignore */
		}
	}
	if (cmd.transmission == 's') {
		if (!self->allow_shm_transfer) {
			return TRUE;
		}
	}

	/* Get cursor position for delete commands */
	{
		GstTerminal *term;
		GstCursor *cursor;
		gint cur_col;
		gint cur_row;

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

		/* Process the command */
		response = NULL;
		gst_kitty_image_cache_process(self->cache, &cmd,
			cur_col, cur_row, &response);

		/*
		 * Delete commands remove placements but don't modify any
		 * terminal line content, so no lines get marked dirty by
		 * the escape processor. Without an explicit dirty mark,
		 * the renderer skips those lines and old image pixels
		 * persist in the pixmap from the previous frame.
		 *
		 * Force a full redraw so line backgrounds get repainted
		 * over the area where the old image was.
		 */
		if (term != NULL) {
			gst_terminal_mark_dirty(term, -1);
		}
	}

	/* Send response back to PTY via terminal signal */
	if (response != NULL && terminal != NULL) {
		/*
		 * Record the APC body (between \033_G and \033\\) so we can
		 * detect and discard the echo if the line discipline reflects
		 * it back. Extract body by skipping the \033_G prefix (3 bytes)
		 * and trimming the \033\\ suffix (2 bytes).
		 */
		{
			gsize resp_len;

			resp_len = strlen(response);
			if (resp_len > 5) {
				gchar *body;

				body = g_strndup(response + 3, resp_len - 5);
				g_queue_push_tail(self->sent_responses, body);

				/* Cap queue size to prevent unbounded growth */
				while (g_queue_get_length(self->sent_responses) >
				       MAX_SENT_RESPONSES) {
					g_free(g_queue_pop_head(self->sent_responses));
				}
			}
		}

		g_signal_emit_by_name(terminal, "response",
			response, (glong)strlen(response));
	}
	g_free(response);

	return TRUE;
}

/* ===== Render overlay implementation ===== */

/*
 * kittygfx_render:
 *
 * Renders all visible image placements on the terminal.
 * Iterates placements sorted by z-index and draws each
 * using the render context's draw_image function.
 *
 * Negative z-index placements render behind text (rendered
 * before text by the overlay system). Positive z-index
 * placements render on top.
 */
static void
kittygfx_render(
	GstRenderOverlay *overlay,
	gpointer          render_ctx,
	gint              width,
	gint              height
){
	GstKittygfxModule *self;
	GstRenderContext *ctx;
	GstModuleManager *mgr;
	GstTerminal *term;
	GList *visible;
	GList *l;
	gint rows;
	gint top_row;

	self = GST_KITTYGFX_MODULE(overlay);
	ctx = (GstRenderContext *)render_ctx;

	if (self->cache == NULL || ctx == NULL) {
		return;
	}

	/* Get terminal dimensions for visible range */
	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		return;
	}

	rows = gst_terminal_get_rows(term);
	top_row = 0;

	/* Get placements visible in the current view */
	visible = gst_kitty_image_cache_get_visible_placements(
		self->cache, top_row, top_row + rows - 1);

	for (l = visible; l != NULL; l = l->next) {
		GstImagePlacement *pl;
		GstKittyImage *img;
		gint px;
		gint py;
		gint dw;
		gint dh;
		gint sw;
		gint sh;
		const guint8 *src_data;
		gint src_stride;
		gint64 position_x;
		gint64 position_y;
		gint64 dest_width;
		gint64 dest_height;

		pl = (GstImagePlacement *)l->data;
		img = gst_kitty_image_cache_get_image(
			self->cache, pl->image_id);

		if (img == NULL || img->data == NULL) {
			continue;
		}

		/* Calculate pixel position */
		position_x = ctx->borderpx + (gint64)pl->col * ctx->cw + pl->x_offset;
		position_y = ctx->borderpx + ((gint64)pl->row - top_row) * ctx->ch + pl->y_offset;
		if (position_x < G_MININT || position_x > G_MAXINT ||
		    position_y < G_MININT || position_y > G_MAXINT) {
			continue;
		}
		px = (gint)position_x;
		py = (gint)position_y;

		/* Validate offsets before arithmetic or constructing a pixel pointer. */
		if (pl->src_x < 0 || pl->src_x >= img->width ||
		    pl->src_y < 0 || pl->src_y >= img->height) {
			continue;
		}

		/* Determine source region */
		sw = (pl->crop_w > 0) ? pl->crop_w : img->width;
		sh = (pl->crop_h > 0) ? pl->crop_h : img->height;

		if (sw > img->width - pl->src_x) {
			sw = img->width - pl->src_x;
		}
		if (sh > img->height - pl->src_y) {
			sh = img->height - pl->src_y;
		}

		if (sw <= 0 || sh <= 0) {
			continue;
		}

		/* Calculate destination size */
		dest_width = pl->dst_cols > 0 ? (gint64)pl->dst_cols * ctx->cw : sw;
		dest_height = pl->dst_rows > 0 ? (gint64)pl->dst_rows * ctx->ch : sh;
		if (dest_width <= 0 || dest_width > G_MAXINT ||
		    dest_height <= 0 || dest_height > G_MAXINT) {
			continue;
		}
		if (pl->dst_cols > 0 && pl->dst_rows == 0) {
			dest_height = dest_width * sh / sw;
		} else if (pl->dst_rows > 0 && pl->dst_cols == 0) {
			dest_width = dest_height * sw / sh;
		}
		if (dest_width <= 0 || dest_width > G_MAXINT ||
		    dest_height <= 0 || dest_height > G_MAXINT) {
			continue;
		}
		dw = (gint)dest_width;
		dh = (gint)dest_height;

		/* Get source data pointer (offset by crop region) */
		src_data = img->data + (gsize)pl->src_y * img->stride + (gsize)pl->src_x * 4;
		src_stride = img->stride;

		/* Clip to window bounds */
		if (px >= width || py >= height || position_x + dw <= 0 ||
		    position_y + dh <= 0) {
			continue;
		}
		/* Let the backend clip; shrinking only the destination stretches the
		 * whole source into the visible fragment instead of cropping it. */

		/* Draw the image */
		gst_render_context_draw_image(ctx,
			src_data, sw, sh, src_stride,
			px, py, dw, dh);
	}

	g_list_free(visible);
}

/* ===== GObject lifecycle ===== */

static void
gst_kittygfx_module_finalize(GObject *object)
{
	GstKittygfxModule *self;

	self = GST_KITTYGFX_MODULE(object);
	kittygfx_bind_terminal(self, NULL);

	if (self->cache != NULL) {
		gst_kitty_image_cache_free(self->cache);
		self->cache = NULL;
	}

	if (self->sent_responses != NULL) {
		g_queue_free_full(self->sent_responses, g_free);
		self->sent_responses = NULL;
	}

	G_OBJECT_CLASS(gst_kittygfx_module_parent_class)->finalize(object);
}

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 * Must match the config key under modules: { kittygfx: ... }.
 */
static const gchar *
kittygfx_get_name(GstModule *module)
{
	(void)module;
	return "kittygfx";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
kittygfx_get_description(GstModule *module)
{
	(void)module;
	return "Kitty graphics protocol for inline images";
}

static void
gst_kittygfx_module_class_init(GstKittygfxModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_kittygfx_module_finalize;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = kittygfx_get_name;
	module_class->get_description = kittygfx_get_description;
	module_class->configure = kittygfx_configure;
	module_class->activate = kittygfx_activate;
	module_class->deactivate = kittygfx_deactivate;
}

static void
gst_kittygfx_module_init(GstKittygfxModule *self)
{
	self->cache = NULL;
	self->sent_responses = g_queue_new();

	/* Defaults */
	self->max_ram_mb = 256;
	self->max_single_mb = 64;
	self->max_placements = 4096;
	self->allow_file_transfer = FALSE;
	self->allow_shm_transfer = FALSE;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Module entry point. Returns the GType so the module manager
 * can instantiate this module.
 *
 * Returns: The #GType for #GstKittygfxModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_KITTYGFX_MODULE;
}
