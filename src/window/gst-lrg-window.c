/*
 * gst-lrg-window.c - libregnum (LRG) window
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Owns a graylib #GrlWindow and a ~60fps render-loop GSource on the GLib
 * main loop. Each tick polls raylib input (poll_events), forwards it as
 * GstWindow signals (key-press, button-press, motion-notify, ...), tracks
 * resize/focus/close, then drives the renderer (start_draw -> render ->
 * finish_draw). Input handling mirrors cmacs lrgterm / gsurf's LRG window.
 */

#include "gst-lrg-window.h"
#include "../rendering/gst-renderer.h"
#include <string.h>

/* X keysyms for the non-text keys we forward. These are part of the stable
 * keysym ABI; using the literals avoids pulling X11 headers into the LRG
 * backend. They match gst_terminal_key_to_escape's expectations. */
#define LRG_XK_BackSpace 0xFF08
#define LRG_XK_Tab       0xFF09
#define LRG_XK_Return    0xFF0D
#define LRG_XK_Escape    0xFF1B
#define LRG_XK_Home      0xFF50
#define LRG_XK_Left      0xFF51
#define LRG_XK_Up        0xFF52
#define LRG_XK_Right     0xFF53
#define LRG_XK_Down      0xFF54
#define LRG_XK_Prior     0xFF55  /* Page Up */
#define LRG_XK_Next      0xFF56  /* Page Down */
#define LRG_XK_End       0xFF57
#define LRG_XK_Insert    0xFF63
#define LRG_XK_Delete    0xFFFF
#define LRG_XK_KP_Enter  0xFF8D
#define LRG_XK_F1        0xFFBE  /* F1..F12 are contiguous */

/* X11 modifier + button masks (state passed to the GstWindow signals; the
 * keybind/mouse layers in main.c expect this format). */
#define LRG_SHIFT_MASK    (1 << 0)
#define LRG_CONTROL_MASK  (1 << 2)
#define LRG_MOD1_MASK     (1 << 3)   /* Alt */
#define LRG_MOD4_MASK     (1 << 6)   /* Super */
#define LRG_BUTTON1_MASK  (1 << 8)
#define LRG_BUTTON2_MASK  (1 << 9)
#define LRG_BUTTON3_MASK  (1 << 10)

#define LRG_TICK_MS       (16)       /* ~60fps frame pump */

struct _GstLrgWindow
{
	GstWindow parent_instance;

	GrlWindow   *win;           /* owned */
	GstRenderer *renderer;      /* not owned; driven each tick */

	gchar       *title;
	gint         cols;
	gint         rows;
	gint         cw;
	gint         ch;
	gint         borderpx;
	gint         win_w;
	gint         win_h;

	guint        tick_source;
	GstLrgRenderMode render_mode;

	gdouble      opacity;
	gboolean     pointer_motion;
	gboolean     focused;
	gboolean     visible;

	/* Pointer state for motion dedup + button-mask synthesis */
	gint         last_mouse_x;
	gint         last_mouse_y;

	/* Primary selection text (raylib has only a single clipboard) */
	gchar       *primary_text;
};

G_DEFINE_FINAL_TYPE(GstLrgWindow, gst_lrg_window, GST_TYPE_WINDOW)

/* ===== Input helpers ===== */

/* GrlKey -> X keysym for non-text keys (0 when not a mapped function key). */
static guint
lrg_keysym_for(GrlKey key)
{
	switch (key) {
	case GRL_KEY_BACKSPACE:  return LRG_XK_BackSpace;
	case GRL_KEY_TAB:        return LRG_XK_Tab;
	case GRL_KEY_ENTER:      return LRG_XK_Return;
	case GRL_KEY_KP_ENTER:   return LRG_XK_KP_Enter;
	case GRL_KEY_ESCAPE:     return LRG_XK_Escape;
	case GRL_KEY_HOME:       return LRG_XK_Home;
	case GRL_KEY_LEFT:       return LRG_XK_Left;
	case GRL_KEY_UP:         return LRG_XK_Up;
	case GRL_KEY_RIGHT:      return LRG_XK_Right;
	case GRL_KEY_DOWN:       return LRG_XK_Down;
	case GRL_KEY_PAGE_UP:    return LRG_XK_Prior;
	case GRL_KEY_PAGE_DOWN:  return LRG_XK_Next;
	case GRL_KEY_END:        return LRG_XK_End;
	case GRL_KEY_INSERT:     return LRG_XK_Insert;
	case GRL_KEY_DELETE:     return LRG_XK_Delete;
	default:
		if (key >= GRL_KEY_F1 && key <= GRL_KEY_F12) {
			return LRG_XK_F1 + (guint)(key - GRL_KEY_F1);
		}
		return 0;
	}
}

