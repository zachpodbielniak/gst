/*
 * gst-escape-parser.h - GST Escape Sequence Parser
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Parses VT100/ANSI escape sequences from terminal output.
 * Handles CSI, OSC, DCS, and other control sequences.
 */

#ifndef GST_ESCAPE_PARSER_H
#define GST_ESCAPE_PARSER_H

#include <glib-object.h>
#include "../gst-types.h"
#include "../gst-enums.h"

G_BEGIN_DECLS

#define GST_TYPE_ESCAPE_PARSER             (gst_escape_parser_get_type())
#define GST_ESCAPE_PARSER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ESCAPE_PARSER, GstEscapeParser))
#define GST_ESCAPE_PARSER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ESCAPE_PARSER, GstEscapeParserClass))
#define GST_IS_ESCAPE_PARSER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ESCAPE_PARSER))
#define GST_IS_ESCAPE_PARSER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ESCAPE_PARSER))
#define GST_ESCAPE_PARSER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_ESCAPE_PARSER, GstEscapeParserClass))

struct _GstEscapeParser {
    GObject parent_instance;
};

struct _GstEscapeParserClass {
    GObjectClass parent_class;
};

GType gst_escape_parser_get_type(void) G_GNUC_CONST;

GstEscapeParser *gst_escape_parser_new(GstTerminal *term);

void gst_escape_parser_feed(GstEscapeParser *parser, const gchar *data, gssize len);

void gst_escape_parser_reset(GstEscapeParser *parser);

G_END_DECLS

#endif /* GST_ESCAPE_PARSER_H */
