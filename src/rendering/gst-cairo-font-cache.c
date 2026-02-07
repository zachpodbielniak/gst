/*
 * gst-cairo-font-cache.c - Cairo-based font caching for Wayland rendering
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages font loading using Cairo, cairo-ft, and fontconfig.
 * Loads four font variants (regular, bold, italic, bold+italic),
 * measures character cell dimensions, and maintains a dynamic
 * ring cache for fallback fonts discovered at runtime.
 *
 * This is the Wayland/Cairo counterpart of GstFontCache (Xft-based).
 * Unlike GstFontCache, this does not require an X11 Display connection.
 */

#include "gst-cairo-font-cache.h"
#include <math.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

/**
 * SECTION:gst-cairo-font-cache
 * @title: GstCairoFontCache
 * @short_description: Cairo font loading and glyph fallback caching
 *
 * #GstCairoFontCache loads four font variants from a fontconfig
 * specification and maintains a dynamic ring cache of fallback
 * fonts for characters not present in the primary font.
 * Uses Cairo and FreeType for font rendering without X11 dependency.
 */

/* ASCII printable characters for measuring average char width */
static const gchar *ascii_printable =
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~";

/*
 * CairoFontVariant:
 * @font_face: cairo font face created from fontconfig match
 * @scaled_font: cairo scaled font for glyph rendering
 * @pattern: fontconfig pattern used for matching
 * @set: fontconfig font set for fallback searching
 * @height: total font height (ascent + descent)
 * @width: character cell width
 * @ascent: ascent above baseline
 * @descent: descent below baseline
 * @badslant: TRUE if font could not match requested slant
 * @badweight: TRUE if font could not match requested weight
 *
 * Holds a single font variant along with its metrics.
 */
typedef struct {
	cairo_font_face_t   *font_face;
	cairo_scaled_font_t *scaled_font;
	FcPattern           *pattern;
	FcFontSet           *set;
	gint                height;
	gint                width;
	gint                ascent;
	gint                descent;
	gint                badslant;
	gint                badweight;
} CairoFontVariant;

/*
 * CairoFontRingEntry:
 * @font_face: cairo font face for the fallback font
 * @scaled_font: cairo scaled font for the fallback
 * @flags: GstFontStyle value this entry was loaded for
 * @unicodep: codepoint this entry was loaded for
 *
 * Ring cache entry for a fallback font discovered at runtime.
 */
typedef struct {
	cairo_font_face_t   *font_face;
	cairo_scaled_font_t *scaled_font;
	gint                flags;
	GstRune             unicodep;
} CairoFontRingEntry;

struct _GstCairoFontCache
{
	GObject parent_instance;

	/* The four font variants */
	CairoFontVariant font;      /* regular */
	CairoFontVariant bfont;     /* bold */
	CairoFontVariant ifont;     /* italic */
	CairoFontVariant ibfont;    /* bold+italic */

	/* Character cell dimensions */
	gint cw;
	gint ch;

	/* Fallback font ring cache (dynamic array) */
	CairoFontRingEntry *frc;
	gint frc_len;
	gint frc_cap;

	/* Font name and size tracking */
	gchar *used_font;
	gdouble used_fontsize;
	gdouble default_fontsize;

	/* Font matrix and options for creating scaled fonts */
	cairo_matrix_t font_matrix;
	cairo_matrix_t ctm;
	cairo_font_options_t *font_options;

	/* Whether fonts are currently loaded */
	gboolean fonts_loaded;
};

G_DEFINE_TYPE(GstCairoFontCache, gst_cairo_font_cache, G_TYPE_OBJECT)

/*
 * unload_font_variant:
 * @f: font variant to unload
 *
 * Frees resources associated with a single font variant.
 */
static void
unload_font_variant(CairoFontVariant *f)
{
	if (f->scaled_font != NULL) {
		cairo_scaled_font_destroy(f->scaled_font);
		f->scaled_font = NULL;
	}
	if (f->font_face != NULL) {
		cairo_font_face_destroy(f->font_face);
		f->font_face = NULL;
	}
	if (f->pattern != NULL) {
		FcPatternDestroy(f->pattern);
		f->pattern = NULL;
	}
	if (f->set != NULL) {
		FcFontSetDestroy(f->set);
		f->set = NULL;
	}
}

