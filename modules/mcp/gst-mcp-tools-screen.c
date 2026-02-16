/*
 * gst-mcp-tools-screen.c - Screen reading MCP tools
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tools: read_screen, read_scrollback, search_scrollback,
 *        get_cursor_position, get_cell_attributes
 */

#include "gst-mcp-tools.h"
#include "gst-mcp-module.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/boxed/gst-glyph.h"
#include "../../src/boxed/gst-cursor.h"
#include "../../src/module/gst-module-manager.h"
#include "../../modules/scrollback/gst-scrollback-module.h"

/* ===== read_screen ===== */

/*
 * handle_read_screen:
 *
 * Reads the visible terminal screen content as text.
 * Optionally includes per-glyph attributes (bold, fg, bg, etc.)
 * when include_attributes is true.
 */
static McpToolResult *
handle_read_screen(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstTerminal *term;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	McpToolResult *result;
	gint rows, cols, y;
	gboolean include_attrs;

	(void)server;
	(void)name;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Terminal not available");
		return result;
	}

	include_attrs = FALSE;
	if (arguments != NULL && json_object_has_member(arguments, "include_attributes")) {
		include_attrs = json_object_get_boolean_member(arguments, "include_attributes");
	}

	gst_terminal_get_size(term, &cols, &rows);

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "rows");
	json_builder_add_int_value(builder, rows);
	json_builder_set_member_name(builder, "cols");
	json_builder_add_int_value(builder, cols);
	json_builder_set_member_name(builder, "lines");
	json_builder_begin_array(builder);

	for (y = 0; y < rows; y++) {
		GstLine *line;
		g_autofree gchar *text = NULL;

		line = gst_terminal_get_line(term, y);
		if (line == NULL) {
			json_builder_add_string_value(builder, "");
			continue;
		}

		text = gst_line_to_string(line);

		if (!include_attrs) {
			json_builder_add_string_value(builder, text != NULL ? text : "");
		} else {
			/* With attributes: emit object per line */
			gint x;

			json_builder_begin_object(builder);
			json_builder_set_member_name(builder, "text");
			json_builder_add_string_value(builder, text != NULL ? text : "");
			json_builder_set_member_name(builder, "cells");
			json_builder_begin_array(builder);

			for (x = 0; x < cols; x++) {
				const GstGlyph *g;
				gchar buf[8];
				gint len;

				g = gst_line_get_glyph_const(line, x);
				if (g == NULL) {
					break;
				}

				len = g_unichar_to_utf8(g->rune, buf);
				buf[len] = '\0';

				json_builder_begin_object(builder);
				json_builder_set_member_name(builder, "char");
				json_builder_add_string_value(builder, buf);
				json_builder_set_member_name(builder, "fg");
				json_builder_add_int_value(builder, g->fg);
				json_builder_set_member_name(builder, "bg");
				json_builder_add_int_value(builder, g->bg);
				json_builder_set_member_name(builder, "bold");
				json_builder_add_boolean_value(builder,
					gst_glyph_has_attr(g, GST_GLYPH_ATTR_BOLD));
				json_builder_set_member_name(builder, "italic");
				json_builder_add_boolean_value(builder,
					gst_glyph_has_attr(g, GST_GLYPH_ATTR_ITALIC));
				json_builder_set_member_name(builder, "underline");
				json_builder_add_boolean_value(builder,
					gst_glyph_has_attr(g, GST_GLYPH_ATTR_UNDERLINE));
				json_builder_set_member_name(builder, "reverse");
				json_builder_add_boolean_value(builder,
					gst_glyph_has_attr(g, GST_GLYPH_ATTR_REVERSE));
				json_builder_end_object(builder);
			}

			json_builder_end_array(builder);
			json_builder_end_object(builder);
		}
	}

	json_builder_end_array(builder);
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

/* ===== read_scrollback ===== */

/*
 * handle_read_scrollback:
 *
 * Reads lines from the scrollback buffer. Requires the scrollback
 * module to be loaded and active. Returns lines as text with an
 * offset and total count.
 */
