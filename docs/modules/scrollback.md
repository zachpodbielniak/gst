# Scrollback Module

Ring buffer scrollback history with keyboard and mouse navigation.

## Overview

The scrollback module maintains a history of lines that have scrolled off the top of the terminal. You can scroll through this history using keyboard shortcuts or the mouse wheel. A position indicator appears in the top-right corner when you are scrolled away from the live output.

When you scroll up, the terminal pauses at your current position. New output continues in the background. Scrolling to the bottom (or pressing the shortcut) returns to live output.

## Configuration

### YAML

```yaml
modules:
  scrollback:
    enabled: true
    lines: 10000
    mouse_scroll_lines: 3
```

### C Config

```c
gst_config_set_module_config_bool(config, "scrollback", "enabled", TRUE);
gst_config_set_module_config_int(config, "scrollback", "lines", 10000);
gst_config_set_module_config_int(config, "scrollback", "mouse_scroll_lines", 3);
```

### Options

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `enabled` | boolean | `true` | | Enable the module |
| `lines` | integer | `10000` | 100-1,000,000 | Maximum lines stored in the ring buffer |
| `mouse_scroll_lines` | integer | `3` | 1-100 | Lines scrolled per mouse wheel click |

## Keybindings

The scrollback module handles these key combinations directly:

| Key | Action |
|-----|--------|
| `Shift+Page_Up` | Scroll up one page |
| `Shift+Page_Down` | Scroll down one page |
| `Shift+Home` (`Ctrl+Shift+Home`) | Jump to top of scrollback |
| `Shift+End` (`Ctrl+Shift+End`) | Jump to bottom (live output) |

Mouse wheel scrolling is handled via the configurable [mouse bindings](../keybindings.md):

| Button | Action |
|--------|--------|
| `Button4` | Scroll up |
| `Button5` | Scroll down |
| `Shift+Button4` | Fast scroll up |
| `Shift+Button5` | Fast scroll down |

## Usage

- Scroll up with `Shift+Page_Up` or the mouse wheel to browse history
- A `[offset/total]` indicator appears in the top-right corner when scrolled
- Press `Shift+End` or `Ctrl+Shift+End` to return to live output
- New output accumulates while you browse history -- you won't lose anything

## Notes

- The ring buffer uses a fixed capacity. When full, the oldest lines are discarded.
- The MCP module's `read_scrollback` and `search_scrollback` tools depend on this module being active.
- Memory usage is approximately proportional to `lines * average_columns * sizeof(GstGlyph)`.