static guint
lrg_current_mods(void)
{
	guint mods = 0;

	if (grl_input_is_key_down(GRL_KEY_LEFT_SHIFT)
	    || grl_input_is_key_down(GRL_KEY_RIGHT_SHIFT)) {
		mods |= LRG_SHIFT_MASK;
	}
	if (grl_input_is_key_down(GRL_KEY_LEFT_CONTROL)
	    || grl_input_is_key_down(GRL_KEY_RIGHT_CONTROL)) {
		mods |= LRG_CONTROL_MASK;
	}
	if (grl_input_is_key_down(GRL_KEY_LEFT_ALT)
	    || grl_input_is_key_down(GRL_KEY_RIGHT_ALT)) {
		mods |= LRG_MOD1_MASK;
	}
	if (grl_input_is_key_down(GRL_KEY_LEFT_SUPER)
	    || grl_input_is_key_down(GRL_KEY_RIGHT_SUPER)) {
		mods |= LRG_MOD4_MASK;
	}
	return mods;
}

/* Non-text keys that should auto-repeat while held (the press queue only
 * fires on the initial press). */
static const GrlKey lrg_repeat_keys[] = {
	GRL_KEY_BACKSPACE, GRL_KEY_DELETE, GRL_KEY_LEFT, GRL_KEY_RIGHT,
	GRL_KEY_UP, GRL_KEY_DOWN, GRL_KEY_PAGE_UP, GRL_KEY_PAGE_DOWN,
	GRL_KEY_ENTER, GRL_KEY_TAB,
};

/*
 * lrg_emit_key:
 * Dispatch one non-character GrlKey (special key, or Ctrl/Alt + printable).
 */
static void
lrg_emit_key(GstLrgWindow *self, GrlKey key, guint mods)
{
	GstWindow *win = GST_WINDOW(self);
	guint keysym = lrg_keysym_for(key);
	gboolean printable = (key >= GRL_KEY_SPACE && key <= GRL_KEY_GRAVE);
	gboolean kcm = (mods & (LRG_CONTROL_MASK | LRG_MOD1_MASK | LRG_MOD4_MASK)) != 0;

	if (keysym != 0) {
		/* Mapped function key: no text. */
		g_signal_emit_by_name(win, "key-press", keysym, mods, "", 0);
		return;
	}

	if (printable && kcm) {
		guint ks;
		gchar text[2];
		gint len = 0;

		/* Keysym: Shift+letter -> uppercase, else lowercase (matches the
		 * keybind parser's normalization). */
		if (key >= GRL_KEY_A && key <= GRL_KEY_Z) {
			ks = (mods & LRG_SHIFT_MASK) ? (guint)key : (guint)key + 32;
		} else {
			ks = (guint)key;
		}

		/* Control combinations produce a control byte as the text (the X11
		 * backend gets this from XLookupString). Alt-only keeps the letter
		 * so on_key_press prefixes ESC. */
		if (mods & LRG_CONTROL_MASK) {
			if (key >= GRL_KEY_A && key <= GRL_KEY_Z) {
				text[0] = (gchar)(key - GRL_KEY_A + 1);   /* ^A..^Z */
				len = 1;
			} else if (key == GRL_KEY_SPACE) {
				text[0] = 0; len = 1;                      /* ^@ (NUL) */
			} else if (key == GRL_KEY_LEFT_BRACKET) {
				text[0] = 0x1b; len = 1;                   /* ^[ */
			} else if (key == GRL_KEY_BACKSLASH) {
				text[0] = 0x1c; len = 1;                   /* ^\ */
			} else if (key == GRL_KEY_RIGHT_BRACKET) {
				text[0] = 0x1d; len = 1;                   /* ^] */
			}
		} else {
			/* Alt + printable: letter text, ESC-prefixed downstream. */
			text[0] = (gchar)((key >= GRL_KEY_A && key <= GRL_KEY_Z)
				? key + 32 : (guint)key);
			len = 1;
		}

		if (len > 0) {
			text[len] = '\0';
			g_signal_emit_by_name(win, "key-press", ks, mods, text, len);
		} else {
			g_signal_emit_by_name(win, "key-press", ks, mods, "", 0);
		}
	}
	/* Plain printable (no ctrl/alt) is handled via the char queue; modifier
	 * keys themselves are ignored. */
}

