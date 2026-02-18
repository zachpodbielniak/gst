# Box Drawing Module

Pixel-perfect rendering of Unicode box-drawing characters.

## Overview

The boxdraw module intercepts Unicode box-drawing characters (U+2500 through U+259F) and renders them using graphics primitives (lines and rectangles) instead of font glyphs. This produces pixel-perfect alignment between adjacent box characters, eliminating the gaps and misalignment common with font-rendered box drawing.

This is especially useful for TUI applications that draw borders, tables, and panels (e.g. htop, lazygit, midnight commander).

## Configuration

### YAML

```yaml
modules:
  boxdraw:
    enabled: true
    bold_offset: 1
```

### C Config

```c
gst_config_set_module_config_bool(config, "boxdraw", "enabled", TRUE);
gst_config_set_module_config_int(config, "boxdraw", "bold_offset", 1);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable the module |
| `bold_offset` | integer | `1` | Pixel offset for bold box-drawing lines (affects line thickness) |

## Usage

The module works automatically -- no interaction needed. When a box-drawing character appears in the terminal output, the module draws it with precise pixel coordinates instead of using the font.

### Supported Characters

| Range | Description |
|-------|-------------|
| U+2500-U+257F | Box Drawing (128 characters: single/double/heavy lines, corners, T-pieces, crosses) |
| U+2580-U+259F | Block Elements (partial blocks, shading) |

Dashed line variants (U+2504-U+250B, U+254C-U+254F) fall through to the font renderer since dashing requires more complex rendering.

## Notes

- Uses a 128-entry lookup table for U+2500-U+257F with up to 4 drawing primitives per character
- Works with both X11 (Xlib primitives) and Wayland (Cairo) backends via the abstract render context
- The `bold_offset` controls how much thicker bold variants are drawn
