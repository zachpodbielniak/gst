/*
 * gst-font-provider.h
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for providing fonts to the terminal.
 */

#ifndef GST_FONT_PROVIDER_H
#define GST_FONT_PROVIDER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_FONT_PROVIDER (gst_font_provider_get_type())

G_DECLARE_INTERFACE(GstFontProvider, gst_font_provider, GST, FONT_PROVIDER, GObject)

/**
 * GstFontProviderInterface:
 * @parent_iface: The parent interface.
 * @get_font_description: Virtual method to get a font description string.
 *
 * Interface for providing terminal fonts.
 */
struct _GstFontProviderInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	gchar * (*get_font_description) (GstFontProvider *self);
};

/**
 * gst_font_provider_get_font_description:
 * @self: A #GstFontProvider instance.
 *
 * Gets the font description string to use for terminal rendering.
 * The string should be in a format suitable for Xft/fontconfig (e.g.,
 * "monospace:size=12").
 *
 * Returns: (transfer full): A newly allocated font description string, or %NULL.
 */
gchar *
gst_font_provider_get_font_description(GstFontProvider *self);

G_END_DECLS

#endif /* GST_FONT_PROVIDER_H */
