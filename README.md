<p align="center">
  <img src="data/logo.png" alt="GST Logo" width="192">
</p>

# GST - GObject Simple Terminal

A GObject-based terminal emulator with modular extensibility. GST is a reimplementation of [st (suckless terminal)](https://st.suckless.org/) using GObject for clean architecture and a plugin-based module system to replace st's patch-based customization.

## Features

- **GObject Architecture**: Clean separation between terminal emulation, rendering, and windowing
- **Module System**: GModule-based plugins with well-defined hook points (replacing st's patches)
- **YAML Configuration**: Runtime-configurable via YAML files
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

```
gst [options] [-e command [args]]

Options:
  -c, --config PATH     Use specified config file
  -t, --title TITLE     Window title
  -g, --geometry WxH    Window geometry (cols x rows)
  -f, --font FONT       Font specification
  -n, --name NAME       Window name
  -w, --windid ID       Embed in window ID
  -e, --exec CMD        Execute command instead of shell
  -l, --line            Read from stdin
  -v, --version         Show version
  -h, --help            Show help
  --license             Show license (AGPLv3)
```

### Examples

```bash
gst                          # Start with default shell
gst -e htop                  # Run htop
gst -g 120x40                # Start with 120 columns, 40 rows
gst -f 'JetBrains Mono:14'   # Use specific font
gst -c ~/.config/gst/my.yaml # Use custom config
```

## Configuration

Configuration files are searched in this order:
1. `--config PATH` (command line override)
2. `~/.config/gst/config.yaml`
3. `/etc/gst/config.yaml`
4. `/usr/share/gst/config.yaml`

See `data/default-config.yaml` for all available options.

## Modules

Modules are loaded from:
1. `$GST_MODULE_PATH` (environment variable, colon-separated)
2. `~/.config/gst/modules/`
3. `/etc/gst/modules/`
4. `/usr/share/gst/modules/`

### Built-in Modules

| Module | Description |
|--------|-------------|
| scrollback | Ring buffer scrollback with mouse/keyboard scroll |
| transparency | X11 composite transparency |
| url-click | Clickable URLs with regex detection |
| boxdraw | Optimized box drawing characters |
| visualbell | Visual bell indicator |
| undercurl | Undercurl text decoration |
| keyboard-select | Keyboard-based text selection |
| ligatures | Font ligature support |
| externalpipe | External command piping |
| kitty-graphics | Inline image rendering |

### Writing Modules

See `docs/modules/` for the module development guide and `examples/custom-module/` for a minimal example.

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

## License

AGPLv3 - See [LICENSE](LICENSE) for details.

## Credits

Based on [st (suckless terminal)](https://st.suckless.org/) by suckless.org.