/*
 * lrg_window_forward_input:
 * Drain raylib's input queues for this frame into GstWindow signals.
 */
static void
lrg_window_forward_input(GstLrgWindow *self)
{
	GstWindow *win = GST_WINDOW(self);
	guint mods = lrg_current_mods();
	gint ch;
	GrlKey key;
	guint i;
	gint mx;
	gint my;
	guint btn_state;
	gfloat wheel;
	static const struct { GrlMouseButton grl; guint xbtn; guint mask; } buttons[] = {
		{ GRL_MOUSE_BUTTON_LEFT,   1, LRG_BUTTON1_MASK },
		{ GRL_MOUSE_BUTTON_MIDDLE, 2, LRG_BUTTON2_MASK },
		{ GRL_MOUSE_BUTTON_RIGHT,  3, LRG_BUTTON3_MASK },
	};
	gboolean any_button = FALSE;

	/* --- Keyboard: printable text from the char queue --- */
	while ((ch = grl_input_get_char_pressed()) != 0) {
		gchar text[8];
		gint len;

		len = g_unichar_to_utf8((gunichar)ch, text);
		text[len] = '\0';
		g_signal_emit_by_name(win, "key-press", (guint)ch, mods, text, len);
	}

	/* --- Keyboard: special keys + Ctrl/Alt-modified printables --- */
	while ((key = grl_input_get_key_pressed()) != 0) {
		lrg_emit_key(self, key, mods);
	}

	/* Auto-repeat held navigation/editing keys. */
	for (i = 0; i < G_N_ELEMENTS(lrg_repeat_keys); i++) {
		if (grl_input_is_key_pressed_repeat(lrg_repeat_keys[i])) {
			lrg_emit_key(self, lrg_repeat_keys[i], mods);
		}
	}

	/* --- Mouse --- */
	mx = grl_input_get_mouse_x();
	my = grl_input_get_mouse_y();

	btn_state = mods;
	for (i = 0; i < G_N_ELEMENTS(buttons); i++) {
		if (grl_input_is_mouse_button_down(buttons[i].grl)) {
			btn_state |= buttons[i].mask;
			any_button = TRUE;
		}
	}

	for (i = 0; i < G_N_ELEMENTS(buttons); i++) {
		if (grl_input_is_mouse_button_pressed(buttons[i].grl)) {
			g_signal_emit_by_name(win, "button-press",
				buttons[i].xbtn, btn_state, mx, my, (gulong)0);
		}
		if (grl_input_is_mouse_button_released(buttons[i].grl)) {
			g_signal_emit_by_name(win, "button-release",
				buttons[i].xbtn, btn_state, mx, my, (gulong)0);
		}
	}

	/* Motion: only while dragging or when motion reporting is enabled. */
	if ((mx != self->last_mouse_x || my != self->last_mouse_y)
	    && (any_button || self->pointer_motion)) {
		g_signal_emit_by_name(win, "motion-notify", btn_state, mx, my);
	}
	self->last_mouse_x = mx;
	self->last_mouse_y = my;

	/* Wheel -> X11 buttons 4 (up) / 5 (down), press + release. */
	wheel = grl_input_get_mouse_wheel_move();
	if (wheel != 0.0f) {
		guint wbtn = (wheel > 0.0f) ? 4 : 5;

		g_signal_emit_by_name(win, "button-press", wbtn, mods, mx, my,
			(gulong)0);
		g_signal_emit_by_name(win, "button-release", wbtn, mods, mx, my,
			(gulong)0);
	}
}

/* ===== Frame pump ===== */

static gboolean
lrg_window_tick(gpointer user_data)
{
	GstLrgWindow *self = user_data;

	if (self->win == NULL) {
		self->tick_source = 0;
		return G_SOURCE_REMOVE;
	}

	/* Update raylib input state for this frame (single poll point). */
	grl_window_poll_events(self->win);

	if (grl_window_should_close(self->win)) {
		self->tick_source = 0;
		g_signal_emit_by_name(self, "close-request");
		return G_SOURCE_REMOVE;
	}

	/* Resize -> emit configure with the new pixel size. */
	if (grl_window_is_resized(self->win)) {
		gint w = grl_window_get_width(self->win);
		gint h = grl_window_get_height(self->win);

		if (w > 0 && h > 0 && (w != self->win_w || h != self->win_h)) {
			self->win_w = w;
			self->win_h = h;
			g_signal_emit_by_name(self, "configure", (guint)w, (guint)h);
		}
	}

	/* Focus tracking. */
	{
		gboolean foc = grl_window_is_focused(self->win);

		if (foc != self->focused) {
			self->focused = foc;
			g_signal_emit_by_name(self, "focus-change", foc);
		}
	}

	lrg_window_forward_input(self);

	/* Drive the renderer (start_draw begins the frame, finish_draw swaps). */
	if (self->renderer != NULL) {
		if (gst_renderer_start_draw(self->renderer)) {
			gst_renderer_render(self->renderer);
			gst_renderer_finish_draw(self->renderer);
		}
	}

	return G_SOURCE_CONTINUE;
}