static McpToolResult *
handle_read_scrollback(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstModule *sb_mod;
	GstScrollbackModule *sb;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	McpToolResult *result;
	gint offset, count, total, i;

	(void)server;
	(void)name;
	(void)user_data;

	mgr = gst_module_manager_get_default();
	sb_mod = gst_module_manager_get_module(mgr, "scrollback");
	if (sb_mod == NULL || !gst_module_is_active(sb_mod)) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result,
			"Scrollback module not loaded or not active");
		return result;
	}

	sb = GST_SCROLLBACK_MODULE(sb_mod);
	total = gst_scrollback_module_get_count(sb);

	offset = 0;
	count = 100;
	if (arguments != NULL) {
		if (json_object_has_member(arguments, "offset")) {
			offset = (gint)json_object_get_int_member(arguments, "offset");
		}
		if (json_object_has_member(arguments, "count")) {
			count = (gint)json_object_get_int_member(arguments, "count");
		}
	}

	/* Clamp */
	if (offset < 0) { offset = 0; }
	if (offset > total) { offset = total; }
	if (count < 1) { count = 1; }
	if (count > 1000) { count = 1000; }
	if (offset + count > total) { count = total - offset; }

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "total_lines");
	json_builder_add_int_value(builder, total);
	json_builder_set_member_name(builder, "offset");
	json_builder_add_int_value(builder, offset);
	json_builder_set_member_name(builder, "count");
	json_builder_add_int_value(builder, count);
	json_builder_set_member_name(builder, "lines");
	json_builder_begin_array(builder);

	for (i = 0; i < count; i++) {
		const GstGlyph *glyphs;
		gint ncols, x;
		GString *line_str;

		glyphs = gst_scrollback_module_get_line_glyphs(sb, offset + i, &ncols);
		if (glyphs == NULL) {
			json_builder_add_string_value(builder, "");
			continue;
		}

		/* Convert glyphs to UTF-8 string */
		line_str = g_string_new(NULL);
		for (x = 0; x < ncols; x++) {
			gchar buf[8];
			gint len;

			if (gst_glyph_is_dummy(&glyphs[x])) {
				continue;
			}
			len = g_unichar_to_utf8(glyphs[x].rune, buf);
			buf[len] = '\0';
			g_string_append(line_str, buf);
		}

		json_builder_add_string_value(builder, line_str->str);
		g_string_free(line_str, TRUE);
	}

	json_builder_end_array(builder);
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

/* ===== search_scrollback ===== */

/*
 * handle_search_scrollback:
 *
 * Searches scrollback buffer lines with a regex pattern.
 * Returns matching lines with their indices and match positions.
 */
static McpToolResult *
handle_search_scrollback(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstModule *sb_mod;
	GstScrollbackModule *sb;
	g_autoptr(GRegex) regex = NULL;
	g_autoptr(GError) error = NULL;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	McpToolResult *result;
	const gchar *pattern;
	gint max_results, total, i, found;

	(void)server;
	(void)name;
	(void)user_data;

	if (arguments == NULL || !json_object_has_member(arguments, "pattern")) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Missing required parameter: pattern");
		return result;
	}

	pattern = json_object_get_string_member(arguments, "pattern");

	regex = g_regex_new(pattern, G_REGEX_CASELESS, 0, &error);
	if (regex == NULL) {
		gchar *msg;

		msg = g_strdup_printf("Invalid regex: %s", error->message);
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, msg);
		g_free(msg);
		return result;
	}

	mgr = gst_module_manager_get_default();
	sb_mod = gst_module_manager_get_module(mgr, "scrollback");
	if (sb_mod == NULL || !gst_module_is_active(sb_mod)) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result,
			"Scrollback module not loaded or not active");
		return result;
	}

	sb = GST_SCROLLBACK_MODULE(sb_mod);
	total = gst_scrollback_module_get_count(sb);

	max_results = 50;
	if (arguments != NULL && json_object_has_member(arguments, "max_results")) {
		max_results = (gint)json_object_get_int_member(arguments, "max_results");
	}
	if (max_results < 1) { max_results = 1; }
	if (max_results > 500) { max_results = 500; }

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "pattern");
	json_builder_add_string_value(builder, pattern);
	json_builder_set_member_name(builder, "total_lines");
	json_builder_add_int_value(builder, total);
	json_builder_set_member_name(builder, "matches");
	json_builder_begin_array(builder);

	found = 0;
	for (i = 0; i < total && found < max_results; i++) {
		const GstGlyph *glyphs;
		gint ncols, x;
		GString *line_str;
		GMatchInfo *match_info;

		glyphs = gst_scrollback_module_get_line_glyphs(sb, i, &ncols);
		if (glyphs == NULL) {
			continue;
		}

		/* Build line string */
		line_str = g_string_new(NULL);
		for (x = 0; x < ncols; x++) {
			gchar buf[8];
			gint len;

			if (gst_glyph_is_dummy(&glyphs[x])) {
				continue;
			}
			len = g_unichar_to_utf8(glyphs[x].rune, buf);
			buf[len] = '\0';
			g_string_append(line_str, buf);
		}

		if (g_regex_match(regex, line_str->str, 0, &match_info)) {
			gint start, end;

			g_match_info_fetch_pos(match_info, 0, &start, &end);

			json_builder_begin_object(builder);
			json_builder_set_member_name(builder, "line_index");
			json_builder_add_int_value(builder, i);
			json_builder_set_member_name(builder, "text");
			json_builder_add_string_value(builder, line_str->str);
			json_builder_set_member_name(builder, "match_start");
			json_builder_add_int_value(builder, start);
			json_builder_set_member_name(builder, "match_end");
			json_builder_add_int_value(builder, end);
			json_builder_end_object(builder);

			found++;
		}
		g_match_info_free(match_info);
		g_string_free(line_str, TRUE);
	}

	json_builder_end_array(builder);
	json_builder_set_member_name(builder, "match_count");
	json_builder_add_int_value(builder, found);
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

