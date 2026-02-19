# Sixel Module

DEC Sixel graphics protocol for inline terminal images.

## Overview

The sixel module implements the DEC Sixel graphics protocol, enabling programs to display bitmap images directly in the terminal. Sixel is a legacy protocol from DEC terminals that has seen renewed adoption because, unlike the Kitty graphics protocol, it passes through tmux and works over SSH without modification.

The module includes a full sixel parser state machine, a configurable color palette, and a placement manager with memory budgeting. Images are rendered as overlays on the terminal surface.

## Configuration

### YAML

```yaml
modules:
  sixel:
    enabled: true
    max_width: 4096
    max_height: 4096
    max_colors: 1024
    max_total_ram_mb: 128
    max_placements: 256
```

### C Config

```c
gst_config_set_module_config_bool(config, "sixel", "enabled", TRUE);
gst_config_set_module_config_int(config, "sixel", "max_width", 4096);
gst_config_set_module_config_int(config, "sixel", "max_height", 4096);
gst_config_set_module_config_int(config, "sixel", "max_colors", 1024);
gst_config_set_module_config_int(config, "sixel", "max_total_ram_mb", 128);
gst_config_set_module_config_int(config, "sixel", "max_placements", 256);
```

### Options

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `enabled` | boolean | `false` | | Enable the module |
| `max_width` | integer | `4096` | 1-65536 | Maximum image width in pixels |
| `max_height` | integer | `4096` | 1-65536 | Maximum image height in pixels |
| `max_colors` | integer | `1024` | 2-65536 | Maximum palette colors per image |
| `max_total_ram_mb` | integer | `128` | 1-4096 | Total memory budget for all stored images (MB) |
| `max_placements` | integer | `256` | 1-65536 | Maximum number of active image placements |

## Dependencies

No extra packages are required. The sixel decoder is implemented in pure C using only GLib. It parses the sixel data format directly and renders via the abstract render context's `draw_image` vtable (X11 XRender or Cairo, depending on backend).

## Protocol

Sixel data arrives as a DCS (Device Control String) escape sequence:

```
DCS <P1> ; <P2> ; <P3> q <sixel-data> ST
```

Where:
- `P1` = pixel aspect ratio (ignored by most modern terminals)
- `P2` = background mode (0 = device default, 1 = no change, 2 = set to color 0)
- `P3` = horizontal grid size (ignored)

### Sixel Commands

| Command | Syntax | Description |
|---------|--------|-------------|
| Data | `?` through `~` (0x3F-0x7E) | Six vertical pixels, value = byte - 0x3F |
| Color | `# <Pc> ; <Pu> ; <Px> ; <Py> ; <Pz>` | Define color: Pc=index, Pu=1 for HLS / 2 for RGB |
| Color select | `# <Pc>` | Select color Pc for subsequent data |
| Repeat | `! <count> <data>` | Repeat next data byte `count` times |
| CR | `$` | Carriage return (back to column 0, same sixel row) |
| NL | `-` | Next sixel row (advance 6 pixels down) |

### Color Formats

| Pu | Format | Component ranges |
|----|--------|-----------------|
| 1 | HLS | H: 0-360, L: 0-100, S: 0-100 |
| 2 | RGB | R: 0-100, G: 0-100, B: 0-100 (percentage) |

## Usage

### Supported Programs

- **chafa** -- image-to-terminal converter with sixel support
- **libsixel** tools (`img2sixel`) -- dedicated sixel encoder
- **yazi** -- file manager (configure `image_filter = "sixel"`)
- **timg** -- terminal image viewer
- **matplotlib** -- Python plotting (sixel backend)
- **gnuplot** -- `set terminal sixelgd`

### Quick Test

```bash
# Using chafa (install: dnf install chafa)
chafa --format=sixel image.png

# Using img2sixel (install: dnf install libsixel-utils)
img2sixel image.png
```

## tmux Compatibility

Sixel graphics work inside tmux (3.4+) with passthrough enabled:

```
set -g allow-passthrough on
```

This is a key advantage over the Kitty graphics protocol, which tmux does not support. Programs like yazi can be configured to use sixel as a fallback inside tmux sessions.

## Notes

- Each sixel band is 6 pixels tall. Images are built up band-by-band from top to bottom.
- Placements are stored in a hash table keyed by terminal cell position. When the terminal scrolls, placement positions are adjusted.
- Images that exceed `max_width` or `max_height` are clipped. New images that would exceed `max_total_ram_mb` are rejected.
- The decoder handles malformed sixel data gracefully -- unknown bytes are skipped without crashing.

## Source Files

| File | Description |
|------|-------------|
| `modules/sixel/gst-sixel-module.c` | Module implementation: DCS parser, sixel decoder, image store, render overlay |
| `modules/sixel/gst-sixel-module.h` | Type macros and struct declaration |
