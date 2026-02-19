# Sync Update Module

Synchronized output via DEC private mode 2026 to eliminate flicker.

## Overview

The sync update module implements DEC private mode 2026 (synchronized updates). This protocol lets programs tell the terminal "I'm about to send a batch of updates -- hold off on rendering until I'm done." This prevents partial frames from being displayed, eliminating visual flicker during rapid screen updates.

Programs like Neovim, tmux, and modern TUI frameworks (notcurses, crossterm, ratatui) use this mode to ensure atomic screen redraws.

The module includes a safety timeout that forces a repaint if the application fails to close the batch (e.g. due to a crash), preventing the terminal from appearing frozen.

## Configuration

### YAML

```yaml
modules:
  sync_update:
    enabled: true
    timeout: 150
```

### C Config

```c
gst_config_set_module_config_bool(config, "sync_update", "enabled", TRUE);
gst_config_set_module_config_int(config, "sync_update", "timeout", 150);
```

### Options

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `enabled` | boolean | `true` | | Enable the module |
| `timeout` | integer | `150` | 10-5000 | Safety timeout in milliseconds |

## Protocol

### Begin Synchronized Update

```
ESC [ ? 2026 h
```

Sets DEC private mode 2026. The terminal suppresses rendering until the batch is closed.

### End Synchronized Update

```
ESC [ ? 2026 l
```

Resets DEC private mode 2026. The terminal immediately renders all accumulated changes.

## Usage

```bash
# Atomic screen update from the shell
printf '\e[?2026h'    # begin batch
# ... send screen updates ...
printf '\e[?2026l'    # end batch, render now
```

Most TUI frameworks handle this automatically when the terminal advertises mode 2026 support.

## Notes

- The module works by connecting to the `mode-changed` signal on `GstTerminal`, not through the hook interface system. No keyboard or mouse bindings are used.
- The timeout value should be high enough to allow normal application batches to complete, but low enough to recover quickly from a crashed application. The default of 150ms works well for most TUI programs.
- If the timeout fires, the terminal force-repaints and logs a debug message. This is a safety mechanism, not an error.

## Source Files

| File | Description |
|------|-------------|
| `modules/sync_update/gst-syncupdate-module.c` | Module implementation |
| `modules/sync_update/gst-syncupdate-module.h` | Type macros and struct declaration |