/*
 * create_scaled_font:
 * @font_face: cairo font face
 * @font_matrix: font size matrix
 * @ctm: current transformation matrix
 * @options: font rendering options
 *
 * Creates a cairo_scaled_font_t from a font face and matrices.
 *
 * Returns: (transfer full): new scaled font, or NULL on failure
 */
static cairo_scaled_font_t *
create_scaled_font(
	cairo_font_face_t          *font_face,
	const cairo_matrix_t       *font_matrix,
	const cairo_matrix_t       *ctm,
	cairo_font_options_t       *options
){
	cairo_scaled_font_t *sf;

	sf = cairo_scaled_font_create(font_face, font_matrix, ctm, options);
	if (cairo_scaled_font_status(sf) != CAIRO_STATUS_SUCCESS) {
		cairo_scaled_font_destroy(sf);
		return NULL;
	}

	return sf;
}

/*
 * load_font_variant:
 * @f: (out): font variant to populate
 * @pattern: fontconfig pattern to match
 * @font_matrix: font size matrix
 * @ctm: current transformation matrix
 * @options: font rendering options
 *
 * Loads a single font variant from a fontconfig pattern.
 * Creates a cairo font face and scaled font, then measures
 * the ASCII printable set to determine cell width.
 *
 * Returns: 0 on success, non-zero on failure
 */
static gint
load_font_variant(
	CairoFontVariant           *f,
	FcPattern                  *pattern,
	const cairo_matrix_t       *font_matrix,
	const cairo_matrix_t       *ctm,
	cairo_font_options_t       *options
){
	FcPattern *configured;
	FcPattern *match;
	FcResult result;
	cairo_font_extents_t extents;
	cairo_text_extents_t text_extents;
	gint wantattr;
	gint haveattr;

	/* Duplicate and configure pattern */
	configured = FcPatternDuplicate(pattern);
	if (configured == NULL) {
		return 1;
	}

	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	FcDefaultSubstitute(configured);

	/* Find best matching font */
	match = FcFontMatch(NULL, configured, &result);
	if (match == NULL) {
		FcPatternDestroy(configured);
		return 1;
	}

	/* Create cairo font face from the fontconfig match */
	f->font_face = cairo_ft_font_face_create_for_pattern(match);
	if (f->font_face == NULL ||
	    cairo_font_face_status(f->font_face) != CAIRO_STATUS_SUCCESS) {
		if (f->font_face != NULL) {
			cairo_font_face_destroy(f->font_face);
			f->font_face = NULL;
		}
		FcPatternDestroy(match);
		FcPatternDestroy(configured);
		return 1;
	}

	/* Create scaled font for metrics measurement */
	f->scaled_font = create_scaled_font(f->font_face, font_matrix,
		ctm, options);
	if (f->scaled_font == NULL) {
		cairo_font_face_destroy(f->font_face);
		f->font_face = NULL;
		FcPatternDestroy(match);
		FcPatternDestroy(configured);
		return 1;
	}

	/* Check if slant matches what we requested */
	if (FcPatternGetInteger(pattern, FC_SLANT, 0,
	    &wantattr) == FcResultMatch) {
		if (FcPatternGetInteger(match, FC_SLANT, 0,
		    &haveattr) != FcResultMatch || haveattr < wantattr) {
			f->badslant = 1;
		}
	}

	/* Check if weight matches what we requested */
	if (FcPatternGetInteger(pattern, FC_WEIGHT, 0,
	    &wantattr) == FcResultMatch) {
		if (FcPatternGetInteger(match, FC_WEIGHT, 0,
		    &haveattr) != FcResultMatch || haveattr != wantattr) {
			f->badweight = 1;
		}
	}

	/* Get font metrics from the scaled font */
	cairo_scaled_font_extents(f->scaled_font, &extents);

	f->ascent = (gint)ceil(extents.ascent);
	f->descent = (gint)ceil(extents.descent);
	f->height = f->ascent + f->descent;

	/* Measure average character width from ASCII printable set */
	cairo_scaled_font_text_extents(f->scaled_font,
		ascii_printable, &text_extents);

	/* DIVCEIL equivalent: round up division */
	f->width = (gint)ceil(text_extents.x_advance /
		(gdouble)strlen(ascii_printable));

	f->set = NULL;
	f->pattern = configured;

	/* match is now owned by the font face (cairo-ft takes ownership) */

	return 0;
}

