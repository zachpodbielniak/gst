/*
 * gst-ligatures-module.c - Font ligature rendering module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements GstGlyphTransformer to shape runs of adjacent glyphs
 * through HarfBuzz, detecting and rendering font ligatures such as
 * "calt" (contextual alternates) and "liga" (standard ligatures).
 *
 * When a ligature is detected, the module renders the shaped glyphs
 * directly via gst_render_context_draw_glyph_id() and returns TRUE
 * so the default renderer skips those columns. A per-row skip bitmap
 * prevents double-rendering of columns already covered by a ligature.
 *
 * An optional GHashTable cache avoids re-shaping identical codepoint
 * runs. The cache is bounded by a configurable maximum size.
 */

#include "gst-ligatures-module.h"
#include "../../src/config/gst-config.h"
#include "../../src/rendering/gst-render-context.h"
#include "../../src/rendering/gst-font-cache.h"
#include "../../src/rendering/gst-cairo-font-cache.h"
#include "../../src/core/gst-line.h"
#include "../../src/boxed/gst-glyph.h"
#include "../../src/module/gst-module-manager.h"

#include <hb.h>
#include <hb-ft.h>
#include <string.h>

/**
 * SECTION:gst-ligatures-module
 * @title: GstLigaturesModule
 * @short_description: HarfBuzz-based font ligature renderer
 *
 * #GstLigaturesModule intercepts glyph rendering via the
 * #GstGlyphTransformer interface. For each glyph, it extracts
 * a run of adjacent same-attribute codepoints from the current
 * line, shapes them through HarfBuzz, and checks whether the
 * shaping produced ligatures (fewer output glyphs than input
 * codepoints, or different glyph IDs). If a ligature is found,
 * the module renders the shaped output and marks subsequent
 * columns as "skip" so they are not rendered again.
 */

/* ===== Constants ===== */

#define GST_LIGATURES_MAX_COLS         (4096)
#define GST_LIGATURES_MAX_RUN_LEN      (64)
#define GST_LIGATURES_DEFAULT_CACHE_SZ (4096)

/* ===== Shaping cache entry ===== */

/*
 * ShapedGlyph:
 * @glyph_id: font-internal glyph index from HarfBuzz
 * @x_offset: horizontal position offset in font units
 * @x_advance: horizontal advance in font units
 *
 * A single glyph in a shaped output sequence.
 */
typedef struct
{
	guint32 glyph_id;
	gint    x_offset;
	gint    x_advance;
} ShapedGlyph;

/*
 * CacheEntry:
 * @glyphs: array of shaped glyph results
 * @num_glyphs: number of entries in glyphs array
 * @is_ligature: TRUE if shaping produced a ligature
 *
 * Cached result of shaping a specific codepoint run.
 */
typedef struct
{
	ShapedGlyph *glyphs;
	guint        num_glyphs;
	gboolean     is_ligature;
} CacheEntry;

/* ===== Module private data ===== */

struct _GstLigaturesModule
{
	GstModule    parent_instance;

	/* HarfBuzz resources */
	hb_font_t   *hb_font;

	/* OpenType features to enable */
	hb_feature_t *features;
	guint         num_features;

	/* Per-row skip bitmap: marks columns already rendered by a ligature */
	gboolean      skip_cols[GST_LIGATURES_MAX_COLS];
	gint          skip_row_y;   /* y position of the current skip bitmap */

	/* Shaping cache: hash of codepoint run -> CacheEntry */
	GHashTable   *cache;
	gsize         cache_size;
	gsize         max_cache_size;
};

/* Forward declarations */
static void
gst_ligatures_module_transformer_init(GstGlyphTransformerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstLigaturesModule, gst_ligatures_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_GLYPH_TRANSFORMER,
		gst_ligatures_module_transformer_init))

/* ===== Cache helpers ===== */

/*
 * cache_entry_free:
 * @data: pointer to a CacheEntry
 *
 * Frees a cache entry and its shaped glyph array.
 */
