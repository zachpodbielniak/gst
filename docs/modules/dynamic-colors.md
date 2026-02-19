# Dynamic Colors Module

Runtime color palette changes via OSC escape sequences.

## Overview

The dynamic colors module allows programs to query and modify the terminal's color palette at runtime using standard OSC escape sequences. This enables tools like `base16-shell`, `theme.sh`, and Neovim colorscheme plugins to change the terminal's foreground, background, cursor color, and individual palette entries without restarting the terminal.

Color changes take effect immediately and trigger a full repaint. Query responses are written back to the PTY in standard `rgb:RR/GG/BB` format.

## Configuration

### YAML

```yaml
modules:
  dynamic_colors:
    enabled: true
    allow_query: true
    allow_set: true
```

### C Config

```c
gst_config_set_module_config_bool(config, "dynamic_colors", "enabled", TRUE);
gst_config_set_module_config_bool(config, "dynamic_colors", "allow_query", TRUE);
gst_config_set_module_config_bool(config, "dynamic_colors", "allow_set", TRUE);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable the module |
| `allow_query` | boolean | `true` | Allow programs to query current colors |
| `allow_set` | boolean | `true` | Allow programs to change colors |

## Escape Sequences

### Set Colors

| Sequence | Action |
|----------|--------|
| `ESC ] 10 ; <color> ST` | Set default foreground color |
| `ESC ] 11 ; <color> ST` | Set default background color |
| `ESC ] 12 ; <color> ST` | Set cursor color |
| `ESC ] 4 ; <index> ; <color> ST` | Set palette color at `<index>` (0-255) |

### Query Colors

| Sequence | Response |
|----------|----------|
| `ESC ] 10 ; ? ST` | `ESC ] 10 ; rgb:RR/GG/BB ST` |
| `ESC ] 11 ; ? ST` | `ESC ] 11 ; rgb:RR/GG/BB ST` |
| `ESC ] 12 ; ? ST` | `ESC ] 12 ; rgb:RR/GG/BB ST` |
| `ESC ] 4 ; <index> ; ? ST` | `ESC ] 4 ; <index> ; rgb:RR/GG/BB ST` |

### Reset Colors

| Sequence | Action |
|----------|--------|
| `ESC ] 104 ST` | Reset all palette colors to defaults |
| `ESC ] 104 ; <index> ST` | Reset palette color `<index>` to default |

### Color Format

Colors can be specified as:

- `#RRGGBB` -- hex format (e.g. `#cdd6f4`)
- `rgb:RR/GG/BB` -- X11 format with hex components (e.g. `rgb:cd/d6/f4`)

## Usage

```bash
# Set background to Catppuccin Mocha base
printf '\e]11;#1e1e2e\a'

# Query current foreground
printf '\e]10;?\a'

# Set palette color 1 (red) to Catppuccin red
printf '\e]4;1;#f38ba8\a'

# Reset all palette colors
printf '\e]104\a'
```

## Security

- Set `allow_query: false` to prevent programs from reading your current color scheme. Some users consider color scheme fingerprinting a privacy concern.
- Set `allow_set: false` to prevent programs from changing your colors (useful if you want a locked-down palette).

## Source Files

| File | Description |
|------|-------------|
| `modules/dynamic_colors/gst-dyncolors-module.c` | Module implementation |
| `modules/dynamic_colors/gst-dyncolors-module.h` | Type macros and struct declaration |
