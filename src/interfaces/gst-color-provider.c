/*
 * gst-color-provider.c
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for providing color schemes to the terminal.
 */

#include "gst-color-provider.h"

G_DEFINE_INTERFACE(GstColorProvider, gst_color_provider, G_TYPE_OBJECT)

static void
gst_color_provider_default_init(GstColorProviderInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

/**
 * gst_color_provider_get_color:
 * @self: A #GstColorProvider instance.
 * @index: The color index (0-255 for standard terminal colors).
 * @color: (out): Location to store the color.
 *
 * Retrieves the color at the specified index.
 *
 * Returns: %TRUE if the color was found, %FALSE otherwise.
 */
gboolean
gst_color_provider_get_color(GstColorProvider *self,
                             guint             index,
                             GdkRGBA          *color)
{
	GstColorProviderInterface *iface;

	g_return_val_if_fail(GST_IS_COLOR_PROVIDER(self), FALSE);
	g_return_val_if_fail(color != NULL, FALSE);

	iface = GST_COLOR_PROVIDER_GET_IFACE(self);
	g_return_val_if_fail(iface->get_color != NULL, FALSE);

	return iface->get_color(self, index, color);
}