static void
cache_entry_free(gpointer data)
{
	CacheEntry *entry;

	entry = (CacheEntry *)data;
	if (entry != NULL) {
		g_free(entry->glyphs);
		g_free(entry);
	}
}

/*
 * run_hash:
 * @codepoints: array of Unicode codepoints
 * @len: number of codepoints
 *
 * Computes a hash for a codepoint run using FNV-1a.
 *
 * Returns: 32-bit hash value
 */
static guint
run_hash(const guint32 *codepoints, guint len)
{
	guint hash;
	guint i;

	hash = 2166136261u;
	for (i = 0; i < len; i++) {
		hash ^= codepoints[i];
		hash *= 16777619u;
	}
	return hash;
}

/*
 * RunKey:
 * @codepoints: owned array of codepoints
 * @len: number of codepoints
 *
 * Key type for the shaping cache hash table.
 */
typedef struct
{
	guint32 *codepoints;
	guint    len;
} RunKey;

static guint
run_key_hash(gconstpointer key)
{
	const RunKey *rk;

	rk = (const RunKey *)key;
	return run_hash(rk->codepoints, rk->len);
}

static gboolean
run_key_equal(gconstpointer a, gconstpointer b)
{
	const RunKey *ka;
	const RunKey *kb;

	ka = (const RunKey *)a;
	kb = (const RunKey *)b;

	if (ka->len != kb->len) {
		return FALSE;
	}
	return memcmp(ka->codepoints, kb->codepoints,
		ka->len * sizeof(guint32)) == 0;
}

static void
run_key_free(gpointer data)
{
	RunKey *rk;

	rk = (RunKey *)data;
	if (rk != NULL) {
		g_free(rk->codepoints);
		g_free(rk);
	}
}

/* ===== HarfBuzz font creation ===== */

/*
 * create_hb_font_from_manager:
 * @self: the ligatures module
 *
 * Creates a HarfBuzz font from the font cache stored in the
 * module manager. Handles both X11 (XftFont -> FT_Face) and
 * Wayland (cairo_scaled_font_t -> FT_Face) backends.
 *
 * Returns: a new hb_font_t, or NULL on failure
 */
static hb_font_t *
create_hb_font_from_manager(GstLigaturesModule *self)
{
	GstModuleManager *mgr;
	gpointer font_cache;
	gint backend_type;
	FT_Face ft_face;
	hb_font_t *hb_font;

	mgr = gst_module_manager_get_default();
	if (mgr == NULL) {
		return NULL;
	}

	font_cache = gst_module_manager_get_font_cache(mgr);
	backend_type = gst_module_manager_get_backend_type(mgr);

	if (font_cache == NULL) {
		g_warning("ligatures: no font cache available");
		return NULL;
	}

	ft_face = NULL;

	if (backend_type == GST_BACKEND_X11) {
		/*
		 * X11 path: get the XftFont from the font cache, then
		 * lock the underlying FreeType face with XftLockFace().
		 */
		GstFontCache *x11_cache;
		GstFontVariant *fv;

		x11_cache = (GstFontCache *)font_cache;
		fv = gst_font_cache_get_font(x11_cache, GST_FONT_STYLE_NORMAL);

		if (fv == NULL || fv->match == NULL) {
			g_warning("ligatures: no regular font loaded");
			return NULL;
		}

		ft_face = XftLockFace(fv->match);
		if (ft_face == NULL) {
			g_warning("ligatures: XftLockFace failed");
			return NULL;
		}

		/*
		 * Create HarfBuzz font from the FreeType face.
		 * hb_ft_font_create_referenced() adds a reference to ft_face,
		 * so we can unlock it after creation.
		 */
		hb_font = hb_ft_font_create_referenced(ft_face);
		XftUnlockFace(fv->match);

	} else if (backend_type == GST_BACKEND_WAYLAND) {
		/*
		 * Wayland path: get the cairo_scaled_font_t from the cairo
		 * font cache, then lock its FreeType face.
		 */
		GstCairoFontCache *cairo_cache;
		cairo_scaled_font_t *scaled_font;
		gulong dummy_glyph;

		cairo_cache = (GstCairoFontCache *)font_cache;

		/* Look up the regular font via a dummy glyph query */
		if (!gst_cairo_font_cache_lookup_glyph(cairo_cache,
		    (GstRune)' ', GST_FONT_STYLE_NORMAL,
		    &scaled_font, &dummy_glyph))
		{
			g_warning("ligatures: no regular Cairo font loaded");
			return NULL;
		}

		ft_face = (FT_Face)cairo_ft_scaled_font_lock_face(scaled_font);
		if (ft_face == NULL) {
			g_warning("ligatures: cairo_ft_scaled_font_lock_face failed");
			return NULL;
		}

		hb_font = hb_ft_font_create_referenced(ft_face);
		cairo_ft_scaled_font_unlock_face(scaled_font);

	} else {
		g_warning("ligatures: unknown backend type %d", backend_type);
		return NULL;
	}

	return hb_font;
}

