/*
 * gst-wallpaper-module.c - Background image wallpaper module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Renders a PNG or JPEG image behind terminal text using the
 * GstBackgroundProvider interface. The image is loaded via
 * stb_image (zero external dependencies) and drawn through the
 * abstract render context draw_image vtable.
 *
 * Default-background cells are drawn with reduced alpha on
 * Wayland (Cairo compositing) or skipped entirely on X11
 * so the wallpaper shows through.
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#include "gst-wallpaper-module.h"
#include "../../src/config/gst-config.h"
#include "../../src/rendering/gst-render-context.h"

#include <math.h>

/**
 * SECTION:gst-wallpaper-module
 * @title: GstWallpaperModule
 * @short_description: Background image behind terminal text
 *
 * #GstWallpaperModule loads a PNG or JPEG image and renders it
 * as the terminal background via the #GstBackgroundProvider
 * interface. Supports fill, fit, stretch, and center scale modes.
 * Pre-scales the image on window resize for per-frame performance.
 */

/* Scale mode enum */
enum {
	GST_WALLPAPER_FILL    = 0,  /* cover window, crop excess */
	GST_WALLPAPER_FIT     = 1,  /* fit within window, letterbox */
	GST_WALLPAPER_STRETCH = 2,  /* stretch to exact window size */
	GST_WALLPAPER_CENTER  = 3   /* no scaling, center in window */
};

struct _GstWallpaperModule
{
	GstModule   parent_instance;

	/* Config values */
	gchar      *image_path;
	gint        scale_mode;
	gdouble     bg_alpha;

	/* Source image (decoded once from file) */
	guint8     *src_pixels;     /* RGBA, 4 bytes per pixel */
	gint        src_w;
	gint        src_h;

	/* Pre-scaled cache (recomputed on window resize) */
	guint8     *scaled_pixels;
	gint        scaled_w;
	gint        scaled_h;
	gint        scaled_stride;
	gint        draw_x;        /* x offset for centering/letterboxing */
	gint        draw_y;        /* y offset for centering/letterboxing */

	/* Cached window dimensions for resize detection */
	gint        last_win_w;
	gint        last_win_h;

	gboolean    image_loaded;
};

/* Forward declaration */
static void
gst_wallpaper_module_background_init(GstBackgroundProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstWallpaperModule, gst_wallpaper_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_BACKGROUND_PROVIDER,
		gst_wallpaper_module_background_init))

/* ===== Image scaling ===== */

/*
 * bilinear_scale:
 *
 * Scales RGBA pixel data using bilinear interpolation.
 * Allocates and returns a new buffer of size dst_w * dst_h * 4.
 * Returns NULL on allocation failure.
 */
static guint8 *
bilinear_scale(
	const guint8 *src,
	gint          src_w,
	gint          src_h,
	gint          dst_w,
	gint          dst_h
){
	guint8 *dst;
	gint dx;
	gint dy;
	gint src_stride;
	gint dst_stride;

	dst_stride = dst_w * 4;
	src_stride = src_w * 4;
	dst = (guint8 *)g_malloc(dst_stride * dst_h);
	if (dst == NULL) {
		return NULL;
	}

	for (dy = 0; dy < dst_h; dy++) {
		gdouble sy;
		gint sy0;
		gint sy1;
		gdouble fy;

		/* Map destination pixel to source coordinate */
		sy = ((gdouble)dy + 0.5) * ((gdouble)src_h / (gdouble)dst_h) - 0.5;
		sy0 = (gint)floor(sy);
		sy1 = sy0 + 1;
		fy = sy - (gdouble)sy0;

		if (sy0 < 0) { sy0 = 0; }
		if (sy1 >= src_h) { sy1 = src_h - 1; }

		for (dx = 0; dx < dst_w; dx++) {
			gdouble sx;
			gint sx0;
			gint sx1;
			gdouble fx;
			const guint8 *p00;
			const guint8 *p10;
			const guint8 *p01;
			const guint8 *p11;
			gint c;

			sx = ((gdouble)dx + 0.5) * ((gdouble)src_w / (gdouble)dst_w) - 0.5;
			sx0 = (gint)floor(sx);
			sx1 = sx0 + 1;
			fx = sx - (gdouble)sx0;

			if (sx0 < 0) { sx0 = 0; }
			if (sx1 >= src_w) { sx1 = src_w - 1; }

			/* Four source pixels for interpolation */
			p00 = src + sy0 * src_stride + sx0 * 4;
			p10 = src + sy0 * src_stride + sx1 * 4;
			p01 = src + sy1 * src_stride + sx0 * 4;
			p11 = src + sy1 * src_stride + sx1 * 4;

			/* Bilinear blend each channel (R, G, B, A) */
			for (c = 0; c < 4; c++) {
				gdouble top;
				gdouble bot;
				gdouble val;

				top = (gdouble)p00[c] * (1.0 - fx) + (gdouble)p10[c] * fx;
				bot = (gdouble)p01[c] * (1.0 - fx) + (gdouble)p11[c] * fx;
				val = top * (1.0 - fy) + bot * fy;
				dst[dy * dst_stride + dx * 4 + c] = (guint8)(val + 0.5);
			}
		}
	}

	return dst;
}

