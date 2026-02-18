# Transparency Module

Window opacity control with focus-aware adjustment.

## Overview

The transparency module sets the window opacity using the `_NET_WM_WINDOW_OPACITY` hint (X11) or the equivalent Wayland mechanism. You can configure different opacity levels for when the window is focused versus unfocused, allowing the terminal to become more transparent when you switch to another window.

## Configuration

### YAML

```yaml
modules:
  transparency:
    enabled: true
    opacity: 0.9
    focus_opacity: 0.95
    unfocus_opacity: 0.8
```

If you only need a single opacity level (no focus tracking), just set `opacity`:

```yaml
modules:
  transparency:
    enabled: true
    opacity: 0.85
```

### C Config

```c
gst_config_set_module_config_bool(config, "transparency", "enabled", TRUE);
gst_config_set_module_config_double(config, "transparency", "opacity", 0.9);
gst_config_set_module_config_double(config, "transparency", "focus_opacity", 0.95);
gst_config_set_module_config_double(config, "transparency", "unfocus_opacity", 0.8);
```

### Options

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `enabled` | boolean | `false` | | Enable the module |
| `opacity` | double | `0.9` | 0.0-1.0 | Static opacity (used when focus/unfocus are equal) |
| `focus_opacity` | double | `0.9` | 0.0-1.0 | Opacity when the window has focus |
| `unfocus_opacity` | double | `0.9` | 0.0-1.0 | Opacity when the window loses focus |

## Usage

- `1.0` = fully opaque, `0.0` = fully transparent
- If `focus_opacity` and `unfocus_opacity` are the same, focus tracking is skipped for efficiency
- The `opacity` value is used as the initial opacity; `focus_opacity`/`unfocus_opacity` take over once focus events are received

## Requirements

- **X11**: Requires a compositor that supports `_NET_WM_WINDOW_OPACITY` (e.g. picom, compton, or a compositing window manager)
- **Wayland**: Compositor support varies; works with compositors that respect client opacity hints