/* ===== Internal shaping logic ===== */

/*
 * extract_run:
 * @line: the current terminal line
 * @start_col: column to start extracting from
 * @max_cols: total columns in the line
 * @codepoints: (out): buffer to fill with codepoints
 * @max_run: maximum run length
 *
 * Extracts a run of codepoints starting at start_col that share
 * the same foreground color, background color, and attribute flags.
 * Stops at the first attribute change, empty glyph, wide-dummy,
 * or end of line.
 *
 * Returns: number of codepoints extracted
 */
static guint
extract_run(
	GstLine        *line,
	gint            start_col,
	gint            max_cols,
	guint32        *codepoints,
	guint           max_run
){
	const GstGlyph *first;
	const GstGlyph *g;
	guint len;
	gint col;

	if (line == NULL || start_col >= line->len) {
		return 0;
	}

	first = gst_line_get_glyph_const(line, start_col);
	if (first == NULL || first->rune == 0 || first->rune == ' ') {
		/* Single space/empty, no ligature possible */
		codepoints[0] = first ? first->rune : (guint32)' ';
		return 1;
	}

	len = 0;
	for (col = start_col; col < max_cols && col < line->len && len < max_run; col++) {
		g = gst_line_get_glyph_const(line, col);
		if (g == NULL) {
			break;
		}

		/* Stop at wide dummy cells (second half of wide char) */
		if (gst_glyph_has_attr(g, GST_GLYPH_ATTR_WDUMMY)) {
			break;
		}

		/* Stop if attributes change from the first glyph */
		if (col > start_col) {
			if (g->fg != first->fg ||
			    g->bg != first->bg ||
			    g->attr != first->attr)
			{
				break;
			}
		}

		/* Skip empty/space codepoints - they break runs */
		if (g->rune == 0 || g->rune == ' ') {
			break;
		}

		codepoints[len] = g->rune;
		len++;
	}

	return len;
}

/*
 * shape_run:
 * @self: the ligatures module
 * @codepoints: array of Unicode codepoints
 * @len: number of codepoints
 * @entry_out: (out): receives the shaping result
 *
 * Shapes a codepoint run through HarfBuzz. Checks the cache first;
 * if not cached, performs the shaping and stores the result.
 *
 * Returns: TRUE if a cached or newly shaped result was produced
 */
