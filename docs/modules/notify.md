# Notify Module

Desktop notifications from terminal applications via OSC escape sequences.

## Overview

The notify module lets programs running in the terminal send desktop notifications using standard OSC escape sequences. It supports three common notification protocols: iTerm2 (OSC 9), rxvt-unicode (OSC 777), and kitty (OSC 99). Notifications are dispatched via `notify-send`, which works with any freedesktop.org-compliant notification daemon.

When `suppress_focused` is enabled (the default), notifications are silently dropped while the terminal window has focus -- they only appear when the terminal is in the background.

## Configuration

### YAML

```yaml
modules:
  notify:
    enabled: true
    show_title: true
    suppress_focused: true
    timeout: -1
    urgency: normal
```

### C Config

```c
gst_config_set_module_config_bool(config, "notify", "enabled", TRUE);
gst_config_set_module_config_bool(config, "notify", "show_title", TRUE);
gst_config_set_module_config_bool(config, "notify", "suppress_focused", TRUE);
gst_config_set_module_config_int(config, "notify", "timeout", -1);
gst_config_set_module_config_string(config, "notify", "urgency", "normal");
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `show_title` | boolean | `true` | Prepend the terminal window title to notification body |
| `suppress_focused` | boolean | `true` | Suppress notifications when the terminal has focus |
| `timeout` | integer | `-1` | Notification timeout in seconds (`-1` = system default) |
| `urgency` | string | `"normal"` | Notification urgency: `"low"`, `"normal"`, or `"critical"` |

## Escape Sequences

The module handles three notification protocols:

### OSC 9 (iTerm2)

```
ESC ] 9 ; <message> ST
```

Sends a notification with `<message>` as the body. No separate title field -- uses the terminal window title if `show_title` is true.

### OSC 777 (rxvt-unicode)

```
ESC ] 777 ; notify ; <title> ; <body> ST
```

Sends a notification with an explicit `<title>` and `<body>`.

### OSC 99 (kitty)

```
ESC ] 99 ; <key=value pairs> ; <body> ST
```

Kitty-style notification with metadata key-value pairs. The body is the final semicolon-separated field.

## Usage

Programs can send notifications from the shell:

```bash
# iTerm2 style (simplest)
printf '\e]9;Build complete\a'

# rxvt-unicode style (with title)
printf '\e]777;notify;Build Status;Compilation finished\a'
```

## Notes

- Requires `notify-send` to be installed (part of `libnotify` / `libnotify-tools`).
- The `suppress_focused` check uses the `WIN_MODE_FOCUSED` flag, which is updated on window focus/unfocus events.
- The `timeout` value is converted to milliseconds when passed to `notify-send` via the `-t` flag. A value of `-1` omits the flag entirely, using the notification daemon's default.

## Source Files

| File | Description |
|------|-------------|
| `modules/notify/gst-notify-module.c` | Module implementation |
| `modules/notify/gst-notify-module.h` | Type macros and struct declaration |
