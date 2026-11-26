/*
 * gst-progress-module.c - OSC 9;4 progress in the window title
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * ConEmu and Windows Terminal report progress as:
 *   OSC 9 ; 4 ; state ; progress ST
 *
 * States: 0 remove, 1 set (0-100), 2 error, 3 indeterminate, 4 paused.
 * gst has one window and no dock progress API, so the state is a short
 * ASCII prefix on the current title. State 0 puts the previous title back.
 * A later OSC 0/2 replaces the base title and the prefix is applied again.
 */

#include "gst-progress-module.h"

#include "../../src/core/gst-terminal.h"
#include "../../src/module/gst-module-manager.h"

struct _GstProgressModule
{
	GstModule   parent_instance;
	GstTerminal *watched;     /* unowned, the title-changed source */
	gulong       title_sig;
	gchar       *base_title;  /* title under the prefix, may be NULL */
	gboolean     have_base;
	gboolean     active;
	gboolean     applying;    /* ignore the title we just wrote */
	gint         state;
	gint         value;
	gboolean     have_value;
};

static void
gst_progress_module_escape_init(GstEscapeHandlerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstProgressModule, gst_progress_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_progress_module_escape_init))

/*
 * format_title:
 *
 * Build the visible title. Prefixes are ASCII and one token:
 * state 1 "[42%] ", state 2 "[!] " or "[! 42%] ", state 3 "[~] ",
 * state 4 "[= 42%] ". The base is never NULL here.
 *
 * Returns: (transfer full): the title to set
 */
static gchar *
format_title(
	gint         state,
	gboolean     have_value,
	gint         value,
	const gchar *base
){
	if (base == NULL) {
		base = "";
	}

	if (state == 1) {
		return g_strdup_printf("[%d%%] %s", value, base);
	}
	if (state == 2) {
		if (have_value) {
			return g_strdup_printf("[! %d%%] %s", value, base);
		}
		return g_strdup_printf("[!] %s", base);
	}
	if (state == 3) {
		return g_strdup_printf("[~] %s", base);
	}
	if (state == 4) {
		return g_strdup_printf("[= %d%%] %s", value, base);
	}
	return g_strdup(base);
}

/*
 * apply_title:
 *
 * Write the prefixed title, or the saved base when progress is off.
 * The applying flag stops title-changed from treating that write as
 * a new application title.
 */
static void
apply_title(
	GstProgressModule *self,
	GstTerminal       *term
){
	gchar *shown;
	const gchar *base;

	if (term == NULL) {
		return;
	}

	base = self->base_title;
	if (self->active) {
		shown = format_title(self->state, self->have_value,
			self->value, base);
	} else {
		shown = g_strdup(self->base_title);
	}

	self->applying = TRUE;
	gst_terminal_set_title(term, shown);
	self->applying = FALSE;
	g_free(shown);
}

/*
 * on_title_changed:
 *
 * OSC 0/2 and other title writes land here. While we are applying
 * our own prefix the signal is ignored. Any other write becomes the
 * new base, and an active prefix is put back on top of it.
 */
static void
on_title_changed(
	GstTerminal *term,
	const gchar *title,
	gpointer     user_data
){
	GstProgressModule *self;

	self = GST_PROGRESS_MODULE(user_data);
	if (self->applying) {
		return;
	}

	g_free(self->base_title);
	self->base_title = g_strdup(title);
	self->have_base = TRUE;
	if (self->active) {
		apply_title(self, term);
	}
}

static void
disconnect_title(GstProgressModule *self)
{
	if (self->title_sig != 0 && self->watched != NULL &&
	    GST_IS_TERMINAL(self->watched))
	{
		g_signal_handler_disconnect(self->watched, self->title_sig);
	}
	self->title_sig = 0;
	self->watched = NULL;
}

static void
watch_terminal(
	GstProgressModule *self,
	gpointer           terminal
){
	if (self->title_sig != 0) {
		return;
	}
	if (terminal == NULL || !GST_IS_TERMINAL(terminal)) {
		return;
	}
	self->watched = GST_TERMINAL(terminal);
	self->title_sig = g_signal_connect(self->watched, "title-changed",
		G_CALLBACK(on_title_changed), self);
}

static void
capture_base(
	GstProgressModule *self,
	GstTerminal       *term
){
	const gchar *current;

	if (self->have_base) {
		return;
	}
	current = NULL;
	if (term != NULL) {
		current = gst_terminal_get_title(term);
	}
	g_free(self->base_title);
	self->base_title = g_strdup(current);
	self->have_base = TRUE;
}

/*
 * clear_progress:
 *
 * State 0 restores the exact title from before the prefix, including
 * a NULL title, then forgets it. A clear while nothing is showing
 * does not write the title.
 */
static void
clear_progress(
	GstProgressModule *self,
	GstTerminal       *term
){
	gboolean was_active;

	was_active = self->active;
	self->active = FALSE;
	self->state = 0;
	self->value = 0;
	self->have_value = FALSE;
	if (was_active && term != NULL) {
		apply_title(self, term);
	}
	g_clear_pointer(&self->base_title, g_free);
	self->have_base = FALSE;
}

static void
show_progress(
	GstProgressModule *self,
	GstTerminal       *term,
	gint               state,
	gboolean           have_value,
	gint               value
){
	capture_base(self, term);
	self->active = TRUE;
	self->state = state;
	self->have_value = have_value;
	self->value = value;
	if (term != NULL) {
		apply_title(self, term);
	}
}

