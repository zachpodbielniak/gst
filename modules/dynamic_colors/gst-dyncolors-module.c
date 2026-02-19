/*
 * gst-dyncolors-module.c - Runtime color change module
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Handles OSC color change escape sequences:
 *   OSC 10 ; color_spec ST - set/query foreground
 *   OSC 11 ; color_spec ST - set/query background
 *   OSC 12 ; color_spec ST - set/query cursor color
 *   OSC 4  ; index ; color_spec ST - set/query palette color
 *   OSC 104 ; index ST - reset palette color
 *   OSC 104 ST - reset all colors
 *
 * Color specs: rgb:RR/GG/BB, rgb:RRRR/GGGG/BBBB, #RRGGBB, #RGB
 * Query: "?" as color_spec
 * Response: ESC ] N ; rgb:RRRR/GGGG/BBBB ST
 */

#include "gst-dyncolors-module.h"
#include "../../src/config/gst-config.h"
#include "../../src/config/gst-color-scheme.h"
#include "../../src/module/gst-module-manager.h"
#include "../../src/core/gst-terminal.h"
#include "../../src/core/gst-pty.h"
#include "../../src/gst-enums.h"

struct _GstDyncolorsModule
{
	GstModule parent_instance;

	gboolean  allow_query;
	gboolean  allow_set;

	/* Original colors for OSC 104 reset */
	guint32   orig_fg;
	guint32   orig_bg;
	guint32   orig_cursor;
	guint32   orig_palette[256];
	gboolean  have_originals;
};

