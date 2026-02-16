/*
 * test-mcp-module.c - MCP module unit tests
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests module lifecycle, config parsing, and tool registration.
 * Requires MCP=1 and mcp-glib to be built.
 */

#include <glib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>

#include <mcp.h>

#include "../modules/mcp/gst-mcp-module.h"
#include "../src/module/gst-module.h"
#include "../src/module/gst-module-manager.h"

/* ===== Module type tests ===== */

/*
 * test_mcp_module_type:
 *
 * Verifies the GstMcpModule GType is registered and
 * creates a valid GObject instance.
 */
static void
test_mcp_module_type(void)
{
	GType type;

	type = gst_mcp_module_get_type();
	g_assert_true(type != G_TYPE_INVALID);
	g_assert_true(g_type_is_a(type, GST_TYPE_MODULE));
}

/*
 * test_mcp_module_new:
 *
 * Verifies creating a new MCP module instance.
 */
static void
test_mcp_module_new(void)
{
	GstMcpModule *mod;

	mod = g_object_new(GST_TYPE_MCP_MODULE, NULL);
	g_assert_nonnull(mod);
	g_assert_true(GST_IS_MCP_MODULE(mod));
	g_assert_true(GST_IS_MODULE(mod));
	g_object_unref(mod);
}

/* ===== Module vfunc tests ===== */

/*
 * test_mcp_module_name:
 *
 * Verifies that the module reports its name as "mcp".
 */
static void
test_mcp_module_name(void)
{
	GstMcpModule *mod;
	const gchar *name;

	mod = g_object_new(GST_TYPE_MCP_MODULE, NULL);
	name = gst_module_get_name(GST_MODULE(mod));
	g_assert_cmpstr(name, ==, "mcp");
	g_object_unref(mod);
}

/*
 * test_mcp_module_description:
 *
 * Verifies that the module has a non-empty description.
 */
static void
test_mcp_module_description(void)
{
	GstMcpModule *mod;
	const gchar *desc;

	mod = g_object_new(GST_TYPE_MCP_MODULE, NULL);
	desc = gst_module_get_description(GST_MODULE(mod));
	g_assert_nonnull(desc);
	g_assert_true(strlen(desc) > 0);
	g_object_unref(mod);
}

/* ===== Default values tests ===== */

/*
 * test_mcp_module_defaults:
 *
 * Verifies all per-tool flags default to FALSE and
 * transport defaults are correct.
 */
static void
test_mcp_module_defaults(void)
{
	GstMcpModule *mod;

	mod = g_object_new(GST_TYPE_MCP_MODULE, NULL);

	/* Transport defaults */
	g_assert_cmpstr(mod->transport_type, ==, "http");
	g_assert_cmpuint(mod->http_port, ==, 8808);
	g_assert_cmpstr(mod->http_host, ==, "127.0.0.1");

	/* All tools should default to FALSE */
	g_assert_false(mod->tool_read_screen);
	g_assert_false(mod->tool_read_scrollback);
	g_assert_false(mod->tool_search_scrollback);
	g_assert_false(mod->tool_get_cursor_position);
	g_assert_false(mod->tool_get_cell_attributes);
	g_assert_false(mod->tool_get_foreground_process);
	g_assert_false(mod->tool_get_working_directory);
	g_assert_false(mod->tool_is_shell_idle);
	g_assert_false(mod->tool_get_pty_info);
	g_assert_false(mod->tool_list_detected_urls);
	g_assert_false(mod->tool_get_config);
	g_assert_false(mod->tool_list_modules);
	g_assert_false(mod->tool_set_config);
	g_assert_false(mod->tool_toggle_module);
	g_assert_false(mod->tool_get_window_info);
	g_assert_false(mod->tool_set_window_title);
	g_assert_false(mod->tool_send_text);
	g_assert_false(mod->tool_send_keys);

	/* Server should be NULL before activation */
	g_assert_null(mod->server);
	g_assert_null(mod->cancellable);

	g_object_unref(mod);
}

/* ===== Module entry point test ===== */

/*
 * test_mcp_module_register:
 *
 * Verifies the gst_module_register entry point returns
 * the correct GType.
 */
static void
test_mcp_module_register(void)
{
	GType type;

	type = gst_module_register();
	g_assert_true(type == GST_TYPE_MCP_MODULE);
}

/* ===== Module manager integration tests ===== */

