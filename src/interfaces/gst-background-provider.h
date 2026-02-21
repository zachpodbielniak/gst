/*
 * gst-background-provider.h
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for providing a background image behind terminal text.
 * Dispatched via GST_HOOK_RENDER_BACKGROUND before line drawing.
 */

#ifndef GST_BACKGROUND_PROVIDER_H
#define GST_BACKGROUND_PROVIDER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_BACKGROUND_PROVIDER (gst_background_provider_get_type())

G_DECLARE_INTERFACE(GstBackgroundProvider, gst_background_provider,
                    GST, BACKGROUND_PROVIDER, GObject)

/**
 * GstBackgroundProviderInterface:
 * @parent_iface: The parent interface.
 * @render_background: Virtual method to render the background.
 *
 * Interface for rendering a background behind terminal text.
 * Implementations should draw their background image via the
 * abstract render context, then set ctx->has_wallpaper and
 * ctx->wallpaper_bg_alpha to control cell background transparency.
 */
struct _GstBackgroundProviderInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	void (*render_background) (GstBackgroundProvider *self,
	                           gpointer               render_context,
	                           gint                   width,
	                           gint                   height);
};

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
                                          gint                   height);

G_END_DECLS

#endif /* GST_BACKGROUND_PROVIDER_H */
