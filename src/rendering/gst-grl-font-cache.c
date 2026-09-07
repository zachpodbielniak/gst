/*
 * gst-grl-font-cache.c - graylib glyph atlas for the LRG backend
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Wraps GstCairoFontCache (the cairo-ft path also used by the Wayland
 * renderer) and caches each glyph as a graylib texture: cairo rasterizes the
 * glyph once as white + alpha coverage, which is then blitted tinted with the
 * foreground color. This makes on-screen text identical to the non-LRG
 * backends. Mirrors cmacs lrgterm (cmacs-lrgfont.c).
 */

#include "gst-grl-font-cache.h"
#include <math.h>
#include <string.h>

/* One cached glyph: a graylib texture plus its cairo bearings. */
typedef struct
{
	GrlTexture *tex;   /* NULL for zero-ink glyphs (space etc.) */
	cairo_scaled_font_t *font; /* Retain atlas identity after fallback eviction. */
	gint        bx;    /* x bearing (pen -> left edge of ink) */
	gint        by;    /* baseline -> top edge of ink (= -y_bearing) */
	gint        w;
	gint        h;
} GlyphEntry;

/* Atlas key: cairo scaled-font identity + glyph index (handles fallback
 * fonts that share glyph-index numbering). */
typedef struct
{
	gpointer font;
	gulong   index;
} GlyphKey;

struct _GstGrlFontCache
{
	GObject parent_instance;

	GstCairoFontCache *cairo_cache;   /* owned: metrics + glyph lookup */
	GHashTable        *atlas;         /* GlyphKey* -> GlyphEntry* */
};

G_DEFINE_FINAL_TYPE(GstGrlFontCache, gst_grl_font_cache, G_TYPE_OBJECT)

/* ===== Atlas key/value helpers ===== */

static guint
glyph_key_hash(gconstpointer k)
{
	const GlyphKey *gk = k;

	return g_direct_hash(gk->font) ^ (guint)gk->index;
}

static gboolean
glyph_key_equal(gconstpointer a, gconstpointer b)
{
	const GlyphKey *x = a;
	const GlyphKey *y = b;

	return x->font == y->font && x->index == y->index;
}

static void
glyph_entry_free(gpointer data)
{
	GlyphEntry *e = data;

	if (e->tex != NULL) {
		g_object_unref(e->tex);
	}
	cairo_scaled_font_destroy(e->font);
	g_free(e);
}

/* ===== Glyph rasterization ===== */

/*
 * make_texture:
 * @rgba: w*h*4 RGBA pixels
 *
 * Uploads an RGBA buffer to a new graylib texture (crisp point filter).
 *
 * Returns: (transfer full) (nullable): a new #GrlTexture, or %NULL
 */
static GrlTexture *
make_texture(const guint8 *rgba, gint w, gint h)
{
	g_autoptr(GrlColor) blank = NULL;
	g_autoptr(GrlImage) img = NULL;
	g_autoptr(GrlRectangle) rect = NULL;
	GrlTexture *tex;

	blank = grl_color_new(0, 0, 0, 0);
	img = grl_image_new_color(w, h, blank);
	if (img == NULL) {
		return NULL;
	}
	tex = grl_texture_new_from_image(img);
	if (tex == NULL) {
		return NULL;
	}

	rect = grl_rectangle_new(0.0f, 0.0f, (gfloat)w, (gfloat)h);
	grl_texture_update_rec(tex, rect, rgba);
	grl_texture_set_filter(tex, GRL_TEXTURE_FILTER_POINT);
	return tex;
}

/*
 * bake_glyph:
 *
 * Rasterizes glyph @glyph_index of @scaled with cairo (white + alpha
 * coverage), uploads it as a texture, and inserts a #GlyphEntry into the
 * atlas under @key (a copy of which is taken). Returns the inserted entry.
 */
