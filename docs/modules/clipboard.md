# Clipboard Module

Automatic clipboard synchronization on text selection.

## Overview

The clipboard module mirrors the behavior of the st-clipboard patch: when you select text with the mouse, it is automatically copied to both the X11 PRIMARY selection and the system CLIPBOARD. This means you can paste your selection with either middle-click (PRIMARY) or `Ctrl+V` / `Ctrl+Shift+v` (CLIPBOARD) without an extra copy step.

Without this module, selected text is only available via PRIMARY selection (middle-click paste), which is the default X11 behavior.

## Configuration

### YAML

```yaml
modules:
  clipboard:
    enabled: true
```

### C Config

```c
gst_config_set_module_config_bool(config, "clipboard", "enabled", TRUE);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable the module |

## Usage

The module works automatically. Select text with the mouse and it is immediately available in both PRIMARY and CLIPBOARD:

- **Middle-click** anywhere to paste (PRIMARY selection -- standard X11 behavior)
- **Ctrl+Shift+v** to paste in GST (CLIPBOARD -- added by this module)
- **Ctrl+V** to paste in other applications (CLIPBOARD)

## Notes

- Works with both X11 and Wayland backends via the abstract `GstWindow` API
- Listens for the left mouse button release (`button-release` signal on the window) to trigger the clipboard sync
- Does not add any keybindings -- clipboard copy/paste keybindings are part of the [core keybinding system](../keybindings.md)
