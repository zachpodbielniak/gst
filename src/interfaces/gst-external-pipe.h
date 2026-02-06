/*
 * gst-external-pipe.h
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for piping terminal data to external commands.
 */

#ifndef GST_EXTERNAL_PIPE_H
#define GST_EXTERNAL_PIPE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_EXTERNAL_PIPE (gst_external_pipe_get_type())

G_DECLARE_INTERFACE(GstExternalPipe, gst_external_pipe, GST, EXTERNAL_PIPE, GObject)

/**
 * GstExternalPipeInterface:
 * @parent_iface: The parent interface.
 * @pipe_data: Virtual method to pipe data to an external command.
 *
 * Interface for piping terminal data to external commands.
 */
struct _GstExternalPipeInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	gboolean (*pipe_data) (GstExternalPipe *self,
	                       const gchar     *command,
	                       const gchar     *data,
	                       gsize            length);
};

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
                            gsize            length);

G_END_DECLS

#endif /* GST_EXTERNAL_PIPE_H */