static void
gst_cairo_font_cache_dispose(GObject *object)
{
	GstCairoFontCache *self;

	self = GST_CAIRO_FONT_CACHE(object);

	if (self->fonts_loaded) {
		gst_cairo_font_cache_unload_fonts(self);
	}

	g_clear_pointer(&self->used_font, g_free);
	g_clear_pointer(&self->frc, g_free);

	if (self->font_options != NULL) {
		cairo_font_options_destroy(self->font_options);
		self->font_options = NULL;
	}

	G_OBJECT_CLASS(gst_cairo_font_cache_parent_class)->dispose(object);
}

static void
gst_cairo_font_cache_class_init(GstCairoFontCacheClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_cairo_font_cache_dispose;
}

static void
gst_cairo_font_cache_init(GstCairoFontCache *self)
{
	memset(&self->font, 0, sizeof(CairoFontVariant));
	memset(&self->bfont, 0, sizeof(CairoFontVariant));
	memset(&self->ifont, 0, sizeof(CairoFontVariant));
	memset(&self->ibfont, 0, sizeof(CairoFontVariant));

	self->cw = 0;
	self->ch = 0;
	self->frc = NULL;
	self->frc_len = 0;
	self->frc_cap = 0;
	self->used_font = NULL;
	self->used_fontsize = 0;
	self->default_fontsize = 0;
	self->font_options = NULL;
	self->fonts_loaded = FALSE;

	/* Identity CTM */
	cairo_matrix_init_identity(&self->ctm);

	/* Font matrix will be set during load_fonts */
	cairo_matrix_init_identity(&self->font_matrix);
}

/**
 * gst_cairo_font_cache_new:
 *
 * Creates a new Cairo font cache instance.
 *
 * Returns: (transfer full): A new #GstCairoFontCache
 */
GstCairoFontCache *
gst_cairo_font_cache_new(void)
{
	return (GstCairoFontCache *)g_object_new(
		GST_TYPE_CAIRO_FONT_CACHE, NULL);
}

/**
 * gst_cairo_font_cache_load_fonts:
 * @self: A #GstCairoFontCache
 * @fontstr: fontconfig font specification
 * @fontsize: desired size in pixels (0 for pattern default)
 *
 * Loads all four font variants. The Cairo equivalent of
 * gst_font_cache_load_fonts() but without X11 dependency.
 *
 * Returns: TRUE on success
 */
