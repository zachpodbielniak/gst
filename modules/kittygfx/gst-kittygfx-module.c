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

#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/core/gst-terminal.h"
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

G_DEFINE_TYPE_WITH_CODE(GstKittygfxModule, gst_kittygfx_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_kittygfx_escape_handler_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_RENDER_OVERLAY,
		gst_kittygfx_render_overlay_init))

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
	YamlMapping *mod_cfg;

	self = GST_KITTYGFX_MODULE(base);
	cfg = (GstConfig *)config;

	mod_cfg = gst_config_get_module_config(cfg, "kittygfx");
	if (mod_cfg == NULL) {
		return;
	}

	if (yaml_mapping_has_member(mod_cfg, "max_total_ram_mb")) {
		self->max_ram_mb = (gint)yaml_mapping_get_int_member(
			mod_cfg, "max_total_ram_mb");
	}
	if (yaml_mapping_has_member(mod_cfg, "max_single_image_mb")) {
		self->max_single_mb = (gint)yaml_mapping_get_int_member(
			mod_cfg, "max_single_image_mb");
	}
	if (yaml_mapping_has_member(mod_cfg, "max_placements")) {
		self->max_placements = (gint)yaml_mapping_get_int_member(
			mod_cfg, "max_placements");
	}
	if (yaml_mapping_has_member(mod_cfg, "allow_file_transfer")) {
		self->allow_file_transfer = yaml_mapping_get_boolean_member(
			mod_cfg, "allow_file_transfer");
	}
	if (yaml_mapping_has_member(mod_cfg, "allow_shm_transfer")) {
		self->allow_shm_transfer = yaml_mapping_get_boolean_member(
			mod_cfg, "allow_shm_transfer");
	}
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
		if (cmd.action == 'd' && term != NULL) {
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
		g_free(response);
	}

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

		pl = (GstImagePlacement *)l->data;
		img = gst_kitty_image_cache_get_image(
			self->cache, pl->image_id);

		if (img == NULL || img->data == NULL) {
			continue;
		}

		/* Calculate pixel position */
		px = ctx->borderpx + pl->col * ctx->cw + pl->x_offset;
		py = ctx->borderpx + (pl->row - top_row) * ctx->ch + pl->y_offset;

		/* Determine source region */
		sw = (pl->crop_w > 0) ? pl->crop_w : img->width;
		sh = (pl->crop_h > 0) ? pl->crop_h : img->height;

		if (pl->src_x + sw > img->width) {
			sw = img->width - pl->src_x;
		}
		if (pl->src_y + sh > img->height) {
			sh = img->height - pl->src_y;
		}

		if (sw <= 0 || sh <= 0) {
			continue;
		}

		/* Calculate destination size */
		if (pl->dst_cols > 0) {
			dw = pl->dst_cols * ctx->cw;
		} else {
			dw = sw;
		}
		if (pl->dst_rows > 0) {
			dh = pl->dst_rows * ctx->ch;
		} else {
			dh = sh;
		}

		/* Get source data pointer (offset by crop region) */
		src_data = img->data + (pl->src_y * img->stride) + (pl->src_x * 4);
		src_stride = img->stride;

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
