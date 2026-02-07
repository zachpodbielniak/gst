/*
 * gst-wayland-window.c - Wayland window implementation
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Wayland-based terminal window using libdecor for universal window
 * decorations (CSD on GNOME, SSD on wlroots), wl_keyboard + xkbcommon
 * for input, wl_data_device for clipboard, and GLib main loop integration.
 * Implements all GstWindow virtual methods for the Wayland backend.
 */

#include "gst-wayland-window.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

/* Generated Wayland protocol implementations */
#include "../wayland-protocols/primary-selection-unstable-v1-client-protocol.h"
#include "../wayland-protocols/primary-selection-unstable-v1-protocol.c"

#include <libdecor.h>

/*
 * X11 modifier mask compatibility.
 * xkbcommon keysyms are compatible with X11 keysyms.
 * We define the modifier mask values so that the keybind system
 * (which uses ShiftMask, ControlMask, Mod1Mask) works unchanged.
 */
#ifndef ShiftMask
#define ShiftMask   (1 << 0)
#define LockMask    (1 << 1)
#define ControlMask (1 << 2)
#define Mod1Mask    (1 << 3)
#define Mod2Mask    (1 << 4)
#define Mod3Mask    (1 << 5)
#define Mod4Mask    (1 << 6)
#define Mod5Mask    (1 << 7)
#endif

/**
 * SECTION:gst-wayland-window
 * @title: GstWaylandWindow
 * @short_description: Wayland window with libdecor and xkbcommon
 *
 * #GstWaylandWindow implements #GstWindow for the Wayland display
 * protocol. Uses libdecor for window management and universal
 * decorations, xkbcommon for keyboard input processing,
 * wl_data_device for clipboard, and zwp_primary_selection for
 * primary selection.
 */

struct _GstWaylandWindow
{
	GstWindow parent_instance;

	/* Wayland core globals */
	struct wl_display    *display;
	struct wl_registry   *registry;
	struct wl_compositor *compositor;
	struct wl_shm        *shm;
	struct wl_seat       *seat;
	struct wl_output     *output;

	/* Window surface and libdecor */
	struct wl_surface      *surface;
	struct libdecor        *libdecor_ctx;
	struct libdecor_frame  *libdecor_frame;

	/* Keyboard input */
	struct wl_keyboard   *keyboard;
	struct xkb_context   *xkb_ctx;
	struct xkb_keymap    *xkb_keymap;
	struct xkb_state     *xkb_state;

	/* Pointer input */
	struct wl_pointer    *pointer;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor      *default_cursor;
	struct wl_surface     *cursor_surface;
	gdouble              pointer_x;
	gdouble              pointer_y;
	guint                pointer_button_state;
	guint32              pointer_serial;

	/* Clipboard (wl_data_device) */
	struct wl_data_device_manager *data_device_manager;
	struct wl_data_device *data_device;
	struct wl_data_offer  *data_offer;
	struct wl_data_source *data_source;

	/* Primary selection */
	struct zwp_primary_selection_device_manager_v1 *primary_mgr;
	struct zwp_primary_selection_device_v1 *primary_device;
	struct zwp_primary_selection_offer_v1  *primary_offer;
	struct zwp_primary_selection_source_v1 *primary_source;

	/* Selection text storage */
	gchar *selection_text;
	gchar *clipboard_text;

	/* Window state */
	gint    win_w;
	gint    win_h;
	gint    cw;
	gint    ch;
	gint    borderpx;
	guint32 keyboard_serial;
	gboolean configured;
	gboolean closed;
	gboolean focused;

	/* Key repeat (via GLib timeout) */
	guint repeat_timer_id;
	guint32 repeat_key;
	gint32 repeat_delay;
	gint32 repeat_rate;

	/* Rendering-level opacity (0.0 = transparent, 1.0 = opaque) */
	gdouble opacity;

	/* GLib main loop integration */
	GIOChannel *wl_channel;
	guint       wl_watch_id;
};

G_DEFINE_TYPE(GstWaylandWindow, gst_wayland_window, GST_TYPE_WINDOW)

/* Forward declarations for listener callbacks */
static void registry_global(void *data, struct wl_registry *reg,
	uint32_t id, const char *interface, uint32_t version);
static void registry_global_remove(void *data, struct wl_registry *reg,
	uint32_t id);

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove
};

/* ===== libdecor callbacks ===== */

/*
 * libdecor_error_cb:
 * @context: the libdecor context
 * @error: the error code
 * @message: human-readable error message
 *
 * Called when libdecor encounters a fatal error. Logs the
 * error message as a warning.
 */
static void
libdecor_error_cb(
	struct libdecor *context,
	enum libdecor_error error,
	const char *message
){
	(void)context;
	(void)error;
	g_warning("wayland: libdecor error: %s", message);
}

static struct libdecor_interface libdecor_iface = {
	libdecor_error_cb
};

/*
 * frame_configure_cb:
 * @frame: the libdecor frame
 * @configuration: the new configuration from the compositor
 * @user_data: the GstWaylandWindow
 *
 * Called when the compositor configures the window (resize, state change).
 * Extracts content size, commits the decoration state, and emits
 * the "configure" signal so the renderer can reallocate buffers.
 */
static void
frame_configure_cb(
	struct libdecor_frame         *frame,
	struct libdecor_configuration *configuration,
	void                          *user_data
){
	GstWaylandWindow *self;
	struct libdecor_state *state;
	int width;
	int height;

	self = (GstWaylandWindow *)user_data;
	width = 0;
	height = 0;

	/* Extract content size; returns false if compositor didn't specify */
	if (!libdecor_configuration_get_content_size(configuration,
	    frame, &width, &height)) {
		/* Use our stored size for initial configure */
		width = self->win_w;
		height = self->win_h;
	}

	state = libdecor_state_new(width, height);
	libdecor_frame_commit(frame, state, configuration);
	libdecor_state_free(state);

	if (width > 0 && height > 0) {
		if (width != self->win_w || height != self->win_h) {
			self->win_w = width;
			self->win_h = height;
			g_signal_emit_by_name(self, "configure",
				(guint)width, (guint)height);
		}
	}

	self->configured = TRUE;
}

/*
 * frame_close_cb:
 * @frame: the libdecor frame
 * @user_data: the GstWaylandWindow
 *
 * Called when the user clicks the close button. Marks the
 * window closed and emits "close-request".
 */
