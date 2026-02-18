<p align="center">
  <img src="data/logo.png" alt="GST Logo" width="192">
</p>

# GST - GObject Simple Terminal

A GObject-based terminal emulator with modular extensibility. GST is a reimplementation of [st (suckless terminal)](https://st.suckless.org/) using GObject for clean architecture and a plugin-based module system to replace st's patch-based customization.

## Features

- **GObject Architecture**: Clean separation between terminal emulation, rendering, and windowing
- **Module System**: GModule-based plugins with well-defined hook points (replacing st's patches)
- **Dual Configuration**: YAML files for simple config, compiled C for advanced customization
- **X11 and Wayland**: Both backends supported with runtime auto-detection
- **GObject Introspection**: Full GIR support for language bindings

## Building

### Dependencies

- glib-2.0, gobject-2.0, gio-2.0, gmodule-2.0
- x11, xft, fontconfig
- libyaml (yaml-0.1), json-glib-1.0
- gcc (gnu89)
- gobject-introspection (optional, for GIR generation)

On Fedora:
```bash
sudo dnf install gcc make \
    glib2-devel \
    gobject-introspection-devel \
    libX11-devel \
    libXft-devel \
    fontconfig-devel \
    libyaml-devel \
    json-glib-devel
```

### Build Commands

```bash
make              # Build all (lib, gst, modules)
make lib          # Build static and shared libraries
make gst          # Build the gst executable
make gir          # Generate GIR/typelib
make modules      # Build all modules
make test         # Run the test suite
make install      # Install to PREFIX
make DEBUG=1      # Build with debug symbols
```

## Usage

```bash
gst                          # Start with default shell
gst -e htop                  # Run htop
gst -g 120x40                # Start with 120 columns, 40 rows
gst -f 'JetBrains Mono:14'   # Use specific font
gst -c ~/.config/gst/my.yaml # Use custom config
gst --x11                    # Force X11 backend
gst --wayland                # Force Wayland backend
```

See [docs/cli.md](docs/cli.md) for the complete CLI reference.

## Configuration

Generate a default config file:

```bash
gst --generate-yaml-config > ~/.config/gst/config.yaml
```

Configuration files are searched in this order:
1. `--config PATH` (command line override)
2. `~/.config/gst/config.yaml`
3. `/etc/gst/config.yaml`
4. `/usr/share/gst/config.yaml`

GST also supports a C configuration file for advanced customization (compiled automatically at startup). See [docs/c-config.md](docs/c-config.md) for details.

See [docs/configuration.md](docs/configuration.md) for the complete configuration reference.

## Modules

Modules are loaded from:
1. `$GST_MODULE_PATH` (environment variable, colon-separated)
2. `~/.config/gst/modules/`
3. `/etc/gst/modules/`
4. `/usr/share/gst/modules/`

### Built-in Modules

| Module | Description | Default |
|--------|-------------|---------|
| [scrollback](docs/modules/scrollback.md) | Ring buffer scrollback with keyboard/mouse navigation | Enabled |
| [transparency](docs/modules/transparency.md) | Window opacity with focus tracking | Disabled |
| [urlclick](docs/modules/urlclick.md) | URL detection and opening | Enabled |
| [externalpipe](docs/modules/externalpipe.md) | Pipe screen content to external commands | Disabled |
| [boxdraw](docs/modules/boxdraw.md) | Pixel-perfect box-drawing characters | Enabled |
| [visualbell](docs/modules/visualbell.md) | Visual flash on terminal bell | Disabled |
| [undercurl](docs/modules/undercurl.md) | Curly underline rendering | Enabled |
| [clipboard](docs/modules/clipboard.md) | Auto-sync selection to system clipboard | Enabled |
| [font2](docs/modules/font2.md) | Fallback font preloading | Disabled |
| [keyboard_select](docs/modules/keyboard-select.md) | Vim-like keyboard text selection | Disabled |
| [kittygfx](docs/modules/kittygfx.md) | Kitty graphics protocol (inline images) | Disabled |
| [mcp](docs/modules/mcp.md) | MCP server for AI assistant integration | Disabled |

See [docs/modules/README.md](docs/modules/README.md) for the complete module guide.

### Writing Modules

See [docs/modules/module-system.md](docs/modules/module-system.md) for the module development guide.

## Architecture

```
gst/
├── src/
│   ├── core/           # Terminal emulation
│   ├── rendering/      # Abstract renderer + X11 implementation
│   ├── window/         # Abstract window + X11 implementation
│   ├── config/         # YAML configuration
│   ├── module/         # Module system
│   ├── selection/      # Selection and clipboard
│   ├── boxed/          # GBoxed types (glyph, cursor)
│   ├── interfaces/     # Module hook interfaces
│   └── util/           # UTF-8, base64 utilities
├── modules/            # Built-in modules
├── tests/              # GTest unit tests
└── data/               # Default config and color schemes
```

## Documentation

- [Quick Start Guide](docs/quickstart.md) - Get up and running
- [CLI Reference](docs/cli.md) - All command-line options
- [Configuration Reference](docs/configuration.md) - Every config option explained
- [Keybindings](docs/keybindings.md) - Keyboard and mouse bindings
- [Color Schemes](docs/colors.md) - Theming and palette customization
- [C Configuration](docs/c-config.md) - Advanced config via compiled C
- [Module Guide](docs/modules/README.md) - All built-in modules
- [Module Development](docs/modules/module-system.md) - Writing custom modules
- [MCP Server](docs/modules/mcp.md) - AI assistant integration
- [Known Issues](docs/known-issues.md) - Open issues and workarounds

## License

AGPLv3 - See [LICENSE](LICENSE) for details.

## Credits

Based on [st (suckless terminal)](https://st.suckless.org/) by suckless.org.
