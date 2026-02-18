# GST Quick Start Guide

GST (GObject Simple Terminal) is a fast, modular terminal emulator based on [st](https://st.suckless.org/) with GObject architecture and a plugin system.

## Install Dependencies

**Fedora:**

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

**Optional (Wayland backend):**

```bash
sudo dnf install wayland-devel wayland-protocols-devel \
    libxkbcommon-devel cairo-devel
```

**Optional (MCP module for AI assistant integration):**

```bash
sudo dnf install libsoup3-devel libdex-devel libpng-devel
```

## Build

```bash
git clone --recurse-submodules <repo-url>
cd gst
make
```

For a debug build with symbols:

```bash
make DEBUG=1
```

To include the MCP module:

```bash
MCP=1 make
```

## First Launch

```bash
./build/release/gst
```

Or after `make install`:

```bash
gst
```

GST auto-detects whether to use X11 or Wayland based on your session. You can force a backend with `--x11` or `--wayland`.

## Generate a Config File

GST works out of the box with sensible defaults. To customize it, generate a config file:

```bash
gst --generate-yaml-config > ~/.config/gst/config.yaml
```

Edit the file with your preferred editor. Changes take effect on next launch.

To generate a config with only specific modules included:

```bash
gst --generate-yaml-config --modules scrollback,transparency,urlclick \
    > ~/.config/gst/config.yaml
```

## Enable a Module

Modules extend GST's functionality. Edit your config to enable one:

```yaml
modules:
  transparency:
    enabled: true
    opacity: 0.85
```

See [Module Documentation](modules/README.md) for all available modules.

## Common Tasks

**Change the font:**

```yaml
font:
  primary: "JetBrains Mono:pixelsize=14:antialias=true:autohint=true"
```

**Change window size:**

```yaml
window:
  geometry: 120x40
```

Or from the command line:

```bash
gst -g 120x40
```

**Set a color scheme:**

```yaml
colors:
  foreground: "#cdd6f4"
  background: "#1e1e2e"
```

See [Color Schemes](colors.md) for complete theming instructions.

**Custom keybindings:**

```yaml
keybinds:
  "Ctrl+Shift+c": clipboard_copy
  "Ctrl+Shift+v": clipboard_paste
```

See [Keybindings](keybindings.md) for all available actions.

## Next Steps

- [CLI Reference](cli.md) - All command-line options
- [Configuration Reference](configuration.md) - Every config option explained
- [Keybindings](keybindings.md) - Keyboard and mouse bindings
- [Color Schemes](colors.md) - Theming and palette customization
- [C Configuration](c-config.md) - Advanced config via compiled C
- [Module Guide](modules/README.md) - All built-in modules
- [Module Development](modules/module-system.md) - Writing your own modules
