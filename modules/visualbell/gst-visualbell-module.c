/*
 * gst-visualbell-module.c - Visual bell notification module
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sample module demonstrating the GST module architecture.
 * Implements GstBellHandler to provide visual bell notifications.
 * Currently logs to stdout as a placeholder; a full implementation
 * would flash the terminal background via the renderer.
 */

#include "gst-visualbell-module.h"

/**
 * SECTION:gst-visualbell-module
 * @title: GstVisualbellModule
 * @short_description: Visual bell notification module
 *
 * #GstVisualbellModule is a sample module that implements the
 * #GstBellHandler interface. When a bell event occurs, it provides
 * a visual notification instead of (or alongside) an audio bell.
 */

/* Private data for the visual bell module */
struct _GstVisualbellModule
{
	GstModule parent_instance;
	guint     flash_duration_ms;
};

/* Forward declarations for interface implementation */
static void
gst_visualbell_module_bell_handler_init(GstBellHandlerInterface *iface);

/* Register the type with GstBellHandler interface */
G_DEFINE_TYPE_WITH_CODE(GstVisualbellModule, gst_visualbell_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_BELL_HANDLER,
		gst_visualbell_module_bell_handler_init))

/* ===== GstModule vfuncs ===== */

/*
 * get_name:
 *
 * Returns the module's unique identifier string.
 */
static const gchar *
gst_visualbell_module_get_name(GstModule *module)
{
	(void)module;
	return "visualbell";
}

/*
 * get_description:
 *
 * Returns a human-readable description of the module.
 */
static const gchar *
gst_visualbell_module_get_description(GstModule *module)
{
	(void)module;
	return "Visual bell notification";
}

/*
 * activate:
 *
 * Activates the visual bell module. Returns TRUE on success.
 */
static gboolean
gst_visualbell_module_activate(GstModule *module)
{
	g_debug("visualbell: activated");
	return TRUE;
}

/*
 * deactivate:
 *
 * Deactivates the visual bell module.
 */
static void
gst_visualbell_module_deactivate(GstModule *module)
{
	g_debug("visualbell: deactivated");
}

/*
 * configure:
 *
 * Configures the module from the application config.
 * Placeholder: a full implementation would read flash_duration_ms
 * from the config's modules.visualbell section.
 */
static void
gst_visualbell_module_configure(GstModule *module, gpointer config)
{
	(void)config;
	g_debug("visualbell: configured");
}

/* ===== GstBellHandler interface ===== */

/*
 * handle_bell:
 *
 * Handles a terminal bell event by producing a visual notification.
 * Currently prints to stdout as a placeholder. A full implementation
 * would invert the terminal colors briefly via the renderer.
 */
static void
gst_visualbell_module_handle_bell(GstBellHandler *handler)
{
	GstVisualbellModule *self;

	self = GST_VISUALBELL_MODULE(handler);
	(void)self;

	g_print("VISUAL BELL!\n");
}

static void
gst_visualbell_module_bell_handler_init(GstBellHandlerInterface *iface)
{
	iface->handle_bell = gst_visualbell_module_handle_bell;
}

/* ===== GObject class/instance init ===== */

static void
gst_visualbell_module_class_init(GstVisualbellModuleClass *klass)
{
	GstModuleClass *module_class;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_visualbell_module_get_name;
	module_class->get_description = gst_visualbell_module_get_description;
	module_class->activate = gst_visualbell_module_activate;
	module_class->deactivate = gst_visualbell_module_deactivate;
	module_class->configure = gst_visualbell_module_configure;
}

static void
gst_visualbell_module_init(GstVisualbellModule *self)
{
	self->flash_duration_ms = 100;
}

/* ===== Module entry point ===== */

/**
 * gst_module_register:
 *
 * Entry point called by the module manager when loading the .so file.
 * Returns the GType so the manager can instantiate the module.
 *
 * Returns: The #GType for #GstVisualbellModule
 */
G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_VISUALBELL_MODULE;
}