static GlyphEntry *
bake_glyph(
	GstGrlFontCache     *self,
	cairo_scaled_font_t *scaled,
	gulong               glyph_index,
	const GlyphKey      *key
){
	cairo_glyph_t cg;
	cairo_text_extents_t ext;
	GlyphEntry *entry;
	GlyphKey *key_copy;
	cairo_surface_t *surf;
	cairo_t *cr;
	const guint8 *data;
	gint stride;
	gint w;
	gint h;
	gint row;
	gint col;
	guint8 *rgba;

	cg.index = glyph_index;
	cg.x = 0.0;
	cg.y = 0.0;
	cairo_scaled_font_glyph_extents(scaled, &cg, 1, &ext);

	entry = g_new0(GlyphEntry, 1);
	entry->font = cairo_scaled_font_reference(scaled);
	entry->bx = (gint)floor(ext.x_bearing);
	entry->by = (gint)ceil(-ext.y_bearing);
	w = (gint)ceil(ext.x_bearing + ext.width) - entry->bx;
	h = (gint)ceil(ext.y_bearing + ext.height) + entry->by;
	entry->w = w;
	entry->h = h;
	entry->tex = NULL;

	/* Non-zero-ink glyphs get a rasterized texture. */
	if (w > 0 && h > 0) {
		surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		cr = cairo_create(surf);

		cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

		cairo_set_scaled_font(cr, scaled);
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cg.x = -entry->bx;
		cg.y = entry->by;
		cairo_show_glyphs(cr, &cg, 1);
		cairo_surface_flush(surf);

		data = cairo_image_surface_get_data(surf);
		stride = cairo_image_surface_get_stride(surf);

		/* cairo premultiplied BGRA -> white + alpha-coverage RGBA, so the
		 * draw-time tint becomes the glyph color. */
		rgba = g_malloc0((gsize)w * (gsize)h * 4);
		for (row = 0; row < h; row++) {
			for (col = 0; col < w; col++) {
				guint32 pixel;
				guint8 a;
				guint8 *o = &rgba[(row * w + col) * 4];

				memcpy(&pixel, data + row * stride + col * 4, 4);
				a = (guint8)(pixel >> 24);

				o[0] = 255;
				o[1] = 255;
				o[2] = 255;
				o[3] = a;
			}
		}

		entry->tex = make_texture(rgba, w, h);

		g_free(rgba);
		cairo_destroy(cr);
		cairo_surface_destroy(surf);
	}

	key_copy = g_new(GlyphKey, 1);
	*key_copy = *key;
	g_hash_table_insert(self->atlas, key_copy, entry);

	return entry;
}

/* ===== GObject lifecycle ===== */

static void
gst_grl_font_cache_dispose(GObject *object)
{
	GstGrlFontCache *self = GST_GRL_FONT_CACHE(object);

	if (self->atlas != NULL) {
		g_hash_table_remove_all(self->atlas);
	}
	g_clear_object(&self->cairo_cache);

	G_OBJECT_CLASS(gst_grl_font_cache_parent_class)->dispose(object);
}

static void
gst_grl_font_cache_finalize(GObject *object)
{
	GstGrlFontCache *self = GST_GRL_FONT_CACHE(object);

	g_clear_pointer(&self->atlas, g_hash_table_unref);

	G_OBJECT_CLASS(gst_grl_font_cache_parent_class)->finalize(object);
}

static void
gst_grl_font_cache_class_init(GstGrlFontCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = gst_grl_font_cache_dispose;
	object_class->finalize = gst_grl_font_cache_finalize;
}

static void
gst_grl_font_cache_init(GstGrlFontCache *self)
{
	self->cairo_cache = gst_cairo_font_cache_new();
	self->atlas = g_hash_table_new_full(glyph_key_hash, glyph_key_equal,
		g_free, glyph_entry_free);
}

/* ===== Public API ===== */

GstGrlFontCache *
gst_grl_font_cache_new(void)
{
	return (GstGrlFontCache *)g_object_new(GST_TYPE_GRL_FONT_CACHE, NULL);
}

gboolean
gst_grl_font_cache_load_fonts(
	GstGrlFontCache     *self,
	const gchar         *fontstr,
	gdouble             fontsize
){
	g_return_val_if_fail(GST_IS_GRL_FONT_CACHE(self), FALSE);

	/* The glyph indices change with the font, so drop the atlas. */
	g_hash_table_remove_all(self->atlas);

	return gst_cairo_font_cache_load_fonts(self->cairo_cache, fontstr, fontsize);
}

