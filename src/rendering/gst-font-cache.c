/*
 * gst-font-cache.c - Font caching for terminal rendering
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages Xft font loading, four font variants (regular, bold,
 * italic, bold+italic), and a dynamic ring cache for fallback
 * fonts discovered via fontconfig at runtime.
 *
 * Ported from st's xloadfont(), xloadfonts(), xunloadfont(),
 * xunloadfonts(), and the frc[] (font ring cache) array.
 */

#include "gst-font-cache.h"
#include <math.h>
#include <string.h>

/**
 * SECTION:gst-font-cache
 * @title: GstFontCache
 * @short_description: Manages font loading and glyph fallback caching
 *
 * #GstFontCache loads four font variants from a fontconfig specification
 * and maintains a dynamic ring cache of fallback fonts for characters
 * not present in the primary font.
 */

/* ASCII printable characters for measuring average char width */
static const gchar *ascii_printable =
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~";

/* Fallback ring cache entry */
typedef struct {
	XftFont *font;
	gint flags;        /* GstFontStyle value */
	GstRune unicodep;  /* codepoint this entry was loaded for */
} GstFontRingEntry;

struct _GstFontCache
{
	GObject parent_instance;

	/* The four font variants */
	GstFontVariant font;     /* regular */
	GstFontVariant bfont;    /* bold */
	GstFontVariant ifont;    /* italic */
	GstFontVariant ibfont;   /* bold+italic */

	/* Character cell dimensions */
	gint cw;
	gint ch;

	/* Fallback font ring cache (dynamic array) */
	GstFontRingEntry *frc;
	gint frc_len;
	gint frc_cap;

	/* Font name and size tracking */
	gchar *used_font;
	gdouble used_fontsize;
	gdouble default_fontsize;

	/* X11 display connection (not owned) */
	Display *display;
	gint screen;

	/* Whether fonts are currently loaded */
	gboolean fonts_loaded;
};

G_DEFINE_TYPE(GstFontCache, gst_font_cache, G_TYPE_OBJECT)

/*
 * unload_font_variant:
 * @display: X11 display
 * @f: font variant to unload
 *
 * Frees resources associated with a single font variant.
 */
