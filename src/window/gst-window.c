/*
 * gst-window.c - Abstract base window class
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for terminal windows. Provides GObject signals
 * for event dispatch, replacing st's handler[] function pointer array.
 */

#include "gst-window.h"
#include <X11/keysym.h>

/**
 * SECTION:gst-window
 * @title: GstWindow
 * @short_description: Abstract base class for terminal windows
 *
 * #GstWindow defines signals for all events a terminal window
 * can emit. Subclasses translate platform-specific events
 * (X11, Wayland) into these signals.
 *
 * Signals:
 * - key-press: keyboard input
 * - button-press / button-release: mouse buttons
 * - motion-notify: mouse movement
 * - focus-change: focus in/out
 * - configure: window resize
 * - expose: redraw request
 * - visibility: visibility change
 * - close-request: window close
 * - selection-notify: clipboard data arrived
 * - selection-request: another app wants our selection
 */

/* Signal IDs */
enum {
	SIGNAL_KEY_PRESS,
	SIGNAL_BUTTON_PRESS,
	SIGNAL_BUTTON_RELEASE,
	SIGNAL_MOTION_NOTIFY,
	SIGNAL_FOCUS_CHANGE,
	SIGNAL_CONFIGURE,
	SIGNAL_EXPOSE,
	SIGNAL_VISIBILITY,
	SIGNAL_CLOSE_REQUEST,
	SIGNAL_SELECTION_NOTIFY,
	SIGNAL_SELECTION_REQUEST,
	N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

/* Private structure */
typedef struct
{
	gchar *title;
	guint  width;
	guint  height;
	gboolean visible;
} GstWindowPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(GstWindow, gst_window, G_TYPE_OBJECT)

static void
gst_window_dispose(GObject *object)
{
	GstWindow *self;
	GstWindowPrivate *priv;

	self = GST_WINDOW(object);
	priv = gst_window_get_instance_private(self);

	g_clear_pointer(&priv->title, g_free);

	G_OBJECT_CLASS(gst_window_parent_class)->dispose(object);
}

static void
gst_window_class_init(GstWindowClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_window_dispose;

	/* Virtual methods default to NULL */
	klass->show = NULL;
	klass->hide = NULL;
	klass->resize = NULL;
	klass->set_title = NULL;

	/**
	 * GstWindow::key-press:
	 * @self: the window
	 * @keysym: X11 keysym value
	 * @state: modifier state
	 * @text: UTF-8 text from input method
	 * @len: length of text
	 *
	 * Emitted when a key is pressed.
	 */
	signals[SIGNAL_KEY_PRESS] = g_signal_new(
		"key-press",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 4,
		G_TYPE_UINT,    /* keysym */
		G_TYPE_UINT,    /* state */
		G_TYPE_STRING,  /* text */
		G_TYPE_INT      /* len */
	);

	/**
	 * GstWindow::button-press:
	 * @self: the window
	 * @button: button number
	 * @state: modifier state
	 * @x: pixel x
	 * @y: pixel y
	 * @time: event timestamp
	 *
	 * Emitted when a mouse button is pressed.
	 */
	signals[SIGNAL_BUTTON_PRESS] = g_signal_new(
		"button-press",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 5,
		G_TYPE_UINT,    /* button */
		G_TYPE_UINT,    /* state */
		G_TYPE_INT,     /* x */
		G_TYPE_INT,     /* y */
		G_TYPE_ULONG    /* time */
	);

	/**
	 * GstWindow::button-release:
	 * @self: the window
	 * @button: button number
	 * @state: modifier state
	 * @x: pixel x
	 * @y: pixel y
	 * @time: event timestamp
	 *
	 * Emitted when a mouse button is released.
	 */
	signals[SIGNAL_BUTTON_RELEASE] = g_signal_new(
		"button-release",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 5,
		G_TYPE_UINT,    /* button */
		G_TYPE_UINT,    /* state */
		G_TYPE_INT,     /* x */
		G_TYPE_INT,     /* y */
		G_TYPE_ULONG    /* time */
	);

	/**
	 * GstWindow::motion-notify:
	 * @self: the window
	 * @state: modifier state
	 * @x: pixel x
	 * @y: pixel y
	 *
	 * Emitted when the mouse moves.
	 */
	signals[SIGNAL_MOTION_NOTIFY] = g_signal_new(
		"motion-notify",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 3,
		G_TYPE_UINT,    /* state */
		G_TYPE_INT,     /* x */
		G_TYPE_INT      /* y */
	);

	/**
	 * GstWindow::focus-change:
	 * @self: the window
	 * @focused: TRUE if focus gained
	 *
	 * Emitted when focus state changes.
	 */
	signals[SIGNAL_FOCUS_CHANGE] = g_signal_new(
		"focus-change",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_BOOLEAN  /* focused */
	);

	/**
	 * GstWindow::configure:
	 * @self: the window
	 * @width: new width in pixels
	 * @height: new height in pixels
	 *
	 * Emitted when the window is resized.
	 */
	signals[SIGNAL_CONFIGURE] = g_signal_new(
		"configure",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		G_TYPE_UINT,    /* width */
		G_TYPE_UINT     /* height */
	);

	/**
	 * GstWindow::expose:
	 * @self: the window
	 *
	 * Emitted when the window needs repainting.
	 */
	signals[SIGNAL_EXPOSE] = g_signal_new(
		"expose",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 0
	);

	/**
	 * GstWindow::visibility:
	 * @self: the window
	 * @visible: TRUE if window is now visible
	 *
	 * Emitted when window visibility changes.
	 */
	signals[SIGNAL_VISIBILITY] = g_signal_new(
		"visibility",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_BOOLEAN  /* visible */
	);

	/**
	 * GstWindow::close-request:
	 * @self: the window
	 *
	 * Emitted when the window close button is clicked.
	 */
	signals[SIGNAL_CLOSE_REQUEST] = g_signal_new(
		"close-request",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 0
	);

	/**
	 * GstWindow::selection-notify:
	 * @self: the window
	 * @data: pasted text data
	 * @len: length of data
	 *
	 * Emitted when selection data arrives (paste).
	 */
	signals[SIGNAL_SELECTION_NOTIFY] = g_signal_new(
		"selection-notify",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		G_TYPE_STRING,  /* data */
		G_TYPE_INT      /* len */
	);

	/**
	 * GstWindow::selection-request:
	 * @self: the window
	 * @event_ptr: opaque pointer to platform event data
	 *
	 * Emitted when another app requests our selection.
	 */
	signals[SIGNAL_SELECTION_REQUEST] = g_signal_new(
		"selection-request",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER  /* event data */
	);
}

static void
gst_window_init(GstWindow *self)
{
	GstWindowPrivate *priv;

	priv = gst_window_get_instance_private(self);
	priv->title = g_strdup("GST Terminal");
	priv->width = 800;
	priv->height = 600;
	priv->visible = FALSE;
}

/* ===== Public API ===== */

void
gst_window_show(GstWindow *self)
{
	GstWindowClass *klass;

	g_return_if_fail(GST_IS_WINDOW(self));

	klass = GST_WINDOW_GET_CLASS(self);
	if (klass->show != NULL) {
		klass->show(self);
	}
}

void
gst_window_hide(GstWindow *self)
{
	GstWindowClass *klass;

	g_return_if_fail(GST_IS_WINDOW(self));

	klass = GST_WINDOW_GET_CLASS(self);
	if (klass->hide != NULL) {
		klass->hide(self);
	}
}

void
gst_window_resize(
	GstWindow *self,
	guint      width,
	guint      height
){
	GstWindowClass *klass;

	g_return_if_fail(GST_IS_WINDOW(self));

	klass = GST_WINDOW_GET_CLASS(self);
	if (klass->resize != NULL) {
		klass->resize(self, width, height);
	}
}

void
gst_window_set_title(
	GstWindow   *self,
	const gchar *title
){
	GstWindowClass *klass;

	g_return_if_fail(GST_IS_WINDOW(self));

	klass = GST_WINDOW_GET_CLASS(self);
	if (klass->set_title != NULL) {
		klass->set_title(self, title);
	}
}

guint
gst_window_get_width(GstWindow *self)
{
	GstWindowPrivate *priv;

	g_return_val_if_fail(GST_IS_WINDOW(self), 0);

	priv = gst_window_get_instance_private(self);
	return priv->width;
}

guint
gst_window_get_height(GstWindow *self)
{
	GstWindowPrivate *priv;

	g_return_val_if_fail(GST_IS_WINDOW(self), 0);

	priv = gst_window_get_instance_private(self);
	return priv->height;
}

gboolean
gst_window_is_visible(GstWindow *self)
{
	GstWindowPrivate *priv;

	g_return_val_if_fail(GST_IS_WINDOW(self), FALSE);

	priv = gst_window_get_instance_private(self);
	return priv->visible;
}

const gchar *
gst_window_get_title(GstWindow *self)
{
	GstWindowPrivate *priv;

	g_return_val_if_fail(GST_IS_WINDOW(self), NULL);

	priv = gst_window_get_instance_private(self);
	return priv->title;
}
