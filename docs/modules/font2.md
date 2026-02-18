# Font2 Module

Fallback font preloading for faster rendering.

## Overview

The font2 module pre-loads fallback fonts into the font cache at startup. Without it, when GST encounters a character not in the primary font (e.g. emoji, Nerd Font icons), fontconfig performs a system-wide search to find a matching font -- this can cause a visible delay on the first occurrence.

By specifying your fallback fonts explicitly, the module loads them into the cache before any rendering begins, eliminating the runtime lookup delay.

## Configuration

### YAML

```yaml
modules:
  font2:
    enabled: true
    fonts:
      - "Symbols Nerd Font:pixelsize=14"
      - "Noto Color Emoji:pixelsize=14"
```

### C Config

```c
gst_config_set_module_config_bool(config, "font2", "enabled", TRUE);

const gchar *fonts[] = {
    "Symbols Nerd Font:pixelsize=14",
    "Noto Color Emoji:pixelsize=14",
    NULL
};
gst_config_set_module_config_strv(config, "font2", "fonts", fonts);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `fonts` | string[] | `[]` | List of fallback font specifications (fontconfig format) |

## Usage

Add any fonts that contain characters your primary font lacks:

- **Nerd Fonts** - For powerline symbols, devicons, and other coding icons
- **Noto Color Emoji** - For emoji rendering
- **CJK fonts** - For Chinese, Japanese, or Korean characters
- **Mathematical fonts** - For mathematical symbols

### Font Format

Fonts use the same fontconfig pattern syntax as the primary font:

```
Family:property=value:property=value
```

Common properties:
- `pixelsize=14` - Size in pixels (should match your primary font)
- `antialias=true` - Anti-aliasing
- `style=Regular` - Font style

### Relationship to Global Fallbacks

If the `fonts` list in the font2 module config is empty, the module falls back to the global `font.fallback` config:

```yaml
font:
  primary: "Liberation Mono:pixelsize=14"
  fallback:
    - "Noto Color Emoji:pixelsize=14"
```

The font2 module-specific `fonts` list takes priority over the global `font.fallback` when both are set.

## Notes

- Works with both X11 (`GstFontCache`) and Wayland (`GstCairoFontCache`) backends
- Fonts are loaded once during module activation and remain in the cache for the terminal's lifetime
- If a specified font is not installed, it is silently skipped
