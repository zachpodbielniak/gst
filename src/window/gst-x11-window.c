/*
 * gst-x11-window.c - X11 window implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Ports st's xinit(), event handling, IME, and X11 selection protocol.
 * Key architectural change: X11 events are delivered via
 * g_io_add_watch() on ConnectionNumber(display) instead of pselect().
 */

#include "gst-x11-window.h"
#include <X11/Xft/Xft.h>
#include <X11/cursorfont.h>
#include <string.h>
#include <unistd.h>

/**
 * SECTION:gst-x11-window
 * @title: GstX11Window
 * @short_description: X11-based terminal window
 *
 * #GstX11Window implements the #GstWindow interface for X11,
 * creating a window, handling events, and managing selections.
 */

/* XEMBED protocol message types */
#define XEMBED_FOCUS_IN  (4)
#define XEMBED_FOCUS_OUT (5)

struct _GstX11Window
{
	GstWindow parent_instance;

	/* X11 core */
	Display *display;
	Window xwindow;
	Colormap colormap;
	Visual *visual;
	gint screen;

	/* X atoms */
	Atom xembed;
	Atom wmdeletewin;
	Atom netwmname;
	Atom netwmiconname;
	Atom netwmpid;

	/* Input method */
	XIM xim;
	XIC xic;

	/* Selection */
	Atom xtarget;          /* UTF8_STRING or XA_STRING */
	gchar *sel_primary;
	gchar *sel_clipboard;

	/* Window state */
	guint width;
	guint height;
	gchar *title;
	gboolean visible;

	/* GMainLoop integration */
	GIOChannel *x11_channel;
	guint x11_watch_id;
};

G_DEFINE_TYPE(GstX11Window, gst_x11_window, GST_TYPE_WINDOW)

/* ===== Event handlers ===== */

/*
 * on_x11_event:
 * @source: GIOChannel for the X11 fd
 * @condition: I/O condition
 * @user_data: the GstX11Window
 *
 * GMainLoop callback that drains all pending X11 events and
 * emits appropriate GObject signals. Replaces st's event loop.
 *
 * Returns: TRUE to keep the watch active
 */