/*
 * compute_scaled_image:
 *
 * Recomputes the pre-scaled pixel buffer for the current window
 * dimensions and scale mode. Sets draw_x, draw_y, scaled_w,
 * scaled_h, and updates the cached window size.
 */
static void
compute_scaled_image(
	GstWallpaperModule *self,
	gint                win_w,
	gint                win_h
){
	gint new_w;
	gint new_h;
	gint crop_x;
	gint crop_y;
	gint crop_w;
	gint crop_h;

	g_free(self->scaled_pixels);
	self->scaled_pixels = NULL;
	self->draw_x = 0;
	self->draw_y = 0;

	self->last_win_w = win_w;
	self->last_win_h = win_h;

	if (!self->image_loaded || self->src_w <= 0 || self->src_h <= 0) {
		return;
	}

	switch (self->scale_mode) {
	case GST_WALLPAPER_STRETCH:
		/* Scale to exact window size (distorts aspect ratio) */
		self->scaled_pixels = bilinear_scale(
			self->src_pixels, self->src_w, self->src_h,
			win_w, win_h);
		self->scaled_w = win_w;
		self->scaled_h = win_h;
		break;

	case GST_WALLPAPER_CENTER:
		/* No scaling; copy source as-is, center in window */
		self->scaled_w = self->src_w;
		self->scaled_h = self->src_h;
		self->draw_x = (win_w - self->src_w) / 2;
		self->draw_y = (win_h - self->src_h) / 2;
		self->scaled_stride = self->src_w * 4;
		self->scaled_pixels = (guint8 *)g_memdup2(
			self->src_pixels,
			(gsize)(self->src_w * self->src_h * 4));
		return; /* skip stride update below */

	case GST_WALLPAPER_FIT: {
		/* Scale to fit within window, preserve aspect ratio */
		gdouble factor_w;
		gdouble factor_h;
		gdouble factor;

		factor_w = (gdouble)win_w / (gdouble)self->src_w;
		factor_h = (gdouble)win_h / (gdouble)self->src_h;
		factor = (factor_w < factor_h) ? factor_w : factor_h;

		new_w = (gint)(self->src_w * factor + 0.5);
		new_h = (gint)(self->src_h * factor + 0.5);
		if (new_w < 1) { new_w = 1; }
		if (new_h < 1) { new_h = 1; }

		self->scaled_pixels = bilinear_scale(
			self->src_pixels, self->src_w, self->src_h,
			new_w, new_h);
		self->scaled_w = new_w;
		self->scaled_h = new_h;
		self->draw_x = (win_w - new_w) / 2;
		self->draw_y = (win_h - new_h) / 2;
		break;
	}

	case GST_WALLPAPER_FILL:
	default: {
		/* Scale to cover window, crop excess, preserve aspect ratio */
		gdouble factor_w;
		gdouble factor_h;
		gdouble factor;

		factor_w = (gdouble)win_w / (gdouble)self->src_w;
		factor_h = (gdouble)win_h / (gdouble)self->src_h;
		factor = (factor_w > factor_h) ? factor_w : factor_h;

		new_w = (gint)(self->src_w * factor + 0.5);
		new_h = (gint)(self->src_h * factor + 0.5);
		if (new_w < 1) { new_w = 1; }
		if (new_h < 1) { new_h = 1; }

		/* Scale to cover size first */
		{
			guint8 *full;

			full = bilinear_scale(
				self->src_pixels, self->src_w, self->src_h,
				new_w, new_h);
			if (full == NULL) {
				return;
			}

			/* Crop to window size from center */
			crop_x = (new_w - win_w) / 2;
			crop_y = (new_h - win_h) / 2;
			crop_w = (win_w < new_w) ? win_w : new_w;
			crop_h = (win_h < new_h) ? win_h : new_h;

			self->scaled_pixels = (guint8 *)g_malloc(
				(gsize)(crop_w * crop_h * 4));
			if (self->scaled_pixels != NULL) {
				gint row;

				for (row = 0; row < crop_h; row++) {
					memcpy(
						self->scaled_pixels + row * crop_w * 4,
						full + (crop_y + row) * new_w * 4 + crop_x * 4,
						(gsize)(crop_w * 4));
				}
			}

			g_free(full);
			self->scaled_w = crop_w;
			self->scaled_h = crop_h;
		}
		break;
	}
	}

	self->scaled_stride = self->scaled_w * 4;
}

