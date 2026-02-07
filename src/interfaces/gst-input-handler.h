/*
 * gst-input-handler.h
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for handling keyboard and mouse input.
 */

#ifndef GST_INPUT_HANDLER_H
#define GST_INPUT_HANDLER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_INPUT_HANDLER (gst_input_handler_get_type())

G_DECLARE_INTERFACE(GstInputHandler, gst_input_handler, GST, INPUT_HANDLER, GObject)

/**
 * GstInputHandlerInterface:
 * @parent_iface: The parent interface.
 * @handle_key_event: Virtual method to handle keyboard events.
 *
 * Interface for handling terminal input events.
 */
struct _GstInputHandlerInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	gboolean (*handle_key_event) (GstInputHandler *self,
	                              guint            keyval,
	                              guint            keycode,
	                              guint            state);
};

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
                                   guint            state);

G_END_DECLS

#endif /* GST_INPUT_HANDLER_H */
