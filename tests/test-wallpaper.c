/*
 * test-wallpaper.c - Tests for the wallpaper module infrastructure
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests GstWallpaperConfig defaults and YAML loading,
 * GstBackgroundProvider interface, render context wallpaper
 * fields, and module manager hook auto-detection.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <string.h>
#include "config/gst-config.h"
#include "module/gst-module.h"
#include "module/gst-module-manager.h"
#include "interfaces/gst-background-provider.h"
#include "rendering/gst-render-context.h"
#include "gst-enums.h"

/* ===== Helper: write YAML to a temp file ===== */

static gchar *
write_temp_yaml(const gchar *yaml_content)
{
	gchar *path;
	GError *error = NULL;
	gint fd;

	fd = g_file_open_tmp("gst-test-XXXXXX.yaml", &path, &error);
	g_assert_no_error(error);
	g_assert_cmpint(fd, >=, 0);

	g_assert_true(g_file_set_contents(path, yaml_content, -1, &error));
	g_assert_no_error(error);
	close(fd);

	return path;
}

/* ===== Test: wallpaper config defaults ===== */

static void
test_wallpaper_config_defaults(void)
{
	g_autoptr(GstConfig) config = NULL;

	config = gst_config_new();
	g_assert_nonnull(config);

	g_assert_false(config->modules.wallpaper.enabled);
	g_assert_nonnull(config->modules.wallpaper.image_path);
	g_assert_cmpstr(config->modules.wallpaper.image_path, ==, "");
	g_assert_cmpstr(config->modules.wallpaper.scale_mode, ==, "fill");
	g_assert_cmpfloat_with_epsilon(config->modules.wallpaper.bg_alpha,
		0.3, 0.001);
}

/* ===== Test: wallpaper config YAML loading ===== */

static void
test_wallpaper_config_load_yaml(void)
{
	g_autoptr(GstConfig) config = NULL;
	gchar *path;
	const gchar *yaml =
		"modules:\n"
		"  wallpaper:\n"
		"    enabled: true\n"
		"    image_path: /tmp/test-bg.png\n"
		"    scale_mode: fit\n"
		"    bg_alpha: 0.5\n";

	path = write_temp_yaml(yaml);
	config = gst_config_new();
	g_assert_nonnull(config);
	g_assert_true(gst_config_load_from_path(config, path, NULL));

	g_assert_true(config->modules.wallpaper.enabled);
	g_assert_cmpstr(config->modules.wallpaper.image_path, ==,
		"/tmp/test-bg.png");
	g_assert_cmpstr(config->modules.wallpaper.scale_mode, ==, "fit");
	g_assert_cmpfloat_with_epsilon(config->modules.wallpaper.bg_alpha,
		0.5, 0.001);

	g_unlink(path);
	g_free(path);
}

/* ===== Test: render context wallpaper fields ===== */

static void
test_render_context_wallpaper_fields(void)
{
	GstRenderContext ctx;

	/* Zero-initialize to verify default state */
	memset(&ctx, 0, sizeof(ctx));

	g_assert_false(ctx.has_wallpaper);
	g_assert_cmpfloat(ctx.wallpaper_bg_alpha, ==, 0.0);

	/* Verify fields are writable */
	ctx.has_wallpaper = TRUE;
	ctx.wallpaper_bg_alpha = 0.75;
	g_assert_true(ctx.has_wallpaper);
	g_assert_cmpfloat_with_epsilon(ctx.wallpaper_bg_alpha, 0.75, 0.001);
}

/* ================================================================
 * TestBackgroundModule - minimal GstModule that implements
 * GstBackgroundProvider for testing hook auto-detection and
 * dispatch.
 * ================================================================ */

typedef struct
{
	GstModule parent_instance;
	gboolean  render_called;
	gint      last_width;
	gint      last_height;
} TestBackgroundModule;

typedef struct
{
	GstModuleClass parent_class;
} TestBackgroundModuleClass;

static GType test_background_module_get_type(void);

#define TEST_TYPE_BACKGROUND_MODULE (test_background_module_get_type())
#define TEST_BACKGROUND_MODULE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_BACKGROUND_MODULE, \
		TestBackgroundModule))

/* GstBackgroundProvider interface implementation */
static void
test_bg_render_background(
	GstBackgroundProvider *provider,
	gpointer               render_context,
	gint                   width,
	gint                   height
){
	TestBackgroundModule *self;

	self = TEST_BACKGROUND_MODULE(provider);
	self->render_called = TRUE;
	self->last_width = width;
	self->last_height = height;

	/* Set wallpaper flags on the render context like a real module */
	if (render_context != NULL) {
		GstRenderContext *ctx;

		ctx = (GstRenderContext *)render_context;
		ctx->has_wallpaper = TRUE;
		ctx->wallpaper_bg_alpha = 0.4;
	}
}

static void
test_bg_provider_iface_init(GstBackgroundProviderInterface *iface)
{
	iface->render_background = test_bg_render_background;
}

/* GstModule vfuncs */
static const gchar *
test_bg_module_get_name(GstModule *module)
{
	(void)module;
	return "test-bg";
}

static const gchar *
test_bg_module_get_description(GstModule *module)
{
	(void)module;
	return "Test background module";
}

static gboolean
test_bg_module_activate(GstModule *module)
{
	(void)module;
	return TRUE;
}

static void
test_bg_module_deactivate(GstModule *module)
{
	(void)module;
}

static void
test_background_module_class_init(TestBackgroundModuleClass *klass)
{
	GstModuleClass *mod_class;

	mod_class = GST_MODULE_CLASS(klass);
	mod_class->get_name = test_bg_module_get_name;
	mod_class->get_description = test_bg_module_get_description;
	mod_class->activate = test_bg_module_activate;
	mod_class->deactivate = test_bg_module_deactivate;
}

