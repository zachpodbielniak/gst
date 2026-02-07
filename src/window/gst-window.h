/*
 * gst-window.h - Abstract base window class
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for terminal windows. Defines signals
 * that replace st's handler[] array for event dispatch.
 * Subclasses (X11, Wayland, etc.) translate platform events
 * into these GObject signals.
 */

#ifndef GST_WINDOW_H
#define GST_WINDOW_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_WINDOW (gst_window_get_type())

G_DECLARE_DERIVABLE_TYPE(GstWindow, gst_window, GST, WINDOW, GObject)

/**
 * GstWindowClass:
 * @parent_class: The parent class
 * @show: Virtual method to show the window
 * @hide: Virtual method to hide the window
 * @resize: Virtual method to resize the window
 * @set_title: Virtual method to set the window title
 * @set_selection: Set selection text (PRIMARY or CLIPBOARD)
 * @paste_clipboard: Request CLIPBOARD contents
 * @paste_primary: Request PRIMARY selection contents
 * @copy_to_clipboard: Copy primary selection to clipboard
 * @bell: Trigger an audible/visual bell
 * @set_opacity: Set window opacity (0.0 transparent, 1.0 opaque)
 * @set_pointer_motion: Enable or disable pointer motion events
 * @set_wm_hints: Set window manager size hints
 * @start_event_watch: Start watching for platform events via GLib main loop
 *
 * The class structure for #GstWindow.
 */
struct _GstWindowClass
{
	GObjectClass parent_class;

	/* Virtual methods - existing */
	void (*show)      (GstWindow   *self);
	void (*hide)      (GstWindow   *self);
	void (*resize)    (GstWindow   *self,
	                   guint        width,
	                   guint        height);
	void (*set_title) (GstWindow   *self,
	                   const gchar *title);

	/* Virtual methods - Phase 9 additions */
	void (*set_selection)    (GstWindow   *self,
	                          const gchar *text,
	                          gboolean     is_clipboard);
	void (*paste_clipboard)  (GstWindow   *self);
	void (*paste_primary)    (GstWindow   *self);
	void (*copy_to_clipboard)(GstWindow   *self);
	void (*bell)             (GstWindow   *self);
	void (*set_opacity)      (GstWindow   *self,
	                          gdouble      opacity);
	void (*set_pointer_motion)(GstWindow  *self,
	                          gboolean     enable);
	void (*set_wm_hints)     (GstWindow   *self,
	                          gint         cw,
	                          gint         ch,
	                          gint         borderpx);
	void (*start_event_watch)(GstWindow   *self);

	/* Padding for future expansion */
	gpointer padding[4];
};

GType
gst_window_get_type(void) G_GNUC_CONST;

void
gst_window_show(GstWindow *self);

void
gst_window_hide(GstWindow *self);

void
gst_window_resize(
	GstWindow *self,
	guint      width,
	guint      height
);

void
gst_window_set_title(
	GstWindow   *self,
	const gchar *title
);

/**
 * gst_window_set_selection:
 * @self: A #GstWindow
 * @text: selection text
 * @is_clipboard: TRUE for CLIPBOARD, FALSE for PRIMARY
 *
 * Sets the selection (PRIMARY or CLIPBOARD).
 */
void
gst_window_set_selection(
	GstWindow   *self,
	const gchar *text,
	gboolean     is_clipboard
);

/**
 * gst_window_paste_clipboard:
 * @self: A #GstWindow
 *
 * Requests the CLIPBOARD contents. When data arrives,
 * the "selection-notify" signal is emitted.
 */
void
gst_window_paste_clipboard(GstWindow *self);

/**
 * gst_window_paste_primary:
 * @self: A #GstWindow
 *
 * Requests the PRIMARY selection contents.
 */
void
gst_window_paste_primary(GstWindow *self);

/**
 * gst_window_copy_to_clipboard:
 * @self: A #GstWindow
 *
 * Copies the primary selection to the clipboard.
 */
void
gst_window_copy_to_clipboard(GstWindow *self);

/**
 * gst_window_bell:
 * @self: A #GstWindow
 *
 * Triggers a bell notification (urgency hint or similar).
 */
void
gst_window_bell(GstWindow *self);

/**
 * gst_window_set_opacity:
 * @self: A #GstWindow
 * @opacity: opacity value (0.0 to 1.0)
 *
 * Sets the window opacity. Not all backends support this.
 */
void
gst_window_set_opacity(
	GstWindow *self,
	gdouble    opacity
);

/**
 * gst_window_set_pointer_motion:
 * @self: A #GstWindow
 * @enable: TRUE to enable pointer motion events
 *
 * Enables or disables pointer motion event reporting.
 */
void
gst_window_set_pointer_motion(
	GstWindow *self,
	gboolean   enable
);

/**
 * gst_window_set_wm_hints:
 * @self: A #GstWindow
 * @cw: character cell width
 * @ch: character cell height
 * @borderpx: border padding
 *
 * Sets window manager hints for size increments.
 */
void
gst_window_set_wm_hints(
	GstWindow *self,
	gint       cw,
	gint       ch,
	gint       borderpx
);

/**
 * gst_window_start_event_watch:
 * @self: A #GstWindow
 *
 * Starts watching for platform events via GLib main loop.
 */
void
gst_window_start_event_watch(GstWindow *self);

/**
 * gst_window_get_width:
 * @self: A #GstWindow
 *
 * Gets the window width in pixels.
 *
 * Returns: width in pixels
 */
guint
gst_window_get_width(GstWindow *self);

/**
 * gst_window_get_height:
 * @self: A #GstWindow
 *
 * Gets the window height in pixels.
 *
 * Returns: height in pixels
 */
guint
gst_window_get_height(GstWindow *self);

/**
 * gst_window_is_visible:
 * @self: A #GstWindow
 *
 * Checks if the window is visible.
 *
 * Returns: TRUE if visible
 */
gboolean
gst_window_is_visible(GstWindow *self);

/**
 * gst_window_get_title:
 * @self: A #GstWindow
 *
 * Gets the window title.
 *
 * Returns: (transfer none) (nullable): the title
 */
const gchar *
gst_window_get_title(GstWindow *self);

G_END_DECLS

#endif /* GST_WINDOW_H */
