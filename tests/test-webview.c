/*
 * test-webview.c - Webview authentication and shutdown regressions
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <glib.h>
#include "../modules/webview/gst-webview-server.c"
#include "../modules/webview/gst-webview-module.c"

/* Only explicit auth=none may disable authentication. */
static void
test_auth_unknown_mode(void)
{
	GstWebviewServer srv = { 0 };
	g_autoptr(GstWebviewModule) module = NULL;
	g_autoptr(SoupServerMessage) message = NULL;

	module = g_object_new(GST_TYPE_WEBVIEW_MODULE, NULL);
	message = g_object_new(SOUP_TYPE_SERVER_MESSAGE, NULL);
	srv.module = module;
	g_assert_true(check_auth_msg(&srv, message));
	g_free(module->auth_mode);
	module->auth_mode = g_strdup("invalid-mode");
	g_assert_false(check_auth_msg(&srv, message));
}

/* Asynchronous close must not leave callbacks pointing at a freed server. */
static void
test_shutdown_disconnects_clients(void)
{
	GstWebviewServer *srv;
	g_autoptr(GInputStream) input = NULL;
	g_autoptr(GOutputStream) output = NULL;
	g_autoptr(GIOStream) stream = NULL;
	g_autoptr(SoupWebsocketConnection) connection = NULL;
	g_autoptr(GUri) uri = NULL;
	g_autoptr(GError) error = NULL;
	gulong closed_handler;
	gulong message_handler;

	/* A pollable memory stream exercises the same asynchronous signal lifetime. */
	input = g_memory_input_stream_new();
	output = g_memory_output_stream_new_resizable();
	stream = g_simple_io_stream_new(input, output);
	uri = g_uri_parse("ws://localhost/ws", G_URI_FLAGS_NONE, &error);
	g_assert_no_error(error);
	connection = soup_websocket_connection_new(G_IO_STREAM(stream), uri,
		SOUP_WEBSOCKET_CONNECTION_SERVER, NULL, NULL, NULL);
	srv = g_new0(GstWebviewServer, 1);
	srv->ws_clients = g_ptr_array_new();
	g_ptr_array_add(srv->ws_clients, g_object_ref(connection));
	closed_handler = g_signal_connect(connection, "closed",
		G_CALLBACK(on_ws_closed), srv);
	message_handler = g_signal_connect(connection, "message",
		G_CALLBACK(on_ws_message), srv);
	gst_webview_server_free(srv);
	g_assert_false(g_signal_handler_is_connected(connection, closed_handler));
	g_assert_false(g_signal_handler_is_connected(connection, message_handler));
	while (g_main_context_iteration(NULL, FALSE)) { }
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/webview/auth/unknown-mode", test_auth_unknown_mode);
	g_test_add_func("/webview/shutdown/disconnects-clients", test_shutdown_disconnects_clients);
	return g_test_run();
}
