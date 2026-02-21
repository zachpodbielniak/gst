/*
 * gst-background-provider.c
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for providing a background image behind terminal text.
 */

#include "gst-background-provider.h"

G_DEFINE_INTERFACE(GstBackgroundProvider, gst_background_provider, G_TYPE_OBJECT)

static void
gst_background_provider_default_init(GstBackgroundProviderInterface *iface)
{
	(void)iface;
}

/**
 * gst_background_provider_render_background:
 * @self: A #GstBackgroundProvider instance.
 * @render_context: Opaque rendering context (GstRenderContext *).
 * @width: The window width in pixels.
 * @height: The window height in pixels.
 *
 * Renders the background behind terminal text. Called before
 * line drawing in the render cycle.
 */
void
gst_background_provider_render_background(GstBackgroundProvider *self,
                                          gpointer               render_context,
                                          gint                   width,
                                          gint                   height)
{
	GstBackgroundProviderInterface *iface;

	g_return_if_fail(GST_IS_BACKGROUND_PROVIDER(self));
	g_return_if_fail(render_context != NULL);

	iface = GST_BACKGROUND_PROVIDER_GET_IFACE(self);
	g_return_if_fail(iface->render_background != NULL);

	iface->render_background(self, render_context, width, height);
}