static void
unload_font_variant(
	Display         *display,
	GstFontVariant  *f
){
	if (f->match != NULL) {
		XftFontClose(display, f->match);
		f->match = NULL;
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
 * load_font_variant:
 * @display: X11 display
 * @screen: X11 screen number
 * @f: (out): font variant to populate
 * @pattern: fontconfig pattern to match
 *
 * Loads a single font variant from a fontconfig pattern.
 * Measures ASCII printable characters to determine cell width.
 * Ports st's xloadfont().
 *
 * Returns: 0 on success, non-zero on failure
 */
static gint
load_font_variant(
	Display         *display,
	gint            screen,
	GstFontVariant  *f,
	FcPattern       *pattern
){
	FcPattern *configured;
	FcPattern *match;
	FcResult result;
	XGlyphInfo extents;
	gint wantattr;
	gint haveattr;

	/* Duplicate and configure pattern */
	configured = FcPatternDuplicate(pattern);
	if (configured == NULL) {
		return 1;
	}

	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	XftDefaultSubstitute(display, screen, configured);

	/* Find best matching font */
	match = FcFontMatch(NULL, configured, &result);
	if (match == NULL) {
		FcPatternDestroy(configured);
		return 1;
	}

	/* Open the matched font */
	f->match = XftFontOpenPattern(display, match);
	if (f->match == NULL) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return 1;
	}

	/* Check if slant matches what we requested */
	if (XftPatternGetInteger(pattern, "slant", 0, &wantattr) == XftResultMatch) {
		if (XftPatternGetInteger(f->match->pattern, "slant", 0, &haveattr) != XftResultMatch
		    || haveattr < wantattr) {
			f->badslant = 1;
		}
	}

	/* Check if weight matches what we requested */
	if (XftPatternGetInteger(pattern, "weight", 0, &wantattr) == XftResultMatch) {
		if (XftPatternGetInteger(f->match->pattern, "weight", 0, &haveattr) != XftResultMatch
		    || haveattr != wantattr) {
			f->badweight = 1;
		}
	}

	/* Measure average character width from ASCII printable set */
	XftTextExtentsUtf8(display, f->match,
		(const FcChar8 *)ascii_printable,
		(gint)strlen(ascii_printable), &extents);

	f->set = NULL;
	f->pattern = configured;

	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = (gshort)f->match->max_advance_width;

	f->height = f->ascent + f->descent;
	/* DIVCEIL equivalent: round up division */
	f->width = (extents.xOff + (gint)strlen(ascii_printable) - 1)
	           / (gint)strlen(ascii_printable);

	return 0;
}

static void
gst_font_cache_dispose(GObject *object)
{
	GstFontCache *self;

	self = GST_FONT_CACHE(object);

	if (self->fonts_loaded && self->display != NULL) {
		gst_font_cache_unload_fonts(self);
	}

	g_clear_pointer(&self->used_font, g_free);
	g_clear_pointer(&self->frc, g_free);

	G_OBJECT_CLASS(gst_font_cache_parent_class)->dispose(object);
}

static void
gst_font_cache_class_init(GstFontCacheClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_font_cache_dispose;
}

static void
gst_font_cache_init(GstFontCache *self)
{
	memset(&self->font, 0, sizeof(GstFontVariant));
	memset(&self->bfont, 0, sizeof(GstFontVariant));
	memset(&self->ifont, 0, sizeof(GstFontVariant));
	memset(&self->ibfont, 0, sizeof(GstFontVariant));

	self->cw = 0;
	self->ch = 0;
	self->frc = NULL;
	self->frc_len = 0;
	self->frc_cap = 0;
	self->used_font = NULL;
	self->used_fontsize = 0;
	self->default_fontsize = 0;
	self->display = NULL;
	self->screen = 0;
	self->fonts_loaded = FALSE;
}

/**
 * gst_font_cache_new:
 *
 * Creates a new font cache instance.
 *
 * Returns: (transfer full): A new #GstFontCache
 */
GstFontCache *
gst_font_cache_new(void)
{
	return (GstFontCache *)g_object_new(GST_TYPE_FONT_CACHE, NULL);
}

/**
 * gst_font_cache_clear:
 * @self: A #GstFontCache
 *
 * Clears the fallback font ring cache.
 */
void
gst_font_cache_clear(GstFontCache *self)
{
	gint i;

	g_return_if_fail(GST_IS_FONT_CACHE(self));

	/* Free all ring cache fonts */
	if (self->display != NULL) {
		for (i = 0; i < self->frc_len; i++) {
			if (self->frc[i].font != NULL) {
				XftFontClose(self->display, self->frc[i].font);
			}
		}
	}
	self->frc_len = 0;
}

/**
 * gst_font_cache_load_fonts:
 * @self: A #GstFontCache
 * @display: X11 display connection
 * @screen: X11 screen number
 * @fontstr: fontconfig font specification
 * @fontsize: desired size in pixels (0 for pattern default)
 *
 * Loads all four font variants. Ports st's xloadfonts().
 *
 * Returns: TRUE on success
 */
gboolean
gst_font_cache_load_fonts(
	GstFontCache    *self,
	Display         *display,
	gint            screen,
	const gchar     *fontstr,
	gdouble         fontsize
){
	FcPattern *pattern;
	gdouble fontval;

	g_return_val_if_fail(GST_IS_FONT_CACHE(self), FALSE);
	g_return_val_if_fail(display != NULL, FALSE);
	g_return_val_if_fail(fontstr != NULL, FALSE);

	self->display = display;
	self->screen = screen;

	/* Parse font specification string */
	if (fontstr[0] == '-') {
		pattern = XftXlfdParse(fontstr, False, False);
	} else {
		pattern = FcNameParse((const FcChar8 *)fontstr);
	}

	if (pattern == NULL) {
		g_warning("gst_font_cache_load_fonts: can't parse font '%s'", fontstr);
		return FALSE;
	}

	/* Handle font size override */
	if (fontsize > 1) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontsize);
		self->used_fontsize = fontsize;
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) == FcResultMatch) {
			self->used_fontsize = fontval;
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) == FcResultMatch) {
			self->used_fontsize = -1;
		} else {
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			self->used_fontsize = 12;
		}
		self->default_fontsize = self->used_fontsize;
	}

	/* Load regular font */
	if (load_font_variant(display, screen, &self->font, pattern) != 0) {
		g_warning("gst_font_cache_load_fonts: can't load font '%s'", fontstr);
		FcPatternDestroy(pattern);
		return FALSE;
	}

	/* If size was in points, get actual pixel size from loaded font */
	if (self->used_fontsize < 0) {
		FcPatternGetDouble(self->font.match->pattern,
			FC_PIXEL_SIZE, 0, &fontval);
		self->used_fontsize = fontval;
		if (fontsize == 0) {
			self->default_fontsize = fontval;
		}
	}

	/* Set character cell dimensions from regular font */
	self->cw = (gint)ceil((gdouble)self->font.width);
	self->ch = (gint)ceil((gdouble)self->font.height);

	/* Load italic variant */
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (load_font_variant(display, screen, &self->ifont, pattern) != 0) {
		g_warning("gst_font_cache_load_fonts: can't load italic font");
		/* Non-fatal: continue without italic */
		self->ifont = self->font;
	}

	/* Load bold+italic variant */
	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (load_font_variant(display, screen, &self->ibfont, pattern) != 0) {
		g_warning("gst_font_cache_load_fonts: can't load bold+italic font");
		self->ibfont = self->font;
	}

	/* Load bold variant (roman slant) */
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (load_font_variant(display, screen, &self->bfont, pattern) != 0) {
		g_warning("gst_font_cache_load_fonts: can't load bold font");
		self->bfont = self->font;
	}

	FcPatternDestroy(pattern);

	/* Store font name */
	g_free(self->used_font);
	self->used_font = g_strdup(fontstr);
	self->fonts_loaded = TRUE;

	return TRUE;
}

