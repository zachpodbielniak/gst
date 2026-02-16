# MCP Server Module

The MCP (Model Context Protocol) module embeds an MCP server inside the GST terminal emulator. It enables AI assistants to read terminal screen content, inspect running processes, detect URLs, manage configuration, and optionally inject input -- capabilities that are unavailable to shell-level MCP servers.

## Requirements

The MCP module requires additional dependencies beyond the base GST build:

```bash
# Fedora 43+
sudo dnf install libsoup3-devel libdex-devel json-glib-devel libpng-devel
```

The module also requires the `mcp-glib` submodule at `deps/mcp-glib`.

## Building

The MCP module is opt-in. Enable it with `MCP=1`:

```bash
MCP=1 make
```

This builds the `mcp-glib` library, the `mcp.so` module, and the `gst-mcp` relay binary. Without `MCP=1`, the module and relay are skipped entirely.

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
    transport: unix-socket   # "unix-socket", "http", or "stdio"
    socket_name: myproject   # custom name -> gst-mcp-myproject.sock
                             # omit for PID-based default
    port: 8808               # only used with transport: http
    host: "127.0.0.1"        # only used with transport: http

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

      # Screenshot capture
      screenshot: false          # captures terminal as base64 PNG image
```

### Safety Model

Every tool defaults to `false`. You must explicitly enable each capability you want to expose. Disabled tools are completely invisible to MCP clients -- they are not listed and cannot be called.

Destructive tools (`send_text`, `send_keys`, `set_config`) are marked with `destructive_hint: true` in the MCP protocol, so clients can warn users before invoking them.

The unix-socket transport uses filesystem permissions for access control. The HTTP transport binds to `127.0.0.1` (localhost only) by default.

## Transports

### Unix Socket (recommended)

The default transport. Creates a Unix domain socket at `$XDG_RUNTIME_DIR/gst-mcp-<PID>.sock` (or `gst-mcp-<NAME>.sock` with a custom name) and accepts connections from the `gst-mcp` relay binary. Each connection gets its own MCP server session, so multiple AI assistants can connect simultaneously.

```yaml
transport: unix-socket
```

#### Named sockets

By default, socket files use the PID, which changes every launch. For per-project setups where you want a stable socket name:

```bash
# Launch GST with a named socket
gst --mcp-socket myproject

# Connect gst-mcp to that specific socket
gst-mcp --name myproject
```

Or via YAML config:

```yaml
modules:
  mcp:
    socket_name: myproject   # creates gst-mcp-myproject.sock
```

This is useful when running multiple GST instances (one per project) and you want each `.mcp.json` to target a specific terminal:

```json
{
  "mcpServers": {
    "gst-terminal": {
      "type": "stdio",
      "command": "gst-mcp",
      "args": ["--name", "myproject"]
    }
  }
}
```

#### Socket discovery

The `gst-mcp` relay binary bridges stdin/stdout to the socket:

```bash
# Auto-discover the socket (picks newest gst-mcp-*.sock)
gst-mcp

# Connect by name (matches gst --mcp-socket NAME)
gst-mcp --name myproject

# Explicit socket path
gst-mcp --socket /run/user/1000/gst-mcp-12345.sock

# Via environment variable
GST_MCP_SOCKET=/run/user/1000/gst-mcp-12345.sock gst-mcp
```

**Discovery order for `gst-mcp`:**

1. `--socket PATH` or `--name NAME` command-line argument
2. `GST_MCP_SOCKET` environment variable
3. Scan `$XDG_RUNTIME_DIR/gst-mcp-*.sock` and pick the newest by mtime

**Socket name override order for GST:**

1. `--mcp-socket NAME` command-line flag (sets `$GST_MCP_SOCKET_NAME`)
2. `$GST_MCP_SOCKET_NAME` environment variable
3. `socket_name` in YAML config
4. Fall back to PID-based naming

### HTTP (optional)

Binds to `host:port` and accepts MCP JSON-RPC over HTTP POST with SSE for notifications. Useful for remote or web-based MCP clients.

```yaml
transport: http
port: 8808
host: "127.0.0.1"
```

Test with curl:

```bash
curl -s http://127.0.0.1:8808/ -X POST \
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

### Screenshot Capture

#### `screenshot`

Captures the current terminal display as a PNG image. Returns the image as base64-encoded data using the MCP image content type. Works with both X11 and Wayland backends.

No parameters.

Returns: Base64-encoded PNG image (`image/png` content type)

#### `save_screenshot`

Captures the current terminal display and writes it as a PNG file to the specified path.

| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | string | File path to write the PNG to |

Returns: `success`, `path`, `bytes`

## Architecture

```
Claude Code <-- stdio --> gst-mcp (relay) <-- Unix socket --> GST (mcp.so)
                          (NDJSON relay)                       GSocketService
                                                                -> McpServer per session
                                                                -> McpStdioTransport over socket streams
```

The MCP module is implemented as a standard GST module (`modules/mcp/`) with:

- `gst-mcp-module.c` -- Module lifecycle (configure, activate, deactivate), socket session management
- `gst-mcp-tools-screen.c` -- 5 screen reading tools
- `gst-mcp-tools-process.c` -- 4 process awareness tools
- `gst-mcp-tools-url.c` -- 1 URL detection tool
- `gst-mcp-tools-config.c` -- 4 config/module management tools
- `gst-mcp-tools-input.c` -- 2 input injection tools
- `gst-mcp-tools-window.c` -- 2 window management tools
- `gst-mcp-tools-screenshot.c` -- 2 screenshot capture tools (libpng)

The `gst-mcp` relay binary (`tools/gst-mcp/gst-mcp.c`) bridges stdin/stdout to the Unix socket using NDJSON line forwarding.

The module accesses terminal state through the `GstModuleManager` singleton. Tool handlers receive the `GstMcpModule` as user_data and check per-tool enable flags during registration. The `gst_mcp_module_setup_server()` function is shared across all transport paths to register tools on each McpServer instance.

## Cross-Module Dependencies

- **Scrollback**: `read_scrollback` and `search_scrollback` require the scrollback module to be loaded and active. If scrollback is not available, these tools return an error.
- **URL detection**: Implemented independently (no dependency on the urlclick module).
- **Process tools**: Use the `/proc` filesystem (Linux-specific).
