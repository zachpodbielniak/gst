/*
 * gst-mcp-tools-process.c - Process awareness MCP tools
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tools: get_foreground_process, get_working_directory,
 *        is_shell_idle, get_pty_info
 *
 * Uses /proc filesystem to read process info from the PTY child.
 */

#include "gst-mcp-tools.h"
#include "gst-mcp-module.h"

#include <mcp.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <unistd.h>
#include <sys/types.h>
#include <termios.h>

#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-pty.h"
#include "../../src/module/gst-module-manager.h"

/*
 * get_fg_pid:
 *
 * Gets the foreground process group ID from the PTY fd.
 * Returns -1 on failure.
 */
static pid_t
get_fg_pid(gint pty_fd)
{
	pid_t fg;

	fg = tcgetpgrp(pty_fd);
	return fg;
}

/*
 * read_proc_file:
 *
 * Reads the contents of a /proc file into a string.
 * Returns a newly allocated string or NULL on failure.
 */
static gchar *
read_proc_file(const gchar *path)
{
	gchar *contents = NULL;
	gsize length = 0;

	if (!g_file_get_contents(path, &contents, &length, NULL)) {
		return NULL;
	}
	/* Strip trailing newline */
	g_strchomp(contents);
	return contents;
}

/* ===== get_foreground_process ===== */

/*
 * handle_get_foreground_process:
 *
 * Returns PID, command name, and full command line of the
 * foreground process in the terminal's PTY.
 */
static McpToolResult *
handle_get_foreground_process(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstPty *pty;
	pid_t fg_pid;
	gint pty_fd;
	g_autofree gchar *comm_path = NULL;
	g_autofree gchar *cmdline_path = NULL;
	g_autofree gchar *comm = NULL;
	g_autofree gchar *cmdline_raw = NULL;
	gchar *json_str;
	JsonBuilder *builder;
	JsonGenerator *gen;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	mgr = gst_module_manager_get_default();
	pty = (GstPty *)gst_module_manager_get_pty(mgr);
	if (pty == NULL || !gst_pty_is_running(pty)) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "PTY not available or not running");
		return result;
	}

	pty_fd = gst_pty_get_fd(pty);
	fg_pid = get_fg_pid(pty_fd);
	if (fg_pid < 0) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Could not determine foreground process");
		return result;
	}

	/* Read /proc/<pid>/comm */
	comm_path = g_strdup_printf("/proc/%d/comm", (int)fg_pid);
	comm = read_proc_file(comm_path);

	/* Read /proc/<pid>/cmdline (NUL-separated) */
	cmdline_path = g_strdup_printf("/proc/%d/cmdline", (int)fg_pid);
	{
		gchar *raw = NULL;
		gsize raw_len = 0;
		gsize i;

		if (g_file_get_contents(cmdline_path, &raw, &raw_len, NULL)) {
			/* Replace NUL separators with spaces */
			for (i = 0; i < raw_len; i++) {
				if (raw[i] == '\0') {
					raw[i] = ' ';
				}
			}
			g_strchomp(raw);
			cmdline_raw = raw;
		}
	}

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "pid");
	json_builder_add_int_value(builder, fg_pid);
	json_builder_set_member_name(builder, "command");
	json_builder_add_string_value(builder, comm != NULL ? comm : "unknown");
	json_builder_set_member_name(builder, "cmdline");
	json_builder_add_string_value(builder, cmdline_raw != NULL ? cmdline_raw : "");
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

/* ===== get_working_directory ===== */

/*
 * handle_get_working_directory:
 *
 * Returns the current working directory of the foreground process
 * by reading /proc/<pid>/cwd symlink.
 */
static McpToolResult *
handle_get_working_directory(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstPty *pty;
	pid_t fg_pid;
	gint pty_fd;
	g_autofree gchar *cwd_path = NULL;
	g_autofree gchar *cwd = NULL;
	gchar *json_str;
	JsonBuilder *builder;
	JsonGenerator *gen;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	mgr = gst_module_manager_get_default();
	pty = (GstPty *)gst_module_manager_get_pty(mgr);
	if (pty == NULL || !gst_pty_is_running(pty)) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "PTY not available or not running");
		return result;
	}

	pty_fd = gst_pty_get_fd(pty);
	fg_pid = get_fg_pid(pty_fd);
	if (fg_pid < 0) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Could not determine foreground process");
		return result;
	}

	cwd_path = g_strdup_printf("/proc/%d/cwd", (int)fg_pid);
	cwd = g_file_read_link(cwd_path, NULL);

	if (cwd == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result,
			"Could not read working directory (permission denied?)");
		return result;
	}

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "pid");
	json_builder_add_int_value(builder, fg_pid);
	json_builder_set_member_name(builder, "path");
	json_builder_add_string_value(builder, cwd);
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