/*
 * parse_progress:
 * @rest: OSC 9 body
 *
 * Returns 0 when this is not OSC 9;4, so notify can still see it.
 * Returns 1 when it is OSC 9;4 but the state or value is unusable.
 * That report is consumed and the title is left alone.
 * Returns 2 when @state_out is 0-4 and the optional value is clamped
 * to 0-100. A missing value sets @have_value_out to FALSE.
 */
static gint
parse_progress(
	const gchar *rest,
	gint        *state_out,
	gboolean    *have_value_out,
	gint        *value_out
){
	const gchar *cursor;
	gchar *end;
	gint64 raw;
	gint state;

	if (rest == NULL || rest[0] != '4') {
		return 0;
	}
	if (rest[1] == '\0') {
		return 1;
	}
	if (rest[1] != ';') {
		return 0;
	}

	cursor = rest + 2;
	if (cursor[0] < '0' || cursor[0] > '9') {
		return 1;
	}
	state = cursor[0] - '0';
	if (cursor[1] != '\0' && cursor[1] != ';') {
		return 1;
	}
	if (state > 4) {
		return 1;
	}

	*state_out = state;
	*have_value_out = FALSE;
	*value_out = 0;
	if (cursor[1] == '\0') {
		return 2;
	}

	cursor += 2;
	if (cursor[0] == '\0') {
		return 1;
	}
	raw = g_ascii_strtoll(cursor, &end, 10);
	if (end == cursor) {
		return 1;
	}
	if (*end != '\0' && *end != ';') {
		return 1;
	}
	if (raw < 0) {
		raw = 0;
	}
	if (raw > 100) {
		raw = 100;
	}
	*have_value_out = TRUE;
	*value_out = (gint)raw;
	return 2;
}

static const gchar *
gst_progress_module_get_name(GstModule *module)
{
	(void)module;
	return "progress";
}

static const gchar *
gst_progress_module_get_description(GstModule *module)
{
	(void)module;
	return "Window-title progress via OSC 9;4";
}

static gboolean
gst_progress_module_activate(GstModule *module)
{
	GstProgressModule *self;
	GstModuleManager *mgr;

	self = GST_PROGRESS_MODULE(module);
	mgr = gst_module_manager_get_default();
	watch_terminal(self, gst_module_manager_get_terminal(mgr));
	g_debug("progress: activated");
	return TRUE;
}

static void
gst_progress_module_deactivate(GstModule *module)
{
	GstProgressModule *self;

	self = GST_PROGRESS_MODULE(module);
	if (self->active) {
		clear_progress(self, self->watched);
	}
	disconnect_title(self);
	g_debug("progress: deactivated");
}

static gboolean
gst_progress_module_handle_escape_string(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstProgressModule *self;
	GstTerminal *term;
	gint osc_num;
	gchar *endptr;
	const gchar *rest;
	gint parsed;
	gint state;
	gint value;
	gboolean have_value;

	(void)len;

	self = GST_PROGRESS_MODULE(handler);
	if (str_type != ']') {
		return FALSE;
	}

	osc_num = (gint)strtol(buf, &endptr, 10);
	if (endptr == buf || *endptr != ';') {
		return FALSE;
	}
	if (osc_num != 9) {
		return FALSE;
	}

	rest = endptr + 1;
	state = 0;
	value = 0;
	have_value = FALSE;
	parsed = parse_progress(rest, &state, &have_value, &value);
	if (parsed == 0) {
		return FALSE;
	}
	if (parsed == 1) {
		return TRUE;
	}

	term = NULL;
	if (terminal != NULL && GST_IS_TERMINAL(terminal)) {
		term = GST_TERMINAL(terminal);
	}
	watch_terminal(self, term);

	if (state == 0) {
		clear_progress(self, term);
		return TRUE;
	}
	if (state == 3) {
		have_value = FALSE;
	}
	if ((state == 1 || state == 4) && !have_value) {
		value = 0;
	}
	show_progress(self, term, state, have_value, value);
	return TRUE;
}

static void
gst_progress_module_escape_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string =
		gst_progress_module_handle_escape_string;
}

static void
gst_progress_module_finalize(GObject *object)
{
	GstProgressModule *self;

	self = GST_PROGRESS_MODULE(object);
	if (self->active && self->watched != NULL) {
		self->active = FALSE;
		apply_title(self, self->watched);
	}
	disconnect_title(self);
	g_clear_pointer(&self->base_title, g_free);
	G_OBJECT_CLASS(gst_progress_module_parent_class)->finalize(object);
}

static void
gst_progress_module_class_init(GstProgressModuleClass *klass)
{
	GObjectClass *object_class;
	GstModuleClass *module_class;

	object_class = G_OBJECT_CLASS(klass);
	module_class = GST_MODULE_CLASS(klass);
	object_class->finalize = gst_progress_module_finalize;
	module_class->get_name = gst_progress_module_get_name;
	module_class->get_description = gst_progress_module_get_description;
	module_class->activate = gst_progress_module_activate;
	module_class->deactivate = gst_progress_module_deactivate;
}

static void
gst_progress_module_init(GstProgressModule *self)
{
	self->watched = NULL;
	self->title_sig = 0;
	self->base_title = NULL;
	self->have_base = FALSE;
	self->active = FALSE;
	self->applying = FALSE;
	self->state = 0;
	self->value = 0;
	self->have_value = FALSE;
	/*
	 * Equal priorities dispatch in module-name order, so "notify"
	 * would run first and consume OSC 9. A lower priority runs first.
	 */
	gst_module_set_priority(GST_MODULE(self), -1);
}

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_PROGRESS_MODULE;
}