static gboolean
shape_run(
	GstLigaturesModule *self,
	const guint32      *codepoints,
	guint               len,
	CacheEntry        **entry_out
){
	RunKey lookup_key;
	CacheEntry *cached;
	hb_buffer_t *buf;
	hb_glyph_info_t *info;
	hb_glyph_position_t *pos;
	guint glyph_count;
	CacheEntry *entry;
	RunKey *store_key;
	guint i;

	/* Check cache first */
	lookup_key.codepoints = (guint32 *)codepoints;
	lookup_key.len = len;

	cached = (CacheEntry *)g_hash_table_lookup(self->cache, &lookup_key);
	if (cached != NULL) {
		*entry_out = cached;
		return TRUE;
	}

	if (self->hb_font == NULL) {
		return FALSE;
	}

	/* Shape through HarfBuzz */
	buf = hb_buffer_create();
	hb_buffer_add_codepoints(buf, codepoints, (int)len, 0, (int)len);
	hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
	hb_buffer_set_script(buf, HB_SCRIPT_COMMON);
	hb_buffer_guess_segment_properties(buf);

	hb_shape(self->hb_font, buf, self->features, self->num_features);

	info = hb_buffer_get_glyph_infos(buf, &glyph_count);
	pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

	/* Build cache entry */
	entry = g_new0(CacheEntry, 1);
	entry->num_glyphs = glyph_count;
	entry->glyphs = g_new0(ShapedGlyph, glyph_count);

	for (i = 0; i < glyph_count; i++) {
		entry->glyphs[i].glyph_id = info[i].codepoint;
		entry->glyphs[i].x_offset = pos[i].x_offset;
		entry->glyphs[i].x_advance = pos[i].x_advance;
	}

	/*
	 * Detect ligature: if the number of output glyphs is fewer than
	 * the number of input codepoints, a ligature was applied.
	 * Also detect if any glyph ID differs from a simple 1:1 mapping
	 * (though the primary indicator is glyph count reduction).
	 */
	entry->is_ligature = (glyph_count < len);

	hb_buffer_destroy(buf);

	/* Store in cache if under limit */
	if (self->cache_size < self->max_cache_size) {
		store_key = g_new0(RunKey, 1);
		store_key->codepoints = g_memdup2(codepoints, len * sizeof(guint32));
		store_key->len = len;

		g_hash_table_insert(self->cache, store_key, entry);
		self->cache_size++;
	}

	*entry_out = entry;
	return TRUE;
}

/* ===== GstGlyphTransformer interface ===== */

/*
 * transform_glyph:
 *
 * Called for each glyph during rendering. Extracts a run of
 * same-attribute codepoints from the current line, shapes them
 * through HarfBuzz, and if a ligature is detected, renders the
 * shaped glyphs directly and marks subsequent columns for skipping.
 */