static void
frame_close_cb(
	struct libdecor_frame *frame,
	void                  *user_data
){
	GstWaylandWindow *self;

	(void)frame;
	self = (GstWaylandWindow *)user_data;
	self->closed = TRUE;
	g_signal_emit_by_name(self, "close-request");
}

/*
 * frame_commit_cb:
 * @frame: the libdecor frame
 * @user_data: the GstWaylandWindow
 *
 * Called by libdecor when it needs the application to commit
 * the wl_surface, so decoration subsurfaces stay in sync.
 */
static void
frame_commit_cb(
	struct libdecor_frame *frame,
	void                  *user_data
){
	GstWaylandWindow *self;

	(void)frame;
	self = (GstWaylandWindow *)user_data;
	wl_surface_commit(self->surface);
}

/*
 * frame_dismiss_popup_cb:
 * @frame: the libdecor frame
 * @seat_name: the seat that triggered the popup
 * @user_data: the GstWaylandWindow
 *
 * Called when a decoration popup should be dismissed. No-op
 * since GST has no popups.
 */
static void
frame_dismiss_popup_cb(
	struct libdecor_frame *frame,
	const char            *seat_name,
	void                  *user_data
){
	(void)frame;
	(void)seat_name;
	(void)user_data;
}

static struct libdecor_frame_interface frame_iface = {
	frame_configure_cb,
	frame_close_cb,
	frame_commit_cb,
	frame_dismiss_popup_cb
};

/* ===== Keyboard callbacks ===== */

static void
keyboard_keymap(
	void               *data,
	struct wl_keyboard *kb,
	uint32_t           format,
	int32_t            fd,
	uint32_t           size
){
	GstWaylandWindow *self;
	char *map_str;

	self = (GstWaylandWindow *)data;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map_str == MAP_FAILED) {
		close(fd);
		return;
	}

	/* Free old keymap/state */
	if (self->xkb_state != NULL) {
		xkb_state_unref(self->xkb_state);
		self->xkb_state = NULL;
	}
	if (self->xkb_keymap != NULL) {
		xkb_keymap_unref(self->xkb_keymap);
		self->xkb_keymap = NULL;
	}

	self->xkb_keymap = xkb_keymap_new_from_string(self->xkb_ctx,
		map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_str, size);
	close(fd);

	if (self->xkb_keymap == NULL) {
		g_warning("wayland: failed to compile xkb keymap");
		return;
	}

	self->xkb_state = xkb_state_new(self->xkb_keymap);
}

static void
keyboard_enter(
	void               *data,
	struct wl_keyboard *kb,
	uint32_t           serial,
	struct wl_surface  *surface,
	struct wl_array    *keys
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	self->keyboard_serial = serial;
	self->focused = TRUE;
	g_signal_emit_by_name(self, "focus-change", TRUE);
	g_signal_emit_by_name(self, "visibility", TRUE);
}

static void
keyboard_leave(
	void               *data,
	struct wl_keyboard *kb,
	uint32_t           serial,
	struct wl_surface  *surface
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	self->focused = FALSE;

	/* Cancel key repeat on focus loss */
	if (self->repeat_timer_id != 0) {
		g_source_remove(self->repeat_timer_id);
		self->repeat_timer_id = 0;
	}

	g_signal_emit_by_name(self, "focus-change", FALSE);
}

/*
 * xkb_to_x11_mods:
 * @state: xkb keyboard state
 *
 * Converts xkbcommon modifier state to X11-compatible modifier
 * mask for compatibility with the keybind system.
 *
 * Returns: X11-style modifier mask
 */
static guint
xkb_to_x11_mods(struct xkb_state *state)
{
	guint mods;

	mods = 0;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
	    XKB_STATE_MODS_EFFECTIVE)) {
		mods |= ShiftMask;
	}
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
	    XKB_STATE_MODS_EFFECTIVE)) {
		mods |= LockMask;
	}
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
	    XKB_STATE_MODS_EFFECTIVE)) {
		mods |= ControlMask;
	}
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT,
	    XKB_STATE_MODS_EFFECTIVE)) {
		mods |= Mod1Mask;
	}
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_NUM,
	    XKB_STATE_MODS_EFFECTIVE)) {
		mods |= Mod2Mask;
	}
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO,
	    XKB_STATE_MODS_EFFECTIVE)) {
		mods |= Mod4Mask;
	}

	return mods;
}

/*
 * emit_key_event:
 * @self: the Wayland window
 * @key: the raw key code
 *
 * Translates a key event to keysym + UTF-8 text and
 * emits the "key-press" signal.
 */
static void
emit_key_event(GstWaylandWindow *self, uint32_t key)
{
	xkb_keysym_t keysym;
	guint mods;
	char buf[128];
	int len;

	if (self->xkb_state == NULL) {
		return;
	}

	keysym = xkb_state_key_get_one_sym(self->xkb_state, key + 8);
	mods = xkb_to_x11_mods(self->xkb_state);
	len = xkb_state_key_get_utf8(self->xkb_state, key + 8,
		buf, sizeof(buf));

	if (len < 0) {
		len = 0;
	}
	buf[len] = '\0';

	g_signal_emit_by_name(self, "key-press",
		(guint)keysym, mods, buf, len);
}

static gboolean
key_repeat_cb(gpointer data)
{
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	emit_key_event(self, self->repeat_key);
	return G_SOURCE_CONTINUE;
}

static gboolean
key_repeat_start_cb(gpointer data)
{
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;

	/* Switch from delay timer to repeat rate timer */
	if (self->repeat_rate > 0) {
		self->repeat_timer_id = g_timeout_add(
			(guint)(1000 / self->repeat_rate),
			key_repeat_cb, self);
	} else {
		self->repeat_timer_id = 0;
	}

	/* Emit one repeat now */
	emit_key_event(self, self->repeat_key);

	return G_SOURCE_REMOVE;
}

static void
keyboard_key(
	void               *data,
	struct wl_keyboard *kb,
	uint32_t           serial,
	uint32_t           time,
	uint32_t           key,
	uint32_t           state
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	self->keyboard_serial = serial;

	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		emit_key_event(self, key);

		/* Set up key repeat */
		if (self->repeat_timer_id != 0) {
			g_source_remove(self->repeat_timer_id);
			self->repeat_timer_id = 0;
		}

		if (self->repeat_delay > 0 &&
		    xkb_keymap_key_repeats(self->xkb_keymap, key + 8)) {
			self->repeat_key = key;
			self->repeat_timer_id = g_timeout_add(
				(guint)self->repeat_delay,
				key_repeat_start_cb, self);
		}
	} else {
		/* Key released: cancel repeat */
		if (self->repeat_timer_id != 0 &&
		    self->repeat_key == key) {
			g_source_remove(self->repeat_timer_id);
			self->repeat_timer_id = 0;
		}
	}
}