gboolean
gst_cairo_font_cache_load_fonts(
	GstCairoFontCache   *self,
	const gchar         *fontstr,
	gdouble             fontsize
){
	FcPattern *pattern;
	gdouble fontval;
	gdouble actual_size;

	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), FALSE);
	g_return_val_if_fail(fontstr != NULL, FALSE);

	/* Parse font specification string */
	pattern = FcNameParse((const FcChar8 *)fontstr);
	if (pattern == NULL) {
		g_warning("gst_cairo_font_cache_load_fonts: "
			"can't parse font '%s'", fontstr);
		return FALSE;
	}

	/* Handle font size override */
	if (fontsize > 1) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontsize);
		self->used_fontsize = fontsize;
		actual_size = fontsize;
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0,
		    &fontval) == FcResultMatch) {
			self->used_fontsize = fontval;
			actual_size = fontval;
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0,
		    &fontval) == FcResultMatch) {
			/*
			 * Point size — convert to pixels.
			 * Assume 96 DPI (standard desktop assumption).
			 */
			actual_size = fontval * 96.0 / 72.0;
			self->used_fontsize = actual_size;
		} else {
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			self->used_fontsize = 12;
			actual_size = 12;
		}
		self->default_fontsize = self->used_fontsize;
	}

	/* Set up font matrix from pixel size */
	cairo_matrix_init_scale(&self->font_matrix, actual_size, actual_size);

	/* Create font options */
	if (self->font_options == NULL) {
		self->font_options = cairo_font_options_create();
		cairo_font_options_set_antialias(self->font_options,
			CAIRO_ANTIALIAS_SUBPIXEL);
		cairo_font_options_set_hint_style(self->font_options,
			CAIRO_HINT_STYLE_SLIGHT);
		cairo_font_options_set_hint_metrics(self->font_options,
			CAIRO_HINT_METRICS_ON);
	}

	/* Load regular font */
	if (load_font_variant(&self->font, pattern,
	    &self->font_matrix, &self->ctm,
	    self->font_options) != 0) {
		g_warning("gst_cairo_font_cache_load_fonts: "
			"can't load font '%s'", fontstr);
		FcPatternDestroy(pattern);
		return FALSE;
	}

	/* Set character cell dimensions from regular font */
	self->cw = (gint)ceil((gdouble)self->font.width);
	self->ch = (gint)ceil((gdouble)self->font.height);

	/* Load italic variant */
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (load_font_variant(&self->ifont, pattern,
	    &self->font_matrix, &self->ctm,
	    self->font_options) != 0) {
		g_warning("gst_cairo_font_cache_load_fonts: "
			"can't load italic font");
		/* Non-fatal: copy regular font references */
		self->ifont = self->font;
		if (self->ifont.font_face != NULL) {
			cairo_font_face_reference(self->ifont.font_face);
		}
		if (self->ifont.scaled_font != NULL) {
			cairo_scaled_font_reference(self->ifont.scaled_font);
		}
		if (self->ifont.pattern != NULL) {
			self->ifont.pattern = FcPatternDuplicate(
				self->font.pattern);
		}
		self->ifont.set = NULL;
	}

	/* Load bold+italic variant */
	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (load_font_variant(&self->ibfont, pattern,
	    &self->font_matrix, &self->ctm,
	    self->font_options) != 0) {
		g_warning("gst_cairo_font_cache_load_fonts: "
			"can't load bold+italic font");
		self->ibfont = self->font;
		if (self->ibfont.font_face != NULL) {
			cairo_font_face_reference(self->ibfont.font_face);
		}
		if (self->ibfont.scaled_font != NULL) {
			cairo_scaled_font_reference(self->ibfont.scaled_font);
		}
		if (self->ibfont.pattern != NULL) {
			self->ibfont.pattern = FcPatternDuplicate(
				self->font.pattern);
		}
		self->ibfont.set = NULL;
	}

	/* Load bold variant (roman slant) */
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (load_font_variant(&self->bfont, pattern,
	    &self->font_matrix, &self->ctm,
	    self->font_options) != 0) {
		g_warning("gst_cairo_font_cache_load_fonts: "
			"can't load bold font");
		self->bfont = self->font;
		if (self->bfont.font_face != NULL) {
			cairo_font_face_reference(self->bfont.font_face);
		}
		if (self->bfont.scaled_font != NULL) {
			cairo_scaled_font_reference(self->bfont.scaled_font);
		}
		if (self->bfont.pattern != NULL) {
			self->bfont.pattern = FcPatternDuplicate(
				self->font.pattern);
		}
		self->bfont.set = NULL;
	}

	FcPatternDestroy(pattern);

	/* Store font name */
	g_free(self->used_font);
	self->used_font = g_strdup(fontstr);
	self->fonts_loaded = TRUE;

	return TRUE;
}

/**
 * gst_cairo_font_cache_unload_fonts:
 * @self: A #GstCairoFontCache
 *
 * Frees all loaded fonts and the ring cache.
 */
void
gst_cairo_font_cache_unload_fonts(GstCairoFontCache *self)
{
	g_return_if_fail(GST_IS_CAIRO_FONT_CACHE(self));

	if (!self->fonts_loaded) {
		return;
	}

	/* Free ring cache entries */
	gst_cairo_font_cache_clear(self);

	/* Free the four main font variants */
	unload_font_variant(&self->font);
	unload_font_variant(&self->bfont);
	unload_font_variant(&self->ifont);
	unload_font_variant(&self->ibfont);

	self->fonts_loaded = FALSE;
}

/**
 * gst_cairo_font_cache_clear:
 * @self: A #GstCairoFontCache
 *
 * Clears the fallback font ring cache.
 */