static void
gst_dyncolors_module_escape_handler_init(GstEscapeHandlerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(GstDyncolorsModule, gst_dyncolors_module,
	GST_TYPE_MODULE,
	G_IMPLEMENT_INTERFACE(GST_TYPE_ESCAPE_HANDLER,
		gst_dyncolors_module_escape_handler_init))

/* ===== Color parsing ===== */

/*
 * parse_color_spec:
 *
 * Parses an X11 color specification into an ARGB value.
 * Supports:
 *   rgb:RR/GG/BB (2 hex digits per component)
 *   rgb:RRRR/GGGG/BBBB (4 hex digits, high byte used)
 *   #RRGGBB (6 hex digits)
 *   #RGB (3 hex digits, expanded)
 *
 * Returns: TRUE if parsed successfully
 */
static gboolean
parse_color_spec(const gchar *spec, guint32 *color_out)
{
	guint r, g, b;

	if (spec == NULL || spec[0] == '\0') {
		return FALSE;
	}

	/* rgb:RR/GG/BB or rgb:RRRR/GGGG/BBBB */
	if (g_str_has_prefix(spec, "rgb:")) {
		const gchar *p;
		gchar *end1, *end2, *end3;

		p = spec + 4;
		r = (guint)strtoul(p, &end1, 16);
		if (*end1 != '/') return FALSE;
		g = (guint)strtoul(end1 + 1, &end2, 16);
		if (*end2 != '/') return FALSE;
		b = (guint)strtoul(end2 + 1, &end3, 16);

		/* If 4-digit per component, take high byte */
		if ((end1 - p) == 4) {
			r >>= 8;
			g >>= 8;
			b >>= 8;
		}

		*color_out = (0xFFu << 24) | (r << 16) | (g << 8) | b;
		return TRUE;
	}

	/* #RRGGBB */
	if (spec[0] == '#' && strlen(spec) == 7) {
		guint32 val;

		val = (guint32)strtoul(spec + 1, NULL, 16);
		*color_out = (0xFFu << 24) | val;
		return TRUE;
	}

	/* #RGB */
	if (spec[0] == '#' && strlen(spec) == 4) {
		r = (guint)strtoul((gchar[]){spec[1], '\0'}, NULL, 16);
		g = (guint)strtoul((gchar[]){spec[2], '\0'}, NULL, 16);
		b = (guint)strtoul((gchar[]){spec[3], '\0'}, NULL, 16);
		r = r | (r << 4);
		g = g | (g << 4);
		b = b | (b << 4);
		*color_out = (0xFFu << 24) | (r << 16) | (g << 8) | b;
		return TRUE;
	}

	return FALSE;
}

/*
 * format_color_response:
 *
 * Formats an OSC color query response in the standard format:
 *   ESC ] N ; rgb:RRRR/GGGG/BBBB ST
 *
 * Returns: A newly allocated response string
 */
static gchar *
format_color_response(gint osc_num, guint32 color)
{
	guint r, g, b;

	r = (color >> 16) & 0xFF;
	g = (color >> 8) & 0xFF;
	b = color & 0xFF;

	/* Expand to 16-bit per component as xterm does */
	return g_strdup_printf("\033]%d;rgb:%04x/%04x/%04x\033\\",
		osc_num, r | (r << 8), g | (g << 8), b | (b << 8));
}

/*
 * save_originals:
 *
 * Saves the current colors so OSC 104 can reset them.
 * Called on the first color modification.
 */
static void
save_originals(GstDyncolorsModule *self, GstColorScheme *scheme)
{
	guint i;

	if (self->have_originals) {
		return;
	}

	self->orig_fg = gst_color_scheme_get_foreground(scheme);
	self->orig_bg = gst_color_scheme_get_background(scheme);
	self->orig_cursor = gst_color_scheme_get_cursor_color(scheme);

	for (i = 0; i < 256; i++) {
		self->orig_palette[i] = gst_color_scheme_get_color(scheme, i);
	}

	self->have_originals = TRUE;
}

/*
 * send_pty_response:
 *
 * Sends a response string back to the PTY so the
 * application receives the query result. Writes directly
 * to the PTY master fd via the module manager's stored
 * PTY reference.
 */
static void
send_pty_response(const gchar *response)
{
	GstModuleManager *mgr;
	gpointer pty;

	mgr = gst_module_manager_get_default();
	pty = gst_module_manager_get_pty(mgr);
	if (pty != NULL && GST_IS_PTY(pty)) {
		gst_pty_write(GST_PTY(pty), response,
			(gssize)strlen(response));
	}
}

/* ===== GstModule vfuncs ===== */

static const gchar *
gst_dyncolors_module_get_name(GstModule *module)
{
	(void)module;
	return "dynamic_colors";
}

static const gchar *
gst_dyncolors_module_get_description(GstModule *module)
{
	(void)module;
	return "Runtime color changes via OSC 10/11/12/4/104";
}

static void
gst_dyncolors_module_configure(GstModule *module, gpointer config)
{
	GstDyncolorsModule *self;
	GstConfig *cfg;

	self = GST_DYNCOLORS_MODULE(module);
	cfg = (GstConfig *)config;

	self->allow_query = cfg->modules.dynamic_colors.allow_query;
	self->allow_set = cfg->modules.dynamic_colors.allow_set;

	g_debug("dynamic_colors: configured (query=%d, set=%d)",
		self->allow_query, self->allow_set);
}

static gboolean
gst_dyncolors_module_activate(GstModule *module)
{
	(void)module;
	g_debug("dynamic_colors: activated");
	return TRUE;
}

static void
gst_dyncolors_module_deactivate(GstModule *module)
{
	(void)module;
	g_debug("dynamic_colors: deactivated");
}

/* ===== GstEscapeHandler interface ===== */

/*
 * handle_escape_string:
 *
 * Handles OSC 4/10/11/12/104 color sequences.
 * The raw buffer contains the full OSC with semicolons intact.
 */
static gboolean
gst_dyncolors_module_handle_escape_string(
	GstEscapeHandler *handler,
	gchar             str_type,
	const gchar      *buf,
	gsize             len,
	gpointer          terminal
){
	GstDyncolorsModule *self;
	GstModuleManager *mgr;
	GstColorScheme *scheme;
	gint osc_num;
	gchar *endptr;
	const gchar *rest;

	(void)len;
	(void)terminal;

	self = GST_DYNCOLORS_MODULE(handler);

	if (str_type != ']') {
		return FALSE;
	}

	osc_num = (gint)strtol(buf, &endptr, 10);
	if (endptr == buf) {
		return FALSE;
	}

	/* Skip semicolon */
	if (*endptr == ';') {
		rest = endptr + 1;
	} else {
		rest = endptr;
	}

	/* Get color scheme from module manager */
	mgr = gst_module_manager_get_default();
	scheme = (GstColorScheme *)gst_module_manager_get_color_scheme(mgr);
	if (scheme == NULL) {
		g_debug("dynamic_colors: no color scheme available");
		return FALSE;
	}

	switch (osc_num) {
	case 10: /* Foreground */
		if (rest[0] == '?') {
			if (self->allow_query) {
				g_autofree gchar *resp = NULL;

				resp = format_color_response(10,
					gst_color_scheme_get_foreground(
						scheme));
				send_pty_response(resp);
			}
		} else if (self->allow_set) {
			guint32 color;

			if (parse_color_spec(rest, &color)) {
				save_originals(self, scheme);
				gst_color_scheme_set_foreground(
					scheme, color);
			}
		}
		return TRUE;

	case 11: /* Background */
		if (rest[0] == '?') {
			if (self->allow_query) {
				g_autofree gchar *resp = NULL;

				resp = format_color_response(11,
					gst_color_scheme_get_background(
						scheme));
				send_pty_response(resp);
			}
		} else if (self->allow_set) {
			guint32 color;

			if (parse_color_spec(rest, &color)) {
				save_originals(self, scheme);
				gst_color_scheme_set_background(
					scheme, color);
			}
		}
		return TRUE;

	case 12: /* Cursor */
		if (rest[0] == '?') {
			if (self->allow_query) {
				g_autofree gchar *resp = NULL;

				resp = format_color_response(12,
					gst_color_scheme_get_cursor_color(
						scheme));
				send_pty_response(resp);
			}
		} else if (self->allow_set) {
			guint32 color;

			if (parse_color_spec(rest, &color)) {
				save_originals(self, scheme);
				gst_color_scheme_set_cursor_color(
					scheme, color);
			}
		}
		return TRUE;

	case 4: /* Set palette color: OSC 4 ; index ; color_spec */
		{
			gint idx;
			const gchar *color_spec;

			idx = (gint)strtol(rest, &endptr, 10);
			if (endptr == rest || *endptr != ';') {
				return FALSE;
			}
			color_spec = endptr + 1;

			if (idx < 0 || idx > 255) {
				return TRUE;
			}

			if (color_spec[0] == '?') {
				if (self->allow_query) {
					guint32 c;
					g_autofree gchar *resp = NULL;

					c = gst_color_scheme_get_color(
						scheme, (guint)idx);
					resp = g_strdup_printf(
						"\033]4;%d;rgb:%04x/%04x/%04x\033\\",
						idx,
						(((c >> 16) & 0xFF) * 0x101),
						(((c >> 8) & 0xFF) * 0x101),
						((c & 0xFF) * 0x101));
					send_pty_response(resp);
				}
			} else if (self->allow_set) {
				guint32 color;

				if (parse_color_spec(color_spec, &color)) {
					save_originals(self, scheme);
					gst_color_scheme_set_color(
						scheme, (guint)idx, color);
				}
			}
		}
		return TRUE;

	case 104: /* Reset color */
		if (!self->have_originals) {
			return TRUE;
		}

		if (rest[0] == '\0' || rest == endptr) {
			/* Reset all */
			guint i;

			gst_color_scheme_set_foreground(scheme,
				self->orig_fg);
			gst_color_scheme_set_background(scheme,
				self->orig_bg);
			gst_color_scheme_set_cursor_color(scheme,
				self->orig_cursor);
			for (i = 0; i < 256; i++) {
				gst_color_scheme_set_color(scheme, i,
					self->orig_palette[i]);
			}
		} else {
			/* Reset specific index */
			gint idx;

			idx = (gint)strtol(rest, NULL, 10);
			if (idx >= 0 && idx <= 255 &&
			    self->have_originals)
			{
				gst_color_scheme_set_color(scheme,
					(guint)idx,
					self->orig_palette[idx]);
			}
		}
		return TRUE;

	default:
		return FALSE;
	}
}

static void
gst_dyncolors_module_escape_handler_init(GstEscapeHandlerInterface *iface)
{
	iface->handle_escape_string =
		gst_dyncolors_module_handle_escape_string;
}

/* ===== GObject lifecycle ===== */

static void
gst_dyncolors_module_class_init(GstDyncolorsModuleClass *klass)
{
	GstModuleClass *module_class;

	module_class = GST_MODULE_CLASS(klass);
	module_class->get_name = gst_dyncolors_module_get_name;
	module_class->get_description = gst_dyncolors_module_get_description;
	module_class->activate = gst_dyncolors_module_activate;
	module_class->deactivate = gst_dyncolors_module_deactivate;
	module_class->configure = gst_dyncolors_module_configure;
}

static void
gst_dyncolors_module_init(GstDyncolorsModule *self)
{
	self->allow_query = TRUE;
	self->allow_set = TRUE;
	self->have_originals = FALSE;
}

/* ===== Module entry point ===== */

G_MODULE_EXPORT GType
gst_module_register(void)
{
	return GST_TYPE_DYNCOLORS_MODULE;
}
