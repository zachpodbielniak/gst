# GST - Claude Code Instructions

## Project Overview

GST (GObject Simple Terminal) is a GObject-based terminal emulator reimplementing st (suckless terminal) with a modular plugin architecture. The project replaces st's patch system with GModule-based extensibility.

## Build Commands

```bash
make              # Build everything
make DEBUG=1      # Debug build with symbols
make test         # Run GTest suite
make clean        # Clean build artifacts
```

## Directory Structure

- `src/core/` - Terminal emulation (GstTerminal, GstPty, GstEscapeParser)
- `src/rendering/` - Rendering (abstract GstRenderer, GstX11Renderer)
- `src/window/` - Windowing (abstract GstWindow, GstX11Window)
- `src/config/` - YAML configuration (GstConfig, GstColorScheme)
- `src/module/` - Module system (GstModule, GstModuleManager)
- `src/boxed/` - Boxed types (GstGlyph, GstCursor)
- `src/interfaces/` - Module hook interfaces
- `modules/` - Built-in modules (scrollback, transparency, etc.)
- `deps/yaml-glib/` - YAML parsing submodule

## Code Style

- **C Standard**: gnu89 (gcc -std=gnu89)
- **Indentation**: 4-space-width TAB characters (not spaces)
- **Comments**: `/* */` only, never `//`
- **Naming**:
  - Types: `GstPascalCase`
  - Functions: `gst_snake_case`
  - Macros/defines: `GST_UPPER_SNAKE_CASE`
  - Local variables: `snake_case`
- **Function declarations**: Return type on its own line

```c
GstTerminal *
gst_terminal_new(
    gint    cols,
    gint    rows
){
    /* implementation */
}
```

## GObject Patterns

- Use `G_DEFINE_TYPE_WITH_PRIVATE` for classes with private data
- Use `G_DEFINE_ABSTRACT_TYPE` for abstract base classes
- Use `G_DEFINE_INTERFACE` for interfaces
- Use `G_DEFINE_BOXED_TYPE` for boxed types
- Always provide GObject Introspection (gir) compatible documentation comments

## Key Files

- `src/gst.h` - Main umbrella header
- `src/gst-types.h` - Forward declarations and type aliases
- `src/gst-enums.h` - All enumerations with GType registration
- `src/core/gst-terminal.c` - Core terminal emulation
- `config.mk` - Build configuration
- `data/default-config.yaml` - Default configuration template

## Dependencies

- glib-2.0, gobject-2.0, gio-2.0, gmodule-2.0
- x11, xft, fontconfig
- libyaml
- deps/yaml-glib (submodule)

## Testing

Tests use GTest framework. Test files are in `tests/test-*.c`.

```bash
make test                    # Run all tests
./build/debug/test-glyph     # Run specific test
```

## Module Development

Modules implement interfaces from `src/interfaces/` and register via:

```c
G_MODULE_EXPORT GType gst_module_register(void);
```

Hook points are defined in `GstHookPoint` enum in `src/gst-enums.h`.

## Current Status

Phase 1 (Foundation) is complete:
- Build system
- Core headers and types
- Boxed types (GstGlyph, GstCursor, GstLine)
- GstTerminal skeleton with properties/signals
- Stub implementations for all major classes

Next phases:
- Port escape sequence parser from st.c
- Implement PTY management
- Port X11 rendering
- Implement YAML configuration loading
- Build module system with hook dispatch
