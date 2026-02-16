# MCP Server Module

The MCP (Model Context Protocol) module embeds an MCP server inside the GST terminal emulator. It enables AI assistants to read terminal screen content, inspect running processes, detect URLs, manage configuration, and optionally inject input -- capabilities that are unavailable to shell-level MCP servers.

## Requirements

The MCP module requires additional dependencies beyond the base GST build:

```bash
# Fedora 43+
sudo dnf install libsoup3-devel libdex-devel json-glib-devel
```

The module also requires the `mcp-glib` submodule at `deps/mcp-glib`.

## Building

The MCP module is opt-in. Enable it with `MCP=1`:

```bash
MCP=1 make
```

This builds the `mcp-glib` library and the `mcp.so` module. Without `MCP=1`, the module is skipped entirely.

To verify the build configuration:

```bash
MCP=1 make show-config
# Look for:
#   MCP:    1
#   MCP_AVAILABLE:1
```

## Configuration

Add the `mcp` section to your `~/.config/gst/config.yaml`:

```yaml
modules:
  mcp:
    enabled: true
    transport: http          # "http" or "stdio"
    port: 8808
    host: "127.0.0.1"

    # Per-tool enable/disable (all default to false for safety)
    tools:
      # Screen reading
      read_screen: true
      read_scrollback: true
      search_scrollback: true
      get_cursor_position: true
      get_cell_attributes: true

      # Process awareness
      get_foreground_process: true
      get_working_directory: true
      is_shell_idle: true
      get_pty_info: true

      # URL detection
      list_detected_urls: true

      # Config / module management
      get_config: true
      list_modules: true
      set_config: false          # runtime config changes
      toggle_module: false       # enable/disable other modules

      # Window management
      get_window_info: true
      set_window_title: true

      # Input injection (use with caution)
      send_text: false           # writes text to the PTY
      send_keys: false           # sends key sequences to the PTY
```

### Safety Model

Every tool defaults to `false`. You must explicitly enable each capability you want to expose. Disabled tools are completely invisible to MCP clients -- they are not listed and cannot be called.

Destructive tools (`send_text`, `send_keys`, `set_config`) are marked with `destructive_hint: true` in the MCP protocol, so clients can warn users before invoking them.

The HTTP transport binds to `127.0.0.1` (localhost only) by default.

## Transports

### HTTP (recommended)

The default transport. Binds to `host:port` and accepts MCP JSON-RPC over HTTP POST with SSE for notifications.

```yaml
transport: http
port: 8808
host: "127.0.0.1"
```

Test with curl:

```bash
curl -s http://127.0.0.1:8808/mcp -X POST \
  -H 'Content-Type: application/json' \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2025-03-26",
      "capabilities": {},
      "clientInfo": {"name": "test", "version": "1.0"}
    }
  }'
```

### stdio

Uses stdin/stdout for MCP JSON-RPC. The terminal still renders via X11/Wayland; stdin/stdout are separate from the PTY. Useful when GST is spawned by an MCP client as a subprocess.

```yaml
transport: stdio
```

## Tools Reference

### Screen Reading

#### `read_screen`

Reads the visible terminal screen content as text.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `include_attributes` | boolean | false | Include per-cell color and style info |

Returns: `rows`, `cols`, `lines[]` (text or objects with cell attributes)

#### `read_scrollback`

Reads lines from the scrollback history buffer. Requires the scrollback module to be active.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `offset` | integer | 0 | Line offset (0 = most recent) |
| `count` | integer | 100 | Number of lines (max 1000) |

Returns: `total_lines`, `offset`, `count`, `lines[]`

#### `search_scrollback`