static gboolean
on_x11_event(
	GIOChannel      *source,
	GIOCondition    condition,
	gpointer        user_data
){
	GstX11Window *self;
	XEvent ev;

	self = GST_X11_WINDOW(user_data);

	while (XPending(self->display)) {
		XNextEvent(self->display, &ev);

		if (XFilterEvent(&ev, None)) {
			continue;
		}

		switch (ev.type) {
		case KeyPress:
			{
				XKeyEvent *ke = &ev.xkey;
				KeySym ksym;
				gchar buf[64];
				gint len;
				Status status;

				if (self->xic != NULL) {
					len = XmbLookupString(self->xic, ke, buf,
						(gint)sizeof(buf), &ksym, &status);
				} else {
					len = XLookupString(ke, buf, (gint)sizeof(buf),
						&ksym, NULL);
				}

				/* Null-terminate for signal emission */
				if (len >= 0 && len < (gint)sizeof(buf)) {
					buf[len] = '\0';
				}

				g_signal_emit_by_name(self, "key-press",
					(guint)ksym, (guint)ke->state, buf, len);
			}
			break;

		case ButtonPress:
			g_signal_emit_by_name(self, "button-press",
				(guint)ev.xbutton.button,
				(guint)ev.xbutton.state,
				(gint)ev.xbutton.x,
				(gint)ev.xbutton.y,
				(gulong)ev.xbutton.time);
			break;

		case ButtonRelease:
			g_signal_emit_by_name(self, "button-release",
				(guint)ev.xbutton.button,
				(guint)ev.xbutton.state,
				(gint)ev.xbutton.x,
				(gint)ev.xbutton.y,
				(gulong)ev.xbutton.time);
			break;

		case MotionNotify:
			g_signal_emit_by_name(self, "motion-notify",
				(guint)ev.xmotion.state,
				(gint)ev.xmotion.x,
				(gint)ev.xmotion.y);
			break;

		case FocusIn:
			if (ev.xfocus.mode == NotifyGrab) {
				break;
			}
			if (self->xic != NULL) {
				XSetICFocus(self->xic);
			}
			g_signal_emit_by_name(self, "focus-change", TRUE);
			break;

		case FocusOut:
			if (ev.xfocus.mode == NotifyGrab) {
				break;
			}
			if (self->xic != NULL) {
				XUnsetICFocus(self->xic);
			}
			g_signal_emit_by_name(self, "focus-change", FALSE);
			break;

		case ConfigureNotify:
			if ((guint)ev.xconfigure.width != self->width
			    || (guint)ev.xconfigure.height != self->height) {
				self->width = (guint)ev.xconfigure.width;
				self->height = (guint)ev.xconfigure.height;
				g_signal_emit_by_name(self, "configure",
					self->width, self->height);
			}
			break;

		case Expose:
			g_signal_emit_by_name(self, "expose");
			break;

		case VisibilityNotify:
			{
				gboolean vis;

				vis = (ev.xvisibility.state != VisibilityFullyObscured);
				self->visible = vis;
				g_signal_emit_by_name(self, "visibility", vis);
			}
			break;

		case UnmapNotify:
			self->visible = FALSE;
			g_signal_emit_by_name(self, "visibility", FALSE);
			break;

		case ClientMessage:
			if (ev.xclient.message_type == self->xembed
			    && ev.xclient.format == 32) {
				if (ev.xclient.data.l[1] == XEMBED_FOCUS_IN) {
					g_signal_emit_by_name(self, "focus-change", TRUE);
				} else if (ev.xclient.data.l[1] == XEMBED_FOCUS_OUT) {
					g_signal_emit_by_name(self, "focus-change", FALSE);
				}
			} else if ((Atom)ev.xclient.data.l[0] == self->wmdeletewin) {
				g_signal_emit_by_name(self, "close-request");
			}
			break;

		case SelectionNotify:
			{
				/* Handle incoming selection data (paste) */
				XSelectionEvent *se = &ev.xselection;
				Atom type;
				gint format;
				gulong nitems;
				gulong rem;
				guchar *data;

				if (se->property == None) {
					break;
				}

				if (XGetWindowProperty(self->display, self->xwindow,
				    se->property, 0, 65536, True, AnyPropertyType,
				    &type, &format, &nitems, &rem, &data) == Success) {
					if (data != NULL && nitems > 0) {
						/* Convert newlines to carriage returns */
						gulong i;
						for (i = 0; i < nitems * (gulong)format / 8; i++) {
							if (data[i] == '\n') {
								data[i] = '\r';
							}
						}
						g_signal_emit_by_name(self, "selection-notify",
							(const gchar *)data,
							(gint)(nitems * (gulong)format / 8));
					}
					if (data != NULL) {
						XFree(data);
					}
				}
				XDeleteProperty(self->display, self->xwindow, se->property);
			}
			break;

		case SelectionRequest:
			{
				/* Respond to other apps requesting our selection */
				XSelectionRequestEvent *xsre;
				XSelectionEvent xev;
				Atom xa_targets;
				Atom clipboard;
				const gchar *seltext;

				xsre = &ev.xselectionrequest;

				memset(&xev, 0, sizeof(xev));
				xev.type = SelectionNotify;
				xev.requestor = xsre->requestor;
				xev.selection = xsre->selection;
				xev.target = xsre->target;
				xev.time = xsre->time;
				xev.property = None;

				if (xsre->property == None) {
					xsre->property = xsre->target;
				}

				xa_targets = XInternAtom(self->display, "TARGETS", 0);
				clipboard = XInternAtom(self->display, "CLIPBOARD", 0);

				if (xsre->target == xa_targets) {
					Atom string = self->xtarget;
					XChangeProperty(xsre->display, xsre->requestor,
						xsre->property, XA_ATOM, 32, PropModeReplace,
						(guchar *)&string, 1);
					xev.property = xsre->property;
				} else if (xsre->target == self->xtarget
				           || xsre->target == XA_STRING) {
					seltext = NULL;
					if (xsre->selection == XA_PRIMARY) {
						seltext = self->sel_primary;
					} else if (xsre->selection == clipboard) {
						seltext = self->sel_clipboard;
					}
					if (seltext != NULL) {
						XChangeProperty(xsre->display, xsre->requestor,
							xsre->property, xsre->target,
							8, PropModeReplace,
							(const guchar *)seltext,
							(gint)strlen(seltext));
						xev.property = xsre->property;
					}
				}

				XSendEvent(xsre->display, xsre->requestor, 1, 0,
					(XEvent *)&xev);
			}
			break;

		default:
			break;
		}
	}

	return TRUE;
}

