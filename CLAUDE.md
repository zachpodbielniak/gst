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

Phases 1-4 are complete (110 tests pass):

Phase 1 (Foundation):
- Build system, core headers and types
- Boxed types (GstGlyph, GstCursor, GstLine)
- GstTerminal skeleton with properties/signals

Phase 2 (Terminal Emulation):
- Escape sequence parser ported from st.c
- PTY management (GstPty)
- Selection system (GstSelection)

Phase 3 (X11 Rendering):
- Font cache (GstFontCache)
- X11 renderer (GstX11Renderer)
- X11 window (GstX11Window)
- main.c wiring via GObject signals

Phase 4 (YAML Configuration):
- GstConfig: full YAML loading from ~/.config/gst/config.yaml
- Config search path: --config > XDG_CONFIG_HOME > /etc > /usr/share
- Sections: terminal, window, font, colors, cursor, selection, draw, modules
- GstColorScheme: setters + load_from_config() for palette application
- main.c integrated: all hardcoded constants replaced with config getters
- CLI options (--font, --geometry, --title, --config) override config values
- Save/load round-trip via yaml-glib builder/generator

Phase 5 (Module System):
- GstModule: added configure vfunc, priority (get/set), is_active accessor
- GstModuleManager: hook table with priority-sorted dispatch per hook point
- Interface auto-detection: registers hooks based on implemented interfaces
- .so loading via GModule: gst_module_register() entry point convention
- Directory scanning: load_from_directory() for bulk module loading
- Typed dispatchers: dispatch_bell(), dispatch_key_event(), dispatch_render_overlay()
- Config integration: set_config(), activate_all(), deactivate_all()
- main.c wiring: module loading from $GST_MODULE_PATH/user/system dirs
- main.c: bell dispatch through modules, key event interception by modules
- Visual bell sample module (modules/visualbell/) validates architecture end-to-end
- 12 new module tests (122 total tests pass)

Phase 6 (Built-in Modules):
- Six built-in modules: scrollback, transparency, urlclick, externalpipe, boxdraw, visualbell
- Ring buffer scrollback with Shift+PgUp/PgDn keyboard navigation and overlay rendering
- Window transparency via _NET_WM_WINDOW_OPACITY with focus/unfocus tracking
- URL detection and opening via configurable regex and opener command
- External pipe: pipe visible terminal content to shell commands
- Box-drawing: pixel-perfect rendering of U+2500-U+259F using X11 primitives
- Visual bell: flash notification on bell event
- Render context (GstX11RenderContext) shared between renderer and modules
- Undercurl rendering support via GstRenderOverlay
- 5 new tests (127 total tests pass)

Phase 7 (Per-Module YAML Configuration):
- Module manager checks `enabled: false` in module config to skip activation
- All 6 built-in modules read config via gst_config_get_module_config()
- Scrollback: `lines` (capacity), `mouse_scroll_lines`
- Transparency: `opacity`, `focus_opacity`, `unfocus_opacity`
- URL click: `opener`, `regex` (recompiles GRegex on change)
- External pipe: `command`
- Boxdraw: `bold_offset`
- Visual bell: `duration` (flash_duration_ms)
- Fixed config key naming: `url_click` → `urlclick` to match module name
- 3 new tests (130 total tests pass)

Next phases:
- Key binding parsing and remapping
- Wayland renderer backend
