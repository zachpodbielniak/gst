/*
 * gst-wallpaper-module.h - Background image wallpaper module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Renders a PNG or JPEG image as the terminal background.
 * Default-background cells become transparent so the image
 * shows through beneath the text.
 */

#ifndef GST_WALLPAPER_MODULE_H
#define GST_WALLPAPER_MODULE_H

#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-background-provider.h"

G_BEGIN_DECLS

#define GST_TYPE_WALLPAPER_MODULE (gst_wallpaper_module_get_type())

G_DECLARE_FINAL_TYPE(GstWallpaperModule, gst_wallpaper_module,
	GST, WALLPAPER_MODULE, GstModule)

G_MODULE_EXPORT GType
gst_module_register(void);

G_END_DECLS

#endif /* GST_WALLPAPER_MODULE_H */