static void
keyboard_modifiers(
	void               *data,
	struct wl_keyboard *kb,
	uint32_t           serial,
	uint32_t           mods_depressed,
	uint32_t           mods_latched,
	uint32_t           mods_locked,
	uint32_t           group
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	if (self->xkb_state != NULL) {
		xkb_state_update_mask(self->xkb_state,
			mods_depressed, mods_latched, mods_locked,
			0, 0, group);
	}
}

static void
keyboard_repeat_info(
	void               *data,
	struct wl_keyboard *kb,
	int32_t            rate,
	int32_t            delay
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	self->repeat_rate = rate;
	self->repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_keymap,
	keyboard_enter,
	keyboard_leave,
	keyboard_key,
	keyboard_modifiers,
	keyboard_repeat_info
};

/* ===== Pointer callbacks ===== */

static void
pointer_enter(
	void              *data,
	struct wl_pointer *pointer,
	uint32_t          serial,
	struct wl_surface *surface,
	wl_fixed_t        sx,
	wl_fixed_t        sy
){
	GstWaylandWindow *self;
	struct wl_cursor_image *image;
	struct wl_buffer *buffer;

	self = (GstWaylandWindow *)data;
	self->pointer_serial = serial;
	self->pointer_x = wl_fixed_to_double(sx);
	self->pointer_y = wl_fixed_to_double(sy);

	/* Set cursor image */
	if (self->default_cursor != NULL && self->cursor_surface != NULL) {
		image = self->default_cursor->images[0];
		buffer = wl_cursor_image_get_buffer(image);
		wl_pointer_set_cursor(pointer, serial,
			self->cursor_surface,
			(int32_t)image->hotspot_x,
			(int32_t)image->hotspot_y);
		wl_surface_attach(self->cursor_surface, buffer, 0, 0);
		wl_surface_damage(self->cursor_surface, 0, 0,
			(int32_t)image->width, (int32_t)image->height);
		wl_surface_commit(self->cursor_surface);
	}
}

static void
pointer_leave(
	void              *data,
	struct wl_pointer *pointer,
	uint32_t          serial,
	struct wl_surface *surface
){
	(void)data;
	(void)pointer;
	(void)serial;
	(void)surface;
}

static void
pointer_motion(
	void              *data,
	struct wl_pointer *pointer,
	uint32_t          time,
	wl_fixed_t        sx,
	wl_fixed_t        sy
){
	GstWaylandWindow *self;
	guint mods;

	self = (GstWaylandWindow *)data;
	self->pointer_x = wl_fixed_to_double(sx);
	self->pointer_y = wl_fixed_to_double(sy);

	mods = (self->xkb_state != NULL)
		? xkb_to_x11_mods(self->xkb_state) : 0;
	mods |= self->pointer_button_state;

	g_signal_emit_by_name(self, "motion-notify",
		mods,
		(gint)self->pointer_x,
		(gint)self->pointer_y);
}

static void
pointer_button(
	void              *data,
	struct wl_pointer *pointer,
	uint32_t          serial,
	uint32_t          time,
	uint32_t          button,
	uint32_t          state
){
	GstWaylandWindow *self;
	guint x11_button;
	guint mods;

	self = (GstWaylandWindow *)data;
	self->pointer_serial = serial;

	/*
	 * Convert Linux button codes to X11 button numbers.
	 * BTN_LEFT=0x110=1, BTN_RIGHT=0x111=3, BTN_MIDDLE=0x112=2
	 */
	switch (button) {
	case 0x110: x11_button = 1; break; /* BTN_LEFT */
	case 0x111: x11_button = 3; break; /* BTN_RIGHT */
	case 0x112: x11_button = 2; break; /* BTN_MIDDLE */
	default:    x11_button = button - 0x110 + 1; break;
	}

	/* Track button state for motion events */
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (x11_button == 1) {
			self->pointer_button_state |= (1 << 8);
		} else if (x11_button == 2) {
			self->pointer_button_state |= (1 << 9);
		} else if (x11_button == 3) {
			self->pointer_button_state |= (1 << 10);
		}
	} else {
		if (x11_button == 1) {
			self->pointer_button_state &= ~(1 << 8);
		} else if (x11_button == 2) {
			self->pointer_button_state &= ~(1 << 9);
		} else if (x11_button == 3) {
			self->pointer_button_state &= ~(1 << 10);
		}
	}

	mods = (self->xkb_state != NULL)
		? xkb_to_x11_mods(self->xkb_state) : 0;
	mods |= self->pointer_button_state;

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		g_signal_emit_by_name(self, "button-press",
			x11_button, mods,
			(gint)self->pointer_x, (gint)self->pointer_y,
			(gulong)time);
	} else {
		g_signal_emit_by_name(self, "button-release",
			x11_button, mods,
			(gint)self->pointer_x, (gint)self->pointer_y,
			(gulong)time);
	}
}

static void
pointer_axis(
	void              *data,
	struct wl_pointer *pointer,
	uint32_t          time,
	uint32_t          axis,
	wl_fixed_t        value
){
	GstWaylandWindow *self;
	guint x11_button;
	guint mods;
	gdouble val;

	self = (GstWaylandWindow *)data;
	val = wl_fixed_to_double(value);

	/* Translate axis to X11 scroll buttons */
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		x11_button = (val > 0) ? 5 : 4; /* 5=down, 4=up */
	} else {
		x11_button = (val > 0) ? 7 : 6; /* 7=right, 6=left */
	}

	mods = (self->xkb_state != NULL)
		? xkb_to_x11_mods(self->xkb_state) : 0;

	/* Emit press + release for scroll, matching X11 behavior */
	g_signal_emit_by_name(self, "button-press",
		x11_button, mods,
		(gint)self->pointer_x, (gint)self->pointer_y,
		(gulong)time);
	g_signal_emit_by_name(self, "button-release",
		x11_button, mods,
		(gint)self->pointer_x, (gint)self->pointer_y,
		(gulong)time);
}

static void
pointer_frame(void *data, struct wl_pointer *pointer)
{
	(void)data; (void)pointer;
}

