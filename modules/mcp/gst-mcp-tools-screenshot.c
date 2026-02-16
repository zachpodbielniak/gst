/*
 * gst-mcp-tools-screenshot.c - Screenshot capture MCP tool
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Captures the terminal display as a PNG image and returns it
 * as base64-encoded data via the MCP image content type.
 * Uses libpng for encoding (compression level 1 for speed).
 *
 * Tool: screenshot
 */

#include "gst-mcp-tools.h"
#include "gst-mcp-module.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <png.h>
#include <string.h>

#include "../../src/rendering/gst-renderer.h"
#include "../../src/module/gst-module-manager.h"

/* ===== PNG encoding ===== */

/**
 * PngWriteCtx:
 *
 * Context for writing PNG data to a dynamically-growing memory
 * buffer. Used as the user pointer for png_set_write_fn().
 */
typedef struct {
	guint8   *data;
	gsize     len;
	gsize     alloc;
} PngWriteCtx;

/**
 * png_write_to_mem:
 *
 * libpng write callback that appends data to a PngWriteCtx buffer.
 */
static void
png_write_to_mem(
	png_structp  png,
	png_bytep    buf,
	png_size_t   len
){
	PngWriteCtx *ctx;

	ctx = (PngWriteCtx *)png_get_io_ptr(png);

	/* Grow buffer if needed */
	while (ctx->len + len > ctx->alloc) {
		ctx->alloc = (ctx->alloc == 0) ? 4096 : ctx->alloc * 2;
		ctx->data = (guint8 *)g_realloc(ctx->data, ctx->alloc);
	}

	memcpy(ctx->data + ctx->len, buf, len);
	ctx->len += len;
}

/**
 * png_flush_noop:
 *
 * No-op flush callback for libpng (writing to memory).
 */
static void
png_flush_noop(png_structp png)
{
	(void)png;
}

/**
 * encode_rgba_to_png:
 * @pixels: RGBA pixel data (8 bits per channel, row-major)
 * @width: image width in pixels
 * @height: image height in pixels
 * @stride: row stride in bytes
 * @out_data: (out): allocated PNG data (caller must g_free)
 * @out_len: (out): length of PNG data
 *
 * Encodes raw RGBA pixel data to an in-memory PNG file.
 * The pixel data is already in RGBA order (as returned by
 * gst_renderer_capture_screenshot), so no per-pixel
 * conversion is needed.
 *
 * Returns: %TRUE on success
 */
static gboolean
encode_rgba_to_png(
	const guint8  *pixels,
	gint           width,
	gint           height,
	gint           stride,
	guint8       **out_data,
	gsize         *out_len
){
	png_structp png;
	png_infop info;
	PngWriteCtx ctx;
	gint y;

	memset(&ctx, 0, sizeof(ctx));

	png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
		NULL, NULL, NULL);
	if (png == NULL)
		return FALSE;

	info = png_create_info_struct(png);
	if (info == NULL) {
		png_destroy_write_struct(&png, NULL);
		return FALSE;
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		g_free(ctx.data);
		return FALSE;
	}

	png_set_write_fn(png, &ctx, png_write_to_mem, png_flush_noop);

	png_set_IHDR(png, info, (png_uint_32)width, (png_uint_32)height,
		8, PNG_COLOR_TYPE_RGB_ALPHA,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);

	/* Use fastest compression for responsive screenshots */
	png_set_compression_level(png, 1);

	png_write_info(png, info);

	/*
	 * Pixel data is already RGBA (from capture_screenshot vfunc),
	 * so we can write rows directly without conversion.
	 */
	for (y = 0; y < height; y++) {
		png_write_row(png,
			(png_bytep)(pixels + (gsize)y * (gsize)stride));
	}

	png_write_end(png, info);
	png_destroy_write_struct(&png, &info);

	*out_data = ctx.data;
	*out_len = ctx.len;
	return TRUE;
}

/* ===== Shared helper: capture + encode ===== */

/*
 * capture_png:
 *
 * Captures the terminal display and encodes it as PNG.
 * On success, sets out_data/out_len to the allocated PNG buffer
 * (caller must g_free). Returns an error McpToolResult on failure,
 * or NULL on success.
 */
