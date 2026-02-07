/*
 * gst-kittygfx-parser.c - Kitty graphics protocol command parser
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Parses kitty graphics APC commands in the key=value format.
 * Input format (after 'G' prefix): key=val[,key=val]...;payload
 */

#include "gst-kittygfx-parser.h"
#include <string.h>
#include <stdlib.h>

/*
 * parse_uint32:
 *
 * Parse a string value as an unsigned 32-bit integer.
 * Returns 0 on invalid input.
 */
static guint32
parse_uint32(const gchar *val, gsize val_len)
{
	gchar buf[16];
	gsize copy_len;

	copy_len = (val_len < sizeof(buf) - 1) ? val_len : sizeof(buf) - 1;
	memcpy(buf, val, copy_len);
	buf[copy_len] = '\0';

	return (guint32)strtoul(buf, NULL, 10);
}

/*
 * parse_int32:
 *
 * Parse a string value as a signed 32-bit integer.
 * Returns 0 on invalid input.
 */
static gint32
parse_int32(const gchar *val, gsize val_len)
{
	gchar buf[16];
	gsize copy_len;

	copy_len = (val_len < sizeof(buf) - 1) ? val_len : sizeof(buf) - 1;
	memcpy(buf, val, copy_len);
	buf[copy_len] = '\0';

	return (gint32)strtol(buf, NULL, 10);
}

/*
 * apply_key_value:
 *
 * Applies a single key=value pair to the command structure.
 * The key is a single character; the value is a string (not NUL-terminated)
 * with the given length.
 */
static void
apply_key_value(
	GstGraphicsCommand *cmd,
	gchar               key,
	const gchar        *val,
	gsize               val_len
){
	if (val_len == 0) {
		return;
	}

	switch (key) {
	case 'a':
		cmd->action = val[0];
		break;
	case 'i':
		cmd->image_id = parse_uint32(val, val_len);
		break;
	case 'I':
		cmd->image_number = parse_uint32(val, val_len);
		break;
	case 'p':
		cmd->placement_id = parse_uint32(val, val_len);
		break;
	case 'f':
		cmd->format = (gint)parse_uint32(val, val_len);
		break;
	case 't':
		cmd->transmission = val[0];
		break;
	case 'm':
		cmd->more = (gint)parse_uint32(val, val_len);
		break;
	case 'o':
		cmd->compression = val[0];
		break;
	case 's':
		cmd->src_width = (gint)parse_uint32(val, val_len);
		break;
	case 'v':
		cmd->src_height = (gint)parse_uint32(val, val_len);
		break;
	case 'x':
		cmd->src_x = (gint)parse_uint32(val, val_len);
		break;
	case 'y':
		cmd->src_y = (gint)parse_uint32(val, val_len);
		break;
	case 'w':
		cmd->crop_w = (gint)parse_uint32(val, val_len);
		break;
	case 'h':
		cmd->crop_h = (gint)parse_uint32(val, val_len);
		break;
	case 'c':
		cmd->dst_cols = (gint)parse_uint32(val, val_len);
		break;
	case 'r':
		cmd->dst_rows = (gint)parse_uint32(val, val_len);
		break;
	case 'X':
		cmd->x_offset = (gint)parse_uint32(val, val_len);
		break;
	case 'Y':
		cmd->y_offset = (gint)parse_uint32(val, val_len);
		break;
	case 'z':
		cmd->z_index = parse_int32(val, val_len);
		break;
	case 'C':
		cmd->cursor_movement = (gint)parse_uint32(val, val_len);
		break;
	case 'q':
		cmd->quiet = (gint)parse_uint32(val, val_len);
		break;
	case 'd':
		cmd->delete_target = val[0];
		break;
	default:
		/* Unknown key - silently ignore per protocol spec */
		break;
	}
}

/**
 * gst_gfx_command_parse:
 * @buf: the APC string content after the leading 'G'
 * @len: length of the buffer
 * @cmd: (out): parsed command structure
 *
 * Parses key=value pairs separated by commas, with an optional
 * semicolon-separated base64 payload. Sets defaults for action ('t'),
 * format (32), and transmission ('d').
 *
 * Returns: %TRUE on success
 */
gboolean
gst_gfx_command_parse(
	const gchar        *buf,
	gsize               len,
	GstGraphicsCommand *cmd
){
	const gchar *p;
	const gchar *end;
	const gchar *semicolon;

	if (buf == NULL || len == 0 || cmd == NULL) {
		return FALSE;
	}

	/* Zero-initialize and set defaults */
	memset(cmd, 0, sizeof(*cmd));
	cmd->action = 't';
	cmd->format = 32;
	cmd->transmission = 'd';

	p = buf;
	end = buf + len;

	/* Find the payload separator (semicolon) */
	semicolon = (const gchar *)memchr(buf, ';', len);
	if (semicolon != NULL) {
		cmd->payload = semicolon + 1;
		cmd->payload_len = (gsize)(end - cmd->payload);
		end = semicolon; /* limit key=value parsing to before ; */
	}

	/* Parse key=value pairs */
	while (p < end) {
		gchar key;
		const gchar *eq;
		const gchar *comma;
		const gchar *val;
		gsize val_len;

		/* Skip whitespace or commas */
		if (*p == ',' || *p == ' ') {
			p++;
			continue;
		}

		key = *p;
		p++;

		/* Expect '=' */
		if (p >= end || *p != '=') {
			return FALSE;
		}
		p++; /* skip '=' */

		eq = p;

		/* Find end of value (next comma or end) */
		comma = (const gchar *)memchr(eq, ',', (gsize)(end - eq));
		if (comma != NULL) {
			val = eq;
			val_len = (gsize)(comma - eq);
			p = comma + 1;
		} else {
			val = eq;
			val_len = (gsize)(end - eq);
			p = end;
		}

		apply_key_value(cmd, key, val, val_len);
	}

	return TRUE;
}