void
gst_cairo_font_cache_clear(GstCairoFontCache *self)
{
	gint i;

	g_return_if_fail(GST_IS_CAIRO_FONT_CACHE(self));

	for (i = 0; i < self->frc_len; i++) {
		if (self->frc[i].scaled_font != NULL) {
			cairo_scaled_font_destroy(self->frc[i].scaled_font);
		}
		if (self->frc[i].font_face != NULL) {
			cairo_font_face_destroy(self->frc[i].font_face);
		}
	}
	self->frc_len = 0;
}

/**
 * gst_cairo_font_cache_get_char_width:
 * @self: A #GstCairoFontCache
 *
 * Returns: character cell width in pixels
 */
gint
gst_cairo_font_cache_get_char_width(GstCairoFontCache *self)
{
	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), 0);

	return self->cw;
}

/**
 * gst_cairo_font_cache_get_char_height:
 * @self: A #GstCairoFontCache
 *
 * Returns: character cell height in pixels
 */
gint
gst_cairo_font_cache_get_char_height(GstCairoFontCache *self)
{
	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), 0);

	return self->ch;
}

/**
 * gst_cairo_font_cache_get_ascent:
 * @self: A #GstCairoFontCache
 *
 * Returns: font ascent in pixels
 */
gint
gst_cairo_font_cache_get_ascent(GstCairoFontCache *self)
{
	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), 0);

	return self->font.ascent;
}

/*
 * get_variant:
 * @self: font cache
 * @style: font style
 *
 * Returns the font variant for the given style.
 */
static CairoFontVariant *
get_variant(
	GstCairoFontCache *self,
	GstFontStyle      style
){
	switch (style) {
	case GST_FONT_STYLE_NORMAL:
		return &self->font;
	case GST_FONT_STYLE_ITALIC:
		return &self->ifont;
	case GST_FONT_STYLE_BOLD:
		return &self->bfont;
	case GST_FONT_STYLE_BOLD_ITALIC:
		return &self->ibfont;
	default:
		return &self->font;
	}
}

/*
 * get_glyph_index:
 * @scaled_font: cairo scaled font
 * @rune: Unicode code point
 *
 * Gets the glyph index for a Unicode code point using
 * the FreeType face underlying the cairo scaled font.
 *
 * Returns: glyph index, or 0 if not found
 */
static gulong
get_glyph_index(
	cairo_scaled_font_t *scaled_font,
	GstRune             rune
){
	FT_Face ft_face;
	cairo_status_t status;
	FT_UInt idx;

	status = cairo_scaled_font_status(scaled_font);
	if (status != CAIRO_STATUS_SUCCESS) {
		return 0;
	}

	ft_face = cairo_ft_scaled_font_lock_face(scaled_font);
	if (ft_face == NULL) {
		return 0;
	}

	idx = FT_Get_Char_Index(ft_face, (FT_ULong)rune);
	cairo_ft_scaled_font_unlock_face(scaled_font);

	return (gulong)idx;
}

/**
 * gst_cairo_font_cache_lookup_glyph:
 * @self: A #GstCairoFontCache
 * @rune: Unicode code point
 * @style: desired font style
 * @font_out: (out): the cairo_scaled_font_t containing the glyph
 * @glyph_out: (out): the glyph index
 *
 * Looks up a glyph, searching main font, ring cache,
 * then fontconfig system fonts. The Cairo equivalent of
 * gst_font_cache_lookup_glyph().
 *
 * Returns: TRUE if glyph was found
 */