/**
 * gst_font_cache_unload_fonts:
 * @self: A #GstFontCache
 *
 * Frees all loaded fonts and the ring cache.
 */
void
gst_font_cache_unload_fonts(GstFontCache *self)
{
	g_return_if_fail(GST_IS_FONT_CACHE(self));

	if (!self->fonts_loaded) {
		return;
	}

	/* Free ring cache entries */
	gst_font_cache_clear(self);

	/* Free the four main font variants */
	unload_font_variant(self->display, &self->font);
	unload_font_variant(self->display, &self->bfont);
	unload_font_variant(self->display, &self->ifont);
	unload_font_variant(self->display, &self->ibfont);

	self->fonts_loaded = FALSE;
}

/**
 * gst_font_cache_get_font:
 * @self: A #GstFontCache
 * @style: font style to retrieve
 *
 * Returns the font variant for the given style.
 *
 * Returns: (transfer none) (nullable): the font variant
 */
GstFontVariant *
gst_font_cache_get_font(
	GstFontCache    *self,
	GstFontStyle    style
){
	g_return_val_if_fail(GST_IS_FONT_CACHE(self), NULL);

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

/**
 * gst_font_cache_get_char_width:
 * @self: A #GstFontCache
 *
 * Returns: character cell width in pixels
 */
gint
gst_font_cache_get_char_width(GstFontCache *self)
{
	g_return_val_if_fail(GST_IS_FONT_CACHE(self), 0);

	return self->cw;
}

/**
 * gst_font_cache_get_char_height:
 * @self: A #GstFontCache
 *
 * Returns: character cell height in pixels
 */
gint
gst_font_cache_get_char_height(GstFontCache *self)
{
	g_return_val_if_fail(GST_IS_FONT_CACHE(self), 0);

	return self->ch;
}

/**
 * gst_font_cache_lookup_glyph:
 * @self: A #GstFontCache
 * @rune: Unicode code point
 * @style: desired font style
 * @font_out: (out): XftFont containing the glyph
 * @glyph_out: (out): glyph index
 *
 * Looks up a glyph, searching main font, ring cache,
 * then fontconfig system fonts. Ports st's frc[] lookup.
 *
 * Returns: TRUE if glyph was found
 */
gboolean
gst_font_cache_lookup_glyph(
	GstFontCache    *self,
	GstRune         rune,
	GstFontStyle    style,
	XftFont         **font_out,
	FT_UInt         *glyph_out
){
	GstFontVariant *fv;
	FT_UInt glyphidx;
	gint f;
	FcResult fcres;
	FcPattern *fcpattern;
	FcPattern *fontpattern;
	FcCharSet *fccharset;
	FcFontSet *fcsets[1];

	g_return_val_if_fail(GST_IS_FONT_CACHE(self), FALSE);
	g_return_val_if_fail(font_out != NULL, FALSE);
	g_return_val_if_fail(glyph_out != NULL, FALSE);

	/* Get the appropriate font variant */
	fv = gst_font_cache_get_font(self, style);

	/* Try the main font first */
	glyphidx = XftCharIndex(self->display, fv->match, (FcChar32)rune);
	if (glyphidx != 0) {
		*font_out = fv->match;
		*glyph_out = glyphidx;
		return TRUE;
	}

	/* Search the fallback ring cache */
	for (f = 0; f < self->frc_len; f++) {
		glyphidx = XftCharIndex(self->display, self->frc[f].font, (FcChar32)rune);
		/* Found in cache with matching style */
		if (glyphidx != 0 && self->frc[f].flags == (gint)style) {
			*font_out = self->frc[f].font;
			*glyph_out = glyphidx;
			return TRUE;
		}
		/* Previously cached miss for this codepoint+style */
		if (glyphidx == 0 && self->frc[f].flags == (gint)style
		    && self->frc[f].unicodep == rune) {
			*font_out = self->frc[f].font;
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
			(gsize)self->frc_cap * sizeof(GstFontRingEntry));
	}

	/* Open the fallback font and add to cache */
	if (fontpattern != NULL) {
		self->frc[self->frc_len].font =
			XftFontOpenPattern(self->display, fontpattern);
	} else {
		self->frc[self->frc_len].font = NULL;
	}

	if (self->frc[self->frc_len].font == NULL) {
		/* Could not open fallback font */
		FcPatternDestroy(fcpattern);
		FcCharSetDestroy(fccharset);
		*font_out = fv->match;
		*glyph_out = 0;
		return FALSE;
	}

	self->frc[self->frc_len].flags = (gint)style;
	self->frc[self->frc_len].unicodep = rune;

	glyphidx = XftCharIndex(self->display,
		self->frc[self->frc_len].font, (FcChar32)rune);

	*font_out = self->frc[self->frc_len].font;
	*glyph_out = glyphidx;

	self->frc_len++;

	FcPatternDestroy(fcpattern);
	FcCharSetDestroy(fccharset);

	return (glyphidx != 0);
}

/**
 * gst_font_cache_get_used_font:
 * @self: A #GstFontCache
 *
 * Returns: (transfer none) (nullable): the font name
 */
const gchar *
gst_font_cache_get_used_font(GstFontCache *self)
{
	g_return_val_if_fail(GST_IS_FONT_CACHE(self), NULL);

	return self->used_font;
}

/**
 * gst_font_cache_get_font_size:
 * @self: A #GstFontCache
 *
 * Returns: current font size in pixels
 */
gdouble
gst_font_cache_get_font_size(GstFontCache *self)
{
	g_return_val_if_fail(GST_IS_FONT_CACHE(self), 0);

	return self->used_fontsize;
}

/**
 * gst_font_cache_get_default_font_size:
 * @self: A #GstFontCache
 *
 * Returns: default font size in pixels
 */
gdouble
gst_font_cache_get_default_font_size(GstFontCache *self)
{
	g_return_val_if_fail(GST_IS_FONT_CACHE(self), 0);

	return self->default_fontsize;
}