/* ===== Virtual method implementations ===== */

static void
gst_x11_window_show_impl(GstWindow *window)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(window);
	XMapWindow(self->display, self->xwindow);
	XSync(self->display, False);
	self->visible = TRUE;
}

static void
gst_x11_window_hide_impl(GstWindow *window)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(window);
	XUnmapWindow(self->display, self->xwindow);
	self->visible = FALSE;
}

static void
gst_x11_window_resize_impl(
	GstWindow *window,
	guint      width,
	guint      height
){
	GstX11Window *self;

	self = GST_X11_WINDOW(window);
	self->width = width;
	self->height = height;
	XResizeWindow(self->display, self->xwindow, width, height);
}

static void
gst_x11_window_set_title_impl(
	GstWindow   *window,
	const gchar *title
){
	GstX11Window *self;
	Atom utf8;

	self = GST_X11_WINDOW(window);

	g_free(self->title);
	self->title = g_strdup(title);

	if (title == NULL) {
		title = "GST Terminal";
	}

	utf8 = XInternAtom(self->display, "UTF8_STRING", False);

	XStoreName(self->display, self->xwindow, title);

	XChangeProperty(self->display, self->xwindow, self->netwmname,
		utf8, 8, PropModeReplace,
		(const guchar *)title, (gint)strlen(title));
	XChangeProperty(self->display, self->xwindow, self->netwmiconname,
		utf8, 8, PropModeReplace,
		(const guchar *)title, (gint)strlen(title));
}

/*
 * gst_x11_window_set_selection_impl:
 *
 * Sets the X11 selection (PRIMARY or CLIPBOARD) by taking ownership
 * via XSetSelectionOwner and storing the text locally.
 */
static void
gst_x11_window_set_selection_impl(
	GstWindow   *window,
	const gchar *text,
	gboolean     is_clipboard
){
	GstX11Window *self;
	Atom sel;

	self = GST_X11_WINDOW(window);

	if (is_clipboard) {
		g_free(self->sel_clipboard);
		self->sel_clipboard = g_strdup(text);
		sel = XInternAtom(self->display, "CLIPBOARD", 0);
	} else {
		g_free(self->sel_primary);
		self->sel_primary = g_strdup(text);
		sel = XA_PRIMARY;
	}

	XSetSelectionOwner(self->display, sel, self->xwindow, CurrentTime);
}

/*
 * gst_x11_window_paste_clipboard_impl:
 *
 * Requests the CLIPBOARD contents via XConvertSelection.
 * Data arrives asynchronously via SelectionNotify event.
 */
static void
gst_x11_window_paste_clipboard_impl(GstWindow *window)
{
	GstX11Window *self;
	Atom clipboard;

	self = GST_X11_WINDOW(window);

	clipboard = XInternAtom(self->display, "CLIPBOARD", 0);
	XConvertSelection(self->display, clipboard, self->xtarget,
		clipboard, self->xwindow, CurrentTime);
}

/*
 * gst_x11_window_paste_primary_impl:
 *
 * Requests the PRIMARY selection contents via XConvertSelection.
 */
static void
gst_x11_window_paste_primary_impl(GstWindow *window)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(window);

	XConvertSelection(self->display, XA_PRIMARY, self->xtarget,
		XA_PRIMARY, self->xwindow, CurrentTime);
}

/*
 * gst_x11_window_copy_to_clipboard_impl:
 *
 * Copies the primary selection text to the clipboard selection.
 */
static void
gst_x11_window_copy_to_clipboard_impl(GstWindow *window)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(window);

	if (self->sel_primary != NULL) {
		g_free(self->sel_clipboard);
		self->sel_clipboard = g_strdup(self->sel_primary);
		gst_x11_window_set_selection_impl(window, self->sel_clipboard, TRUE);
	}
}