/* ===== GstWindow vfuncs ===== */

static void
gst_lrg_window_show(GstWindow *window)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	if (!self->visible) {
		self->visible = TRUE;
		g_signal_emit_by_name(self, "visibility", TRUE);
	}
}

static void
gst_lrg_window_hide(GstWindow *window)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	if (self->visible) {
		self->visible = FALSE;
		g_signal_emit_by_name(self, "visibility", FALSE);
	}
}

static void
gst_lrg_window_do_resize(GstWindow *window, guint width, guint height)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	self->win_w = (gint)width;
	self->win_h = (gint)height;
	if (self->win != NULL) {
		grl_window_set_size(self->win, (gint)width, (gint)height);
	}
}

static void
gst_lrg_window_do_set_title(GstWindow *window, const gchar *title)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	g_free(self->title);
	self->title = g_strdup(title);
	if (self->win != NULL) {
		grl_window_set_title(self->win, title ? title : "gst");
	}
}

static void
gst_lrg_window_do_set_selection(
	GstWindow   *window,
	const gchar *text,
	gboolean     is_clipboard
){
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	if (is_clipboard) {
		if (self->win != NULL && text != NULL) {
			grl_window_set_clipboard_text(self->win, text);
		}
	} else {
		/* PRIMARY: raylib has no separate primary selection; keep a copy. */
		g_free(self->primary_text);
		self->primary_text = g_strdup(text);
	}
}

static void
gst_lrg_window_do_paste_clipboard(GstWindow *window)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);
	gchar *text;

	if (self->win == NULL) {
		return;
	}
	text = grl_window_get_clipboard_text(self->win);
	if (text != NULL) {
		g_signal_emit_by_name(self, "selection-notify", text,
			(gint)strlen(text));
		g_free(text);
	}
}

static void
gst_lrg_window_do_paste_primary(GstWindow *window)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	/* Use our stored primary; fall back to the system clipboard. */
	if (self->primary_text != NULL) {
		g_signal_emit_by_name(self, "selection-notify", self->primary_text,
			(gint)strlen(self->primary_text));
		return;
	}
	gst_lrg_window_do_paste_clipboard(window);
}

static void
gst_lrg_window_do_copy_to_clipboard(GstWindow *window)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	if (self->win != NULL && self->primary_text != NULL) {
		grl_window_set_clipboard_text(self->win, self->primary_text);
	}
}

static void
gst_lrg_window_do_set_opacity(GstWindow *window, gdouble opacity)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	self->opacity = CLAMP(opacity, 0.0, 1.0);
	if (self->win != NULL) {
		grl_window_set_opacity(self->win, (gfloat)self->opacity);
	}
}

static void
gst_lrg_window_do_set_pointer_motion(GstWindow *window, gboolean enable)
{
	GST_LRG_WINDOW(window)->pointer_motion = enable;
}

static void
gst_lrg_window_do_set_wm_hints(
	GstWindow   *window,
	gint         cw,
	gint         ch,
	gint         borderpx
){
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	self->cw = cw;
	self->ch = ch;
	self->borderpx = borderpx;
	if (self->win != NULL) {
		/* Keep at least one cell visible. */
		grl_window_set_min_size(self->win, cw + 2 * borderpx,
			ch + 2 * borderpx);
	}
}

static void
gst_lrg_window_start_event_watch(GstWindow *window)
{
	GstLrgWindow *self = GST_LRG_WINDOW(window);

	if (self->tick_source == 0) {
		self->tick_source = g_timeout_add(LRG_TICK_MS, lrg_window_tick, self);
	}
}

/* ===== Public API ===== */

GrlWindow *
gst_lrg_window_get_grl_window(GstLrgWindow *self)
{
	g_return_val_if_fail(GST_IS_LRG_WINDOW(self), NULL);
	return self->win;
}

gdouble
gst_lrg_window_get_opacity(GstLrgWindow *self)
{
	g_return_val_if_fail(GST_IS_LRG_WINDOW(self), 1.0);
	return self->opacity;
}

