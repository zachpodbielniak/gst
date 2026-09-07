/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Consumer regressions. Optional exporters are enabled by GST_TEST_MCP and
 * GST_TEST_WEBVIEW; link their normal dependencies when enabling these tests. */
#include <glib.h>

#define gst_module_register graphics_ligatures_register
#include "../modules/ligatures/gst-ligatures-module.c"
#undef gst_module_register
#define gst_module_register graphics_urlclick_register
#include "../modules/urlclick/gst-urlclick-module.c"
#undef gst_module_register

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTerminal, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstLine, gst_line_free)

#ifdef GST_TEST_MCP
#include "../modules/mcp/gst-mcp-tools-url.c"
#endif
#ifdef GST_TEST_WEBVIEW
#include "../modules/webview/gst-webview-server.c"
#endif

#ifdef GST_HAVE_WAYLAND
#include "../src/rendering/gst-wayland-render-context.h"

/* Observe real manager dispatch without depending on Kitty image uploads. */
typedef struct { GstModule parent; } ConsumerTransformer;
typedef struct { GstModuleClass parent; } ConsumerTransformerClass;
static guint transform_calls;
static GstLine *expected_line;

static gboolean
consumer_transform(GstGlyphTransformer *self, gunichar rune, gpointer data,
	gint x, gint y, gint width, gint height)
{
	GstRenderContext *ctx = (GstRenderContext *)data;
	(void)self; (void)x; (void)y; (void)width; (void)height;
	transform_calls++;
	g_assert_cmpuint(rune, ==, 0x10EEEE);
	g_assert_true(ctx->current_line == expected_line);
	g_assert_cmpint(ctx->current_col, ==, 0);
	g_assert_cmpint(ctx->current_cols, ==, 2);
	return TRUE;
}

static void
consumer_transformer_iface_init(GstGlyphTransformerInterface *iface)
{
	iface->transform_glyph = consumer_transform;
}

G_DEFINE_TYPE_WITH_CODE(ConsumerTransformer, consumer_transformer, GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_GLYPH_TRANSFORMER, consumer_transformer_iface_init))

static const gchar *
consumer_name(GstModule *module)
{
	(void)module;
	return "consumer-transformer";
}

static gboolean
consumer_activate(GstModule *module)
{
	(void)module;
	return TRUE;
}

static void
consumer_transformer_class_init(ConsumerTransformerClass *klass)
{
	GST_MODULE_CLASS(klass)->get_name = consumer_name;
	GST_MODULE_CLASS(klass)->activate = consumer_activate;
}

static void
consumer_transformer_init(ConsumerTransformer *self)
{
	(void)self;
}

/* The overlay cell path passes coordinate clusters, but not text clusters,
 * to transformers and never leaves a borrowed history line in the context. */
static void
test_cell_transform_context(void)
{
	g_autoptr(GstLine) line = gst_line_new(2);
	g_autoptr(GstCairoFontCache) cache = gst_cairo_font_cache_new();
	GstModuleManager *mgr = gst_module_manager_get_default();
	GstModule *module = g_object_new(consumer_transformer_get_type(), NULL);
	GstWaylandRenderContext ctx = { 0 };
	GstGlyph glyph = GST_GLYPH_INIT;

	g_assert_true(gst_cairo_font_cache_load_fonts(cache, "monospace", 14));
	ctx.base.backend = GST_BACKEND_WAYLAND;
	ctx.base.cw = gst_cairo_font_cache_get_char_width(cache);
	ctx.base.ch = gst_cairo_font_cache_get_char_height(cache);
	ctx.base.opacity = 1.0;
	ctx.font_cache = cache;
	ctx.surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		ctx.base.cw * 2, ctx.base.ch);
	ctx.cr = cairo_create(ctx.surface);
	gst_wayland_render_context_init_ops(&ctx);
	glyph.rune = 0x10EEEE;
	gst_glyph_append(&glyph, 0x305);
	gst_line_set_glyph(line, 0, &glyph);
	gst_glyph_clear(&glyph);
	glyph.rune = 'e';
	gst_glyph_append(&glyph, 0x301);
	gst_line_set_glyph(line, 1, &glyph);
	gst_glyph_clear(&glyph);
	g_assert_true(gst_module_manager_register(mgr, module));
	g_assert_true(gst_module_activate(module));
	expected_line = line;
	transform_calls = 0;
	gst_render_context_draw_cell(&ctx.base, line, 0, 2, 0, 0);
	gst_render_context_draw_cell(&ctx.base, line, 1, 2, ctx.base.cw, 0);
	g_assert_cmpuint(transform_calls, ==, 1);
	g_assert_null(ctx.base.current_line);
	g_assert_cmpint(cairo_status(ctx.cr), ==, CAIRO_STATUS_SUCCESS);
	gst_module_manager_unregister(mgr, consumer_name(module));
	g_object_unref(module);
	expected_line = NULL;
	cairo_destroy(ctx.cr);
	cairo_surface_destroy(ctx.surface);
}
#endif

