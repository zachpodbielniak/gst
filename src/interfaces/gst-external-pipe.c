/*
 * gst-external-pipe.c
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for piping terminal data to external commands.
 */

#include "gst-external-pipe.h"

G_DEFINE_INTERFACE(GstExternalPipe, gst_external_pipe, G_TYPE_OBJECT)

static void
gst_external_pipe_default_init(GstExternalPipeInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

/**
 * gst_external_pipe_pipe_data:
 * @self: A #GstExternalPipe instance.
 * @command: The command to pipe data to.
 * @data: The data to pipe.
 * @length: The length of the data.
 *
 * Pipes data to an external command.
 *
 * Returns: %TRUE if the data was piped successfully, %FALSE otherwise.
 */
gboolean
gst_external_pipe_pipe_data(GstExternalPipe *self,
                            const gchar     *command,
                            const gchar     *data,
                            gsize            length)
{
	GstExternalPipeInterface *iface;

	g_return_val_if_fail(GST_IS_EXTERNAL_PIPE(self), FALSE);
	g_return_val_if_fail(command != NULL, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);

	iface = GST_EXTERNAL_PIPE_GET_IFACE(self);
	g_return_val_if_fail(iface->pipe_data != NULL, FALSE);

	return iface->pipe_data(self, command, data, length);
}
