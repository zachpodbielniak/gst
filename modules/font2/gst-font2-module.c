/*
 * gst-font2-module.c - Spare/fallback font loading module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Pre-loads fallback fonts (e.g., Nerd Fonts, emoji) into the
 * font ring cache so they are tried before fontconfig's slow
 * system-wide search. Ports st's font2/xloadsparefonts() behavior.
 *
 * The module reads its font list from the YAML config:
 *   modules:
 *     font2:
 *       fonts:
 *         - "Symbols Nerd Font:pixelsize=14"
 *         - "Noto Color Emoji:pixelsize=14"
 *
 * If no module-specific fonts are configured, it falls back to
 * the global font.fallback list from config.
 *
 * On activation, the module gets the font cache and backend type
 * from the module manager and calls the appropriate
 * load_spare_fonts() function.
 */

#include "gst-font2-module.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/config/gst-config.h"
#include "../../src/gst-enums.h"
#include "../../src/rendering/gst-font-cache.h"

#ifdef GST_HAVE_WAYLAND
#include "../../src/rendering/gst-cairo-font-cache.h"
#endif

/**
 * SECTION:gst-font2-module
 * @title: GstFont2Module
 * @short_description: Pre-load fallback fonts into ring cache
 *
 * #GstFont2Module reads a list of fallback font specifications from
 * the YAML configuration and pre-loads them into the font ring cache.
 * This ensures that glyphs from these fonts (e.g., Nerd Font icons,
 * emoji) are found quickly without fontconfig's slow system-wide
 * fallback search.
 */

struct _GstFont2Module
{
	GstModule parent_instance;

	/* Font specs read from config (NULL-terminated, owned) */
	gchar **fonts;
};

G_DEFINE_TYPE(GstFont2Module, gst_font2_module, GST_TYPE_MODULE)

/* ===== GstModule vfuncs ===== */

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 * Must match the config key under modules: { font2: ... }.
 */
static const gchar *
gst_font2_module_get_name(GstModule *module)
{
	(void)module;
	return "font2";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
gst_font2_module_get_description(GstModule *module)
{
	(void)module;
	return "Pre-load fallback fonts into ring cache";
}

/*
 * configure:
 *
 * Reads the font list from module config or falls back to
 * the global font.fallback configuration.
 */
static void
gst_font2_module_configure(GstModule *module, gpointer config)
{
	GstFont2Module *self;
	GstConfig *cfg;
	YamlMapping *mod_cfg;

	self = GST_FONT2_MODULE(module);
	cfg = (GstConfig *)config;

	/* Free previous font list */
	g_strfreev(self->fonts);
	self->fonts = NULL;

	/* Try module-specific font list first */
	mod_cfg = gst_config_get_module_config(cfg, "font2");
	if (mod_cfg != NULL && yaml_mapping_has_member(mod_cfg, "fonts"))
	{
		YamlSequence *seq;
		guint len;
		guint i;

		seq = yaml_mapping_get_sequence_member(mod_cfg, "fonts");
		if (seq != NULL)
		{
			len = yaml_sequence_get_length(seq);
			if (len > 0)
			{
				self->fonts = g_new0(gchar *, len + 1);
				for (i = 0; i < len; i++)
				{
					self->fonts[i] = g_strdup(
						yaml_sequence_get_string_element(seq, i));
				}
				self->fonts[len] = NULL;

				g_debug("font2: configured %u fonts from module config", len);
				return;
			}
		}
	}

	/* Fall back to global font.fallback list */
	{
		const gchar *const *fallbacks;

		fallbacks = gst_config_get_font_fallbacks(cfg);
		if (fallbacks != NULL && fallbacks[0] != NULL)
		{
			guint count;
			guint i;

			for (count = 0; fallbacks[count] != NULL; count++)
				;

			self->fonts = g_new0(gchar *, count + 1);
			for (i = 0; i < count; i++)
			{
				self->fonts[i] = g_strdup(fallbacks[i]);
			}
			self->fonts[count] = NULL;

			g_debug("font2: configured %u fonts from global fallbacks",
				count);
		}
	}
}

/*
 * activate:
 *
 * Gets the font cache and backend type from the module manager,
 * then calls load_spare_fonts() with the configured font list.
 */
static gboolean
gst_font2_module_activate(GstModule *module)
{
	GstFont2Module *self;
	GstModuleManager *mgr;
	gpointer cache;
	gint backend_type;

	self = GST_FONT2_MODULE(module);

	if (self->fonts == NULL || self->fonts[0] == NULL)
	{
		g_debug("font2: no fonts configured, skipping");
		return TRUE;
	}

	mgr = gst_module_manager_get_default();
	cache = gst_module_manager_get_font_cache(mgr);
	backend_type = gst_module_manager_get_backend_type(mgr);

	if (cache == NULL)
	{
		g_warning("font2: no font cache available");
		return FALSE;
	}

	if (backend_type == GST_BACKEND_X11)
	{
		gst_font_cache_load_spare_fonts(
			GST_FONT_CACHE(cache),
			(const gchar **)self->fonts);
	}
#ifdef GST_HAVE_WAYLAND
	else if (backend_type == GST_BACKEND_WAYLAND)
	{
		gst_cairo_font_cache_load_spare_fonts(
			GST_CAIRO_FONT_CACHE(cache),
			(const gchar **)self->fonts);
	}
#endif
	else
	{
		g_warning("font2: unknown backend type %d", backend_type);
		return FALSE;
	}

	g_debug("font2: activated");
	return TRUE;
}

/*
 * deactivate:
 *
 * Nothing to undo: spare fonts remain in the ring cache until
 * the cache is cleared (e.g., on zoom) or the process exits.
 */
static void
gst_font2_module_deactivate(GstModule *module)
{
	(void)module;
	g_debug("font2: deactivated");
}

/* ===== GObject lifecycle ===== */

static void
gst_font2_module_finalize(GObject *object)
{
	GstFont2Module *self;

	self = GST_FONT2_MODULE(object);

	g_strfreev(self->fonts);
	self->fonts = NULL;

	G_OBJECT_CLASS(gst_font2_module_parent_class)->finalize(object);
}

static void
gst_font2_module_class_init(GstFont2ModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_font2_module_finalize;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_font2_module_get_name;
	module_class->get_description = gst_font2_module_get_description;
	module_class->activate = gst_font2_module_activate;
	module_class->deactivate = gst_font2_module_deactivate;
	module_class->configure = gst_font2_module_configure;
}

static void
gst_font2_module_init(GstFont2Module *self)
{
	self->fonts = NULL;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 * Returns the GType so the manager can instantiate the module.
 *
 * Returns: The #GType for #GstFont2Module
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_FONT2_MODULE;
}