gboolean
gst_cairo_font_cache_lookup_glyph(
	GstCairoFontCache       *self,
	GstRune                 rune,
	GstFontStyle            style,
	cairo_scaled_font_t     **font_out,
	gulong                  *glyph_out
){
	CairoFontVariant *fv;
	gulong glyphidx;
	gint f;
	FcResult fcres;
	FcPattern *fcpattern;
	FcPattern *fontpattern;
	FcCharSet *fccharset;
	FcFontSet *fcsets[1];
	CairoFontRingEntry *entry;

	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), FALSE);
	g_return_val_if_fail(font_out != NULL, FALSE);
	g_return_val_if_fail(glyph_out != NULL, FALSE);

	/* Get the appropriate font variant */
	fv = get_variant(self, style);

	/* Try the main font first */
	glyphidx = get_glyph_index(fv->scaled_font, rune);
	if (glyphidx != 0) {
		*font_out = fv->scaled_font;
		*glyph_out = glyphidx;
		return TRUE;
	}

	/* Search the fallback ring cache */
	for (f = 0; f < self->frc_len; f++) {
		glyphidx = get_glyph_index(self->frc[f].scaled_font, rune);
		/* Found in cache with matching style */
		if (glyphidx != 0 && self->frc[f].flags == (gint)style) {
			*font_out = self->frc[f].scaled_font;
			*glyph_out = glyphidx;
			return TRUE;
		}
		/* Previously cached miss for this codepoint+style */
		if (glyphidx == 0 && self->frc[f].flags == (gint)style
		    && self->frc[f].unicodep == rune) {
			*font_out = self->frc[f].scaled_font;
			*glyph_out = 0;
			return FALSE;
		}
	}

	/* Not in cache: search system fonts via fontconfig */
	if (fv->set == NULL) {
		fv->set = FcFontSort(0, fv->pattern, 1, 0, &fcres);
	}
	fcsets[0] = fv->set;

	fcpattern = FcPatternDuplicate(fv->pattern);
	fccharset = FcCharSetCreate();

	FcCharSetAddChar(fccharset, (FcChar32)rune);
	FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
	FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);

	FcConfigSubstitute(0, fcpattern, FcMatchPattern);
	FcDefaultSubstitute(fcpattern);
	fontpattern = FcFontSetMatch(0, fcsets, 1, fcpattern, &fcres);

	/* Grow ring cache if needed */
	if (self->frc_len >= self->frc_cap) {
		self->frc_cap += 16;
		self->frc = g_realloc(self->frc,
			(gsize)self->frc_cap * sizeof(CairoFontRingEntry));
	}

	entry = &self->frc[self->frc_len];
	entry->font_face = NULL;
	entry->scaled_font = NULL;
	entry->flags = (gint)style;
	entry->unicodep = rune;

	/* Create cairo font from the fallback match */
	if (fontpattern != NULL) {
		entry->font_face = cairo_ft_font_face_create_for_pattern(
			fontpattern);
		if (entry->font_face != NULL &&
		    cairo_font_face_status(entry->font_face) ==
		    CAIRO_STATUS_SUCCESS) {
			entry->scaled_font = create_scaled_font(
				entry->font_face,
				&self->font_matrix, &self->ctm,
				self->font_options);
		}
	}

	if (entry->scaled_font == NULL) {
		/* Could not open fallback font */
		if (entry->font_face != NULL) {
			cairo_font_face_destroy(entry->font_face);
			entry->font_face = NULL;
		}
		FcPatternDestroy(fcpattern);
		FcCharSetDestroy(fccharset);
		*font_out = fv->scaled_font;
		*glyph_out = 0;
		return FALSE;
	}

	glyphidx = get_glyph_index(entry->scaled_font, rune);

	*font_out = entry->scaled_font;
	*glyph_out = glyphidx;

	self->frc_len++;

	FcPatternDestroy(fcpattern);
	FcCharSetDestroy(fccharset);

	return (glyphidx != 0);
}

/**
 * gst_cairo_font_cache_get_used_font:
 * @self: A #GstCairoFontCache
 *
 * Returns: (transfer none) (nullable): the font name
 */
const gchar *
gst_cairo_font_cache_get_used_font(GstCairoFontCache *self)
{
	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), NULL);

	return self->used_font;
}

/**
 * gst_cairo_font_cache_get_font_size:
 * @self: A #GstCairoFontCache
 *
 * Returns: current font size in pixels
 */
gdouble
gst_cairo_font_cache_get_font_size(GstCairoFontCache *self)
{
	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), 0);

	return self->used_fontsize;
}

/**
 * gst_cairo_font_cache_get_default_font_size:
 * @self: A #GstCairoFontCache
 *
 * Returns: default font size in pixels
 */
gdouble
gst_cairo_font_cache_get_default_font_size(GstCairoFontCache *self)
{
	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), 0);

	return self->default_fontsize;
}

/**
 * gst_cairo_font_cache_load_spare_fonts:
 * @self: A #GstCairoFontCache
 * @fonts: (array zero-terminated=1): NULL-terminated array of font spec strings
 *
 * Pre-loads fallback fonts into the ring cache so they are searched
 * before fontconfig's slow system-wide lookup. For each font spec,
 * loads 4 style variants (normal, italic, bold, bold+italic) adjusted
 * to the current primary font's pixel size. Cairo counterpart of
 * gst_font_cache_load_spare_fonts().
 *
 * Returns: The number of font specs successfully loaded (up to 4 entries each)
 */
