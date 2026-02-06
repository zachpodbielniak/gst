/*
 * gst-font-provider.c
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for providing fonts to the terminal.
 */

#include "gst-font-provider.h"

G_DEFINE_INTERFACE(GstFontProvider, gst_font_provider, G_TYPE_OBJECT)

static void
gst_font_provider_default_init(GstFontProviderInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

/**
 * gst_font_provider_get_font_description:
 * @self: A #GstFontProvider instance.
 *
 * Gets the font description to use for terminal rendering.
 *
 * Returns: (transfer full): A newly allocated #PangoFontDescription, or %NULL.
 */
PangoFontDescription *
gst_font_provider_get_font_description(GstFontProvider *self)
{
	GstFontProviderInterface *iface;

	g_return_val_if_fail(GST_IS_FONT_PROVIDER(self), NULL);

	iface = GST_FONT_PROVIDER_GET_IFACE(self);
	g_return_val_if_fail(iface->get_font_description != NULL, NULL);

	return iface->get_font_description(self);
}