/*
 * gst_x11_window_bell_impl:
 *
 * Triggers an X11 bell by setting the urgency hint on the window.
 */
static void
gst_x11_window_bell_impl(GstWindow *window)
{
	GstX11Window *self;
	XWMHints *wm;

	self = GST_X11_WINDOW(window);

	/* Set urgency hint briefly */
	wm = XGetWMHints(self->display, self->xwindow);
	if (wm != NULL) {
		wm->flags |= XUrgencyHint;
		XSetWMHints(self->display, self->xwindow, wm);
		XFree(wm);
	}
}

/*
 * gst_x11_window_set_opacity_impl:
 *
 * Sets the window opacity via the _NET_WM_WINDOW_OPACITY X11 property.
 */
static void
gst_x11_window_set_opacity_impl(
	GstWindow *window,
	gdouble    opacity
){
	GstX11Window *self;
	Atom atom;
	guint32 val;

	self = GST_X11_WINDOW(window);

	atom = XInternAtom(self->display, "_NET_WM_WINDOW_OPACITY", False);

	/* Clamp opacity to [0.0, 1.0] */
	if (opacity < 0.0) {
		opacity = 0.0;
	} else if (opacity > 1.0) {
		opacity = 1.0;
	}

	val = (guint32)(opacity * (gdouble)0xFFFFFFFF);

	XChangeProperty(self->display, self->xwindow, atom,
		XA_CARDINAL, 32, PropModeReplace,
		(guchar *)&val, 1);
	XSync(self->display, False);
}

/*
 * gst_x11_window_set_pointer_motion_impl:
 *
 * Enables or disables pointer motion event reporting by
 * modifying the X11 event mask.
 */
static void
gst_x11_window_set_pointer_motion_impl(
	GstWindow *window,
	gboolean   enable
){
	GstX11Window *self;
	XWindowAttributes attrs;
	long event_mask;

	self = GST_X11_WINDOW(window);

	XGetWindowAttributes(self->display, self->xwindow, &attrs);
	event_mask = attrs.your_event_mask;

	if (enable) {
		event_mask |= PointerMotionMask;
	} else {
		event_mask &= ~PointerMotionMask;
		event_mask |= ButtonMotionMask;
	}

	XSelectInput(self->display, self->xwindow, event_mask);
}

/*
 * gst_x11_window_set_wm_hints_impl:
 *
 * Sets window manager hints for size increments so the WM
 * snaps to character cell boundaries.
 */
static void
gst_x11_window_set_wm_hints_impl(
	GstWindow *window,
	gint       cw,
	gint       ch,
	gint       borderpx
){
	GstX11Window *self;
	XSizeHints *sizeh;
	XClassHint class_hint;
	XWMHints wm;

	self = GST_X11_WINDOW(window);

	sizeh = XAllocSizeHints();

	sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
	sizeh->height = (gint)self->height;
	sizeh->width = (gint)self->width;
	sizeh->height_inc = ch;
	sizeh->width_inc = cw;
	sizeh->base_height = 2 * borderpx;
	sizeh->base_width = 2 * borderpx;
	sizeh->min_height = ch + 2 * borderpx;
	sizeh->min_width = cw + 2 * borderpx;

	memset(&wm, 0, sizeof(wm));
	wm.flags = InputHint;
	wm.input = 1;

	class_hint.res_name = (char *)"gst";
	class_hint.res_class = (char *)"Gst";

	XSetWMProperties(self->display, self->xwindow, NULL, NULL,
		NULL, 0, sizeh, &wm, &class_hint);
	XFree(sizeh);
}

/*
 * gst_x11_window_start_event_watch_impl:
 *
 * Starts watching for X11 events via GLib main loop by
 * setting up a GIOChannel on the X11 connection fd.
 */
