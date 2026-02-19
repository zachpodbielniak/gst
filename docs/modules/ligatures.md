# Ligatures Module

Font ligature rendering via HarfBuzz text shaping.

## Overview

The ligatures module enables OpenType font ligatures in the terminal. Programming fonts like Fira Code, JetBrains Mono, Cascadia Code, and Iosevka include ligatures that combine character sequences like `->`, `=>`, `!=`, `<=`, `>=`, `|>`, `::`, `www` into single glyphs for improved readability.

The module intercepts glyph rendering via the `GstGlyphTransformer` interface. When a row is drawn, the module extracts runs of printable characters, shapes them with HarfBuzz, and detects when the shaper produces fewer output glyphs than input characters (indicating a ligature). Shaped glyph IDs are rendered directly via the render context's `draw_glyph_id` vtable, bypassing the normal codepoint-to-glyph lookup.

## Requirements

The ligatures module requires HarfBuzz development headers:

```bash
# Fedora
sudo dnf install harfbuzz-devel

# Ubuntu/Debian
sudo apt install libharfbuzz-dev
```

The module is conditionally compiled -- if `pkg-config --exists harfbuzz` fails, the module is skipped during build with no error.

## Configuration

### YAML

```yaml
modules:
  ligatures:
    enabled: true
    features:
      - "calt"
      - "liga"
    cache_size: 4096
```

### C Config

```c
gst_config_set_module_config_bool(config, "ligatures", "enabled", TRUE);
gst_config_set_module_config_int(config, "ligatures", "cache_size", 4096);
/* Note: the features list is YAML-only. In C config, the defaults
 * ("calt" + "liga") are used unless overridden via YAML. */
```

### Options

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `enabled` | boolean | `false` | | Enable the module |
| `features` | list of strings | `["calt", "liga"]` | | OpenType feature tags to enable |
| `cache_size` | integer | `4096` | 1-65536 | Maximum entries in the shaping cache |

### Common OpenType Feature Tags

| Tag | Name | Description |
|-----|------|-------------|
| `calt` | Contextual Alternates | Main ligature feature in most programming fonts |
| `liga` | Standard Ligatures | Traditional ligatures |
| `dlig` | Discretionary Ligatures | Optional ligatures (may change meaning) |
| `ss01`-`ss20` | Stylistic Sets | Font-specific alternate glyph styles |
| `cv01`-`cv99` | Character Variants | Font-specific character variants |

Most programming fonts use `calt` for their ligatures. The `liga` tag is included by default for fonts that use it instead.

## Usage

1. Install a programming font with ligature support (e.g. Fira Code, JetBrains Mono)
2. Set it as your terminal font in your GST config
3. Enable the ligatures module
4. Common ligature sequences will now render as combined glyphs

### Recommended Fonts

| Font | Ligature Style | Notes |
|------|---------------|-------|
| Fira Code | Extensive | The most popular ligature font for programming |
| JetBrains Mono | Moderate | Clean, well-balanced ligatures |
| Cascadia Code | Moderate | Microsoft's programming font |
| Iosevka | Extensive | Highly customizable, many variants |
| Victor Mono | Moderate | Includes cursive italics |

## Architecture

### Shaping Pipeline

1. **Row extraction** -- When `transform_glyph()` is called for the first glyph of a row, the module extracts the full codepoint run from the `GstLine`
2. **Cache lookup** -- The run is hashed (FNV-1a) and checked against the shaping cache
3. **HarfBuzz shaping** -- On cache miss, the run is shaped with `hb_shape()` using the configured features
4. **Ligature detection** -- If the output glyph count differs from input, or glyph IDs don't match simple codepoint-to-glyph mapping, a ligature was formed
5. **Rendering** -- Ligature glyphs are drawn via `gst_render_context_draw_glyph_id()`, which calls `XftDrawGlyphs` (X11) or `cairo_show_glyphs` (Wayland)
6. **Skip bitmap** -- Subsequent columns covered by the ligature are marked in a skip bitmap so the default renderer doesn't overdraw them

### Backend Support

The module works with both rendering backends:

- **X11**: Uses `XftLockFace()` to get the FreeType `FT_Face` for HarfBuzz font creation, renders with `XftDrawGlyphs()`
- **Wayland**: Uses `cairo_ft_scaled_font_lock_face()` for the FreeType face, renders with `cairo_show_glyphs()`

## Notes

- The shaping cache uses FNV-1a hashing of codepoint runs. The cache is a fixed-size array with linear probing. When full, the oldest entries are evicted.
- The skip bitmap is reset at the start of each row. Its maximum size is 4096 columns (`GST_LIGATURES_MAX_COLS`).
- Ligature shaping is only attempted for non-ASCII codepoints or when the glyph transformer is invoked (codepoints > 0x7F). ASCII-only rows pass through without shaping overhead.
- Tab, space, and null codepoints are never shaped.

## Source Files

| File | Description |
|------|-------------|
| `modules/ligatures/gst-ligatures-module.c` | Module implementation: HarfBuzz shaping, cache, skip bitmap |
| `modules/ligatures/gst-ligatures-module.h` | Type macros and struct declaration |