static void
pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{
	(void)data; (void)pointer; (void)source;
}

static void
pointer_axis_stop(void *data, struct wl_pointer *pointer,
	uint32_t time, uint32_t axis)
{
	(void)data; (void)pointer; (void)time; (void)axis;
}

static void
pointer_axis_discrete(void *data, struct wl_pointer *pointer,
	uint32_t axis, int32_t discrete)
{
	(void)data; (void)pointer; (void)axis; (void)discrete;
}

static void
pointer_axis_value120(void *data, struct wl_pointer *pointer,
	uint32_t axis, int32_t value120)
{
	(void)data; (void)pointer; (void)axis; (void)value120;
}

static void
pointer_axis_relative_direction(void *data, struct wl_pointer *pointer,
	uint32_t axis, uint32_t direction)
{
	(void)data; (void)pointer; (void)axis; (void)direction;
}

static const struct wl_pointer_listener pointer_listener = {
	pointer_enter,
	pointer_leave,
	pointer_motion,
	pointer_button,
	pointer_axis,
	pointer_frame,
	pointer_axis_source,
	pointer_axis_stop,
	pointer_axis_discrete,
	pointer_axis_value120,
	pointer_axis_relative_direction
};

/* ===== Clipboard (wl_data_device) callbacks ===== */

static void
data_offer_offer(
	void                 *data,
	struct wl_data_offer *offer,
	const char           *mime_type
){
	/* Accept text/plain offers for paste */
	if (g_strcmp0(mime_type, "text/plain;charset=utf-8") == 0 ||
	    g_strcmp0(mime_type, "text/plain") == 0) {
		wl_data_offer_accept(offer, 0, mime_type);
	}
}

static void
data_offer_source_actions(void *data, struct wl_data_offer *offer,
	uint32_t source_actions)
{
	(void)data; (void)offer; (void)source_actions;
}

static void
data_offer_action(void *data, struct wl_data_offer *offer, uint32_t action)
{
	(void)data; (void)offer; (void)action;
}

static const struct wl_data_offer_listener data_offer_listener = {
	data_offer_offer,
	data_offer_source_actions,
	data_offer_action
};

static void
data_device_data_offer(
	void                  *data,
	struct wl_data_device *device,
	struct wl_data_offer  *offer
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;

	/* Free previous offer */
	if (self->data_offer != NULL) {
		wl_data_offer_destroy(self->data_offer);
	}
	self->data_offer = offer;
	wl_data_offer_add_listener(offer, &data_offer_listener, self);
}

static void
data_device_enter(void *data, struct wl_data_device *device,
	uint32_t serial, struct wl_surface *surface,
	wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer)
{
	(void)data; (void)device; (void)serial;
	(void)surface; (void)x; (void)y; (void)offer;
}

static void
data_device_leave(void *data, struct wl_data_device *device)
{
	(void)data; (void)device;
}

static void
data_device_motion(void *data, struct wl_data_device *device,
	uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)data; (void)device; (void)time; (void)x; (void)y;
}

static void
data_device_drop(void *data, struct wl_data_device *device)
{
	(void)data; (void)device;
}

static void
data_device_selection(
	void                  *data,
	struct wl_data_device *device,
	struct wl_data_offer  *offer
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;

	if (self->data_offer != NULL && self->data_offer != offer) {
		wl_data_offer_destroy(self->data_offer);
	}
	self->data_offer = offer;
}

static const struct wl_data_device_listener data_device_listener = {
	data_device_data_offer,
	data_device_enter,
	data_device_leave,
	data_device_motion,
	data_device_drop,
	data_device_selection
};

/* ===== Primary selection callbacks ===== */

static void
primary_offer_offer(
	void                                    *data,
	struct zwp_primary_selection_offer_v1   *offer,
	const char                              *mime_type
){
	(void)data; (void)offer; (void)mime_type;
}

static const struct zwp_primary_selection_offer_v1_listener
primary_offer_listener = {
	primary_offer_offer
};

static void
primary_device_data_offer(
	void                                    *data,
	struct zwp_primary_selection_device_v1   *device,
	struct zwp_primary_selection_offer_v1   *offer
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	if (self->primary_offer != NULL) {
		zwp_primary_selection_offer_v1_destroy(self->primary_offer);
	}
	self->primary_offer = offer;
	zwp_primary_selection_offer_v1_add_listener(offer,
		&primary_offer_listener, self);
}

static void
primary_device_selection(
	void                                    *data,
	struct zwp_primary_selection_device_v1   *device,
	struct zwp_primary_selection_offer_v1   *offer
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	if (self->primary_offer != NULL && self->primary_offer != offer) {
		zwp_primary_selection_offer_v1_destroy(self->primary_offer);
	}
	self->primary_offer = offer;
}

static const struct zwp_primary_selection_device_v1_listener
primary_device_listener = {
	primary_device_data_offer,
	primary_device_selection
};

/* ===== Seat callbacks ===== */

static void
seat_capabilities(
	void           *data,
	struct wl_seat *seat,
	uint32_t       caps
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;

	/* Keyboard */
	if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
		if (self->keyboard == NULL) {
			self->keyboard = wl_seat_get_keyboard(seat);
			wl_keyboard_add_listener(self->keyboard,
				&keyboard_listener, self);
		}
	} else if (self->keyboard != NULL) {
		wl_keyboard_destroy(self->keyboard);
		self->keyboard = NULL;
	}

	/* Pointer */
	if (caps & WL_SEAT_CAPABILITY_POINTER) {
		if (self->pointer == NULL) {
			self->pointer = wl_seat_get_pointer(seat);
			wl_pointer_add_listener(self->pointer,
				&pointer_listener, self);
		}
	} else if (self->pointer != NULL) {
		wl_pointer_destroy(self->pointer);
		self->pointer = NULL;
	}
}

static void
seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
	seat_capabilities,
	seat_name
};

/* ===== Registry callbacks ===== */

