# GST Configuration Reference

This is the complete reference for all GST configuration options. Each option is shown with its YAML syntax and equivalent C API call.

For getting started quickly, see the [Quick Start Guide](quickstart.md). For the C configuration system, see [C Configuration](c-config.md).

## Config File Location

YAML config files are searched in this order:

1. `--config PATH` (command-line override)
2. `~/.config/gst/config.yaml`
3. `/etc/gst/config.yaml`
4. `/usr/share/gst/config.yaml`

If no file is found, built-in defaults are used.

To generate a default config file:

```bash
gst --generate-yaml-config > ~/.config/gst/config.yaml
```

## Top-Level Options

### ignore_yaml

Skip the YAML config entirely (equivalent to `--no-yaml-config`).

```yaml
ignore_yaml: true
```

This is useful if you use C config exclusively but want to keep a YAML file around for reference.

---

## terminal

Shell and terminal settings.

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `shell` | string | `$SHELL` or `/bin/bash` | any path | Shell command to spawn |
| `term` | string | `st-256color` | any string | `$TERM` environment variable |
| `tabspaces` | integer | `8` | 1-64 | Spaces per tab stop |

### YAML

```yaml
terminal:
  shell: /bin/bash
  term: st-256color
  tabspaces: 8
```

### C API

```c
gst_config_set_shell(config, "/bin/bash");
gst_config_set_term_name(config, "st-256color");
gst_config_set_tabspaces(config, 8);
```

| Getter | Setter |
|--------|--------|
| `gst_config_get_shell(config)` | `gst_config_set_shell(config, value)` |
| `gst_config_get_term_name(config)` | `gst_config_set_term_name(config, value)` |
| `gst_config_get_tabspaces(config)` | `gst_config_set_tabspaces(config, value)` |

---

## window

Window appearance and dimensions.

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `title` | string | `gst` | any string | Default window title |
| `geometry` | string | `80x24` | `COLSxROWS` | Terminal dimensions |
| `border` | integer | `2` | 0-100 | Border padding in pixels |

### YAML

```yaml
window:
  title: gst
  geometry: 80x24
  border: 2
```

### C API

```c
gst_config_set_title(config, "gst");
gst_config_set_cols(config, 80);
gst_config_set_rows(config, 24);
gst_config_set_border_px(config, 2);
```

Note: In YAML, columns and rows are specified together as `geometry: COLSxROWS`. In C, they are set separately via `gst_config_set_cols()` and `gst_config_set_rows()`.

| Getter | Setter |
|--------|--------|
| `gst_config_get_title(config)` | `gst_config_set_title(config, value)` |
| `gst_config_get_cols(config)` | `gst_config_set_cols(config, value)` |
| `gst_config_get_rows(config)` | `gst_config_set_rows(config, value)` |
| `gst_config_get_border_px(config)` | `gst_config_set_border_px(config, value)` |

---

## font

