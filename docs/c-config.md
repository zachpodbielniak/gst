# GST C Configuration

## Overview

GST supports a C configuration file, inspired by the suckless tradition of `config.h`. Instead of editing and recompiling the terminal itself, you write a small C file that GST compiles into a shared object (`.so`) and loads at startup. This gives you the full power of C -- conditional logic, computed values, external libraries -- while keeping the terminal binary unchanged.

C config is **optional**. Most users only need the [YAML config](configuration.md). Use C config when you need:

- Conditional configuration (e.g. different settings per hostname)
- Computed values (e.g. colors from environment variables)
- Access to GObject APIs beyond simple key-value settings

## How It Works

1. GST finds your `config.c` file (see search path below)
2. Computes a SHA256 hash of the file contents
3. If a cached `.so` with the same hash exists, loads it directly
4. Otherwise, compiles it with `gcc` via the [crispy](https://gitlab.com/zachpodbielniak/crispy) library
5. Loads the `.so` and calls your `gst_config_init()` function
6. Your function runs after YAML config is loaded -- values you set override YAML

The compiled `.so` is cached in `$XDG_CACHE_HOME/gst/` (usually `~/.cache/gst/`). Recompilation only happens when the source file changes.

## Config File Search Path

1. `--c-config PATH` (explicit override)
2. `~/.config/gst/config.c`
3. `/etc/gst/config.c`
4. `/usr/share/gst/config.c`

## Getting Started

Generate the default template:

```bash
gst --generate-c-config > ~/.config/gst/config.c
```

Edit it, then just launch GST -- it compiles automatically:

```bash
gst
```

To compile without launching:

```bash
gst --recompile
```

## Structure

A C config file has one required function:

```c
#include <gst/gst.h>

G_MODULE_EXPORT gboolean
gst_config_init(void)
{
    GstConfig *config = gst_config_get_default();

    /* Set your options here */
    gst_config_set_title(config, "my terminal");
    gst_config_set_font_primary(config,
        "JetBrains Mono:pixelsize=14:antialias=true");

    return TRUE;
}
```

Key points:
- Include `<gst/gst.h>` for all GST types and functions
- The function must be `G_MODULE_EXPORT gboolean gst_config_init(void)`
- Get the config singleton with `gst_config_get_default()`
- Return `TRUE` on success, `FALSE` to fall back to YAML-only config

## CRISPY_PARAMS

If your config needs extra compiler flags (custom include paths, additional libraries), define `CRISPY_PARAMS` at the top of the file:

```c
#define CRISPY_PARAMS "-I/custom/path -lmylib"

#include <gst/gst.h>
/* ... */
```

The config compiler extracts this define and passes its value as extra flags to `gcc`. Shell expansion is supported (e.g. `$(pkg-config --cflags mylib)`).

## Complete API Reference

### Terminal

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_set_shell` | `(config, "/bin/bash")` | Shell to spawn |
| `gst_config_set_term_name` | `(config, "st-256color")` | `$TERM` value |
| `gst_config_set_tabspaces` | `(config, 8)` | Tab stop width (1-64) |

### Window

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_set_title` | `(config, "gst")` | Window title |
| `gst_config_set_cols` | `(config, 80)` | Column count |
| `gst_config_set_rows` | `(config, 24)` | Row count |
| `gst_config_set_border_px` | `(config, 2)` | Border padding in pixels (0-100) |

### Font

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_set_font_primary` | `(config, "font:pixelsize=14")` | Primary font (fontconfig format) |
| `gst_config_set_font_fallbacks` | `(config, strv)` | NULL-terminated array of fallback fonts |

### Colors

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_set_fg_index` | `(config, 7)` | Foreground palette index (0-255) |
| `gst_config_set_bg_index` | `(config, 0)` | Background palette index (0-255) |
| `gst_config_set_cursor_fg_index` | `(config, 0)` | Cursor text palette index |
| `gst_config_set_cursor_bg_index` | `(config, 7)` | Cursor background palette index |
| `gst_config_set_fg_hex` | `(config, "#cdd6f4")` | Foreground hex color (overrides index) |
| `gst_config_set_bg_hex` | `(config, "#1e1e2e")` | Background hex color (overrides index) |
| `gst_config_set_cursor_fg_hex` | `(config, "#1e1e2e")` | Cursor text hex color |
| `gst_config_set_cursor_bg_hex` | `(config, "#cdd6f4")` | Cursor background hex color |
| `gst_config_set_palette_hex` | `(config, palette, 16)` | 16-color palette from hex string array |

### Cursor

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_set_cursor_shape` | `(config, GST_CURSOR_SHAPE_BLOCK)` | `BLOCK`, `UNDERLINE`, or `BAR` |
| `gst_config_set_cursor_blink` | `(config, FALSE)` | Enable/disable blinking |
| `gst_config_set_blink_rate` | `(config, 500)` | Blink rate in ms (50-5000) |

### Selection

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_set_word_delimiters` | `(config, " \`'\"()[]{}|")` | Word delimiter characters |

### Draw Latency

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_set_min_latency` | `(config, 8)` | Min draw latency in ms (1-1000) |
| `gst_config_set_max_latency` | `(config, 33)` | Max draw latency in ms (1-1000) |

### Keybindings

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_add_keybind` | `(config, "Ctrl+Shift+c", "clipboard_copy")` | Add a keyboard binding |
| `gst_config_add_mousebind` | `(config, "Button4", "scroll_up")` | Add a mouse binding |
| `gst_config_clear_keybinds` | `(config)` | Remove all keyboard bindings |
| `gst_config_clear_mousebinds` | `(config)` | Remove all mouse bindings |

### Module Configuration

Module options are set using generic typed setters:

| Function | Arguments | Description |
|----------|-----------|-------------|
| `gst_config_set_module_config_bool` | `(config, "module", "key", TRUE)` | Set boolean |
| `gst_config_set_module_config_int` | `(config, "module", "key", 10000)` | Set integer |
| `gst_config_set_module_config_double` | `(config, "module", "key", 0.9)` | Set double |
| `gst_config_set_module_config_string` | `(config, "module", "key", "value")` | Set string |
| `gst_config_set_module_config_strv` | `(config, "module", "key", strv)` | Set string array |
| `gst_config_set_module_config_sub_bool` | `(config, "module", "sub", "key", TRUE)` | Set nested bool (e.g. MCP tools) |

Examples:

```c
/* Scrollback: 20000 lines */
gst_config_set_module_config_bool(config, "scrollback", "enabled", TRUE);
gst_config_set_module_config_int(config, "scrollback", "lines", 20000);

/* Transparency: 85% opacity */
gst_config_set_module_config_bool(config, "transparency", "enabled", TRUE);
gst_config_set_module_config_double(config, "transparency", "opacity", 0.85);

/* URL click: custom opener */
gst_config_set_module_config_string(config, "urlclick", "opener", "firefox");

/* Font2: fallback fonts */
const gchar *fonts[] = {
    "Symbols Nerd Font:pixelsize=14",
    "Noto Color Emoji:pixelsize=14",
    NULL
};
gst_config_set_module_config_strv(config, "font2", "fonts", fonts);

/* MCP: enable specific tools */
gst_config_set_module_config_bool(config, "mcp", "enabled", TRUE);
gst_config_set_module_config_sub_bool(config, "mcp", "tools", "read_screen", TRUE);
gst_config_set_module_config_sub_bool(config, "mcp", "tools", "send_text", FALSE);
```

## Relationship to YAML Config

The load order is:

1. Built-in defaults
2. YAML config file
3. **C config** (your `gst_config_init()` runs here)
4. CLI flags

C config values override YAML values. This means you can use YAML for most settings and C config only for the things that need programmatic logic.

To skip YAML entirely and use only C config:

```bash
gst --no-yaml-config
```

To skip C config entirely:

```bash
gst --no-c-config
```

## CLI Flags

| Flag | Description |
|------|-------------|
| `--c-config PATH` | Use a specific C config file |
| `--generate-c-config` | Print the default C config template to stdout |
| `--recompile` | Compile the C config and exit (do not start terminal) |
| `--no-c-config` | Skip C config compilation and loading |

## Advanced: Accessing the Module Manager

In `gst_config_init()`, the module manager singleton is also available:

```c
GstModuleManager *mgr = gst_module_manager_get_default();
```

This runs before modules are activated, so you can configure module settings that will be picked up during activation.

## Example: Full C Config

```c
#include <gst/gst.h>

G_MODULE_EXPORT gboolean
gst_config_init(void)
{
    GstConfig *config = gst_config_get_default();

    /* Terminal */
    gst_config_set_shell(config, "/bin/bash");
    gst_config_set_term_name(config, "st-256color");
    gst_config_set_tabspaces(config, 4);

    /* Window */
    gst_config_set_title(config, "gst");
    gst_config_set_cols(config, 120);
    gst_config_set_rows(config, 40);
    gst_config_set_border_px(config, 2);

    /* Font */
    gst_config_set_font_primary(config,
        "JetBrains Mono:pixelsize=14:antialias=true:autohint=true");

    /* Catppuccin Mocha colors */
    gst_config_set_fg_hex(config, "#cdd6f4");
    gst_config_set_bg_hex(config, "#1e1e2e");
    gst_config_set_cursor_fg_hex(config, "#1e1e2e");
    gst_config_set_cursor_bg_hex(config, "#f5e0dc");

    const gchar *palette[] = {
        "#45475a", "#f38ba8", "#a6e3a1", "#f9e2af",
        "#89b4fa", "#f5c2e7", "#94e2d5", "#bac2de",
        "#585b70", "#f38ba8", "#a6e3a1", "#f9e2af",
        "#89b4fa", "#f5c2e7", "#94e2d5", "#a6adc8",
        NULL
    };
    gst_config_set_palette_hex(config, palette, 16);

    /* Cursor */
    gst_config_set_cursor_shape(config, GST_CURSOR_SHAPE_BAR);
    gst_config_set_cursor_blink(config, TRUE);
    gst_config_set_blink_rate(config, 400);

    /* Keybindings */
    gst_config_add_keybind(config, "Shift+Insert", "paste_primary");

    /* Modules */
    gst_config_set_module_config_bool(config, "scrollback", "enabled", TRUE);
    gst_config_set_module_config_int(config, "scrollback", "lines", 20000);

    gst_config_set_module_config_bool(config, "transparency", "enabled", TRUE);
    gst_config_set_module_config_double(config, "transparency", "opacity", 0.9);

    return TRUE;
}
```