static void
registry_global(
	void               *data,
	struct wl_registry *reg,
	uint32_t           id,
	const char         *interface,
	uint32_t           version
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;

	if (g_strcmp0(interface, wl_compositor_interface.name) == 0) {
		self->compositor = (struct wl_compositor *)wl_registry_bind(
			reg, id, &wl_compositor_interface,
			MIN(version, 4));
	} else if (g_strcmp0(interface, wl_shm_interface.name) == 0) {
		self->shm = (struct wl_shm *)wl_registry_bind(
			reg, id, &wl_shm_interface,
			MIN(version, 1));
	} else if (g_strcmp0(interface, wl_seat_interface.name) == 0) {
		self->seat = (struct wl_seat *)wl_registry_bind(
			reg, id, &wl_seat_interface,
			MIN(version, 5));
		wl_seat_add_listener(self->seat, &seat_listener, self);
	} else if (g_strcmp0(interface,
	    wl_data_device_manager_interface.name) == 0) {
		self->data_device_manager =
			(struct wl_data_device_manager *)wl_registry_bind(
			reg, id, &wl_data_device_manager_interface,
			MIN(version, 3));
	} else if (g_strcmp0(interface,
	    zwp_primary_selection_device_manager_v1_interface.name) == 0) {
		self->primary_mgr =
			(struct zwp_primary_selection_device_manager_v1 *)
			wl_registry_bind(reg, id,
			&zwp_primary_selection_device_manager_v1_interface,
			MIN(version, 1));
	} else if (g_strcmp0(interface, wl_output_interface.name) == 0) {
		if (self->output == NULL) {
			self->output = (struct wl_output *)wl_registry_bind(
				reg, id, &wl_output_interface,
				MIN(version, 2));
		}
	}
}

static void
registry_global_remove(
	void               *data,
	struct wl_registry *reg,
	uint32_t           id
){
	(void)data; (void)reg; (void)id;
}

/* ===== Clipboard helper: read data from fd ===== */

/*
 * read_fd_to_string:
 * @fd: file descriptor to read from
 *
 * Reads all data from a file descriptor into a string.
 * Closes the fd when done.
 *
 * Returns: (transfer full): the data as a string, or NULL
 */
