/*
 * gst-color-scheme.c - Terminal color scheme handling
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-color-scheme.h"

/**
 * SECTION:gst-color-scheme
 * @title: GstColorScheme
 * @short_description: Terminal color palette management
 *
 * #GstColorScheme manages the 256-color palette used for terminal
 * rendering, including the 16 standard colors and extended palette.
 */

#define GST_COLOR_PALETTE_SIZE (256)

struct _GstColorScheme
{
	GObject parent_instance;

	gchar *name;
	guint32 foreground;
	guint32 background;
	guint32 cursor_color;
	guint32 palette[GST_COLOR_PALETTE_SIZE];
};

G_DEFINE_TYPE(GstColorScheme, gst_color_scheme, G_TYPE_OBJECT)

static void
gst_color_scheme_dispose(GObject *object)
{
	GstColorScheme *self;

	self = GST_COLOR_SCHEME(object);

	g_clear_pointer(&self->name, g_free);

	G_OBJECT_CLASS(gst_color_scheme_parent_class)->dispose(object);
}

static void
gst_color_scheme_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_color_scheme_parent_class)->finalize(object);
}

static void
gst_color_scheme_init_default_palette(GstColorScheme *self)
{
	guint i;

	/*
	 * Initialize the standard 16 colors (0-15)
	 * Using typical xterm-like defaults
	 */

	/* Normal colors (0-7) */
	self->palette[0] = 0xFF000000;  /* black */
	self->palette[1] = 0xFFCD0000;  /* red */
	self->palette[2] = 0xFF00CD00;  /* green */
	self->palette[3] = 0xFFCDCD00;  /* yellow */
	self->palette[4] = 0xFF0000EE;  /* blue */
	self->palette[5] = 0xFFCD00CD;  /* magenta */
	self->palette[6] = 0xFF00CDCD;  /* cyan */
	self->palette[7] = 0xFFE5E5E5;  /* white */

	/* Bright colors (8-15) */
	self->palette[8] = 0xFF7F7F7F;   /* bright black (gray) */
	self->palette[9] = 0xFFFF0000;   /* bright red */
	self->palette[10] = 0xFF00FF00;  /* bright green */
	self->palette[11] = 0xFFFFFF00;  /* bright yellow */
	self->palette[12] = 0xFF5C5CFF;  /* bright blue */
	self->palette[13] = 0xFFFF00FF;  /* bright magenta */
	self->palette[14] = 0xFF00FFFF;  /* bright cyan */
	self->palette[15] = 0xFFFFFFFF;  /* bright white */

	/*
	 * Initialize 216 color cube (16-231)
	 * 6x6x6 RGB cube
	 */
	for (i = 0; i < 216; i++)
	{
		guint r, g, b;
		guint idx;

		idx = i + 16;
		r = (i / 36) % 6;
		g = (i / 6) % 6;
		b = i % 6;

		/* Convert 0-5 to 0-255 range */
		r = (r > 0) ? (r * 40 + 55) : 0;
		g = (g > 0) ? (g * 40 + 55) : 0;
		b = (b > 0) ? (b * 40 + 55) : 0;

		self->palette[idx] = 0xFF000000 | (r << 16) | (g << 8) | b;
	}

	/* Initialize grayscale (232-255) */
	for (i = 0; i < 24; i++)
	{
		guint gray;
		guint idx;

		idx = i + 232;
		gray = i * 10 + 8;

		self->palette[idx] = 0xFF000000 | (gray << 16) | (gray << 8) | gray;
	}
}

static void
gst_color_scheme_class_init(GstColorSchemeClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_color_scheme_dispose;
	object_class->finalize = gst_color_scheme_finalize;
}

static void
gst_color_scheme_init(GstColorScheme *self)
{
	self->name = g_strdup("default");
	self->foreground = 0xFFFFFFFF;  /* white */
	self->background = 0xFF000000;  /* black */
	self->cursor_color = 0xFFFFFFFF;

	gst_color_scheme_init_default_palette(self);
}

/**
 * gst_color_scheme_new:
 * @name: The name of the color scheme
 *
 * Creates a new color scheme with default colors.
 *
 * Returns: (transfer full): A new #GstColorScheme
 */
GstColorScheme *
gst_color_scheme_new(const gchar *name)
{
	GstColorScheme *scheme;

	scheme = (GstColorScheme *)g_object_new(GST_TYPE_COLOR_SCHEME, NULL);

	if (name != NULL)
	{
		g_free(scheme->name);
		scheme->name = g_strdup(name);
	}

	return scheme;
}

/**
 * gst_color_scheme_get_name:
 * @self: A #GstColorScheme
 *
 * Gets the name of the color scheme.
 *
 * Returns: (transfer none): The color scheme name
 */
const gchar *
gst_color_scheme_get_name(GstColorScheme *self)
{
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), NULL);

	return self->name;
}

/**
 * gst_color_scheme_get_foreground:
 * @self: A #GstColorScheme
 *
 * Gets the default foreground color as ARGB.
 *
 * Returns: The foreground color
 */
guint32
gst_color_scheme_get_foreground(GstColorScheme *self)
{
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), 0xFFFFFFFF);

	return self->foreground;
}

/**
 * gst_color_scheme_get_background:
 * @self: A #GstColorScheme
 *
 * Gets the default background color as ARGB.
 *
 * Returns: The background color
 */
guint32
gst_color_scheme_get_background(GstColorScheme *self)
{
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), 0xFF000000);

	return self->background;
}

/**
 * gst_color_scheme_get_color:
 * @self: A #GstColorScheme
 * @index: The color index (0-255)
 *
 * Gets a palette color by index.
 *
 * Returns: The color as ARGB
 */
guint32
gst_color_scheme_get_color(
	GstColorScheme *self,
	guint           index
){
	g_return_val_if_fail(GST_IS_COLOR_SCHEME(self), 0);
	g_return_val_if_fail(index < GST_COLOR_PALETTE_SIZE, 0);

	return self->palette[index];
}