Font configuration using [fontconfig](https://www.freedesktop.org/wiki/Software/fontconfig/) format.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `primary` | string | `Liberation Mono:pixelsize=14:antialias=true:autohint=true` | Primary font |
| `fallback` | string[] | `[]` | Fallback font list (tried in order) |

### YAML

```yaml
font:
  primary: "Liberation Mono:pixelsize=14:antialias=true:autohint=true"
  fallback:
    - "Noto Color Emoji:pixelsize=14"
    - "Symbols Nerd Font:pixelsize=14"
```

### C API

```c
gst_config_set_font_primary(config,
    "Liberation Mono:pixelsize=14:antialias=true:autohint=true");

const gchar *fallbacks[] = {
    "Noto Color Emoji:pixelsize=14",
    "Symbols Nerd Font:pixelsize=14",
    NULL
};
gst_config_set_font_fallbacks(config, fallbacks);
```

| Getter | Setter |
|--------|--------|
| `gst_config_get_font_primary(config)` | `gst_config_set_font_primary(config, value)` |
| `gst_config_get_font_fallbacks(config)` | `gst_config_set_font_fallbacks(config, strv)` |

### Font Format

Fonts use fontconfig pattern syntax: `Family:property=value:property=value`

Common properties:
- `pixelsize=14` - Font size in pixels
- `size=10` - Font size in points
- `antialias=true` - Enable anti-aliasing
- `autohint=true` - Enable auto-hinting
- `style=Bold` - Font style

For pre-loading fallback fonts into the font cache at startup (avoiding runtime fontconfig lookups), see the [font2 module](modules/font2.md).

---

## colors

Terminal color configuration. See [Color Schemes](colors.md) for a detailed theming guide.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `foreground` | integer or string | `7` | Default text color (palette index 0-255 or `"#RRGGBB"`) |
| `background` | integer or string | `0` | Default background color |
| `cursor_fg` | integer or string | `0` | Cursor text color |
| `cursor_bg` | integer or string | `7` | Cursor background color |
| `palette` | string[] | (16 defaults) | 16-color palette as `"#RRGGBB"` hex strings |

### YAML

Colors can be specified as palette indices (integer) or hex strings:

```yaml
colors:
  # Using palette indices
  foreground: 7
  background: 0
  cursor_fg: 0
  cursor_bg: 7

  # Or using hex colors
  # foreground: "#cdd6f4"
  # background: "#1e1e2e"
  # cursor_fg: "#1e1e2e"
  # cursor_bg: "#cdd6f4"

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

### C API

```c
/* By palette index */
gst_config_set_fg_index(config, 7);
gst_config_set_bg_index(config, 0);
gst_config_set_cursor_fg_index(config, 0);
gst_config_set_cursor_bg_index(config, 7);

/* By hex color (overrides index) */
gst_config_set_fg_hex(config, "#cdd6f4");
gst_config_set_bg_hex(config, "#1e1e2e");
gst_config_set_cursor_fg_hex(config, "#1e1e2e");
gst_config_set_cursor_bg_hex(config, "#cdd6f4");

/* Set palette */
const gchar *palette[] = {
    "#000000", "#cc0000", "#4e9a06", "#c4a000",
    "#3465a4", "#75507b", "#06989a", "#d3d7cf",
    "#555753", "#ef2929", "#8ae234", "#fce94f",
    "#729fcf", "#ad7fa8", "#34e2e2", "#eeeeec",
    NULL
};
gst_config_set_palette_hex(config, palette, 16);
```

| Getter | Setter |
|--------|--------|
| `gst_config_get_fg_index(config)` | `gst_config_set_fg_index(config, index)` |
| `gst_config_get_bg_index(config)` | `gst_config_set_bg_index(config, index)` |
| `gst_config_get_cursor_fg_index(config)` | `gst_config_set_cursor_fg_index(config, index)` |
| `gst_config_get_cursor_bg_index(config)` | `gst_config_set_cursor_bg_index(config, index)` |
| `gst_config_get_fg_hex(config)` | `gst_config_set_fg_hex(config, hex)` |
| `gst_config_get_bg_hex(config)` | `gst_config_set_bg_hex(config, hex)` |
| `gst_config_get_cursor_fg_hex(config)` | `gst_config_set_cursor_fg_hex(config, hex)` |
| `gst_config_get_cursor_bg_hex(config)` | `gst_config_set_cursor_bg_hex(config, hex)` |
| `gst_config_get_palette_hex(config)` | `gst_config_set_palette_hex(config, palette, n)` |
| `gst_config_get_n_palette(config)` | (set via `gst_config_set_palette_hex`) |

---

## cursor

Cursor appearance.

| Option | Type | Default | Values / Range | Description |
|--------|------|---------|----------------|-------------|
| `shape` | string | `block` | `block`, `underline`, `bar` | Cursor shape |
| `blink` | boolean | `false` | `true`, `false` | Enable cursor blinking |
| `blink_rate` | integer | `500` | 50-5000 ms | Blink rate in milliseconds |

### YAML

```yaml
cursor:
  shape: block
  blink: false
  blink_rate: 500
```

### C API

```c
gst_config_set_cursor_shape(config, GST_CURSOR_SHAPE_BLOCK);
gst_config_set_cursor_blink(config, FALSE);
gst_config_set_blink_rate(config, 500);
```

Cursor shape enum values:
- `GST_CURSOR_SHAPE_BLOCK`
- `GST_CURSOR_SHAPE_UNDERLINE`
- `GST_CURSOR_SHAPE_BAR`

| Getter | Setter |
|--------|--------|
| `gst_config_get_cursor_shape(config)` | `gst_config_set_cursor_shape(config, shape)` |
| `gst_config_get_cursor_blink(config)` | `gst_config_set_cursor_blink(config, blink)` |
| `gst_config_get_blink_rate(config)` | `gst_config_set_blink_rate(config, ms)` |

---

## selection

Text selection behavior.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `word_delimiters` | string | `` `'\"()[]{}|`` (with leading space) | Characters that delimit words for double-click selection |

### YAML

```yaml
selection:
  word_delimiters: " `'\"()[]{}|"
```

### C API

```c
gst_config_set_word_delimiters(config, " `'\"()[]{}|");
```

| Getter | Setter |
|--------|--------|
| `gst_config_get_word_delimiters(config)` | `gst_config_set_word_delimiters(config, delimiters)` |

---

## keybinds

Keyboard bindings that map key combinations to actions. See [Keybindings](keybindings.md) for the full reference including all available actions, modifier syntax, and customization details.

### YAML

```yaml
keybinds:
  "Ctrl+Shift+c": clipboard_copy
  "Ctrl+Shift+v": clipboard_paste
```

### C API

See [Keybindings - C Config](keybindings.md#c-config).

---

## mousebinds

Mouse button bindings. See [Keybindings](keybindings.md) for the full reference.

### YAML

```yaml
mousebinds:
  "Button4": scroll_up
  "Button5": scroll_down
```

### C API

See [Keybindings - C Config](keybindings.md#c-config).

---

## modules

Module configuration. Each module has its own subsection under `modules:`. All modules support the `enabled` key to toggle them on or off.

See [Module Guide](modules/README.md) for the complete module reference, or click on individual modules below:

- [scrollback](modules/scrollback.md) - Scrollback history buffer
- [transparency](modules/transparency.md) - Window transparency
- [urlclick](modules/urlclick.md) - URL detection and opening
- [externalpipe](modules/externalpipe.md) - Pipe screen to external commands
- [boxdraw](modules/boxdraw.md) - Pixel-perfect box-drawing characters
- [visualbell](modules/visualbell.md) - Visual bell flash
- [undercurl](modules/undercurl.md) - Curly underline rendering
- [clipboard](modules/clipboard.md) - Clipboard integration
- [font2](modules/font2.md) - Fallback font preloading
- [keyboard_select](modules/keyboard-select.md) - Vim-like keyboard selection
- [kittygfx](modules/kittygfx.md) - Kitty graphics protocol
- [mcp](modules/mcp.md) - MCP server for AI assistants

### YAML (minimal example)

```yaml
modules:
  scrollback:
    enabled: true
    lines: 10000
  transparency:
    enabled: false
```

### C API

Module config uses generic key-value setters:

```c
gst_config_set_module_config_bool(config, "scrollback", "enabled", TRUE);
gst_config_set_module_config_int(config, "scrollback", "lines", 10000);
```

See each module's documentation for its specific options.

---

## Draw Latency (C config only)

These options control the rendering pipeline's batching behavior. They are not exposed in YAML -- use C config if you need to tune them.

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| min_latency | integer | `8` | 1-1000 ms | Minimum wait before rendering a frame |
| max_latency | integer | `33` | 1-1000 ms | Maximum wait before force-rendering |

The renderer batches rapid PTY writes into single frames. `min_latency` is how long to wait for more data before drawing. `max_latency` is the hard limit -- a frame is always drawn after this threshold.

### C API

```c
gst_config_set_min_latency(config, 8);
gst_config_set_max_latency(config, 33);
```

| Getter | Setter |
|--------|--------|
| `gst_config_get_min_latency(config)` | `gst_config_set_min_latency(config, ms)` |
| `gst_config_get_max_latency(config)` | `gst_config_set_max_latency(config, ms)` |
