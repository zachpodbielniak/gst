# OSC 52 Module

Remote clipboard access via the OSC 52 escape sequence.

## Overview

The OSC 52 module enables programs to read and write the system clipboard through escape sequences. This is essential for clipboard integration over SSH sessions, where the remote process has no direct access to the local X11/Wayland selection.

Programs like tmux, Neovim (with OSC 52 clipboard provider), and mosh use this protocol to synchronize clipboard content between the remote session and the local terminal.

By default, clipboard writes are allowed but reads are disabled for security.

## Configuration

### YAML

```yaml
modules:
  osc52:
    enabled: true
    allow_read: false
    allow_write: true
    max_bytes: 100000
```

### C Config

```c
gst_config_set_module_config_bool(config, "osc52", "enabled", TRUE);
gst_config_set_module_config_bool(config, "osc52", "allow_read", FALSE);
gst_config_set_module_config_bool(config, "osc52", "allow_write", TRUE);
gst_config_set_module_config_int(config, "osc52", "max_bytes", 100000);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `allow_read` | boolean | `false` | Allow programs to read clipboard contents |
| `allow_write` | boolean | `true` | Allow programs to set clipboard contents |
| `max_bytes` | integer | `100000` | Maximum decoded payload size in bytes |

## Escape Sequences

### Write to Clipboard

```
ESC ] 52 ; <target> ; <base64-data> ST
```

`<target>` is one or more selection characters:

| Character | Selection |
|-----------|-----------|
| `c` | CLIPBOARD |
| `p` | PRIMARY |
| `s` | Secondary |
| `0`-`7` | Cut buffers (mapped to PRIMARY) |

The `<base64-data>` is standard base64-encoded text.

### Read from Clipboard

```
ESC ] 52 ; <target> ; ? ST
```

If `allow_read` is true, the terminal responds with:

```
ESC ] 52 ; <target> ; <base64-encoded-contents> ST
```

If `allow_read` is false (default), the query is silently ignored.

## Usage

```bash
# Copy text to clipboard
echo -n "hello" | base64 | xargs -I{} printf '\e]52;c;{}\a'

# Simpler with a helper function
osc52_copy() {
    printf '\e]52;c;%s\a' "$(printf '%s' "$1" | base64)"
}
osc52_copy "text to copy"
```

### Neovim Integration

In your `init.lua`:

```lua
vim.g.clipboard = {
    name = 'OSC 52',
    copy = {
        ['+'] = require('vim.ui.clipboard.osc52').copy('+'),
        ['*'] = require('vim.ui.clipboard.osc52').copy('*'),
    },
    paste = {
        ['+'] = require('vim.ui.clipboard.osc52').paste('+'),
        ['*'] = require('vim.ui.clipboard.osc52').paste('*'),
    },
}
```

### tmux Integration

In your `.tmux.conf`:

```
set -g set-clipboard on
```

This tells tmux to forward OSC 52 sequences from programs running inside tmux to the outer terminal (GST). Without this, tmux intercepts OSC 52 and the sequences never reach GST.

## Security

- **`allow_read` is disabled by default.** Enabling it allows any program in the terminal to silently read your clipboard contents. Only enable this if you trust all programs running in the terminal.
- **`max_bytes` limits payload size** to prevent memory exhaustion from maliciously large base64 payloads.
- Write access (`allow_write`) is generally safe -- programs can only set the clipboard, not exfiltrate data from it.

## Notes

- The module decodes base64 using `g_base64_decode_inplace()` from GLib.
- Both CLIPBOARD and PRIMARY selections are supported. The target character determines which selection is modified.
- When used over SSH, the escape sequence travels through the SSH tunnel and is processed by the local terminal emulator (GST), giving the remote program access to the local clipboard.

## Source Files

| File | Description |
|------|-------------|
| `modules/osc52/gst-osc52-module.c` | Module implementation |
| `modules/osc52/gst-osc52-module.h` | Type macros and struct declaration |