static void
gst_x11_window_start_event_watch_impl(GstWindow *window)
{
	GstX11Window *self;
	gint xfd;

	self = GST_X11_WINDOW(window);

	if (self->x11_watch_id != 0) {
		return; /* Already watching */
	}

	xfd = ConnectionNumber(self->display);
	self->x11_channel = g_io_channel_unix_new(xfd);
	g_io_channel_set_encoding(self->x11_channel, NULL, NULL);
	g_io_channel_set_buffered(self->x11_channel, FALSE);

	self->x11_watch_id = g_io_add_watch(self->x11_channel,
		G_IO_IN | G_IO_ERR | G_IO_HUP,
		on_x11_event, self);
}

/* ===== GObject lifecycle ===== */

static void
gst_x11_window_dispose(GObject *object)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(object);

	/* Remove event watch */
	if (self->x11_watch_id != 0) {
		g_source_remove(self->x11_watch_id);
		self->x11_watch_id = 0;
	}

	if (self->x11_channel != NULL) {
		g_io_channel_unref(self->x11_channel);
		self->x11_channel = NULL;
	}

	g_clear_pointer(&self->sel_primary, g_free);
	g_clear_pointer(&self->sel_clipboard, g_free);
	g_clear_pointer(&self->title, g_free);

	G_OBJECT_CLASS(gst_x11_window_parent_class)->dispose(object);
}

static void
gst_x11_window_finalize(GObject *object)
{
	GstX11Window *self;

	self = GST_X11_WINDOW(object);

	/* Destroy IME */
	if (self->xic != NULL) {
		XDestroyIC(self->xic);
		self->xic = NULL;
	}
	if (self->xim != NULL) {
		XCloseIM(self->xim);
		self->xim = NULL;
	}

	/* Destroy window */
	if (self->xwindow != 0 && self->display != NULL) {
		XDestroyWindow(self->display, self->xwindow);
		self->xwindow = 0;
	}

	/* Close display */
	if (self->display != NULL) {
		XCloseDisplay(self->display);
		self->display = NULL;
	}

	G_OBJECT_CLASS(gst_x11_window_parent_class)->finalize(object);
}

static void
gst_x11_window_class_init(GstX11WindowClass *klass)
{
	GObjectClass *object_class;
	GstWindowClass *window_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_x11_window_dispose;
	object_class->finalize = gst_x11_window_finalize;

	window_class = GST_WINDOW_CLASS(klass);
	window_class->show = gst_x11_window_show_impl;
	window_class->hide = gst_x11_window_hide_impl;
	window_class->resize = gst_x11_window_resize_impl;
	window_class->set_title = gst_x11_window_set_title_impl;
	window_class->set_selection = gst_x11_window_set_selection_impl;
	window_class->paste_clipboard = gst_x11_window_paste_clipboard_impl;
	window_class->paste_primary = gst_x11_window_paste_primary_impl;
	window_class->copy_to_clipboard = gst_x11_window_copy_to_clipboard_impl;
	window_class->bell = gst_x11_window_bell_impl;
	window_class->set_opacity = gst_x11_window_set_opacity_impl;
	window_class->set_pointer_motion = gst_x11_window_set_pointer_motion_impl;
	window_class->set_wm_hints = gst_x11_window_set_wm_hints_impl;
	window_class->start_event_watch = gst_x11_window_start_event_watch_impl;
}

static void
gst_x11_window_init(GstX11Window *self)
{
	self->display = NULL;
	self->xwindow = 0;
	self->colormap = 0;
	self->visual = NULL;
	self->screen = 0;
	self->xembed = 0;
	self->wmdeletewin = 0;
	self->netwmname = 0;
	self->netwmiconname = 0;
	self->netwmpid = 0;
	self->xim = NULL;
	self->xic = NULL;
	self->xtarget = 0;
	self->sel_primary = NULL;
	self->sel_clipboard = NULL;
	self->width = 800;
	self->height = 600;
	self->title = g_strdup("GST Terminal");
	self->visible = FALSE;
	self->x11_channel = NULL;
	self->x11_watch_id = 0;
}

/* ===== Public API ===== */

/**
 * gst_x11_window_new:
 * @cols: terminal columns
 * @rows: terminal rows
 * @cw: char cell width
 * @ch: char cell height
 * @borderpx: border padding
 * @embed_id: parent window for embedding (0 for root)
 *
 * Creates an X11 window. Ports st's xinit().
 *
 * Returns: (transfer full) (nullable): new window or NULL
 */
