# GST CLI Reference

## Usage

```
gst [options] [-e command [args]]
```

## Options

### Basic

| Flag | Argument | Description |
|------|----------|-------------|
| `-h, --help` | | Show help message and exit |
| `-v, --version` | | Show version and exit |
| `--license` | | Show AGPLv3 license text and exit |
| `-e, --exec` | `CMD` | Execute command instead of the default shell |
| `-l, --line` | | Read from stdin |

### Window

| Flag | Argument | Description |
|------|----------|-------------|
| `-t, --title` | `TITLE` | Set the window title |
| `-g, --geometry` | `COLSxROWS` | Set window size (e.g. `120x40`) |
| `-f, --font` | `FONT` | Set font (fontconfig format, e.g. `"JetBrains Mono:pixelsize=14"`) |
| `-n, --name` | `NAME` | Set the X11 window name (WM_CLASS) |
| `-w, --windid` | `ID` | Embed terminal in an existing window by X11 window ID |

### Backend

| Flag | Description |
|------|-------------|
| `--x11` | Force X11 backend (ignore `$WAYLAND_DISPLAY`) |
| `--wayland` | Force Wayland backend |

Without these flags, GST auto-detects: if `$WAYLAND_DISPLAY` is set, Wayland is used; otherwise X11.

### Configuration

| Flag | Argument | Description |
|------|----------|-------------|
| `-c, --config` | `PATH` | Use a specific YAML config file |
| `--no-yaml-config` | | Skip YAML config loading entirely (use built-in defaults) |
| `--c-config` | `PATH` | Use a specific C config file |
| `--no-c-config` | | Skip C config compilation and loading |
| `--recompile` | | Compile the C config file and exit (do not start terminal) |

### Config Generation

| Flag | Argument | Description |
|------|----------|-------------|
| `--generate-yaml-config` | | Print the default YAML config to stdout and exit |
| `--generate-c-config` | | Print the default C config template to stdout and exit |
| `--list-modules` | | List all built-in modules with descriptions and exit |
| `--modules` | `MOD1,MOD2` | Select which modules to include when generating config |

The `--modules` flag works with both `--generate-yaml-config` and `--generate-c-config`. It accepts comma or colon-separated module names.

### MCP

| Flag | Argument | Description |
|------|----------|-------------|
| `--mcp-socket` | `NAME` | Set a named MCP socket (creates `gst-mcp-NAME.sock`) |

See [MCP Module](modules/mcp.md) for details.

## Examples

```bash
# Start with default shell
gst

# Run a command
gst -e htop

# Custom geometry and font
gst -g 120x40 -f "JetBrains Mono:pixelsize=16"

# Use a specific config file
gst -c ~/my-gst-config.yaml

# Force X11 backend
gst --x11

# Generate config with selected modules
gst --generate-yaml-config --modules scrollback,transparency,urlclick > ~/.config/gst/config.yaml

# Generate C config template
gst --generate-c-config > ~/.config/gst/config.c

# Recompile C config without starting
gst --recompile

# Start with no config files (pure defaults)
gst --no-yaml-config --no-c-config

# List available modules
gst --list-modules

# Start with named MCP socket
gst --mcp-socket myproject
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `GST_MODULE_PATH` | Colon-separated list of directories to search for module `.so` files |
| `GST_MCP_SOCKET_NAME` | Override the MCP socket name (same as `--mcp-socket`) |
| `WAYLAND_DISPLAY` | If set, GST auto-selects the Wayland backend |
| `SHELL` | Used as the default shell if `terminal.shell` is not configured |
| `XDG_CONFIG_HOME` | Base directory for user config (default: `~/.config`) |
| `XDG_RUNTIME_DIR` | Directory for MCP unix sockets |

## Config Precedence

Configuration values are resolved in this order (later overrides earlier):

1. **Built-in defaults** (hardcoded in source)
2. **YAML config file** (see search path below)
3. **C config file** (compiled and loaded after YAML)
4. **CLI flags** (`-t`, `-g`, `-f` override config values)

### YAML Config Search Path

1. `--config PATH` (explicit override)
2. `~/.config/gst/config.yaml`
3. `/etc/gst/config.yaml`
4. `/usr/share/gst/config.yaml`

### C Config Search Path

1. `--c-config PATH` (explicit override)
2. `~/.config/gst/config.c`
3. `/etc/gst/config.c`
4. `/usr/share/gst/config.c`

### Module Search Path

1. `$GST_MODULE_PATH` (colon-separated directories)
2. `~/.config/gst/modules/`
3. `/etc/gst/modules/`
4. `/usr/share/gst/modules/`
