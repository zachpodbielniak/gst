/*
 * gst-types.h - GST Type Forward Declarations
 *
 * Copyright (C) 2024 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file contains forward declarations for all GST types.
 * Include this header when you need type names but not full definitions.
 */

#ifndef GST_TYPES_H
#define GST_TYPES_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/*
 * Forward declarations - Core Classes
 */

typedef struct _GstTerminal         GstTerminal;
typedef struct _GstTerminalClass    GstTerminalClass;

typedef struct _GstPty              GstPty;
typedef struct _GstPtyClass         GstPtyClass;

typedef struct _GstEscapeParser         GstEscapeParser;
typedef struct _GstEscapeParserClass    GstEscapeParserClass;

typedef struct _GstLine             GstLine;

/*
 * Forward declarations - Rendering Classes
 */

typedef struct _GstRenderer         GstRenderer;
typedef struct _GstRendererClass    GstRendererClass;

typedef struct _GstX11Renderer          GstX11Renderer;
typedef struct _GstX11RendererClass     GstX11RendererClass;

typedef struct _GstFontCache        GstFontCache;
typedef struct _GstFontCacheClass   GstFontCacheClass;

/*
 * Forward declarations - Window Classes
 */

typedef struct _GstWindow           GstWindow;
typedef struct _GstWindowClass      GstWindowClass;

typedef struct _GstX11Window        GstX11Window;
typedef struct _GstX11WindowClass   GstX11WindowClass;

/*
 * Forward declarations - Configuration Classes
 */

typedef struct _GstConfig           GstConfig;
typedef struct _GstConfigClass      GstConfigClass;

typedef struct _GstColorScheme          GstColorScheme;
typedef struct _GstColorSchemeClass     GstColorSchemeClass;

/*
 * Forward declarations - Module Classes
 */

typedef struct _GstModule           GstModule;
typedef struct _GstModuleClass      GstModuleClass;

typedef struct _GstModuleManager        GstModuleManager;
typedef struct _GstModuleManagerClass   GstModuleManagerClass;

typedef struct _GstModuleInfo       GstModuleInfo;

/*
 * Forward declarations - Selection Classes
 */

typedef struct _GstSelection        GstSelection;
typedef struct _GstSelectionClass   GstSelectionClass;

typedef struct _GstClipboard        GstClipboard;
typedef struct _GstClipboardClass   GstClipboardClass;

/*
 * Forward declarations - Boxed Types
 */

typedef struct _GstGlyph            GstGlyph;
typedef struct _GstCursor           GstCursor;

/*
 * Forward declarations - Interfaces
 */

typedef struct _GstColorProvider            GstColorProvider;
typedef struct _GstColorProviderInterface   GstColorProviderInterface;

typedef struct _GstInputHandler             GstInputHandler;
typedef struct _GstInputHandlerInterface    GstInputHandlerInterface;

typedef struct _GstOutputFilter             GstOutputFilter;
typedef struct _GstOutputFilterInterface    GstOutputFilterInterface;

typedef struct _GstRenderOverlay            GstRenderOverlay;
typedef struct _GstRenderOverlayInterface   GstRenderOverlayInterface;

typedef struct _GstFontProvider             GstFontProvider;
typedef struct _GstFontProviderInterface    GstFontProviderInterface;

typedef struct _GstUrlHandler               GstUrlHandler;
typedef struct _GstUrlHandlerInterface      GstUrlHandlerInterface;

typedef struct _GstGlyphTransformer             GstGlyphTransformer;
typedef struct _GstGlyphTransformerInterface    GstGlyphTransformerInterface;

typedef struct _GstBellHandler              GstBellHandler;
typedef struct _GstBellHandlerInterface     GstBellHandlerInterface;

typedef struct _GstExternalPipe             GstExternalPipe;
typedef struct _GstExternalPipeInterface    GstExternalPipeInterface;

/*
 * Common type aliases for clarity
 */

/* Unicode code point (32-bit) */
typedef guint32 GstRune;

/* Color value (RGBA, 8 bits per channel) */
typedef guint32 GstColor;

/* Terminal mode flags */
typedef guint32 GstModeFlags;

/* Glyph attribute flags */
typedef guint32 GstAttrFlags;

/* Cursor style enumeration */
typedef gint GstCursorStyle;

/* Selection type enumeration */
typedef gint GstSelectionType;

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
