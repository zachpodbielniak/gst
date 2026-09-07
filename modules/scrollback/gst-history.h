/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef GST_HISTORY_H
#define GST_HISTORY_H

#include "../../src/module/gst-module-manager.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-line.h"

/* Optional module bridge: no link-time dependency or cached module pointer.
 * Borrowed lines are valid only until the next terminal mutation. */
typedef struct {
	gint (*count)(gpointer self);
	gint (*offset)(gpointer self);
	void (*set_offset)(gpointer self, gint offset);
	const GstLine *(*line)(gpointer self, gint index);
} GstHistoryApi;

static inline const GstHistoryApi *
gst_history_lookup(GstModule **module)
{
	*module = gst_module_manager_get_module(
		gst_module_manager_get_default(), "scrollback");
	return *module != NULL ? (const GstHistoryApi *)
		g_object_get_data(G_OBJECT(*module), "gst-history-api") : NULL;
}

/* Negative rows address history: -1 is the newest saved row. */
static inline const GstLine *
gst_history_line(GstTerminal *term, gint row)
{
	GstModule *module;
	const GstHistoryApi *api;

	if (row >= 0)
		return gst_terminal_get_line(term, row);
	api = gst_history_lookup(&module);
	return api != NULL ? api->line(module, -1 - row) : NULL;
}

/* Build byte-to-cell maps from individual cells, not Unicode character
 * counts. Cluster serialization is provided by the core glyph API. */
static inline gchar *
gst_history_line_text(const GstLine *line, GArray *starts, GArray *ends)
{
	GString *text;
	gint x;

	text = g_string_new(NULL);
	for (x = 0; x < line->len; x++) {
		const GstGlyph *glyph;
		const gchar *cell;
		gchar buffer[7];
		gsize b;
		gint end;

		glyph = gst_line_get_glyph_const(line, x);
		if (glyph->attr & GST_GLYPH_ATTR_WDUMMY)
			continue;
		end = MIN(line->len, x + ((glyph->attr & GST_GLYPH_ATTR_WIDE) ? 2 : 1));
		cell = gst_glyph_get_text(glyph, buffer);
		for (b = 0; cell[b] != '\0'; b++) {
			g_array_append_val(starts, x);
			g_array_append_val(ends, end);
		}
		g_string_append(text, cell);
	}
	return g_string_free(text, FALSE);
}

#endif