/*
 * test_mcp_module_manager_register:
 *
 * Verifies the MCP module can be registered with the module manager.
 */
static void
test_mcp_module_manager_register(void)
{
	GstModuleManager *mgr;
	GstMcpModule *mod;
	GstModule *found;

	mgr = gst_module_manager_new();
	mod = g_object_new(GST_TYPE_MCP_MODULE, NULL);

	gst_module_manager_register(mgr, GST_MODULE(mod));

	found = gst_module_manager_get_module(mgr, "mcp");
	g_assert_nonnull(found);
	g_assert_true(found == GST_MODULE(mod));

	g_object_unref(mgr);
}

/*
 * test_mcp_module_inactive_by_default:
 *
 * Verifies the module starts inactive.
 */
static void
test_mcp_module_inactive_by_default(void)
{
	GstMcpModule *mod;

	mod = g_object_new(GST_TYPE_MCP_MODULE, NULL);
	g_assert_false(gst_module_is_active(GST_MODULE(mod)));
	g_object_unref(mod);
}

/* ===== McpServer creation test ===== */

/*
 * test_mcp_server_new:
 *
 * Verifies that an McpServer can be created with the expected
 * name and version format (validates mcp-glib linkage works).
 */
static void
test_mcp_server_new(void)
{
	McpServer *server;

	server = mcp_server_new("gst-terminal", "0.1.0");
	g_assert_nonnull(server);
	g_assert_true(MCP_IS_SERVER(server));
	g_object_unref(server);
}

/* ===== Tool creation test ===== */

/*
 * test_mcp_tool_creation:
 *
 * Verifies McpTool objects can be created with schemas.
 * This validates the JSON schema parsing used by all tools.
 */
static void
test_mcp_tool_creation(void)
{
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	tool = mcp_tool_new("test_tool", "A test tool description");
	g_assert_nonnull(tool);

	mcp_tool_set_read_only_hint(tool, TRUE);
	mcp_tool_set_open_world_hint(tool, FALSE);

	schema = json_from_string(
		"{\"type\":\"object\",\"properties\":{"
		"\"arg1\":{\"type\":\"string\",\"description\":\"Test arg\"}"
		"}}", NULL);
	g_assert_nonnull(schema);

	mcp_tool_set_input_schema(tool, schema);
	g_object_unref(tool);
}

/*
 * test_mcp_tool_registration:
 *
 * Verifies that tools can be added to a server with handlers.
 */
static McpToolResult *
dummy_handler(
	McpServer   *server,
	const gchar *name,
	JsonObject  *arguments,
	gpointer     user_data
){
	McpToolResult *result;

	(void)server;
	(void)name;
	(void)arguments;
	(void)user_data;

	result = mcp_tool_result_new(FALSE);
	mcp_tool_result_add_text(result, "{\"test\":true}");
	return result;
}

static void
test_mcp_tool_registration(void)
{
	McpServer *server;
	McpTool *tool;
	g_autoptr(JsonNode) schema = NULL;

	server = mcp_server_new("test-server", "1.0.0");
	tool = mcp_tool_new("test_tool", "desc");

	schema = json_from_string("{\"type\":\"object\",\"properties\":{}}", NULL);
	mcp_tool_set_input_schema(tool, schema);

	mcp_server_add_tool(server, tool, dummy_handler, NULL, NULL);

	g_object_unref(tool);
	g_object_unref(server);
}

/* ===== Main ===== */

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* Module type tests */
	g_test_add_func("/mcp/module/type", test_mcp_module_type);
	g_test_add_func("/mcp/module/new", test_mcp_module_new);
	g_test_add_func("/mcp/module/name", test_mcp_module_name);
	g_test_add_func("/mcp/module/description", test_mcp_module_description);
	g_test_add_func("/mcp/module/defaults", test_mcp_module_defaults);
	g_test_add_func("/mcp/module/register-entry", test_mcp_module_register);
	g_test_add_func("/mcp/module/inactive-by-default",
		test_mcp_module_inactive_by_default);

	/* Module manager integration */
	g_test_add_func("/mcp/module/manager-register",
		test_mcp_module_manager_register);

	/* mcp-glib linkage validation */
	g_test_add_func("/mcp/server/new", test_mcp_server_new);
	g_test_add_func("/mcp/tool/creation", test_mcp_tool_creation);
	g_test_add_func("/mcp/tool/registration", test_mcp_tool_registration);

	return g_test_run();
}