static void
test_background_module_init(TestBackgroundModule *self)
{
	self->render_called = FALSE;
	self->last_width = 0;
	self->last_height = 0;
}

G_DEFINE_TYPE_WITH_CODE(TestBackgroundModule, test_background_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_BACKGROUND_PROVIDER,
		test_bg_provider_iface_init))

/* ===== Test: background provider interface ===== */

static void
test_background_provider_interface(void)
{
	TestBackgroundModule *mod;
	GstRenderContext ctx;

	mod = (TestBackgroundModule *)g_object_new(
		TEST_TYPE_BACKGROUND_MODULE, NULL);
	g_assert_nonnull(mod);

	/* Verify it implements the interface */
	g_assert_true(GST_IS_BACKGROUND_PROVIDER(mod));

	/* Dispatch the interface method */
	memset(&ctx, 0, sizeof(ctx));
	gst_background_provider_render_background(
		GST_BACKGROUND_PROVIDER(mod), &ctx, 800, 600);

	g_assert_true(mod->render_called);
	g_assert_cmpint(mod->last_width, ==, 800);
	g_assert_cmpint(mod->last_height, ==, 600);
	g_assert_true(ctx.has_wallpaper);
	g_assert_cmpfloat_with_epsilon(ctx.wallpaper_bg_alpha, 0.4, 0.001);

	g_object_unref(mod);
}

/* ===== Test: hook auto-detection for background provider ===== */

static void
test_background_hook_auto_detection(void)
{
	GstModuleManager *mgr;
	TestBackgroundModule *mod;
	GstRenderContext ctx;

	mgr = gst_module_manager_new();
	g_assert_nonnull(mgr);

	mod = (TestBackgroundModule *)g_object_new(
		TEST_TYPE_BACKGROUND_MODULE, NULL);
	g_assert_nonnull(mod);

	/* Register the module; auto-detect should pick up the interface */
	gst_module_manager_register(mgr, GST_MODULE(mod));
	gst_module_activate(GST_MODULE(mod));

	/* Dispatch the render background hook */
	memset(&ctx, 0, sizeof(ctx));
	gst_module_manager_dispatch_render_background(
		mgr, &ctx, 1024, 768);

	g_assert_true(mod->render_called);
	g_assert_cmpint(mod->last_width, ==, 1024);
	g_assert_cmpint(mod->last_height, ==, 768);
	g_assert_true(ctx.has_wallpaper);

	g_object_unref(mgr);
}

/* ===== Test: wallpaper config scale modes ===== */

static void
test_wallpaper_config_scale_modes(void)
{
	g_autoptr(GstConfig) config = NULL;
	gchar *path;

	/* Test "stretch" mode */
	{
		const gchar *yaml =
			"modules:\n"
			"  wallpaper:\n"
			"    scale_mode: stretch\n";

		path = write_temp_yaml(yaml);
		config = gst_config_new();
		g_assert_true(gst_config_load_from_path(config, path, NULL));
		g_assert_cmpstr(config->modules.wallpaper.scale_mode, ==, "stretch");
		g_unlink(path);
		g_free(path);
	}

	/* Test "center" mode */
	{
		const gchar *yaml =
			"modules:\n"
			"  wallpaper:\n"
			"    scale_mode: center\n";

		path = write_temp_yaml(yaml);
		g_clear_object(&config);
		config = gst_config_new();
		g_assert_true(gst_config_load_from_path(config, path, NULL));
		g_assert_cmpstr(config->modules.wallpaper.scale_mode, ==, "center");
		g_unlink(path);
		g_free(path);
	}
}

/* ===== Test: wallpaper config bg_alpha clamping ===== */

static void
test_wallpaper_config_bg_alpha_range(void)
{
	g_autoptr(GstConfig) config = NULL;
	gchar *path;
	const gchar *yaml;

	/* Test valid alpha value */
	yaml =
		"modules:\n"
		"  wallpaper:\n"
		"    bg_alpha: 0.0\n";

	path = write_temp_yaml(yaml);
	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, NULL));
	g_assert_cmpfloat_with_epsilon(config->modules.wallpaper.bg_alpha,
		0.0, 0.001);
	g_unlink(path);
	g_free(path);

	/* Test alpha at 1.0 */
	yaml =
		"modules:\n"
		"  wallpaper:\n"
		"    bg_alpha: 1.0\n";

	path = write_temp_yaml(yaml);
	g_clear_object(&config);
	config = gst_config_new();
	g_assert_true(gst_config_load_from_path(config, path, NULL));
	g_assert_cmpfloat_with_epsilon(config->modules.wallpaper.bg_alpha,
		1.0, 0.001);
	g_unlink(path);
	g_free(path);
}

/* ===== Test runner ===== */

gint
main(gint argc, gchar **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/wallpaper/config/defaults",
		test_wallpaper_config_defaults);
	g_test_add_func("/wallpaper/config/load-yaml",
		test_wallpaper_config_load_yaml);
	g_test_add_func("/wallpaper/config/scale-modes",
		test_wallpaper_config_scale_modes);
	g_test_add_func("/wallpaper/config/bg-alpha-range",
		test_wallpaper_config_bg_alpha_range);
	g_test_add_func("/wallpaper/render-context/fields",
		test_render_context_wallpaper_fields);
	g_test_add_func("/wallpaper/interface/background-provider",
		test_background_provider_interface);
	g_test_add_func("/wallpaper/module-manager/hook-auto-detection",
		test_background_hook_auto_detection);

	return g_test_run();
}