void
gst_grl_font_cache_unload_fonts(GstGrlFontCache *self)
{
	g_return_if_fail(GST_IS_GRL_FONT_CACHE(self));

	g_hash_table_remove_all(self->atlas);
	gst_cairo_font_cache_unload_fonts(self->cairo_cache);
}

void
gst_grl_font_cache_draw_glyph(
	GstGrlFontCache     *self,
	GstRune             rune,
	GstFontStyle        style,
	gint                cell_x,
	gint                cell_y,
	GstColor            fg
){
	cairo_scaled_font_t *scaled = NULL;
	gulong glyph_index = 0;

	g_return_if_fail(GST_IS_GRL_FONT_CACHE(self));
	if (gst_cairo_font_cache_lookup_glyph(self->cairo_cache, rune, style,
	    &scaled, &glyph_index)) {
		gst_grl_font_cache_draw_glyph_index(self, scaled, glyph_index,
			cell_x, cell_y, fg);
	}
}

void
gst_grl_font_cache_draw_glyph_index(
	GstGrlFontCache     *self,
	cairo_scaled_font_t *scaled,
	gulong              glyph_index,
	gint                cell_x,
	gint                cell_y,
	GstColor            fg
){
	GlyphKey key;
	GlyphEntry *entry;
	gint ascent;
	g_autoptr(GrlColor) tint = NULL;
	g_autoptr(GrlVector2) pos = NULL;

	g_return_if_fail(GST_IS_GRL_FONT_CACHE(self));

	if (scaled == NULL || cairo_scaled_font_status(scaled) != CAIRO_STATUS_SUCCESS) {
		return;
	}

	key.font = scaled;
	key.index = glyph_index;
	entry = g_hash_table_lookup(self->atlas, &key);
	if (entry == NULL) {
		entry = bake_glyph(self, scaled, glyph_index, &key);
	}
	if (entry == NULL || entry->tex == NULL) {
		return;   /* zero-ink (space) or rasterization failed */
	}

	ascent = gst_cairo_font_cache_get_ascent(self->cairo_cache);
	tint = grl_color_new((guint8)GST_COLOR_R(fg), (guint8)GST_COLOR_G(fg),
		(guint8)GST_COLOR_B(fg), 255);
	pos = grl_vector2_new((gfloat)(cell_x + entry->bx),
		(gfloat)(cell_y + ascent - entry->by));
	grl_draw_texture_v(entry->tex, pos, tint);
}

gint
gst_grl_font_cache_get_char_width(GstGrlFontCache *self)
{
	g_return_val_if_fail(GST_IS_GRL_FONT_CACHE(self), 0);
	return gst_cairo_font_cache_get_char_width(self->cairo_cache);
}

gint
gst_grl_font_cache_get_char_height(GstGrlFontCache *self)
{
	g_return_val_if_fail(GST_IS_GRL_FONT_CACHE(self), 0);
	return gst_cairo_font_cache_get_char_height(self->cairo_cache);
}

gint
gst_grl_font_cache_get_ascent(GstGrlFontCache *self)
{
	g_return_val_if_fail(GST_IS_GRL_FONT_CACHE(self), 0);
	return gst_cairo_font_cache_get_ascent(self->cairo_cache);
}

gdouble
gst_grl_font_cache_get_font_size(GstGrlFontCache *self)
{
	g_return_val_if_fail(GST_IS_GRL_FONT_CACHE(self), 0.0);
	return gst_cairo_font_cache_get_font_size(self->cairo_cache);
}

gdouble
gst_grl_font_cache_get_default_font_size(GstGrlFontCache *self)
{
	g_return_val_if_fail(GST_IS_GRL_FONT_CACHE(self), 0.0);
	return gst_cairo_font_cache_get_default_font_size(self->cairo_cache);
}

const gchar *
gst_grl_font_cache_get_used_font(GstGrlFontCache *self)
{
	g_return_val_if_fail(GST_IS_GRL_FONT_CACHE(self), NULL);
	return gst_cairo_font_cache_get_used_font(self->cairo_cache);
}

GstCairoFontCache *
gst_grl_font_cache_get_cairo_cache(GstGrlFontCache *self)
{
	g_return_val_if_fail(GST_IS_GRL_FONT_CACHE(self), NULL);
	return self->cairo_cache;
}