static gboolean
gst_ligatures_module_transform_glyph(
	GstGlyphTransformer *transformer,
	gunichar             codepoint,
	gpointer             render_context,
	gint                 x,
	gint                 y,
	gint                 width,
	gint                 height
){
	GstLigaturesModule *self;
	GstRenderContext *ctx;
	GstLine *line;
	gint col;
	gint cols;
	guint32 run_buf[GST_LIGATURES_MAX_RUN_LEN];
	guint run_len;
	CacheEntry *entry;
	GstFontStyle style;
	const GstGlyph *first_glyph;
	gint px;
	guint i;

	self = GST_LIGATURES_MODULE(transformer);
	ctx = (GstRenderContext *)render_context;

	/* Read current line and position from the render context */
	line = (GstLine *)ctx->current_line;
	col = ctx->current_col;
	cols = ctx->current_cols;

	if (line == NULL || col < 0 || cols <= 0) {
		return FALSE;
	}

	/* Reset skip bitmap when we move to a new row (y changes) */
	if (y != self->skip_row_y) {
		memset(self->skip_cols, 0, (gsize)cols * sizeof(gboolean));
		self->skip_row_y = y;
	}

	/* If this column was already rendered as part of a ligature, skip it */
	if (col < GST_LIGATURES_MAX_COLS && self->skip_cols[col]) {
		return TRUE;
	}

	/* Only process printable non-space characters */
	if (codepoint == 0 || codepoint == ' ' || codepoint == '\t') {
		return FALSE;
	}

	/* Extract the codepoint run starting at this column */
	run_len = extract_run(line, col, cols, run_buf, GST_LIGATURES_MAX_RUN_LEN);

	/* Single-character runs cannot form ligatures */
	if (run_len <= 1) {
		return FALSE;
	}

	/* Shape the run */
	entry = NULL;
	if (!shape_run(self, run_buf, run_len, &entry) || entry == NULL) {
		return FALSE;
	}

	/* If no ligature was detected, let the default renderer handle it */
	if (!entry->is_ligature) {
		return FALSE;
	}

	/*
	 * Ligature detected: render the shaped glyphs ourselves.
	 * Determine the font style from the first glyph's attributes.
	 */
	first_glyph = gst_line_get_glyph_const(line, col);
	style = GST_FONT_STYLE_NORMAL;
	if (first_glyph != NULL) {
		gboolean is_bold;
		gboolean is_italic;

		is_bold = gst_glyph_has_attr(first_glyph, GST_GLYPH_ATTR_BOLD);
		is_italic = gst_glyph_has_attr(first_glyph, GST_GLYPH_ATTR_ITALIC);

		if (is_bold && is_italic) {
			style = GST_FONT_STYLE_BOLD_ITALIC;
		} else if (is_bold) {
			style = GST_FONT_STYLE_BOLD;
		} else if (is_italic) {
			style = GST_FONT_STYLE_ITALIC;
		}
	}

	/* Clear the background for the entire run */
	gst_render_context_fill_rect_bg(ctx, x, y,
		width * (gint)run_len, height);

	/*
	 * Render each shaped glyph. HarfBuzz positions are in font units;
	 * for hb_ft_font the units are 26.6 fixed point (1/64th pixel).
	 * We use the advance values to position glyphs within the run.
	 */
	px = x;
	for (i = 0; i < entry->num_glyphs; i++) {
		gint gx;
		gint gy;

		gx = px + (entry->glyphs[i].x_offset / 64);
		gy = y;

		gst_render_context_draw_glyph_id(ctx,
			entry->glyphs[i].glyph_id, style, gx, gy);

		px += entry->glyphs[i].x_advance / 64;
	}

	/* Mark columns 1..N-1 of the run as "skip" */
	for (i = 1; i < run_len; i++) {
		gint skip_col;

		skip_col = col + (gint)i;
		if (skip_col < GST_LIGATURES_MAX_COLS) {
			self->skip_cols[skip_col] = TRUE;
		}
	}

	return TRUE;
}