Searches the scrollback buffer with a regex pattern.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pattern` | string | (required) | Regex search pattern |
| `max_results` | integer | 50 | Maximum matches (max 500) |

Returns: `pattern`, `total_lines`, `match_count`, `matches[]` with `line_index`, `text`, `match_start`, `match_end`

#### `get_cursor_position`

Returns the cursor position and metadata. No parameters.

Returns: `row`, `col`, `character`, `visible`, `shape` (block/underline/bar)

#### `get_cell_attributes`

Returns detailed glyph attributes at a specific position.

| Parameter | Type | Description |
|-----------|------|-------------|
| `row` | integer | Row (0-based) |
| `col` | integer | Column (0-based) |

Returns: `character`, `codepoint`, `fg`, `bg`, `bold`, `italic`, `underline`, `reverse`, `struck`, `invisible`, `wide`

### Process Awareness

#### `get_foreground_process`

Returns the foreground process in the terminal's PTY. No parameters.

Returns: `pid`, `command`, `cmdline`

#### `get_working_directory`

Returns the current working directory of the foreground process. No parameters.

Returns: `pid`, `path`

#### `is_shell_idle`

Checks whether the shell is at a prompt or a command is running. No parameters.

Returns: `idle` (boolean), `shell_pid`, `foreground_pid`

#### `get_pty_info`

Returns PTY information. No parameters.

Returns: `cols`, `rows`, `child_pid`, `running`

### URL Detection

#### `list_detected_urls`

Scans the visible terminal screen for URLs.

| Parameter | Type | Description |
|-----------|------|-------------|
| `regex` | string | Custom URL regex (overrides default) |

Returns: `count`, `urls[]` with `url`, `row`, `start_col`, `end_col`

### Configuration Management

#### `get_config`

Reads terminal configuration values.

| Parameter | Type | Description |
|-----------|------|-------------|
| `section` | string | Config section: terminal, window, backend |

Returns: Section-specific values (cols, rows, title, width, height, visible, backend type)

#### `set_config`

Modifies a configuration value at runtime. Only whitelisted keys allowed.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | string | Config key (window.title, window.opacity) |
| `value` | string | New value |

Allowed keys: `window.title`, `window.opacity`

#### `list_modules`

Lists all registered modules. No parameters.

Returns: `modules[]` with `name`, `description`, `active`

#### `toggle_module`

Enables or disables a module at runtime. Cannot toggle the MCP module itself.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | string | Module name |
| `enabled` | boolean | true to activate, false to deactivate |

Returns: `name`, `active`

### Window Management

#### `get_window_info`

Returns window information. No parameters.

Returns: `width`, `height`, `title`, `visible`, `backend` (x11/wayland)

#### `set_window_title`

Updates the terminal window title.

| Parameter | Type | Description |
|-----------|------|-------------|
| `title` | string | New window title |

Returns: `success`, `title`

### Input Injection

These tools are disabled by default and should be enabled with caution.

#### `send_text`

Writes text directly to the PTY. The text appears as if typed by the user.

| Parameter | Type | Description |
|-----------|------|-------------|
| `text` | string | Text to write to the PTY |

Returns: `success`, `bytes_written`

#### `send_keys`

Sends key sequences to the PTY.

| Parameter | Type | Description |
|-----------|------|-------------|
| `keys` | string | Space-separated key names |

Supported key names: `Enter`, `Return`, `Tab`, `Escape`, `Esc`, `Backspace`, `Space`, `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `PageUp`, `Page_Up`, `PageDown`, `Page_Down`, `Insert`, `Delete`, `Ctrl+<letter>` (e.g., `Ctrl+c`, `Ctrl+d`)

Returns: `success`, `keys_sent`

Example: `"Ctrl+c Enter"` sends Ctrl-C followed by Enter.

## Architecture

The MCP module is implemented as a standard GST module (`modules/mcp/`) with:

- `gst-mcp-module.c` -- Module lifecycle (configure, activate, deactivate)
- `gst-mcp-tools-screen.c` -- 5 screen reading tools
- `gst-mcp-tools-process.c` -- 4 process awareness tools
- `gst-mcp-tools-url.c` -- 1 URL detection tool
- `gst-mcp-tools-config.c` -- 4 config/module management tools
- `gst-mcp-tools-input.c` -- 2 input injection tools
- `gst-mcp-tools-window.c` -- 2 window management tools

The module accesses terminal state through the `GstModuleManager` singleton. Tool handlers receive the `GstMcpModule` as user_data and check per-tool enable flags during registration.

## Cross-Module Dependencies

- **Scrollback**: `read_scrollback` and `search_scrollback` require the scrollback module to be loaded and active. If scrollback is not available, these tools return an error.
- **URL detection**: Implemented independently (no dependency on the urlclick module).
- **Process tools**: Use the `/proc` filesystem (Linux-specific).