/* ===== Image loading ===== */

/*
 * load_image:
 *
 * Loads the image from self->image_path using stb_image.
 * Decodes to RGBA (4 channels). Copies to a glib-owned buffer
 * since stb uses its own allocator.
 */
static void
load_image(GstWallpaperModule *self)
{
	guint8 *stb_data;
	gint w;
	gint h;
	gint channels;
	gsize size;

	/* Free any previously loaded data */
	g_free(self->src_pixels);
	self->src_pixels = NULL;
	self->image_loaded = FALSE;

	if (self->image_path == NULL || self->image_path[0] == '\0') {
		g_debug("wallpaper: no image path configured");
		return;
	}

	/* Decode image file to RGBA */
	stb_data = stbi_load(self->image_path, &w, &h, &channels, 4);
	if (stb_data == NULL) {
		g_warning("wallpaper: failed to load '%s': %s",
			self->image_path, stbi_failure_reason());
		return;
	}

	/* Copy to glib-owned buffer */
	size = (gsize)(w * h * 4);
	self->src_pixels = (guint8 *)g_memdup2(stb_data, size);
	stbi_image_free(stb_data);

	self->src_w = w;
	self->src_h = h;
	self->image_loaded = TRUE;

	/* Invalidate scaled cache so it gets rebuilt on next render */
	g_free(self->scaled_pixels);
	self->scaled_pixels = NULL;
	self->last_win_w = 0;
	self->last_win_h = 0;

	g_debug("wallpaper: loaded '%s' (%dx%d)", self->image_path, w, h);
}

/* ===== GstBackgroundProvider interface ===== */

/*
 * render_background:
 *
 * Called before line drawing each render cycle. Draws the
 * pre-scaled wallpaper image, detects window resizes, and
 * sets the render context wallpaper flags.
 */
static void
gst_wallpaper_module_render_background(
	GstBackgroundProvider *provider,
	gpointer               render_context,
	gint                   width,
	gint                   height
){
	GstWallpaperModule *self;
	GstRenderContext *ctx;

	self = GST_WALLPAPER_MODULE(provider);
	ctx = (GstRenderContext *)render_context;

	if (!self->image_loaded) {
		return;
	}

	/* Recompute scaled image if window size changed */
	if (width != self->last_win_w || height != self->last_win_h) {
		compute_scaled_image(self, width, height);
	}

	if (self->scaled_pixels == NULL) {
		return;
	}

	/* Draw the pre-scaled wallpaper image (1:1 blit) */
	gst_render_context_draw_image(ctx,
		self->scaled_pixels,
		self->scaled_w, self->scaled_h, self->scaled_stride,
		self->draw_x, self->draw_y,
		self->scaled_w, self->scaled_h);

	/* Signal renderers that wallpaper is active */
	ctx->has_wallpaper = TRUE;
	ctx->wallpaper_bg_alpha = self->bg_alpha;
}