void
gst_lrg_window_set_renderer(GstLrgWindow *self, GstRenderer *renderer)
{
	g_return_if_fail(GST_IS_LRG_WINDOW(self));
	self->renderer = renderer;
}

void
gst_lrg_window_set_render_mode(GstLrgWindow *self, GstLrgRenderMode mode)
{
	g_return_if_fail(GST_IS_LRG_WINDOW(self));

	if (!gst_lrg_render_mode_is_implemented(mode)) {
		g_warning("gst LRG: render mode '%s' is not yet implemented; using 2d",
			gst_lrg_render_mode_to_string(mode));
		mode = GST_LRG_RENDER_MODE_2D;
	}
	self->render_mode = mode;
}

GstLrgRenderMode
gst_lrg_window_get_render_mode(GstLrgWindow *self)
{
	g_return_val_if_fail(GST_IS_LRG_WINDOW(self), GST_LRG_RENDER_MODE_2D);
	return self->render_mode;
}

/* ===== GObject lifecycle ===== */

static void
gst_lrg_window_dispose(GObject *object)
{
	GstLrgWindow *self = GST_LRG_WINDOW(object);

	if (self->tick_source != 0) {
		g_source_remove(self->tick_source);
		self->tick_source = 0;
	}
	g_clear_object(&self->win);
	g_clear_pointer(&self->title, g_free);
	g_clear_pointer(&self->primary_text, g_free);

	G_OBJECT_CLASS(gst_lrg_window_parent_class)->dispose(object);
}

static void
gst_lrg_window_class_init(GstLrgWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstWindowClass *window_class = GST_WINDOW_CLASS(klass);

	object_class->dispose = gst_lrg_window_dispose;

	window_class->show = gst_lrg_window_show;
	window_class->hide = gst_lrg_window_hide;
	window_class->resize = gst_lrg_window_do_resize;
	window_class->set_title = gst_lrg_window_do_set_title;
	window_class->set_selection = gst_lrg_window_do_set_selection;
	window_class->paste_clipboard = gst_lrg_window_do_paste_clipboard;
	window_class->paste_primary = gst_lrg_window_do_paste_primary;
	window_class->copy_to_clipboard = gst_lrg_window_do_copy_to_clipboard;
	window_class->set_opacity = gst_lrg_window_do_set_opacity;
	window_class->set_pointer_motion = gst_lrg_window_do_set_pointer_motion;
	window_class->set_wm_hints = gst_lrg_window_do_set_wm_hints;
	window_class->start_event_watch = gst_lrg_window_start_event_watch;
}

static void
gst_lrg_window_init(GstLrgWindow *self)
{
	self->win = NULL;
	self->renderer = NULL;
	self->title = NULL;
	self->cols = 0;
	self->rows = 0;
	self->cw = 0;
	self->ch = 0;
	self->borderpx = 0;
	self->win_w = 0;
	self->win_h = 0;
	self->tick_source = 0;
	self->render_mode = GST_LRG_RENDER_MODE_2D;
	self->opacity = 1.0;
	self->pointer_motion = FALSE;
	self->focused = TRUE;
	self->visible = FALSE;
	self->last_mouse_x = -1;
	self->last_mouse_y = -1;
	self->primary_text = NULL;
}

GstLrgWindow *
gst_lrg_window_new(
	gint    cols,
	gint    rows,
	gint    cw,
	gint    ch,
	gint    borderpx
){
	GstLrgWindow *self;
	gint w;
	gint h;

	self = (GstLrgWindow *)g_object_new(GST_TYPE_LRG_WINDOW, NULL);

	self->cols = cols;
	self->rows = rows;
	self->cw = cw;
	self->ch = ch;
	self->borderpx = borderpx;

	w = cols * cw + 2 * borderpx;
	h = rows * ch + 2 * borderpx;
	if (w <= 0) w = 640;
	if (h <= 0) h = 480;
	self->win_w = w;
	self->win_h = h;

	/* Open the raylib window (this creates the GL context fonts need). */
	self->win = grl_window_new(w, h, "gst");
	if (self->win == NULL) {
		g_object_unref(self);
		return NULL;
	}

	grl_window_set_state(self->win, GRL_FLAG_WINDOW_RESIZABLE);
	grl_window_set_target_fps(self->win, 60);
	/* Esc must reach the terminal, not close the window. */
	grl_input_set_exit_key(GRL_KEY_NULL);

	return self;
}
