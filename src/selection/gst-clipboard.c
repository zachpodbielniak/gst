/*
 * gst-clipboard.c - Clipboard integration
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "gst-clipboard.h"

/**
 * SECTION:gst-clipboard
 * @title: GstClipboard
 * @short_description: System clipboard integration
 *
 * #GstClipboard handles copying and pasting text to/from the
 * system clipboard, supporting both PRIMARY and CLIPBOARD selections.
 */

struct _GstClipboard
{
	GObject parent_instance;

	/* TODO: Add X11/Wayland clipboard handles */
	gchar *cached_text;
};

G_DEFINE_TYPE(GstClipboard, gst_clipboard, G_TYPE_OBJECT)

/* Singleton instance */
static GstClipboard *default_clipboard = NULL;

static void
gst_clipboard_dispose(GObject *object)
{
	GstClipboard *self;

	self = GST_CLIPBOARD(object);

	g_clear_pointer(&self->cached_text, g_free);

	G_OBJECT_CLASS(gst_clipboard_parent_class)->dispose(object);
}

static void
gst_clipboard_finalize(GObject *object)
{
	G_OBJECT_CLASS(gst_clipboard_parent_class)->finalize(object);
}

static void
gst_clipboard_class_init(GstClipboardClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_clipboard_dispose;
	object_class->finalize = gst_clipboard_finalize;
}

static void
gst_clipboard_init(GstClipboard *self)
{
	self->cached_text = NULL;

	/* TODO: Initialize X11/Wayland clipboard connection */
}

/**
 * gst_clipboard_new:
 *
 * Creates a new clipboard handler.
 *
 * Returns: (transfer full): A new #GstClipboard
 */
GstClipboard *
gst_clipboard_new(void)
{
	return (GstClipboard *)g_object_new(GST_TYPE_CLIPBOARD, NULL);
}

/**
 * gst_clipboard_get_default:
 *
 * Gets the default shared clipboard instance.
 *
 * Returns: (transfer none): The default #GstClipboard
 */
GstClipboard *
gst_clipboard_get_default(void)
{
	if (default_clipboard == NULL)
	{
		default_clipboard = gst_clipboard_new();
	}

	return default_clipboard;
}

/**
 * gst_clipboard_copy:
 * @self: A #GstClipboard
 * @text: The text to copy
 *
 * Copies text to the clipboard.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_clipboard_copy(
	GstClipboard *self,
	const gchar  *text
){
	g_return_val_if_fail(GST_IS_CLIPBOARD(self), FALSE);

	if (text == NULL)
	{
		return FALSE;
	}

	g_free(self->cached_text);
	self->cached_text = g_strdup(text);

	/* TODO: Set X11/Wayland clipboard content */

	return TRUE;
}

/**
 * gst_clipboard_paste:
 * @self: A #GstClipboard
 *
 * Gets the current clipboard contents.
 *
 * Returns: (transfer full) (nullable): The clipboard text, or %NULL
 */
gchar *
gst_clipboard_paste(GstClipboard *self)
{
	g_return_val_if_fail(GST_IS_CLIPBOARD(self), NULL);

	/* TODO: Get X11/Wayland clipboard content */

	if (self->cached_text != NULL)
	{
		return g_strdup(self->cached_text);
	}

	return NULL;
}

/**
 * gst_clipboard_clear:
 * @self: A #GstClipboard
 *
 * Clears the clipboard contents.
 */
void
gst_clipboard_clear(GstClipboard *self)
{
	g_return_if_fail(GST_IS_CLIPBOARD(self));

	g_clear_pointer(&self->cached_text, g_free);

	/* TODO: Clear X11/Wayland clipboard */
}