static gchar *
read_fd_to_string(gint fd)
{
	GString *buf;
	gchar tmp[4096];
	gssize n;

	buf = g_string_new(NULL);

	while (TRUE) {
		n = read(fd, tmp, sizeof(tmp));
		if (n > 0) {
			g_string_append_len(buf, tmp, n);
		} else if (n == 0) {
			break;
		} else {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
	}

	close(fd);
	return g_string_free(buf, FALSE);
}

/* ===== Data source callbacks (for setting selection) ===== */

static void
data_source_send(
	void                  *data,
	struct wl_data_source *source,
	const char            *mime_type,
	int32_t               fd
){
	GstWaylandWindow *self;
	const gchar *text;
	gssize len;
	gssize written;

	self = (GstWaylandWindow *)data;
	text = self->clipboard_text;

	if (text == NULL) {
		close(fd);
		return;
	}

	len = (gssize)strlen(text);
	while (len > 0) {
		written = write(fd, text, (gsize)len);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		text += written;
		len -= written;
	}
	close(fd);
}

static void
data_source_cancelled(
	void                  *data,
	struct wl_data_source *source
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	if (self->data_source == source) {
		wl_data_source_destroy(source);
		self->data_source = NULL;
	}
}

static void
data_source_target(void *data, struct wl_data_source *source,
	const char *mime_type)
{
	(void)data; (void)source; (void)mime_type;
}

static void
data_source_dnd_drop_performed(void *data, struct wl_data_source *source)
{
	(void)data; (void)source;
}

static void
data_source_dnd_finished(void *data, struct wl_data_source *source)
{
	(void)data; (void)source;
}

static void
data_source_action(void *data, struct wl_data_source *source, uint32_t action)
{
	(void)data; (void)source; (void)action;
}

static const struct wl_data_source_listener data_source_listener = {
	data_source_target,
	data_source_send,
	data_source_cancelled,
	data_source_dnd_drop_performed,
	data_source_dnd_finished,
	data_source_action
};

/* ===== Primary selection source callbacks ===== */

static void
primary_source_send(
	void                                    *data,
	struct zwp_primary_selection_source_v1   *source,
	const char                              *mime_type,
	int32_t                                 fd
){
	GstWaylandWindow *self;
	const gchar *text;
	gssize len;
	gssize written;

	self = (GstWaylandWindow *)data;
	text = self->selection_text;

	if (text == NULL) {
		close(fd);
		return;
	}

	len = (gssize)strlen(text);
	while (len > 0) {
		written = write(fd, text, (gsize)len);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		text += written;
		len -= written;
	}
	close(fd);
}

static void
primary_source_cancelled(
	void                                    *data,
	struct zwp_primary_selection_source_v1   *source
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;
	if (self->primary_source == source) {
		zwp_primary_selection_source_v1_destroy(source);
		self->primary_source = NULL;
	}
}

static const struct zwp_primary_selection_source_v1_listener
primary_source_listener = {
	primary_source_send,
	primary_source_cancelled
};

/* ===== GstWindow vfunc implementations ===== */

static void
gst_wayland_window_show_impl(GstWindow *window)
{
	GstWaylandWindow *self;

	self = GST_WAYLAND_WINDOW(window);
	if (self->surface != NULL) {
		wl_surface_commit(self->surface);
		wl_display_flush(self->display);
	}
}

static void
gst_wayland_window_hide_impl(GstWindow *window)
{
	/* Wayland doesn't have a hide concept for toplevel;
	 * minimize would be the closest equivalent but isn't
	 * standard in xdg-shell */
	(void)window;
}

static void
gst_wayland_window_resize_impl(
	GstWindow *window,
	guint      width,
	guint      height
){
	GstWaylandWindow *self;

	self = GST_WAYLAND_WINDOW(window);
	self->win_w = (gint)width;
	self->win_h = (gint)height;
}

static void
gst_wayland_window_set_title_impl(
	GstWindow   *window,
	const gchar *title
){
	GstWaylandWindow *self;

	self = GST_WAYLAND_WINDOW(window);
	if (self->libdecor_frame != NULL && title != NULL) {
		libdecor_frame_set_title(self->libdecor_frame, title);
	}
}

static void
gst_wayland_window_set_selection_impl(
	GstWindow   *window,
	const gchar *text,
	gboolean     is_clipboard
){
	GstWaylandWindow *self;

	self = GST_WAYLAND_WINDOW(window);

	if (is_clipboard) {
		/* Set clipboard via wl_data_source */
		g_free(self->clipboard_text);
		self->clipboard_text = g_strdup(text);

		if (self->data_device_manager != NULL &&
		    self->data_device != NULL) {
			if (self->data_source != NULL) {
				wl_data_source_destroy(self->data_source);
			}
			self->data_source = wl_data_device_manager_create_data_source(
				self->data_device_manager);
			wl_data_source_add_listener(self->data_source,
				&data_source_listener, self);
			wl_data_source_offer(self->data_source,
				"text/plain;charset=utf-8");
			wl_data_device_set_selection(self->data_device,
				self->data_source, self->keyboard_serial);
		}
	} else {
		/* Set primary selection */
		g_free(self->selection_text);
		self->selection_text = g_strdup(text);

		if (self->primary_mgr != NULL &&
		    self->primary_device != NULL) {
			if (self->primary_source != NULL) {
				zwp_primary_selection_source_v1_destroy(
					self->primary_source);
			}
			self->primary_source =
				zwp_primary_selection_device_manager_v1_create_source(
				self->primary_mgr);
			zwp_primary_selection_source_v1_add_listener(
				self->primary_source,
				&primary_source_listener, self);
			zwp_primary_selection_source_v1_offer(
				self->primary_source,
				"text/plain;charset=utf-8");
			zwp_primary_selection_device_v1_set_selection(
				self->primary_device,
				self->primary_source,
				self->keyboard_serial);
		}
	}
}

static void
gst_wayland_window_paste_clipboard_impl(GstWindow *window)
{
	GstWaylandWindow *self;
	gint fds[2];
	gchar *text;

	self = GST_WAYLAND_WINDOW(window);

	if (self->data_offer == NULL) {
		return;
	}

	if (pipe(fds) < 0) {
		return;
	}

	wl_data_offer_receive(self->data_offer,
		"text/plain;charset=utf-8", fds[1]);
	close(fds[1]);
	wl_display_flush(self->display);

	/* Read synchronously (small amount of data for paste) */
	text = read_fd_to_string(fds[0]);
	if (text != NULL && text[0] != '\0') {
		g_signal_emit_by_name(self, "selection-notify",
			text, (gint)strlen(text));
	}
	g_free(text);
}

static void
gst_wayland_window_paste_primary_impl(GstWindow *window)
{
	GstWaylandWindow *self;
	gint fds[2];
	gchar *text;

	self = GST_WAYLAND_WINDOW(window);

	if (self->primary_offer == NULL) {
		return;
	}

	if (pipe(fds) < 0) {
		return;
	}

	zwp_primary_selection_offer_v1_receive(self->primary_offer,
		"text/plain;charset=utf-8", fds[1]);
	close(fds[1]);
	wl_display_flush(self->display);

	text = read_fd_to_string(fds[0]);
	if (text != NULL && text[0] != '\0') {
		g_signal_emit_by_name(self, "selection-notify",
			text, (gint)strlen(text));
	}
	g_free(text);
}

static void
gst_wayland_window_copy_to_clipboard_impl(GstWindow *window)
{
	GstWaylandWindow *self;

	self = GST_WAYLAND_WINDOW(window);

	/* Copy primary selection text to clipboard */
	if (self->selection_text != NULL) {
		gst_wayland_window_set_selection_impl(window,
			self->selection_text, TRUE);
	}
}

static void
gst_wayland_window_bell_impl(GstWindow *window)
{
	/* Wayland has no standard bell mechanism.
	 * xdg_activation could be used for urgency,
	 * but it's not universally supported.
	 * Just log for now. */
	g_debug("wayland: bell");
	(void)window;
}

static void
gst_wayland_window_set_opacity_impl(
	GstWindow *window,
	gdouble    opacity
){
	GstWaylandWindow *self;

	/*
	 * Wayland has no compositor-level opacity protocol like X11's
	 * _NET_WM_WINDOW_OPACITY. Instead, the renderer reads this
	 * value and paints backgrounds with alpha directly into
	 * the ARGB8888 shm buffer.
	 */
	self = GST_WAYLAND_WINDOW(window);
	self->opacity = CLAMP(opacity, 0.0, 1.0);
	g_debug("wayland: set_opacity(%.2f)", self->opacity);
}

static void
gst_wayland_window_set_pointer_motion_impl(
	GstWindow *window,
	gboolean   enable
){
	/* On Wayland, pointer motion events are always delivered
	 * when the pointer is in the surface. No event mask to change. */
	(void)window;
	(void)enable;
}

static void
gst_wayland_window_set_wm_hints_impl(
	GstWindow *window,
	gint       cw,
	gint       ch,
	gint       borderpx
){
	GstWaylandWindow *self;

	self = GST_WAYLAND_WINDOW(window);
	self->cw = cw;
	self->ch = ch;
	self->borderpx = borderpx;

	/* Set minimum content size in libdecor frame */
	if (self->libdecor_frame != NULL) {
		libdecor_frame_set_min_content_size(self->libdecor_frame,
			cw + 2 * borderpx, ch + 2 * borderpx);
	}
}

/* ===== GLib main loop integration ===== */

static gboolean
wayland_event_cb(
	GIOChannel  *source,
	GIOCondition condition,
	gpointer     data
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)data;

	if (condition & (G_IO_ERR | G_IO_HUP)) {
		g_warning("wayland: display connection lost");
		g_signal_emit_by_name(self, "close-request");
		return G_SOURCE_REMOVE;
	}

	if (condition & G_IO_IN) {
		if (wl_display_dispatch(self->display) < 0) {
			g_warning("wayland: dispatch error");
			g_signal_emit_by_name(self, "close-request");
			return G_SOURCE_REMOVE;
		}
		/* Process libdecor plugin events (decoration redraws, etc.) */
		if (self->libdecor_ctx != NULL) {
			libdecor_dispatch(self->libdecor_ctx, 0);
		}
	}

	wl_display_flush(self->display);
	return G_SOURCE_CONTINUE;
}

static void
gst_wayland_window_start_event_watch_impl(GstWindow *window)
{
	GstWaylandWindow *self;
	gint fd;

	self = GST_WAYLAND_WINDOW(window);

	fd = wl_display_get_fd(self->display);
	self->wl_channel = g_io_channel_unix_new(fd);
	self->wl_watch_id = g_io_add_watch(self->wl_channel,
		G_IO_IN | G_IO_ERR | G_IO_HUP,
		wayland_event_cb, self);

	/* Flush any pending requests */
	wl_display_flush(self->display);
}

/* ===== GObject lifecycle ===== */

