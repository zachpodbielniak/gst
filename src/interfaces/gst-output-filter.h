/*
 * gst-output-filter.h
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for filtering terminal output.
 */

#ifndef GST_OUTPUT_FILTER_H
#define GST_OUTPUT_FILTER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_OUTPUT_FILTER (gst_output_filter_get_type())

G_DECLARE_INTERFACE(GstOutputFilter, gst_output_filter, GST, OUTPUT_FILTER, GObject)

/**
 * GstOutputFilterInterface:
 * @parent_iface: The parent interface.
 * @filter_output: Virtual method to filter terminal output data.
 *
 * Interface for filtering terminal output before display.
 */
struct _GstOutputFilterInterface
{
	GTypeInterface parent_iface;

	/* Virtual methods */
	gchar * (*filter_output) (GstOutputFilter *self,
	                          const gchar     *input,
	                          gsize            length,
	                          gsize           *out_length);
};

/**
 * gst_output_filter_filter_output:
 * @self: A #GstOutputFilter instance.
 * @input: The input data to filter.
 * @length: The length of the input data.
 * @out_length: (out): Location to store the output length.
 *
 * Filters terminal output data.
 *
 * Returns: (transfer full): A newly allocated filtered string, or %NULL.
 */
gchar *
gst_output_filter_filter_output(GstOutputFilter *self,
                                const gchar     *input,
                                gsize            length,
                                gsize           *out_length);

G_END_DECLS

#endif /* GST_OUTPUT_FILTER_H */
