/*
 * gst-kittygfx-parser.h - Kitty graphics protocol command parser
 *
 * Copyright (C) 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Parses the kitty graphics protocol APC command format:
 * ESC_G <key>=<val>[,<key>=<val>]... ; <base64_payload> ESC_\
 *
 * The leading 'G' is stripped by the caller; this parser receives
 * the string after 'G'.
 */

#ifndef GST_KITTYGFX_PARSER_H
#define GST_KITTYGFX_PARSER_H

#include <glib.h>

G_BEGIN_DECLS

/*
 * GstGfxAction:
 *
 * Graphics protocol action types (a=<value>).
 */
typedef enum {
	GST_GFX_ACTION_TRANSMIT       = 't',  /* transmit image data */
	GST_GFX_ACTION_TRANSMIT_DISP  = 'T',  /* transmit and display */
	GST_GFX_ACTION_QUERY          = 'q',  /* query support */
	GST_GFX_ACTION_DISPLAY        = 'p',  /* display (place) */
	GST_GFX_ACTION_DELETE          = 'd',  /* delete */
	GST_GFX_ACTION_FRAME          = 'f',  /* animation frame */
	GST_GFX_ACTION_ANIMATE        = 'a',  /* animation control */
	GST_GFX_ACTION_COMPOSE        = 'c'   /* composition mode */
} GstGfxAction;

/*
 * GstGfxFormat:
 *
 * Pixel format of the image data (f=<value>).
 */
typedef enum {
	GST_GFX_FORMAT_RGBA  = 32,  /* 32-bit RGBA */
	GST_GFX_FORMAT_RGB   = 24,  /* 24-bit RGB */
	GST_GFX_FORMAT_PNG   = 100  /* PNG encoded */
} GstGfxFormat;

/*
 * GstGfxTransmission:
 *
 * Transmission medium (t=<value>).
 */
typedef enum {
	GST_GFX_TRANS_DIRECT = 'd',  /* direct (base64 in payload) */
	GST_GFX_TRANS_FILE   = 'f',  /* file path */
	GST_GFX_TRANS_TEMP   = 't',  /* temporary file */
	GST_GFX_TRANS_SHM    = 's'   /* shared memory */
} GstGfxTransmission;

/*
 * GstGfxDelete:
 *
 * Delete target specifier (d=<value> when action is delete).
 */
typedef enum {
	GST_GFX_DEL_ALL          = 'a',  /* all images */
	GST_GFX_DEL_BY_ID        = 'i',  /* by image id */
	GST_GFX_DEL_BY_NUMBER    = 'n',  /* by image number */
	GST_GFX_DEL_AT_CURSOR    = 'c',  /* at cursor position */
	GST_GFX_DEL_AT_CELL      = 'p',  /* at specific cell */
	GST_GFX_DEL_AT_COLUMN    = 'x',  /* at column */
	GST_GFX_DEL_AT_ROW       = 'y',  /* at row */
	GST_GFX_DEL_AT_ZINDEX    = 'z'   /* at z-index */
} GstGfxDelete;

/*
 * GstGraphicsCommand:
 *
 * Parsed kitty graphics protocol command.
 * All fields default to 0 if not specified in the command.
 */
typedef struct
{
	/* Action */
	gchar    action;          /* 'a' key: t, T, q, p, d, f, a, c */

	/* Image identification */
	guint32  image_id;        /* 'i' key: unique image id */
	guint32  image_number;    /* 'I' key: image number */
	guint32  placement_id;    /* 'p' key: placement id (display only) */

	/* Data format and transmission */
	gint     format;          /* 'f' key: 24, 32, or 100 */
	gchar    transmission;    /* 't' key: d, f, t, s */
	gint     more;            /* 'm' key: 1=more chunks coming, 0=last */
	gint     compression;     /* 'o' key: z=zlib compressed */

	/* Image dimensions (source) */
	gint     src_width;       /* 's' key: source pixel width */
	gint     src_height;      /* 'v' key: source pixel height */

	/* Source region (crop) */
	gint     src_x;           /* 'x' key: left offset */
	gint     src_y;           /* 'y' key: top offset */
	gint     crop_w;          /* 'w' key: crop width */
	gint     crop_h;          /* 'h' key: crop height */

	/* Display dimensions (destination) */
	gint     dst_cols;        /* 'c' key: display columns */
	gint     dst_rows;        /* 'r' key: display rows */

	/* Positioning */
	gint     x_offset;        /* 'X' key: x offset within cell */
	gint     y_offset;        /* 'Y' key: y offset within cell */

	/* Z-layering */
	gint32   z_index;         /* 'z' key: z-layer (-1 = below text) */

	/* Cursor movement */
	gint     cursor_movement; /* 'C' key: 0=move cursor, 1=don't */

	/* Quiet mode */
	gint     quiet;           /* 'q' key: 0=all responses, 1=suppress OK, 2=suppress errors */

	/* Delete specifier */
	gchar    delete_target;   /* 'd' key when action='d' */

	/* Payload (base64 encoded data after semicolon) */
	const gchar *payload;     /* points into original buf, not owned */
	gsize        payload_len;

} GstGraphicsCommand;

/**
 * gst_gfx_command_parse:
 * @buf: the APC string content after the leading 'G'
 * @len: length of the buffer
 * @cmd: (out): parsed command structure (zero-initialized by caller)
 *
 * Parses a kitty graphics protocol command string of the form:
 *   key=val[,key=val]...;base64payload
 *
 * Sets defaults for unspecified fields: action='t', format=32,
 * transmission='d'.
 *
 * Returns: %TRUE if parsing succeeded, %FALSE on malformed input
 */
gboolean
gst_gfx_command_parse(
	const gchar        *buf,
	gsize               len,
	GstGraphicsCommand *cmd
);

G_END_DECLS

#endif /* GST_KITTYGFX_PARSER_H */
