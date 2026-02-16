/*
 * gst-mcp-tools-url.c - URL detection MCP tool
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tool: list_detected_urls
 * Scans visible terminal screen for URLs using regex.
 * Independent of the urlclick module.
 */

#include "gst-mcp-tools.h"
#include "gst-mcp-module.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"
#include "../../src/module/gst-module-manager.h"

/* Default URL regex */
#define DEFAULT_URL_REGEX \
	"(https?|ftp|file)://[\\w\\-_.~:/?#\\[\\]@!$&'()*+,;=%]+"

/* ===== list_detected_urls ===== */

/*
 * handle_list_detected_urls:
 *
 * Scans the visible terminal screen for URLs using a regex pattern.
 * Returns all found URLs with their row and column positions.
 * An optional regex parameter overrides the default URL pattern.
 */
static McpToolResult *
handle_list_detected_urls(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstTerminal *term;
	g_autoptr(GRegex) regex = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *pattern;
	JsonBuilder *builder;
	JsonGenerator *gen;
	gchar *json_str;
	McpToolResult *result;
	gint rows, cols, y, url_count;

	(void)server;
	(void)name;
	(void)user_data;

	/* Use custom regex or default */
	pattern = DEFAULT_URL_REGEX;
	if (arguments != NULL && json_object_has_member(arguments, "regex")) {
		pattern = json_object_get_string_member(arguments, "regex");
	}

	regex = g_regex_new(pattern, 0, 0, &error);
	if (regex == NULL) {
		gchar *msg;

		msg = g_strdup_printf("Invalid regex: %s", error->message);
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, msg);
		g_free(msg);
		return result;
	}

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	if (term == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Terminal not available");
		return result;
	}

	gst_terminal_get_size(term, &cols, &rows);

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "urls");
	json_builder_begin_array(builder);

	url_count = 0;
	for (y = 0; y < rows; y++) {
		GstLine *line;
		g_autofree gchar *text = NULL;
		GMatchInfo *match_info;

		line = gst_terminal_get_line(term, y);
		if (line == NULL) {
			continue;
		}

		text = gst_line_to_string(line);
		if (text == NULL || text[0] == '\0') {
			continue;
		}

		if (g_regex_match(regex, text, 0, &match_info)) {
			do {
				gint start, end;
				g_autofree gchar *url = NULL;

				url = g_match_info_fetch(match_info, 0);
				g_match_info_fetch_pos(match_info, 0, &start, &end);

				json_builder_begin_object(builder);
				json_builder_set_member_name(builder, "url");
				json_builder_add_string_value(builder, url);
				json_builder_set_member_name(builder, "row");
				json_builder_add_int_value(builder, y);
				json_builder_set_member_name(builder, "start_col");
				json_builder_add_int_value(builder, start);
				json_builder_set_member_name(builder, "end_col");
				json_builder_add_int_value(builder, end);
				json_builder_end_object(builder);

				url_count++;
			} while (g_match_info_next(match_info, NULL));
		}
		g_match_info_free(match_info);
	}

	json_builder_end_array(builder);
	json_builder_set_member_name(builder, "count");
	json_builder_add_int_value(builder, url_count);
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
gst_mcp_tools_url_register(McpServer *server, GstMcpModule *self)
{
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	if (self->tool_list_detected_urls) {
		tool = mcp_tool_new("list_detected_urls",
			"Scan the visible terminal screen for URLs. "
			"Returns all detected URLs with their row and column positions. "
			"An optional regex parameter overrides the default URL pattern.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"properties\":{"
			"\"regex\":{\"type\":\"string\",\"description\":"
			"\"Custom URL regex (overrides default)\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_list_detected_urls, self, NULL);
		g_object_unref(tool);
	}
}
