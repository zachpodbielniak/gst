/*
 * gst-escape-parser.c - GST Escape Sequence Parser
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Thin wrapper class. The actual escape parsing logic is integrated
 * directly into GstTerminal (gst-terminal.c) for performance and
 * tight coupling with terminal state, matching st's architecture.
 *
 * This class exists for API compatibility and can be used as a
 * standalone facade for feeding data to a terminal.
 */

#include "gst-escape-parser.h"
#include "gst-terminal.h"

typedef struct {
	GstTerminal *term;
} GstEscapeParserPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(GstEscapeParser, gst_escape_parser, G_TYPE_OBJECT)

static void
gst_escape_parser_finalize(GObject *object)
{
	GstEscapeParser *parser = GST_ESCAPE_PARSER(object);
	GstEscapeParserPrivate *priv = gst_escape_parser_get_instance_private(parser);

	if (priv->term != NULL) {
		g_object_unref(priv->term);
	}

	G_OBJECT_CLASS(gst_escape_parser_parent_class)->finalize(object);
}

static void
gst_escape_parser_class_init(GstEscapeParserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gst_escape_parser_finalize;
}

static void
gst_escape_parser_init(GstEscapeParser *parser)
{
	GstEscapeParserPrivate *priv = gst_escape_parser_get_instance_private(parser);
	priv->term = NULL;
}

/**
 * gst_escape_parser_new:
 * @term: a #GstTerminal to parse into
 *
 * Creates a new escape parser bound to the given terminal.
 *
 * Returns: (transfer full): a new GstEscapeParser
 */
GstEscapeParser *
gst_escape_parser_new(GstTerminal *term)
{
	GstEscapeParser *parser;
	GstEscapeParserPrivate *priv;

	g_return_val_if_fail(GST_IS_TERMINAL(term), NULL);

	parser = g_object_new(GST_TYPE_ESCAPE_PARSER, NULL);
	priv = gst_escape_parser_get_instance_private(parser);
	priv->term = g_object_ref(term);

	return parser;
}

/**
 * gst_escape_parser_feed:
 * @parser: a #GstEscapeParser
 * @data: data to parse
 * @len: length of data, or -1 if NUL-terminated
 *
 * Feeds data through the escape parser into the terminal.
 * Delegates to gst_terminal_write().
 */
void
gst_escape_parser_feed(
    GstEscapeParser *parser,
    const gchar     *data,
    gssize          len
){
	GstEscapeParserPrivate *priv;

	g_return_if_fail(GST_IS_ESCAPE_PARSER(parser));
	g_return_if_fail(data != NULL);

	priv = gst_escape_parser_get_instance_private(parser);

	if (priv->term != NULL) {
		gst_terminal_write(priv->term, data, len);
	}
}

/**
 * gst_escape_parser_reset:
 * @parser: a #GstEscapeParser
 *
 * Resets the parser state. Delegates to gst_terminal_reset().
 */
void
gst_escape_parser_reset(GstEscapeParser *parser)
{
	GstEscapeParserPrivate *priv;

	g_return_if_fail(GST_IS_ESCAPE_PARSER(parser));

	priv = gst_escape_parser_get_instance_private(parser);

	if (priv->term != NULL) {
		gst_terminal_reset(priv->term, FALSE);
	}
}