static void
gst_wallpaper_module_background_init(GstBackgroundProviderInterface *iface)
{
	iface->render_background = gst_wallpaper_module_render_background;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_wallpaper_module_get_name(GstModule *module)
{
	(void)module;
	return "wallpaper";
}

static const gchar *
gst_wallpaper_module_get_description(GstModule *module)
{
	(void)module;
	return "Background image behind terminal text";
}

/*
 * parse_scale_mode:
 *
 * Converts a scale mode string to the internal enum value.
 */
static gint
parse_scale_mode(const gchar *str)
{
	if (str == NULL) {
		return GST_WALLPAPER_FILL;
	}
	if (g_strcmp0(str, "fit") == 0) {
		return GST_WALLPAPER_FIT;
	}
	if (g_strcmp0(str, "stretch") == 0) {
		return GST_WALLPAPER_STRETCH;
	}
	if (g_strcmp0(str, "center") == 0) {
		return GST_WALLPAPER_CENTER;
	}
	return GST_WALLPAPER_FILL;
}

static gboolean
gst_wallpaper_module_activate(GstModule *module)
{
	GstWallpaperModule *self;

	self = GST_WALLPAPER_MODULE(module);
	load_image(self);

	g_debug("wallpaper: activated (image_loaded=%s, scale=%d, bg_alpha=%.2f)",
		self->image_loaded ? "yes" : "no",
		self->scale_mode, self->bg_alpha);

	/* Return TRUE even if image failed to load; module is a graceful no-op */
	return TRUE;
}

static void
gst_wallpaper_module_deactivate(GstModule *module)
{
	GstWallpaperModule *self;

	self = GST_WALLPAPER_MODULE(module);

	g_free(self->src_pixels);
	self->src_pixels = NULL;
	g_free(self->scaled_pixels);
	self->scaled_pixels = NULL;
	self->image_loaded = FALSE;

	g_debug("wallpaper: deactivated");
}

/*
 * configure:
 *
 * Reads wallpaper configuration from the config struct:
 *  - image_path: filesystem path to the image file
 *  - scale_mode: "fill", "fit", "stretch", "center"
 *  - bg_alpha: default-bg cell opacity (0.0-1.0)
 */
static void
gst_wallpaper_module_configure(GstModule *module, gpointer config)
{
	GstWallpaperModule *self;
	GstConfig *cfg;

	self = GST_WALLPAPER_MODULE(module);
	cfg = (GstConfig *)config;

	g_free(self->image_path);
	self->image_path = g_strdup(cfg->modules.wallpaper.image_path);
	self->scale_mode = parse_scale_mode(cfg->modules.wallpaper.scale_mode);
	self->bg_alpha = cfg->modules.wallpaper.bg_alpha;

	/* Clamp bg_alpha to valid range */
	if (self->bg_alpha < 0.0) { self->bg_alpha = 0.0; }
	if (self->bg_alpha > 1.0) { self->bg_alpha = 1.0; }

	g_debug("wallpaper: configured (path='%s', mode=%d, bg_alpha=%.2f)",
		self->image_path, self->scale_mode, self->bg_alpha);
}

/* ===== GObject lifecycle ===== */

static void
gst_wallpaper_module_finalize(GObject *object)
{
	GstWallpaperModule *self;

	self = GST_WALLPAPER_MODULE(object);

	g_free(self->image_path);
	g_free(self->src_pixels);
	g_free(self->scaled_pixels);

	G_OBJECT_CLASS(gst_wallpaper_module_parent_class)->finalize(object);
}

static void
gst_wallpaper_module_class_init(GstWallpaperModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_wallpaper_module_finalize;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_wallpaper_module_get_name;
	module_class->get_description = gst_wallpaper_module_get_description;
	module_class->activate = gst_wallpaper_module_activate;
	module_class->deactivate = gst_wallpaper_module_deactivate;
	module_class->configure = gst_wallpaper_module_configure;
}

static void
gst_wallpaper_module_init(GstWallpaperModule *self)
{
	self->image_path = NULL;
	self->scale_mode = GST_WALLPAPER_FILL;
	self->bg_alpha = 0.3;
	self->src_pixels = NULL;
	self->scaled_pixels = NULL;
	self->src_w = 0;
	self->src_h = 0;
	self->scaled_w = 0;
	self->scaled_h = 0;
	self->scaled_stride = 0;
	self->draw_x = 0;
	self->draw_y = 0;
	self->last_win_w = 0;
	self->last_win_h = 0;
	self->image_loaded = FALSE;
}

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_WALLPAPER_MODULE;
}
