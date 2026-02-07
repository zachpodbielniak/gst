/*
 * gst-color-provider.h
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for providing color schemes to the terminal.
 */

#ifndef GST_COLOR_PROVIDER_H
#define GST_COLOR_PROVIDER_H

#include <glib-object.h>
#include "../gst-types.h"

G_BEGIN_DECLS

#define GST_TYPE_COLOR_PROVIDER (gst_color_provider_get_type())

G_DECLARE_INTERFACE(GstColorProvider, gst_color_provider, GST, COLOR_PROVIDER, GObject)

/**
 * GstColorProviderInterface:
 * @parent_iface: The parent interface.
 * @get_color: Virtual method to retrieve a color by index.
 *
 * Interface for providing terminal color schemes.
 */
struct _GstColorProviderInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	gboolean (*get_color) (GstColorProvider *self,
	                       guint             index,
	                       GstColor         *color);
};

/**
 * gst_color_provider_get_color:
 * @self: A #GstColorProvider instance.
 * @index: The color index (0-255 for standard terminal colors).
 * @color: (out): Location to store the color as a #GstColor (RGBA guint32).
 *
 * Retrieves the color at the specified index.
 *
 * Returns: %TRUE if the color was found, %FALSE otherwise.
 */
gboolean
gst_color_provider_get_color(GstColorProvider *self,
                             guint             index,
                             GstColor         *color);

G_END_DECLS

#endif /* GST_COLOR_PROVIDER_H */