/* Shaping must never consume the first scalar of a neighboring cluster. */
static void
test_ligature_boundaries(void)
{
	g_autoptr(GstLine) line = gst_line_new(4);
	GstGlyph glyph = GST_GLYPH_INIT;
	guint32 codepoints[4];

	glyph.rune = '=';
	gst_line_set_glyph(line, 0, &glyph);
	gst_glyph_append(&glyph, 0x301);
	gst_line_set_glyph(line, 1, &glyph);
	gst_glyph_clear(&glyph);
	g_assert_cmpuint(extract_run(line, 0, 4, codepoints, 4), ==, 1);
	g_assert_cmpuint(extract_run(line, 1, 4, codepoints, 4), ==, 0);
	glyph.rune = 0x10EEEE;
	gst_line_set_glyph(line, 1, &glyph);
	g_assert_cmpuint(extract_run(line, 0, 4, codepoints, 4), ==, 1);
	glyph.rune = 0x4E2D;
	glyph.attr = GST_GLYPH_ATTR_WIDE;
	gst_line_set_glyph(line, 1, &glyph);
	g_assert_cmpuint(extract_run(line, 0, 4, codepoints, 4), ==, 1);
}

/* Any unexpected drawing in the bitmap bounds test is a failure. */
static void
unexpected_glyph_id(GstRenderContext *ctx, guint32 glyph_id,
	GstFontStyle style, gint x, gint y)
{
	(void)ctx; (void)glyph_id; (void)style; (void)x; (void)y;
	g_assert_not_reached();
}

/* A terminal wider than the bitmap must not overwrite the module's cache. */
static void
test_ligature_large_grid(void)
{
	g_autoptr(GstLigaturesModule) module = g_object_new(GST_TYPE_LIGATURES_MODULE, NULL);
	g_autoptr(GstLine) line = gst_line_new(GST_LIGATURES_MAX_COLS + 100);
	GstRenderContextOps ops = { 0 };
	GstRenderContext ctx = { 0 };
	GHashTable *cache = module->cache;

	/* A non-NULL glyph-ID op enables dispatch; spaces never invoke it. */
	ops.draw_glyph_id = unexpected_glyph_id;
	ctx.ops = &ops;
	ctx.current_line = line;
	ctx.current_cols = line->len;
	g_assert_false(gst_ligatures_module_transform_glyph(GST_GLYPH_TRANSFORMER(module),
		' ', &ctx, 0, 0, 8, 16));
	g_assert_true(module->cache == cache);
	ctx.current_col = GST_LIGATURES_MAX_COLS;
	g_assert_false(gst_ligatures_module_transform_glyph(GST_GLYPH_TRANSFORMER(module),
		'=', &ctx, 0, 0, 8, 16));
}

/* URL collection keeps owned clusters and omits zero-rune wide dummy cells. */
static void
test_url_text(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(5, 1);
	GstModuleManager *mgr = gst_module_manager_get_default();
	g_autofree gchar *text = NULL;

	gst_module_manager_set_terminal(mgr, term);
	gst_terminal_write(term, "\344\270\255e\314\201X", -1);
	text = collect_visible_text();
	g_assert_cmpstr(text, ==, "\344\270\255e\314\201X \n");
	gst_module_manager_set_terminal(mgr, NULL);
}

#ifdef GST_TEST_MCP
/* Match boundaries inside UTF-8 and cluster text still cover whole cells. */
static void
test_url_columns(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(8, 1);
	GstLine *line;
	gint start, end;

	gst_terminal_write(term, "\344\270\255e\314\201URL", -1);
	line = gst_terminal_get_line(term, 0);
	url_match_columns(line, 6, 9, &start, &end);
	g_assert_cmpint(start, ==, 3);
	g_assert_cmpint(end, ==, 6);
	url_match_columns(line, 4, 6, &start, &end);
	g_assert_cmpint(start, ==, 2);
	g_assert_cmpint(end, ==, 3);
	url_match_columns(line, 0, 3, &start, &end);
	g_assert_cmpint(start, ==, 0);
	g_assert_cmpint(end, ==, 2);
}
#endif

#ifdef GST_TEST_WEBVIEW
/* A combining-only edit must change both hashes and the serialized cell. */
static void
test_webview_cluster(void)
{
	g_autoptr(GstTerminal) term = gst_terminal_new(2, 1);
	g_autoptr(GstColorScheme) scheme = gst_color_scheme_new("test");
	g_autoptr(GString) json = g_string_new(NULL);
	GstLine *line = gst_terminal_get_line(term, 0);
	GstGlyph *glyph = gst_line_get_glyph(line, 0);
	guint32 before;

	glyph->rune = 'e';
	before = hash_row(term, 0, 2);
	gst_glyph_append(glyph, 0x301);
	g_assert_cmpuint(hash_row(term, 0, 2), !=, before);
	g_assert_cmpuint(hash_row(term, 0, 2), ==, hash_glyph_array(line->glyphs, 2));
	append_cell_json(json, glyph, scheme, TRUE);
	g_assert_nonnull(strstr(json->str, "e\314\201"));
}
#endif

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/consumers/ligature-boundaries", test_ligature_boundaries);
	g_test_add_func("/consumers/ligature-large-grid", test_ligature_large_grid);
	g_test_add_func("/consumers/url-text", test_url_text);
#ifdef GST_HAVE_WAYLAND
	g_test_add_func("/consumers/cell-transform-context", test_cell_transform_context);
#endif
#ifdef GST_TEST_MCP
	g_test_add_func("/consumers/url-columns", test_url_columns);
#endif
#ifdef GST_TEST_WEBVIEW
	g_test_add_func("/consumers/webview-cluster", test_webview_cluster);
#endif
	return g_test_run();
}
