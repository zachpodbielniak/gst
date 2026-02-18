# Visual Bell Module

Visual flash notification on terminal bell events.

## Overview

The visualbell module provides a visual indicator when the terminal bell character (ASCII 0x07, `\a`) is received. Instead of (or in addition to) an audible beep, the terminal briefly flashes to notify you.

## Configuration

### YAML

```yaml
modules:
  visualbell:
    enabled: true
    duration: 100
```

### C Config

```c
gst_config_set_module_config_bool(config, "visualbell", "enabled", TRUE);
gst_config_set_module_config_int(config, "visualbell", "duration", 100);
```

### Options

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `enabled` | boolean | `false` | | Enable the module |
| `duration` | integer | `100` | 10-5000 | Flash duration in milliseconds |

## Usage

Enable the module and the terminal will flash whenever a bell character is received. Common triggers:

- Tab completion with no matches (bash)
- `echo -e '\a'`
- Programs that signal errors via bell

## Notes

- The flash duration controls how long the visual indicator is displayed
- This module implements the `GstBellHandler` interface