/* ===== get_cursor_position ===== */

/*
 * handle_get_cursor_position:
 *
 * Returns the cursor position (row, col), the character under the
 * cursor, visibility state, and cursor shape.
 */
static McpToolResult *
handle_get_cursor_position(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstTerminal *term;
	GstCursor *cursor;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	gchar buf[8];
	gint len;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Terminal not available");
		return result;
	}

	cursor = gst_terminal_get_cursor(term);

	len = g_unichar_to_utf8(cursor->glyph.rune, buf);
	buf[len] = '\0';

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "row");
	json_builder_add_int_value(builder, cursor->y);
	json_builder_set_member_name(builder, "col");
	json_builder_add_int_value(builder, cursor->x);
	json_builder_set_member_name(builder, "character");
	json_builder_add_string_value(builder, buf);
	json_builder_set_member_name(builder, "visible");
	json_builder_add_boolean_value(builder, gst_cursor_is_visible(cursor));
	json_builder_set_member_name(builder, "shape");
	switch (cursor->shape) {
	case GST_CURSOR_SHAPE_BLOCK:
		json_builder_add_string_value(builder, "block");
		break;
	case GST_CURSOR_SHAPE_UNDERLINE:
		json_builder_add_string_value(builder, "underline");
		break;
	case GST_CURSOR_SHAPE_BAR:
		json_builder_add_string_value(builder, "bar");
		break;
	default:
		json_builder_add_string_value(builder, "block");
		break;
	}
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

/* ===== get_cell_attributes ===== */

/*
 * handle_get_cell_attributes:
 *
 * Returns detailed glyph attributes at a specific (row, col) position:
 * character, fg/bg color, bold, italic, underline, reverse, etc.
 */