guint
gst_cairo_font_cache_load_spare_fonts(
	GstCairoFontCache   *self,
	const gchar        **fonts
){
	guint loaded;
	guint fi;
	gdouble fontsize;

	g_return_val_if_fail(GST_IS_CAIRO_FONT_CACHE(self), 0);
	g_return_val_if_fail(fonts != NULL, 0);

	if (!self->fonts_loaded)
	{
		return 0;
	}

	loaded = 0;
	fontsize = self->used_fontsize;

	for (fi = 0; fonts[fi] != NULL; fi++)
	{
		FcPattern *pattern;
		gint style;

		/* Parse the spare font specification */
		pattern = FcNameParse((const FcChar8 *)fonts[fi]);
		if (pattern == NULL)
		{
			g_debug("font2: can't parse spare font '%s'", fonts[fi]);
			continue;
		}

		/* Override pixel size to match primary font */
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontsize);

		/*
		 * Load 4 variants: normal(0), italic(1), bold(2), bold+italic(3).
		 * Each variant is added as a ring cache entry so lookup_glyph()
		 * finds it before falling through to system-wide fontconfig search.
		 */
		for (style = 0; style < 4; style++)
		{
			FcPattern *variant_pat;
			FcPattern *configured;
			FcPattern *match;
			FcResult result;
			cairo_font_face_t *face;
			cairo_scaled_font_t *sf;

			variant_pat = FcPatternDuplicate(pattern);
			if (variant_pat == NULL)
			{
				continue;
			}

			/* Set slant */
			FcPatternDel(variant_pat, FC_SLANT);
			if (style == GST_FONT_STYLE_ITALIC ||
			    style == GST_FONT_STYLE_BOLD_ITALIC)
			{
				FcPatternAddInteger(variant_pat, FC_SLANT,
					FC_SLANT_ITALIC);
			}
			else
			{
				FcPatternAddInteger(variant_pat, FC_SLANT,
					FC_SLANT_ROMAN);
			}

			/* Set weight */
			FcPatternDel(variant_pat, FC_WEIGHT);
			if (style == GST_FONT_STYLE_BOLD ||
			    style == GST_FONT_STYLE_BOLD_ITALIC)
			{
				FcPatternAddInteger(variant_pat, FC_WEIGHT,
					FC_WEIGHT_BOLD);
			}

			/* Configure and match */
			configured = FcPatternDuplicate(variant_pat);
			FcConfigSubstitute(NULL, configured, FcMatchPattern);
			FcDefaultSubstitute(configured);

			match = FcFontMatch(NULL, configured, &result);
			FcPatternDestroy(configured);
			FcPatternDestroy(variant_pat);

			if (match == NULL)
			{
				continue;
			}

			/* Create cairo font face from the match */
			face = cairo_ft_font_face_create_for_pattern(match);
			if (face == NULL ||
			    cairo_font_face_status(face) != CAIRO_STATUS_SUCCESS)
			{
				if (face != NULL)
				{
					cairo_font_face_destroy(face);
				}
				FcPatternDestroy(match);
				continue;
			}

			/* Create scaled font for rendering */
			sf = create_scaled_font(face,
				&self->font_matrix, &self->ctm,
				self->font_options);
			if (sf == NULL)
			{
				cairo_font_face_destroy(face);
				FcPatternDestroy(match);
				continue;
			}

			/* Grow ring cache if needed */
			if (self->frc_len >= self->frc_cap)
			{
				self->frc_cap += 16;
				self->frc = g_realloc(self->frc,
					(gsize)self->frc_cap *
					sizeof(CairoFontRingEntry));
			}

			self->frc[self->frc_len].font_face = face;
			self->frc[self->frc_len].scaled_font = sf;
			self->frc[self->frc_len].flags = style;
			self->frc[self->frc_len].unicodep = 0;
			self->frc_len++;
		}

		FcPatternDestroy(pattern);
		loaded++;
	}

	g_debug("font2: loaded %u spare font specs (%d ring cache entries)",
		loaded, self->frc_len);

	return loaded;
}
