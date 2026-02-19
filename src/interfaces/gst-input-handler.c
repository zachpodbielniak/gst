/*
 * gst-input-handler.c
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for handling keyboard and mouse input.
 */

#include "gst-input-handler.h"

G_DEFINE_INTERFACE(GstInputHandler, gst_input_handler, G_TYPE_OBJECT)

static void
gst_input_handler_default_init(GstInputHandlerInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

/**
 * gst_input_handler_handle_key_event:
 * @self: A #GstInputHandler instance.
 * @keyval: The key value.
 * @keycode: The hardware keycode.
 * @state: The modifier state.
 *
 * Handles a keyboard event.
 *
 * Returns: %TRUE if the event was handled, %FALSE to pass through.
 */
gboolean
gst_input_handler_handle_key_event(GstInputHandler *self,
                                   guint            keyval,
                                   guint            keycode,
                                   guint            state)
{
	GstInputHandlerInterface *iface;

	g_return_val_if_fail(GST_IS_INPUT_HANDLER(self), FALSE);

	iface = GST_INPUT_HANDLER_GET_IFACE(self);
	g_return_val_if_fail(iface->handle_key_event != NULL, FALSE);

	return iface->handle_key_event(self, keyval, keycode, state);
}

/**
 * gst_input_handler_handle_mouse_event:
 * @self: A #GstInputHandler instance.
 * @button: The mouse button number (1-9, 4/5 for scroll).
 * @state: The modifier state.
 * @col: The terminal column at the click position.
 * @row: The terminal row at the click position.
 *
 * Handles a mouse button event.
 *
 * Returns: %TRUE if the event was handled, %FALSE to pass through.
 */
gboolean
gst_input_handler_handle_mouse_event(GstInputHandler *self,
                                     guint            button,
                                     guint            state,
                                     gint             col,
                                     gint             row)
{
	GstInputHandlerInterface *iface;

	g_return_val_if_fail(GST_IS_INPUT_HANDLER(self), FALSE);

	iface = GST_INPUT_HANDLER_GET_IFACE(self);

	/* Mouse event handling is optional; return FALSE if not implemented */
	if (iface->handle_mouse_event == NULL)
	{
		return FALSE;
	}

	return iface->handle_mouse_event(self, button, state, col, row);
}