/* ===== is_shell_idle ===== */

/*
 * handle_is_shell_idle:
 *
 * Checks whether the shell is idle (at a prompt) by comparing
 * the foreground process group with the shell PID.
 */
static McpToolResult *
handle_is_shell_idle(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstPty *pty;
	pid_t shell_pid, fg_pid;
	gint pty_fd;
	gboolean idle;
	gchar *json_str;
	JsonBuilder *builder;
	JsonGenerator *gen;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	mgr = gst_module_manager_get_default();
	pty = (GstPty *)gst_module_manager_get_pty(mgr);
	if (pty == NULL || !gst_pty_is_running(pty)) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "PTY not available or not running");
		return result;
	}

	shell_pid = gst_pty_get_child_pid(pty);
	pty_fd = gst_pty_get_fd(pty);
	fg_pid = get_fg_pid(pty_fd);

	if (fg_pid < 0) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Could not determine foreground process");
		return result;
	}

	idle = (fg_pid == shell_pid);

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "idle");
	json_builder_add_boolean_value(builder, idle);
	json_builder_set_member_name(builder, "shell_pid");
	json_builder_add_int_value(builder, shell_pid);
	json_builder_set_member_name(builder, "foreground_pid");
	json_builder_add_int_value(builder, fg_pid);
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

/* ===== get_pty_info ===== */

/*
 * handle_get_pty_info:
 *
 * Returns PTY information: terminal dimensions, child PID,
 * running status, and PTY file descriptor.
 */
static McpToolResult *
handle_get_pty_info(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	GstModuleManager *mgr;
	GstTerminal *term;
	GstPty *pty;
	gint cols, rows;
	gchar *json_str;
	JsonBuilder *builder;
	JsonGenerator *gen;
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	mgr = gst_module_manager_get_default();
	term = (GstTerminal *)gst_module_manager_get_terminal(mgr);
	pty = (GstPty *)gst_module_manager_get_pty(mgr);

	if (term == NULL || pty == NULL) {
		result = mcp_tool_result_new(TRUE);
		mcp_tool_result_add_text(result, "Terminal or PTY not available");
		return result;
	}

	gst_terminal_get_size(term, &cols, &rows);

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "cols");
	json_builder_add_int_value(builder, cols);
	json_builder_set_member_name(builder, "rows");
	json_builder_add_int_value(builder, rows);
	json_builder_set_member_name(builder, "child_pid");
	json_builder_add_int_value(builder, gst_pty_get_child_pid(pty));
	json_builder_set_member_name(builder, "running");
	json_builder_add_boolean_value(builder, gst_pty_is_running(pty));
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
gst_mcp_tools_process_register(McpServer *server, GstMcpModule *self)
{
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	schema = json_from_string("{\"type\":\"object\",\"properties\":{}}", NULL);

	if (self->tool_get_foreground_process) {
		tool = mcp_tool_new("get_foreground_process",
			"Get the PID, command name, and command line of the "
			"foreground process running in the terminal.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_get_foreground_process, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_get_working_directory) {
		tool = mcp_tool_new("get_working_directory",
			"Get the current working directory of the foreground "
			"process in the terminal.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_get_working_directory, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_is_shell_idle) {
		tool = mcp_tool_new("is_shell_idle",
			"Check whether the shell is idle (at a prompt) or "
			"a command is running. Compares the foreground process "
			"group with the shell PID.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_is_shell_idle, self, NULL);
		g_object_unref(tool);
	}

	if (self->tool_get_pty_info) {
		tool = mcp_tool_new("get_pty_info",
			"Get PTY information: terminal dimensions (cols, rows), "
			"child PID, and running status.");
		mcp_tool_set_read_only_hint(tool, TRUE);
		mcp_tool_set_open_world_hint(tool, FALSE);
		mcp_tool_set_input_schema(tool, schema);
		mcp_server_add_tool(server, tool, handle_get_pty_info, self, NULL);
		g_object_unref(tool);
	}
}
