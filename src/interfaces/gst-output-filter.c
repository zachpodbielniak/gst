/*
 * gst-output-filter.c
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for filtering terminal output.
 */

#include "gst-output-filter.h"

G_DEFINE_INTERFACE(GstOutputFilter, gst_output_filter, G_TYPE_OBJECT)

static void
gst_output_filter_default_init(GstOutputFilterInterface *iface)
{
	/* TODO: Add interface properties or signals here if needed */
	(void)iface;
}

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
                                gsize           *out_length)
{
	GstOutputFilterInterface *iface;

	g_return_val_if_fail(GST_IS_OUTPUT_FILTER(self), NULL);
	g_return_val_if_fail(input != NULL, NULL);

	iface = GST_OUTPUT_FILTER_GET_IFACE(self);
	g_return_val_if_fail(iface->filter_output != NULL, NULL);

	return iface->filter_output(self, input, length, out_length);
}
