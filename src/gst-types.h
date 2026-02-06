/*
 * gst-types.h - GST Type Forward Declarations
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file contains forward declarations for manually-defined GST types
 * and common type aliases. Types using G_DECLARE_FINAL_TYPE or
 * G_DECLARE_DERIVABLE_TYPE are declared in their own headers.
 */

#ifndef GST_TYPES_H
#define GST_TYPES_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/*
 * Forward declarations - Manually defined core types
 *
 * Types that use G_DECLARE_FINAL_TYPE / G_DECLARE_DERIVABLE_TYPE /
 * G_DECLARE_INTERFACE are NOT listed here because those macros
 * generate their own typedefs.
 */

typedef struct _GstTerminal         GstTerminal;
typedef struct _GstTerminalClass    GstTerminalClass;

typedef struct _GstPty              GstPty;
typedef struct _GstPtyClass         GstPtyClass;

typedef struct _GstEscapeParser         GstEscapeParser;
typedef struct _GstEscapeParserClass    GstEscapeParserClass;

/*
 * Forward declarations - Boxed Types
 */

typedef struct _GstGlyph            GstGlyph;
typedef struct _GstCursor           GstCursor;
typedef struct _GstLine             GstLine;
typedef struct _GstModuleInfo       GstModuleInfo;

/*
 * Common type aliases
 */

/* Unicode code point (32-bit) */
typedef guint32 GstRune;

/* Color value (RGBA, 8 bits per channel) */
typedef guint32 GstColor;

/*
 * Macros for extracting color components
 */

#define GST_COLOR_R(c)  (((c) >> 24) & 0xFF)
#define GST_COLOR_G(c)  (((c) >> 16) & 0xFF)
#define GST_COLOR_B(c)  (((c) >>  8) & 0xFF)
#define GST_COLOR_A(c)  (((c)      ) & 0xFF)

#define GST_COLOR_RGBA(r, g, b, a) \
    ((((guint32)(r) & 0xFF) << 24) | \
     (((guint32)(g) & 0xFF) << 16) | \
     (((guint32)(b) & 0xFF) <<  8) | \
     (((guint32)(a) & 0xFF)      ))

#define GST_COLOR_RGB(r, g, b)  GST_COLOR_RGBA(r, g, b, 0xFF)

/*
 * Maximum values
 */

#define GST_MAX_COLS        (32767)
#define GST_MAX_ROWS        (32767)
#define GST_MAX_ESC_LEN     (128)
#define GST_MAX_STR_LEN     (4096)
#define GST_MAX_ARGS        (16)

/*
 * Default values
 */

#define GST_DEFAULT_COLS    (80)
#define GST_DEFAULT_ROWS    (24)
#define GST_DEFAULT_TABSTOP (8)

G_END_DECLS

#endif /* GST_TYPES_H */
