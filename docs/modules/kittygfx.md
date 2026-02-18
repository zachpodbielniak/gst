# Kitty Graphics Module

Inline image display using the Kitty graphics protocol.

## Overview

The kittygfx module implements the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/), allowing programs to display images directly in the terminal. This enables image previews in file managers (e.g. yazi, ranger), inline plots in data analysis tools, and other graphical content within the terminal.

The module handles APC (Application Program Command) escape sequences that carry image data, manages an image cache with configurable memory limits, and renders images as overlays on the terminal surface.

## Configuration

### YAML

```yaml
modules:
  kittygfx:
    enabled: true
    max_total_ram_mb: 256
    max_single_image_mb: 64
    max_placements: 4096
    allow_file_transfer: false
    allow_shm_transfer: false
```

### C Config

```c
gst_config_set_module_config_bool(config, "kittygfx", "enabled", TRUE);
gst_config_set_module_config_int(config, "kittygfx", "max_total_ram_mb", 256);
gst_config_set_module_config_int(config, "kittygfx", "max_single_image_mb", 64);
gst_config_set_module_config_int(config, "kittygfx", "max_placements", 4096);
gst_config_set_module_config_bool(config, "kittygfx", "allow_file_transfer", FALSE);
gst_config_set_module_config_bool(config, "kittygfx", "allow_shm_transfer", FALSE);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `max_total_ram_mb` | integer | `256` | Total memory limit for all cached images (MB) |
| `max_single_image_mb` | integer | `64` | Maximum size of a single image (MB) |
| `max_placements` | integer | `4096` | Maximum number of active image placements |
| `allow_file_transfer` | boolean | `false` | Allow loading images via `file://` protocol |
| `allow_shm_transfer` | boolean | `false` | Allow loading images via shared memory |

## Usage

Once enabled, programs that support the Kitty graphics protocol can display images automatically. No keybindings are needed.

### Supported Programs

- **yazi** - File manager with image previews
- **ranger** - File manager (with kitty backend)
- **timg** - Terminal image viewer
- **matplotlib** - Python plotting (with kitty backend)
- **chafa** - Image-to-terminal converter (with kitty protocol support)
- Any program that implements the Kitty graphics protocol

### Transfer Methods

The module supports multiple ways to receive image data:

| Method | Config Flag | Description |
|--------|-------------|-------------|
| Direct | (always on) | Base64-encoded image data in escape sequences |
| File | `allow_file_transfer` | Load image from a local file path |
| Shared Memory | `allow_shm_transfer` | Load image via POSIX shared memory |

Direct transfer is always available. File and shared memory transfers must be explicitly enabled since they allow the terminal to access the filesystem.

## Security

- **File transfers** are disabled by default because they allow programs to make the terminal read arbitrary files from disk
- **Shared memory transfers** are disabled by default for similar security reasons
- The memory limits prevent a malicious program from consuming excessive RAM
- The placement limit prevents excessive rendering overhead

## Known Issues

There is a known issue with yazi where the 'd' (delete) key may auto-trigger on startup. See [Known Issues](../known-issues.md) for details and workarounds.

## Notes

- Images are decoded using the embedded stb_image library (supports PNG, JPEG, GIF, BMP, and more)
- The image cache uses an LRU-style eviction policy when memory limits are reached
- Images are rendered as overlays during each render cycle
- The module intercepts APC escape sequences (`ESC _ G ... ESC \`)
