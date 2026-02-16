/*
 * gst-mcp-tools-input.c - Input injection MCP tools
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tools: send_text, send_keys
 * These tools write directly to the PTY and are gated by
 * per-tool enable flags in the config (disabled by default).
 */

#include "gst-mcp-tools.h"
#include "gst-mcp-module.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <string.h>

#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-pty.h"
#include "../../src/module/gst-module-manager.h"

/* ===== send_text ===== */

/*
 * handle_send_text:
 *
 * Writes text directly to the PTY. The text appears as if
 * typed by the user. Use with caution.
 */
static McpToolResult *
handle_send_text(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstPty *pty;
	const gchar *text;
	gsize len;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)user_data;

	if (arguments == NULL || !json_object_has_member(arguments, "text")) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Missing required parameter: text");
		return result;
	}

	text = json_object_get_string_member(arguments, "text");
	len = strlen(text);

	mgr = gst_module_manager_get_default();
	pty = (GstPty *)gst_module_manager_get_pty(mgr);
	if (pty == NULL || !gst_pty_is_running(pty)) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "PTY not available or not running");
		return result;
	}

	gst_pty_write(pty, text, (gssize)len);

	{
		gchar *json_str;

		json_str = g_strdup_printf(
			"{\"success\":true,\"bytes_written\":%lu}",
			(unsigned long)len);
		result = mcp_tool_result_new(FALSE);
		mcp_tool_result_add_text(result, json_str);
		g_free(json_str);
	}

	return result;
}

/* ===== send_keys ===== */

/*
 * key_name_to_escape:
 *
 * Converts a key name (e.g., "Enter", "Ctrl+c", "Escape", "Up")
 * to the escape sequence string. Returns a static or allocated string.
 * Returns NULL for unrecognized keys.
 */
static const gchar *
key_name_to_escape(const gchar *key_name, gchar *buf, gsize buflen)
{
	/* Simple key name mappings */
	if (g_ascii_strcasecmp(key_name, "Enter") == 0 ||
		g_ascii_strcasecmp(key_name, "Return") == 0)
	{
		return "\r";
	}
	if (g_ascii_strcasecmp(key_name, "Tab") == 0) {
		return "\t";
	}
	if (g_ascii_strcasecmp(key_name, "Escape") == 0 ||
		g_ascii_strcasecmp(key_name, "Esc") == 0)
	{
		return "\033";
	}
	if (g_ascii_strcasecmp(key_name, "Backspace") == 0) {
		return "\177";
	}
	if (g_ascii_strcasecmp(key_name, "Space") == 0) {
		return " ";
	}

	/* Arrow keys */
	if (g_ascii_strcasecmp(key_name, "Up") == 0) {
		return "\033[A";
	}
	if (g_ascii_strcasecmp(key_name, "Down") == 0) {
		return "\033[B";
	}
	if (g_ascii_strcasecmp(key_name, "Right") == 0) {
		return "\033[C";
	}
	if (g_ascii_strcasecmp(key_name, "Left") == 0) {
		return "\033[D";
	}

	/* Navigation */
	if (g_ascii_strcasecmp(key_name, "Home") == 0) {
		return "\033[H";
	}
	if (g_ascii_strcasecmp(key_name, "End") == 0) {
		return "\033[F";
	}
	if (g_ascii_strcasecmp(key_name, "Page_Up") == 0 ||
		g_ascii_strcasecmp(key_name, "PageUp") == 0)
	{
		return "\033[5~";
	}
	if (g_ascii_strcasecmp(key_name, "Page_Down") == 0 ||
		g_ascii_strcasecmp(key_name, "PageDown") == 0)
	{
		return "\033[6~";
	}
	if (g_ascii_strcasecmp(key_name, "Insert") == 0) {
		return "\033[2~";
	}
	if (g_ascii_strcasecmp(key_name, "Delete") == 0) {
		return "\033[3~";
	}

	/* Ctrl+letter */
	if (g_str_has_prefix(key_name, "Ctrl+") ||
		g_str_has_prefix(key_name, "ctrl+"))
	{
		const gchar *letter;
		guchar ctrl_char;

		letter = key_name + 5;
		if (strlen(letter) == 1 && g_ascii_isalpha(letter[0])) {
			ctrl_char = (guchar)(g_ascii_toupper(letter[0]) - 'A' + 1);
			buf[0] = (gchar)ctrl_char;
			buf[1] = '\0';
			return buf;
		}
	}

	return NULL;
}

/*
 * handle_send_keys:
 *
 * Sends key sequences to the PTY. Accepts a space-separated list
 * of key names: "Enter", "Ctrl+c", "Up", "Escape", etc.
 */
static McpToolResult *
handle_send_keys(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstPty *pty;
	const gchar *keys_str;
	gchar **tokens;
	gint i, sent;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)user_data;

	if (arguments == NULL || !json_object_has_member(arguments, "keys")) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Missing required parameter: keys");
		return result;
	}

	keys_str = json_object_get_string_member(arguments, "keys");

	mgr = gst_module_manager_get_default();
	pty = (GstPty *)gst_module_manager_get_pty(mgr);
	if (pty == NULL || !gst_pty_is_running(pty)) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "PTY not available or not running");
		return result;
	}

	tokens = g_strsplit(keys_str, " ", -1);
	sent = 0;

	for (i = 0; tokens[i] != NULL; i++) {
		const gchar *seq;
		gchar buf[8];

		if (tokens[i][0] == '\0') {
			continue;
		}

		seq = key_name_to_escape(tokens[i], buf, sizeof(buf));
		if (seq != NULL) {
			gst_pty_write(pty, seq, (gssize)strlen(seq));
			sent++;
		} else {
			g_warning("mcp: send_keys: unrecognized key '%s'", tokens[i]);
		}
	}

	g_strfreev(tokens);

	{
		gchar *json_str;

		json_str = g_strdup_printf(
			"{\"success\":true,\"keys_sent\":%d}", sent);
		result = mcp_tool_result_new(FALSE);
		mcp_tool_result_add_text(result, json_str);
		g_free(json_str);
	}

	return result;
}

/* ===== Tool Registration ===== */

void
gst_mcp_tools_input_register(McpServer *server, GstMcpModule *self)
{
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	if (self->tool_send_text) {
		tool = mcp_tool_new("send_text",
			"Write text directly to the terminal PTY. "
			"The text appears as if typed by the user. Use with caution.");
		mcp_tool_set_read_only_hint(tool, FALSE);
		mcp_tool_set_destructive_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"required\":[\"text\"],\"properties\":{"
			"\"text\":{\"type\":\"string\",\"description\":"
			"\"Text to write to the PTY\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_send_text, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_send_keys) {
		tool = mcp_tool_new("send_keys",
			"Send key sequences to the terminal PTY. "
			"Accepts space-separated key names: Enter, Ctrl+c, Escape, "
			"Up, Down, Left, Right, Tab, Backspace, Home, End, etc.");
		mcp_tool_set_read_only_hint(tool, FALSE);
		mcp_tool_set_destructive_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		schema = json_from_string(
			"{\"type\":\"object\",\"required\":[\"keys\"],\"properties\":{"
			"\"keys\":{\"type\":\"string\",\"description\":"
			"\"Space-separated key names (e.g. 'Ctrl+c Enter Up Up')\"}"
			"}}", NULL);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_send_keys, self, NULL);
		g_object_unref(tool);
	}
}