static void
gst_wayland_window_dispose(GObject *object)
{
	GstWaylandWindow *self;

	self = GST_WAYLAND_WINDOW(object);

	/* Cancel key repeat timer */
	if (self->repeat_timer_id != 0) {
		g_source_remove(self->repeat_timer_id);
		self->repeat_timer_id = 0;
	}

	/* Remove event watch */
	if (self->wl_watch_id != 0) {
		g_source_remove(self->wl_watch_id);
		self->wl_watch_id = 0;
	}
	if (self->wl_channel != NULL) {
		g_io_channel_unref(self->wl_channel);
		self->wl_channel = NULL;
	}

	/* Free selection text */
	g_clear_pointer(&self->selection_text, g_free);
	g_clear_pointer(&self->clipboard_text, g_free);

	/* Destroy Wayland objects in reverse order */
	if (self->primary_source != NULL) {
		zwp_primary_selection_source_v1_destroy(self->primary_source);
		self->primary_source = NULL;
	}
	if (self->primary_offer != NULL) {
		zwp_primary_selection_offer_v1_destroy(self->primary_offer);
		self->primary_offer = NULL;
	}
	if (self->primary_device != NULL) {
		zwp_primary_selection_device_v1_destroy(self->primary_device);
		self->primary_device = NULL;
	}
	if (self->primary_mgr != NULL) {
		zwp_primary_selection_device_manager_v1_destroy(
			self->primary_mgr);
		self->primary_mgr = NULL;
	}

	if (self->data_source != NULL) {
		wl_data_source_destroy(self->data_source);
		self->data_source = NULL;
	}
	if (self->data_offer != NULL) {
		wl_data_offer_destroy(self->data_offer);
		self->data_offer = NULL;
	}
	if (self->data_device != NULL) {
		wl_data_device_destroy(self->data_device);
		self->data_device = NULL;
	}
	if (self->data_device_manager != NULL) {
		wl_data_device_manager_destroy(self->data_device_manager);
		self->data_device_manager = NULL;
	}

	if (self->cursor_surface != NULL) {
		wl_surface_destroy(self->cursor_surface);
		self->cursor_surface = NULL;
	}
	if (self->cursor_theme != NULL) {
		wl_cursor_theme_destroy(self->cursor_theme);
		self->cursor_theme = NULL;
	}

	if (self->pointer != NULL) {
		wl_pointer_destroy(self->pointer);
		self->pointer = NULL;
	}
	if (self->keyboard != NULL) {
		wl_keyboard_destroy(self->keyboard);
		self->keyboard = NULL;
	}

	if (self->xkb_state != NULL) {
		xkb_state_unref(self->xkb_state);
		self->xkb_state = NULL;
	}
	if (self->xkb_keymap != NULL) {
		xkb_keymap_unref(self->xkb_keymap);
		self->xkb_keymap = NULL;
	}
	if (self->xkb_ctx != NULL) {
		xkb_context_unref(self->xkb_ctx);
		self->xkb_ctx = NULL;
	}

	if (self->libdecor_frame != NULL) {
		libdecor_frame_unref(self->libdecor_frame);
		self->libdecor_frame = NULL;
	}
	if (self->libdecor_ctx != NULL) {
		libdecor_unref(self->libdecor_ctx);
		self->libdecor_ctx = NULL;
	}

	if (self->surface != NULL) {
		wl_surface_destroy(self->surface);
		self->surface = NULL;
	}
	if (self->seat != NULL) {
		wl_seat_destroy(self->seat);
		self->seat = NULL;
	}
	if (self->output != NULL) {
		wl_output_destroy(self->output);
		self->output = NULL;
	}
	if (self->shm != NULL) {
		wl_shm_destroy(self->shm);
		self->shm = NULL;
	}
	if (self->compositor != NULL) {
		wl_compositor_destroy(self->compositor);
		self->compositor = NULL;
	}
	if (self->registry != NULL) {
		wl_registry_destroy(self->registry);
		self->registry = NULL;
	}
	if (self->display != NULL) {
		wl_display_disconnect(self->display);
		self->display = NULL;
	}

	G_OBJECT_CLASS(gst_wayland_window_parent_class)->dispose(object);
}

static void
gst_wayland_window_class_init(GstWaylandWindowClass *klass)
{
	GObjectClass *object_class;
	GstWindowClass *window_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = gst_wayland_window_dispose;

	window_class = GST_WINDOW_CLASS(klass);
	window_class->show = gst_wayland_window_show_impl;
	window_class->hide = gst_wayland_window_hide_impl;
	window_class->resize = gst_wayland_window_resize_impl;
	window_class->set_title = gst_wayland_window_set_title_impl;
	window_class->set_selection = gst_wayland_window_set_selection_impl;
	window_class->paste_clipboard = gst_wayland_window_paste_clipboard_impl;
	window_class->paste_primary = gst_wayland_window_paste_primary_impl;
	window_class->copy_to_clipboard = gst_wayland_window_copy_to_clipboard_impl;
	window_class->bell = gst_wayland_window_bell_impl;
	window_class->set_opacity = gst_wayland_window_set_opacity_impl;
	window_class->set_pointer_motion = gst_wayland_window_set_pointer_motion_impl;
	window_class->set_wm_hints = gst_wayland_window_set_wm_hints_impl;
	window_class->start_event_watch = gst_wayland_window_start_event_watch_impl;
}

static void
gst_wayland_window_init(GstWaylandWindow *self)
{
	self->display = NULL;
	self->registry = NULL;
	self->compositor = NULL;
	self->shm = NULL;
	self->seat = NULL;
	self->output = NULL;
	self->surface = NULL;
	self->libdecor_ctx = NULL;
	self->libdecor_frame = NULL;
	self->keyboard = NULL;
	self->xkb_ctx = NULL;
	self->xkb_keymap = NULL;
	self->xkb_state = NULL;
	self->pointer = NULL;
	self->cursor_theme = NULL;
	self->default_cursor = NULL;
	self->cursor_surface = NULL;
	self->pointer_x = 0;
	self->pointer_y = 0;
	self->pointer_button_state = 0;
	self->pointer_serial = 0;
	self->data_device_manager = NULL;
	self->data_device = NULL;
	self->data_offer = NULL;
	self->data_source = NULL;
	self->primary_mgr = NULL;
	self->primary_device = NULL;
	self->primary_offer = NULL;
	self->primary_source = NULL;
	self->selection_text = NULL;
	self->clipboard_text = NULL;
	self->win_w = 0;
	self->win_h = 0;
	self->cw = 0;
	self->ch = 0;
	self->borderpx = 0;
	self->keyboard_serial = 0;
	self->configured = FALSE;
	self->closed = FALSE;
	self->focused = FALSE;
	self->repeat_timer_id = 0;
	self->repeat_key = 0;
	self->repeat_delay = 400;
	self->repeat_rate = 25;
	self->opacity = 1.0;
	self->wl_channel = NULL;
	self->wl_watch_id = 0;
}