GstX11Window *
gst_x11_window_new(
	gint    cols,
	gint    rows,
	gint    cw,
	gint    ch,
	gint    borderpx,
	gulong  embed_id
){
	GstX11Window *self;
	Display *dpy;
	Window parent;
	XSetWindowAttributes attrs;
	Cursor cursor;
	pid_t thispid;

	/* Open X11 display */
	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		g_warning("gst_x11_window_new: can't open X11 display");
		return NULL;
	}

	self = (GstX11Window *)g_object_new(GST_TYPE_X11_WINDOW, NULL);
	self->display = dpy;
	self->screen = XDefaultScreen(dpy);
	self->visual = XDefaultVisual(dpy, self->screen);
	self->colormap = XDefaultColormap(dpy, self->screen);

	/* Calculate window size */
	self->width = (guint)(2 * borderpx + cols * cw);
	self->height = (guint)(2 * borderpx + rows * ch);

	/* Set up window attributes */
	memset(&attrs, 0, sizeof(attrs));
	attrs.background_pixel = BlackPixel(dpy, self->screen);
	attrs.border_pixel = BlackPixel(dpy, self->screen);
	attrs.bit_gravity = NorthWestGravity;
	attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	attrs.colormap = self->colormap;

	/* Determine parent window */
	parent = (embed_id != 0) ? (Window)embed_id
	         : XRootWindow(dpy, self->screen);

	/* Create window */
	self->xwindow = XCreateWindow(dpy, parent, 0, 0,
		self->width, self->height, 0,
		XDefaultDepth(dpy, self->screen),
		InputOutput, self->visual,
		CWBackPixel | CWBorderPixel | CWBitGravity
			| CWEventMask | CWColormap,
		&attrs);

	/* Set up mouse cursor */
	cursor = XCreateFontCursor(dpy, XC_xterm);
	XDefineCursor(dpy, self->xwindow, cursor);

	/* Register X atoms */
	self->xembed = XInternAtom(dpy, "_XEMBED", False);
	self->wmdeletewin = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	self->netwmname = XInternAtom(dpy, "_NET_WM_NAME", False);
	self->netwmiconname = XInternAtom(dpy, "_NET_WM_ICON_NAME", False);
	XSetWMProtocols(dpy, self->xwindow, &self->wmdeletewin, 1);

	/* Set _NET_WM_PID */
	self->netwmpid = XInternAtom(dpy, "_NET_WM_PID", False);
	thispid = getpid();
	XChangeProperty(dpy, self->xwindow, self->netwmpid, XA_CARDINAL, 32,
		PropModeReplace, (guchar *)&thispid, 1);

	/* Set clipboard target preference */
	self->xtarget = XInternAtom(dpy, "UTF8_STRING", 0);
	if (self->xtarget == None) {
		self->xtarget = XA_STRING;
	}

	/* Initialize input method */
	self->xim = XOpenIM(dpy, NULL, NULL, NULL);
	if (self->xim != NULL) {
		self->xic = XCreateIC(self->xim,
			XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
			XNClientWindow, self->xwindow,
			XNFocusWindow, self->xwindow,
			NULL);
	}

	/* Set window title */
	gst_x11_window_set_title_impl(GST_WINDOW(self), self->title);

	return self;
}

Display *
gst_x11_window_get_display(GstX11Window *self)
{
	g_return_val_if_fail(GST_IS_X11_WINDOW(self), NULL);

	return self->display;
}

Window
gst_x11_window_get_xid(GstX11Window *self)
{
	g_return_val_if_fail(GST_IS_X11_WINDOW(self), 0);

	return self->xwindow;
}

Visual *
gst_x11_window_get_visual(GstX11Window *self)
{
	g_return_val_if_fail(GST_IS_X11_WINDOW(self), NULL);

	return self->visual;
}

Colormap
gst_x11_window_get_colormap(GstX11Window *self)
{
	g_return_val_if_fail(GST_IS_X11_WINDOW(self), 0);

	return self->colormap;
}

gint
gst_x11_window_get_screen(GstX11Window *self)
{
	g_return_val_if_fail(GST_IS_X11_WINDOW(self), 0);

	return self->screen;
}