static McpToolResult *
capture_png(
	guint8 **out_data,
	gsize   *out_len
){
	GstModuleManager *mgr;
	GstRenderer *renderer;
	g_autoptr(GBytes) pixels = NULL;
	const guint8 *pixel_data;
	gsize pixel_len;
	gint width, height, stride;
	McpToolResult *result;

	mgr = gst_module_manager_get_default();
	renderer = (GstRenderer *)gst_module_manager_get_renderer(mgr);
	if (renderer == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Renderer not available");
		return result;
	}

	width = 0;
	height = 0;
	stride = 0;
	pixels = gst_renderer_capture_screenshot(renderer,
		&width, &height, &stride);
	if (pixels == NULL || width <= 0 || height <= 0) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Screenshot capture failed");
		return result;
	}

	pixel_data = (const guint8 *)g_bytes_get_data(pixels, &pixel_len);

	*out_data = NULL;
	*out_len = 0;
	if (!encode_rgba_to_png(pixel_data, width, height, stride,
		out_data, out_len))
	{
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "PNG encoding failed");
		return result;
	}

	return NULL;
}

/* ===== screenshot tool handler ===== */

/*
 * handle_screenshot:
 *
 * Captures the terminal display and returns it as a base64-encoded
 * PNG image via the MCP image content type.
 */
static McpToolResult *
handle_screenshot(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	guint8 *png_data;
	gsize png_len;
	gchar *b64;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	result = capture_png(&png_data, &png_len);
	if (result != NULL)
		return result;

	b64 = g_base64_encode(png_data, png_len);
	g_free(png_data);

	result = mcp_tool_result_new(FALSE);
	mcp_tool_result_add_image(result, b64, "image/png");
	g_free(b64);

	return result;
}

/* ===== save_screenshot tool handler ===== */

/*
 * handle_save_screenshot:
 *
 * Captures the terminal display as PNG and writes it to the
 * file path specified in the "path" argument.
 */
static McpToolResult *
handle_save_screenshot(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	const gchar *path;
	guint8 *png_data;
	gsize png_len;
	g_autoptr(GError) error = NULL;
	McpToolResult *result;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;

	(void)server;
	(void)name;
	(void)user_data;

	/* Validate required path argument */
	if (arguments == NULL || !json_object_has_member(arguments, "path")) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result,
			"Missing required parameter: path");
		return result;
	}

	path = json_object_get_string_member(arguments, "path");
	if (path == NULL || path[0] == '\0') {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result,
			"Parameter 'path' must be a non-empty string");
		return result;
	}

	/* Capture and encode the screenshot */
	result = capture_png(&png_data, &png_len);
	if (result != NULL)
		return result;

	/* Write PNG data to the specified file */
	if (!g_file_set_contents(path, (const gchar *)png_data,
		(gssize)png_len, &error))
	{
		g_free(png_data);
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, error->message);
		return result;
	}

	g_free(png_data);

	/* Return success with path and file size */
	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "success");
	json_builder_add_boolean_value(builder, TRUE);
	json_builder_set_member_name(builder, "path");
	json_builder_add_string_value(builder, path);
	json_builder_set_member_name(builder, "bytes");
	json_builder_add_int_value(builder, (gint64)png_len);
	json_builder_end_object(builder);

	gen = json_generator_new();
	json_generator_set_root(gen, json_builder_get_root(builder));
	json_str = json_generator_to_data(gen, NULL);
	g_object_unref(gen);
	g_object_unref(builder);

	result = mcp_tool_result_new(FALSE);
	mcp_tool_result_add_text(result, json_str);
	g_free(json_str);

	return result;
}

/* ===== Tool Registration ===== */

void
gst_mcp_tools_screenshot_register(McpServer *server, GstMcpModule *self)
{
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	if (self->tool_screenshot) {
		/* screenshot: returns base64 PNG over MCP */
		tool = mcp_tool_new("screenshot",
			"Capture the terminal display as a PNG image. "
			"Returns a base64-encoded PNG of the current "
			"terminal window contents.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"properties\":{}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool,
			handle_screenshot, self, NULL);
		g_object_unref(tool);

		/* save_screenshot: writes PNG to a file path */
		tool = mcp_tool_new("save_screenshot",
			"Capture the terminal display and save it as a "
			"PNG file at the specified path.");
		mcp_tool_set_read_only_hint(tool, FALSE);
		mcp_tool_set_destructive_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"required\":[\"path\"],"
			"\"properties\":{"
			"\"path\":{\"type\":\"string\","
			"\"description\":\"File path to write the PNG to\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool,
			handle_save_screenshot, self, NULL);
		g_object_unref(tool);
	}
}
