# GST Module System

## Overview

The GST module system provides extensibility through dynamically loaded shared objects (`.so` files). Modules extend terminal functionality by implementing GObject interfaces that correspond to hook points in the terminal lifecycle.

## Architecture

### Core Components

- **GstModule** (`src/module/gst-module.h`) - Abstract base class for all modules
- **GstModuleManager** (`src/module/gst-module-manager.h`) - Registry, hook dispatch, and .so loading
- **GstModuleInfo** (`src/module/gst-module-info.h`) - Module metadata boxed type

### Hook Dispatch

The module manager maintains a priority-sorted list per hook point (defined in `GstHookPoint` enum). When an event occurs, the manager walks the hook list and calls each active module's interface method.

- **Consumable hooks** (key events, mouse): dispatch stops when a handler returns `TRUE`
- **Non-consumable hooks** (bell, render overlay): all handlers are called

### Interface-to-Hook Mapping

When a module is registered, the manager introspects its GType to detect implemented interfaces and auto-registers the appropriate hooks:

| Interface | Hook Point |
|-----------|-----------|
| `GstInputHandler` | `GST_HOOK_KEY_PRESS` |
| `GstOutputFilter` | `GST_HOOK_PRE_OUTPUT` |
| `GstBellHandler` | `GST_HOOK_BELL` |
| `GstRenderOverlay` | `GST_HOOK_RENDER_OVERLAY` |
| `GstGlyphTransformer` | `GST_HOOK_GLYPH_TRANSFORM` |
| `GstExternalPipe` | `GST_HOOK_EXTERNAL_PIPE` |
| `GstUrlHandler` | `GST_HOOK_URL_DETECT` |
| `GstColorProvider` | `GST_HOOK_COLOR_QUERY` |
| `GstFontProvider` | `GST_HOOK_FONT_LOAD` |

### Priority System

Modules have a priority value (`GstModulePriority`) that determines dispatch order. Lower values run first:

- `GST_MODULE_PRIORITY_HIGHEST` (-1000)
- `GST_MODULE_PRIORITY_HIGH` (-100)
- `GST_MODULE_PRIORITY_NORMAL` (0) - default
- `GST_MODULE_PRIORITY_LOW` (100)
- `GST_MODULE_PRIORITY_LOWEST` (1000)

## Module Loading

Modules are loaded from `.so` files at startup. Search order:

1. `$GST_MODULE_PATH` (colon-separated directories)
2. `~/.config/gst/modules/`
3. System module directory (`/usr/local/lib/gst/modules/`)

### Loading Sequence

1. `g_module_open()` with `G_MODULE_BIND_LOCAL`
2. `g_module_symbol()` to find `gst_module_register` entry point
3. Entry point returns a `GType` for the module class
4. `g_object_new()` to instantiate
5. Auto-detect interfaces and register hooks
6. Store GModule handle for cleanup

## Writing a Module

### Entry Point

Every module `.so` must export:

```c
G_MODULE_EXPORT GType
gst_module_register(void);
```

### Minimal Example

```c
#include <glib-object.h>
#include <gmodule.h>
#include "../../src/module/gst-module.h"
#include "../../src/interfaces/gst-bell-handler.h"

/* Define the module type */
typedef struct {
    GstModule parent_instance;
} MyModule;

typedef struct {
    GstModuleClass parent_class;
} MyModuleClass;

/* Forward declare interface init */
static void my_module_bell_init(GstBellHandlerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(MyModule, my_module, GST_TYPE_MODULE,
    G_IMPLEMENT_INTERFACE(GST_TYPE_BELL_HANDLER, my_module_bell_init))

/* Implement GstModule vfuncs */
static const gchar *
my_module_get_name(GstModule *mod) { return "my-module"; }

static const gchar *
my_module_get_description(GstModule *mod) { return "My module"; }

static gboolean
my_module_activate(GstModule *mod) { return TRUE; }

static void
my_module_deactivate(GstModule *mod) { }

/* Implement GstBellHandler */
static void
my_module_handle_bell(GstBellHandler *handler)
{
    g_print("Bell!\n");
}

static void
my_module_bell_init(GstBellHandlerInterface *iface)
{
    iface->handle_bell = my_module_handle_bell;
}

static void
my_module_class_init(MyModuleClass *klass)
{
    GstModuleClass *mod_class = GST_MODULE_CLASS(klass);
    mod_class->get_name = my_module_get_name;
    mod_class->get_description = my_module_get_description;
    mod_class->activate = my_module_activate;
    mod_class->deactivate = my_module_deactivate;
}

static void
my_module_init(MyModule *self) { }

/* Entry point */
G_MODULE_EXPORT GType
gst_module_register(void) { return my_module_get_type(); }
```

### Module Makefile

```makefile
MODULE_NAME := mymodule
MODULE_SRC  := my-module.c

CC      ?= gcc
CFLAGS  ?= $(shell pkg-config --cflags glib-2.0 gobject-2.0 gmodule-2.0) -fPIC -I../../src
LDFLAGS ?= -shared

all: $(OUTDIR)/$(MODULE_NAME).so

$(OUTDIR)/$(MODULE_NAME).so: $(MODULE_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L$(LIBDIR) -lgst \
		$(shell pkg-config --libs glib-2.0 gobject-2.0 gmodule-2.0)
```

### Building

From the project root:

```bash
make modules DEBUG=1    # Builds all modules in modules/*/
```

Or manually:

```bash
make -C modules/mymodule \
    OUTDIR=$(pwd)/build/debug/modules \
    LIBDIR=$(pwd)/build/debug
```

### Testing

```bash
GST_MODULE_PATH=./build/debug/modules ./build/debug/gst
```

## Available Interfaces

| Interface | Purpose | Key Method |
|-----------|---------|------------|
| `GstInputHandler` | Intercept keyboard events | `handle_key_event(keyval, keycode, state) -> bool` |
| `GstOutputFilter` | Filter terminal output | `filter_output(input, length) -> string` |
| `GstBellHandler` | Handle bell events | `handle_bell()` |
| `GstRenderOverlay` | Draw overlays on terminal | `render(context, width, height)` |
| `GstGlyphTransformer` | Transform glyph rendering | `transform_glyph(codepoint, context, x, y, w, h) -> bool` |
| `GstExternalPipe` | Pipe data to external commands | `pipe_data(command, data, length) -> bool` |
| `GstUrlHandler` | Handle URL detection/opening | `open_url(url) -> bool` |
| `GstColorProvider` | Provide custom color schemes | `get_color(index, color) -> bool` |
| `GstFontProvider` | Provide custom fonts | `get_font_description() -> string` |