static void
gst_ligatures_module_transformer_init(GstGlyphTransformerInterface *iface)
{
	iface->transform_glyph = gst_ligatures_module_transform_glyph;
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_ligatures_module_get_name(GstModule *module)
{
	(void)module;
	return "ligatures";
}

static const gchar *
gst_ligatures_module_get_description(GstModule *module)
{
	(void)module;
	return "HarfBuzz-based font ligature rendering";
}

/*
 * activate:
 *
 * Creates the HarfBuzz font from the font cache. The font cache
 * must already be loaded before this module is activated.
 */
static gboolean
gst_ligatures_module_activate(GstModule *module)
{
	GstLigaturesModule *self;

	self = GST_LIGATURES_MODULE(module);

	/* Create HarfBuzz font from the active backend's font cache */
	self->hb_font = create_hb_font_from_manager(self);
	if (self->hb_font == NULL) {
		g_warning("ligatures: failed to create HarfBuzz font");
		return FALSE;
	}

	g_debug("ligatures: activated with %u features", self->num_features);
	return TRUE;
}

/*
 * deactivate:
 *
 * Cleans up HarfBuzz resources and the shaping cache.
 */
static void
gst_ligatures_module_deactivate(GstModule *module)
{
	GstLigaturesModule *self;

	self = GST_LIGATURES_MODULE(module);

	if (self->hb_font != NULL) {
		hb_font_destroy(self->hb_font);
		self->hb_font = NULL;
	}

	/* Clear the cache */
	if (self->cache != NULL) {
		g_hash_table_remove_all(self->cache);
		self->cache_size = 0;
	}

	g_debug("ligatures: deactivated");
}

/*
 * configure:
 *
 * Reads ligatures configuration from the YAML config:
 *  - features: list of OpenType feature tags (default: ["calt", "liga"])
 *  - cache_size: maximum shaping cache entries (default: 4096)
 */
static void
gst_ligatures_module_configure(GstModule *module, gpointer config)
{
	GstLigaturesModule *self;
	YamlMapping *mod_cfg;

	self = GST_LIGATURES_MODULE(module);

	mod_cfg = gst_config_get_module_config(
		(GstConfig *)config, "ligatures");
	if (mod_cfg == NULL) {
		g_debug("ligatures: no config section, using defaults");
		return;
	}

	/* Parse feature list */
	if (yaml_mapping_has_member(mod_cfg, "features")) {
		YamlSequence *features_seq;

		features_seq = yaml_mapping_get_sequence_member(mod_cfg, "features");
		if (features_seq != NULL) {
			guint n;
			guint i;

			n = yaml_sequence_get_length(features_seq);

			/* Free previous features if any */
			g_free(self->features);
			self->features = g_new0(hb_feature_t, n);
			self->num_features = 0;

			for (i = 0; i < n; i++) {
				const gchar *tag_str;

				tag_str = yaml_sequence_get_string_element(features_seq, i);
				if (tag_str != NULL) {
					if (hb_feature_from_string(tag_str, -1,
					    &self->features[self->num_features]))
					{
						self->num_features++;
					} else {
						g_warning("ligatures: invalid feature tag '%s'",
							tag_str);
					}
				}
			}
		}
	}

	/* Parse cache size */
	if (yaml_mapping_has_member(mod_cfg, "cache_size")) {
		gint64 val;

		val = yaml_mapping_get_int_member(mod_cfg, "cache_size");
		if (val > 0 && val <= 65536) {
			self->max_cache_size = (gsize)val;
		}
	}

	g_debug("ligatures: configured (%u features, cache_size=%zu)",
		self->num_features, self->max_cache_size);
}

/* ===== GObject lifecycle ===== */

static void
gst_ligatures_module_finalize(GObject *object)
{
	GstLigaturesModule *self;

	self = GST_LIGATURES_MODULE(object);

	if (self->hb_font != NULL) {
		hb_font_destroy(self->hb_font);
		self->hb_font = NULL;
	}

	g_free(self->features);
	self->features = NULL;

	if (self->cache != NULL) {
		g_hash_table_destroy(self->cache);
		self->cache = NULL;
	}

	G_OBJECT_CLASS(gst_ligatures_module_parent_class)->finalize(object);
}

static void
gst_ligatures_module_class_init(GstLigaturesModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_ligatures_module_finalize;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_ligatures_module_get_name;
	module_class->get_description = gst_ligatures_module_get_description;
	module_class->activate = gst_ligatures_module_activate;
	module_class->deactivate = gst_ligatures_module_deactivate;
	module_class->configure = gst_ligatures_module_configure;
}

static void
gst_ligatures_module_init(GstLigaturesModule *self)
{
	/*
	 * Default features: "calt" (contextual alternates) and "liga"
	 * (standard ligatures). These are the most common features
	 * used by programming fonts like Fira Code and JetBrains Mono.
	 */
	self->num_features = 2;
	self->features = g_new0(hb_feature_t, 2);
	hb_feature_from_string("calt", -1, &self->features[0]);
	hb_feature_from_string("liga", -1, &self->features[1]);

	self->hb_font = NULL;
	self->skip_row_y = -1;
	self->max_cache_size = GST_LIGATURES_DEFAULT_CACHE_SZ;
	self->cache_size = 0;

	/* Create the shaping cache */
	self->cache = g_hash_table_new_full(
		run_key_hash, run_key_equal,
		run_key_free, cache_entry_free);

	memset(self->skip_cols, 0, sizeof(self->skip_cols));
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 * Returns the GType so the manager can instantiate the module.
 *
 * Returns: The #GType for #GstLigaturesModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_LIGATURES_MODULE;
}