static McpToolResult *
handle_get_cell_attributes(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstTerminal *term;
	const GstGlyph *g;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	gchar buf[8];
	gint len, row, col, cols, rows;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)user_data;

	if (arguments == NULL ||
		!json_object_has_member(arguments, "row") ||
		!json_object_has_member(arguments, "col"))
	{
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result,
			"Missing required parameters: row, col");
		return result;
	}

	row = (gint)json_object_get_int_member(arguments, "row");
	col = (gint)json_object_get_int_member(arguments, "col");

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Terminal not available");
		return result;
	}

	gst_terminal_get_size(term, &cols, &rows);
	if (row < 0 || row >= rows || col < 0 || col >= cols) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Row or column out of bounds");
		return result;
	}

	g = gst_terminal_get_glyph(term, col, row);
	if (g == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Could not read glyph at position");
		return result;
	}

	len = g_unichar_to_utf8(g->rune, buf);
	buf[len] = '\0';

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "row");
	json_builder_add_int_value(builder, row);
	json_builder_set_member_name(builder, "col");
	json_builder_add_int_value(builder, col);
	json_builder_set_member_name(builder, "character");
	json_builder_add_string_value(builder, buf);
	json_builder_set_member_name(builder, "codepoint");
	json_builder_add_int_value(builder, g->rune);
	json_builder_set_member_name(builder, "fg");
	json_builder_add_int_value(builder, g->fg);
	json_builder_set_member_name(builder, "bg");
	json_builder_add_int_value(builder, g->bg);
	json_builder_set_member_name(builder, "bold");
	json_builder_add_boolean_value(builder, gst_glyph_has_attr(g, GST_GLYPH_ATTR_BOLD));
	json_builder_set_member_name(builder, "italic");
	json_builder_add_boolean_value(builder, gst_glyph_has_attr(g, GST_GLYPH_ATTR_ITALIC));
	json_builder_set_member_name(builder, "underline");
	json_builder_add_boolean_value(builder, gst_glyph_has_attr(g, GST_GLYPH_ATTR_UNDERLINE));
	json_builder_set_member_name(builder, "reverse");
	json_builder_add_boolean_value(builder, gst_glyph_has_attr(g, GST_GLYPH_ATTR_REVERSE));
	json_builder_set_member_name(builder, "struck");
	json_builder_add_boolean_value(builder, gst_glyph_has_attr(g, GST_GLYPH_ATTR_STRUCK));
	json_builder_set_member_name(builder, "invisible");
	json_builder_add_boolean_value(builder, gst_glyph_has_attr(g, GST_GLYPH_ATTR_INVISIBLE));
	json_builder_set_member_name(builder, "wide");
	json_builder_add_boolean_value(builder, gst_glyph_is_wide(g));
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
gst_mcp_tools_screen_register(McpServer *server, GstMcpModule *self)
{
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	if (self->tool_read_screen) {
		tool = mcp_tool_new("read_screen",
			"Read visible terminal screen content as text. "
			"Set include_attributes=true for per-cell color and style info.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"properties\":{"
			"\"include_attributes\":{\"type\":\"boolean\","
			"\"description\":\"Include per-cell fg/bg and style attributes\","
			"\"default\":false}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_read_screen, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_read_scrollback) {
		tool = mcp_tool_new("read_scrollback",
			"Read lines from the scrollback history buffer. "
			"Requires the scrollback module to be active.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"properties\":{"
			"\"offset\":{\"type\":\"integer\",\"description\":"
			"\"Line offset (0=most recent)\",\"default\":0},"
			"\"count\":{\"type\":\"integer\",\"description\":"
			"\"Number of lines to read (max 1000)\",\"default\":100}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_read_scrollback, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_search_scrollback) {
		tool = mcp_tool_new("search_scrollback",
			"Search scrollback buffer with a regex pattern. "
			"Requires the scrollback module to be active.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"required\":[\"pattern\"],\"properties\":{"
			"\"pattern\":{\"type\":\"string\",\"description\":"
			"\"Regex pattern to search for\"},"
			"\"max_results\":{\"type\":\"integer\",\"description\":"
			"\"Maximum matches to return (max 500)\",\"default\":50}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_search_scrollback, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_get_cursor_position) {
		tool = mcp_tool_new("get_cursor_position",
			"Get the current cursor position, character under cursor, "
			"visibility, and shape.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string("{\"type\":\"object\",\"properties\":{}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_get_cursor_position, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_get_cell_attributes) {
		tool = mcp_tool_new("get_cell_attributes",
			"Get detailed glyph attributes at a specific row and column: "
			"character, codepoint, fg/bg color, bold, italic, etc.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"required\":[\"row\",\"col\"],\"properties\":{"
			"\"row\":{\"type\":\"integer\",\"description\":\"Row (0-based)\"},"
			"\"col\":{\"type\":\"integer\",\"description\":\"Column (0-based)\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_get_cell_attributes, self, NULL);
		g_object_unref(tool);
	}
}
