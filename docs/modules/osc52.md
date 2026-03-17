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
    max_bytes: 786432
```

### C Config

```c
gst_config_set_module_config_bool(config, "osc52", "enabled", TRUE);
gst_config_set_module_config_bool(config, "osc52", "allow_read", FALSE);
gst_config_set_module_config_bool(config, "osc52", "allow_write", TRUE);
gst_config_set_module_config_int(config, "osc52", "max_bytes", 786432);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `allow_read` | boolean | `false` | Allow programs to read clipboard contents |
| `allow_write` | boolean | `true` | Allow programs to set clipboard contents |
| `max_bytes` | integer | `786432` | Maximum decoded payload size in bytes (~768KB) |

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

`set-clipboard on` tells tmux to forward OSC 52 sequences to the outer terminal. Without this, tmux intercepts OSC 52 and the sequences never reach GST.

GST's default `TERM` is `gst-256color`, which includes the `Ms` terminfo capability that tmux requires for OSC 52 forwarding. Make sure the terminfo entry is installed:

```bash
make install-terminfo
```

**Older setups using `st-256color`**: If your `TERM` is still `st-256color` (which lacks the `Ms` capability), either switch to `gst-256color` in your GST config or add this override to `.tmux.conf`:

```
set -ga terminal-overrides ',st-256color:Ms=\E]52;%p1%s;%p2%s\007'
```

## Security

- **`allow_read` is disabled by default.** Enabling it allows any program in the terminal to silently read your clipboard contents. Only enable this if you trust all programs running in the terminal.
- **`max_bytes` limits payload size** to prevent memory exhaustion from maliciously large base64 payloads.
- Write access (`allow_write`) is generally safe -- programs can only set the clipboard, not exfiltrate data from it.

## Dependencies

The module uses external clipboard tools to reliably set the system clipboard:

- **Wayland**: `wl-copy` (from `wl-clipboard` package)
- **X11**: `xclip`

These tools bypass the Wayland input serial requirement for `wl_data_device.set_selection`, which silently fails for programmatic clipboard sets (e.g., OSC 52 arriving over SSH). If neither tool is available, the module falls back to the window's built-in selection API.

**Important:** The module intentionally does not also claim clipboard ownership via the internal window API when an external tool succeeds. Doing so would make GST hold a `wl_data_source` with a cached copy of the OSC 52 content. If you then copied from another app, Wayland would send a `cancelled` event asynchronously — but if you pasted before that event was processed, GST would return the stale OSC 52 content instead of the new clipboard. The external tool holds the clipboard; GST reads it back through the normal `data_offer` path.

On Fedora: `sudo dnf install wl-clipboard xclip`

## Notes

- The module decodes base64 using `g_base64_decode_inplace()` from GLib.
- Both CLIPBOARD and PRIMARY selections are supported. The target character determines which selection is modified.
- When used over SSH, the escape sequence travels through the SSH tunnel and is processed by the local terminal emulator (GST), giving the remote program access to the local clipboard.
- On Wayland, the module pipes clipboard text to `wl-copy` (or `wl-copy --primary`) to avoid stale serial issues. On X11, it uses `xclip -selection clipboard`.

## DCS tmux Passthrough

The module also handles DCS tmux passthrough sequences. When tmux is configured with `allow-passthrough on`, programs inside tmux can send escape sequences through tmux to the outer terminal by wrapping them in DCS:

```
ESC P tmux; ESC <doubled-inner-sequence> ESC \
```

ESC bytes in the inner sequence are doubled (`\e\e` becomes `\e`). The module detects the `tmux;` prefix, un-doubles ESC bytes, extracts the inner OSC 52, and processes it normally.

## Remote tmux Troubleshooting

If clipboard doesn't work when inside tmux on a remote host over SSH:

1. **tmux `set-clipboard` must be `on`** (not `external` or `off`):
   ```
   # In ~/.tmux.conf on the REMOTE host
   set -g set-clipboard on
   ```
   Note: tmux 3.3+ changed the default from `on` to `external`.

2. **gst-256color terminfo should be installed on the remote host** so tmux sees the `Ms` capability:
   ```bash
   # On the remote host
   tic -sx /path/to/gst-256color.terminfo
   ```
   Or copy from local: `infocmp gst-256color | ssh remote 'tic -sx -'`

3. **tmux `terminal-features` must include `paste` for `gst-256color`**. tmux auto-grants features like bracketed paste to terminals matching `xterm*`, but `gst-256color` doesn't match. Without this, Ctrl+Shift+V paste into remote tmux silently fails. Add to `~/.tmux.conf` on the **remote** host:
   ```
   set -as terminal-features 'gst-256color:256:RGB:clipboard:ccolour:cstyle:focus:mouse:overline:paste:strikethrough:usstyle:extkeys:osc7'
   ```
   Alternatively, install the terminfo with extended capabilities (`tic -sx`) which includes `BE`/`BD` entries that tmux reads directly.

4. **`wl-copy` (Wayland) or `xclip` (X11) must be installed locally** on the machine running GST.

5. **Diagnostic logging** — run GST with debug output to trace the OSC 52 flow:
   ```bash
   G_MESSAGES_DEBUG=all ./build/debug/gst 2>gst-debug.log
   ```
   Then reproduce the issue and check `gst-debug.log` for lines containing `osc52:`, `term_strhandle:`, and `dispatch_escape_string:`.

6. **Test OSC 52 locally first** to confirm the module works:
   ```bash
   printf '\033]52;c;SGVsbG8=\033\\'
   # Then paste with Ctrl+Shift+V — should get "Hello"
   ```

## Source Files

| File | Description |
|------|-------------|
| `modules/osc52/gst-osc52-module.c` | Module implementation |
| `modules/osc52/gst-osc52-module.h` | Type macros and struct declaration |