/* ===== Public API ===== */

/**
 * gst_wayland_window_new:
 * @cols: terminal columns
 * @rows: terminal rows
 * @cw: character cell width
 * @ch: character cell height
 * @borderpx: border padding
 *
 * Creates a new Wayland window. Connects to the compositor,
 * binds globals, creates a libdecor-managed surface with
 * universal decorations, and sets up input devices.
 *
 * Returns: (transfer full) (nullable): new window, or NULL
 */
GstWaylandWindow *
gst_wayland_window_new(
	gint    cols,
	gint    rows,
	gint    cw,
	gint    ch,
	gint    borderpx
){
	GstWaylandWindow *self;

	self = (GstWaylandWindow *)g_object_new(
		GST_TYPE_WAYLAND_WINDOW, NULL);

	self->cw = cw;
	self->ch = ch;
	self->borderpx = borderpx;
	self->win_w = cols * cw + 2 * borderpx;
	self->win_h = rows * ch + 2 * borderpx;

	/* Connect to Wayland display */
	self->display = wl_display_connect(NULL);
	if (self->display == NULL) {
		g_warning("wayland: failed to connect to display");
		g_object_unref(self);
		return NULL;
	}

	/* Create xkb context */
	self->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (self->xkb_ctx == NULL) {
		g_warning("wayland: failed to create xkb context");
		g_object_unref(self);
		return NULL;
	}

	/* Get registry and bind globals */
	self->registry = wl_display_get_registry(self->display);
	wl_registry_add_listener(self->registry,
		&registry_listener, self);

	/* Round-trip to receive all globals */
	wl_display_roundtrip(self->display);

	/* Verify required globals */
	if (self->compositor == NULL) {
		g_warning("wayland: no wl_compositor");
		g_object_unref(self);
		return NULL;
	}
	if (self->shm == NULL) {
		g_warning("wayland: no wl_shm");
		g_object_unref(self);
		return NULL;
	}

	/* Initialize libdecor (handles xdg-shell + decorations internally) */
	self->libdecor_ctx = libdecor_new(self->display, &libdecor_iface);
	if (self->libdecor_ctx == NULL) {
		g_warning("wayland: failed to initialize libdecor");
		g_object_unref(self);
		return NULL;
	}

	/* Create wl_surface */
	self->surface = wl_compositor_create_surface(self->compositor);
	if (self->surface == NULL) {
		g_warning("wayland: failed to create surface");
		g_object_unref(self);
		return NULL;
	}

	/* Decorate the surface with libdecor */
	self->libdecor_frame = libdecor_decorate(
		self->libdecor_ctx, self->surface,
		&frame_iface, self);
	if (self->libdecor_frame == NULL) {
		g_warning("wayland: failed to create libdecor frame");
		g_object_unref(self);
		return NULL;
	}

	libdecor_frame_set_title(self->libdecor_frame, "GST Terminal");
	libdecor_frame_set_app_id(self->libdecor_frame, "gst");
	libdecor_frame_set_min_content_size(self->libdecor_frame,
		cw + 2 * borderpx, ch + 2 * borderpx);
	libdecor_frame_map(self->libdecor_frame);

	/* Set up data device for clipboard */
	if (self->data_device_manager != NULL && self->seat != NULL) {
		self->data_device = wl_data_device_manager_get_data_device(
			self->data_device_manager, self->seat);
		wl_data_device_add_listener(self->data_device,
			&data_device_listener, self);
	}

	/* Set up primary selection device */
	if (self->primary_mgr != NULL && self->seat != NULL) {
		self->primary_device =
			zwp_primary_selection_device_manager_v1_get_device(
			self->primary_mgr, self->seat);
		zwp_primary_selection_device_v1_add_listener(
			self->primary_device,
			&primary_device_listener, self);
	}

	/* Set up cursor */
	if (self->shm != NULL) {
		self->cursor_theme = wl_cursor_theme_load(
			NULL, 24, self->shm);
		if (self->cursor_theme != NULL) {
			self->default_cursor = wl_cursor_theme_get_cursor(
				self->cursor_theme, "xterm");
			if (self->default_cursor == NULL) {
				self->default_cursor = wl_cursor_theme_get_cursor(
					self->cursor_theme, "left_ptr");
			}
			self->cursor_surface = wl_compositor_create_surface(
				self->compositor);
		}
	}

	/* Commit surface and wait for configure */
	wl_surface_commit(self->surface);
	wl_display_roundtrip(self->display);

	g_debug("wayland: window created (%dx%d)", self->win_w, self->win_h);

	return self;
}

/**
 * gst_wayland_window_get_display:
 * @self: A #GstWaylandWindow
 *
 * Returns: (transfer none): the wl_display
 */
struct wl_display *
gst_wayland_window_get_display(GstWaylandWindow *self)
{
	g_return_val_if_fail(GST_IS_WAYLAND_WINDOW(self), NULL);

	return self->display;
}

/**
 * gst_wayland_window_get_surface:
 * @self: A #GstWaylandWindow
 *
 * Returns: (transfer none): the wl_surface
 */
struct wl_surface *
gst_wayland_window_get_surface(GstWaylandWindow *self)
{
	g_return_val_if_fail(GST_IS_WAYLAND_WINDOW(self), NULL);

	return self->surface;
}

/**
 * gst_wayland_window_get_shm:
 * @self: A #GstWaylandWindow
 *
 * Returns: (transfer none): the wl_shm
 */
struct wl_shm *
gst_wayland_window_get_shm(GstWaylandWindow *self)
{
	g_return_val_if_fail(GST_IS_WAYLAND_WINDOW(self), NULL);

	return self->shm;
}

/**
 * gst_wayland_window_get_opacity:
 * @self: A #GstWaylandWindow
 *
 * Gets the current opacity value. The Wayland renderer uses
 * this to paint backgrounds with alpha transparency.
 *
 * Returns: opacity between 0.0 (fully transparent) and 1.0 (opaque)
 */
gdouble
gst_wayland_window_get_opacity(GstWaylandWindow *self)
{
	g_return_val_if_fail(GST_IS_WAYLAND_WINDOW(self), 1.0);

	return self->opacity;
}
