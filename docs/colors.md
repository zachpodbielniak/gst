# GST Color Schemes

## How Terminal Colors Work

GST uses a 256-color palette, following the standard terminal color model:

| Range | Count | Description |
|-------|-------|-------------|
| 0-7 | 8 | Standard colors (black, red, green, yellow, blue, magenta, cyan, white) |
| 8-15 | 8 | Bright variants of the standard colors |
| 16-231 | 216 | 6x6x6 RGB color cube |
| 232-255 | 24 | Grayscale ramp (dark to light) |

Colors 0-15 are the ones you configure in your palette. Colors 16-255 are computed automatically.

## Default Palette

| Index | Name | Hex | Used For |
|-------|------|-----|----------|
| 0 | Black | `#000000` | Background (default) |
| 1 | Red | `#cc0000` | Errors, diffs (removals) |
| 2 | Green | `#4e9a06` | Success, diffs (additions) |
| 3 | Yellow | `#c4a000` | Warnings |
| 4 | Blue | `#3465a4` | Info, directories |
| 5 | Magenta | `#75507b` | Special files |
| 6 | Cyan | `#06989a` | Links, secondary info |
| 7 | White | `#d3d7cf` | Foreground text (default) |
| 8 | Bright Black | `#555753` | Comments, muted text |
| 9 | Bright Red | `#ef2929` | Bold errors |
| 10 | Bright Green | `#8ae234` | Bold success |
| 11 | Bright Yellow | `#fce94f` | Bold warnings |
| 12 | Bright Blue | `#729fcf` | Bold info |
| 13 | Bright Magenta | `#ad7fa8` | Bold special |
| 14 | Bright Cyan | `#34e2e2` | Bold links |
| 15 | Bright White | `#eeeeec` | Bold text |

## Configuring Colors

### Foreground and Background

You can set foreground and background colors two ways:

**By palette index** (integer 0-255):

```yaml
colors:
  foreground: 7       # white
  background: 0       # black
  cursor_fg: 0
  cursor_bg: 7
```

**By hex color** (string `"#RRGGBB"`):

```yaml
colors:
  foreground: "#cdd6f4"
  background: "#1e1e2e"
  cursor_fg: "#1e1e2e"
  cursor_bg: "#cdd6f4"
```

When using hex colors, the value is used directly and does not reference the palette.

### Custom Palette

Override the 16-color palette by providing an array of hex strings:

```yaml
colors:
  palette:
    - "#000000"  # 0  black
    - "#cc0000"  # 1  red
    - "#4e9a06"  # 2  green
    - "#c4a000"  # 3  yellow
    - "#3465a4"  # 4  blue
    - "#75507b"  # 5  magenta
    - "#06989a"  # 6  cyan
    - "#d3d7cf"  # 7  white
    - "#555753"  # 8  bright black
    - "#ef2929"  # 9  bright red
    - "#8ae234"  # 10 bright green
    - "#fce94f"  # 11 bright yellow
    - "#729fcf"  # 12 bright blue
    - "#ad7fa8"  # 13 bright magenta
    - "#34e2e2"  # 14 bright cyan
    - "#eeeeec"  # 15 bright white
```

You can provide fewer than 16 entries -- only the provided indices are overwritten.

### C API

```c
GstConfig *config = gst_config_get_default();

/* By index */
gst_config_set_fg_index(config, 7);
gst_config_set_bg_index(config, 0);

/* By hex (overrides index) */
gst_config_set_fg_hex(config, "#cdd6f4");
gst_config_set_bg_hex(config, "#1e1e2e");
gst_config_set_cursor_fg_hex(config, "#1e1e2e");
gst_config_set_cursor_bg_hex(config, "#cdd6f4");

/* Full palette */
const gchar *palette[] = {
    "#45475a", "#f38ba8", "#a6e3a1", "#f9e2af",
    "#89b4fa", "#f5c2e7", "#94e2d5", "#bac2de",
    "#585b70", "#f38ba8", "#a6e3a1", "#f9e2af",
    "#89b4fa", "#f5c2e7", "#94e2d5", "#a6adc8",
    NULL
};
gst_config_set_palette_hex(config, palette, 16);
```

## Example: Catppuccin Mocha

A popular dark theme with pastel colors.

### YAML

```yaml
colors:
  foreground: "#cdd6f4"
  background: "#1e1e2e"
  cursor_fg: "#1e1e2e"
  cursor_bg: "#f5e0dc"
  palette:
    - "#45475a"  # 0  surface1
    - "#f38ba8"  # 1  red
    - "#a6e3a1"  # 2  green
    - "#f9e2af"  # 3  yellow
    - "#89b4fa"  # 4  blue
    - "#f5c2e7"  # 5  pink
    - "#94e2d5"  # 6  teal
    - "#bac2de"  # 7  subtext1
    - "#585b70"  # 8  surface2
    - "#f38ba8"  # 9  red
    - "#a6e3a1"  # 10 green
    - "#f9e2af"  # 11 yellow
    - "#89b4fa"  # 12 blue
    - "#f5c2e7"  # 13 pink
    - "#94e2d5"  # 14 teal
    - "#a6adc8"  # 15 subtext0
```

### C Config

```c
G_MODULE_EXPORT gboolean
gst_config_init(void)
{
    GstConfig *config = gst_config_get_default();

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

    return TRUE;
}
```

## Tips

- **Foreground and background by index**: When using palette indices for `foreground` and `background`, changing the palette automatically changes those colors. This is useful for quick theme switching.
- **Hex colors are independent**: When using hex strings for `foreground`/`background`, they are decoupled from the palette. You must update them separately.
- **Bold text**: By default, bold text uses the bright variant of its color (index + 8). So bold red (index 1) renders as bright red (index 9).
- **Cursor visibility**: Choose cursor colors that contrast with your background. The cursor foreground is the text color inside the cursor, and cursor background is the cursor block color.
- **256-color programs**: Colors 16-231 (the RGB cube) and 232-255 (grayscale) are computed from a standard formula and cannot be customized via the palette. They are consistent across all terminals.
