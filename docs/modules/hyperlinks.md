# Hyperlinks Module

OSC 8 explicit hyperlinks with hover underline and click-to-open.

## Overview

The hyperlinks module implements the OSC 8 hyperlink protocol, which allows programs to emit clickable links in terminal output. Unlike URL detection (which relies on regex matching visible text), OSC 8 links have an explicit URI attached by the program. This means:

- Links can have arbitrary display text (e.g. "click here" instead of showing the raw URL)
- No false positives from regex matching
- Links can span multiple cells or lines
- The URI is associated with the text by the application, not guessed by the terminal

When the user hovers over a hyperlink span, it is underlined. Clicking with the configured modifier held (default: Ctrl) opens the URI with the configured opener command.

## Configuration

### YAML

```yaml
modules:
  hyperlinks:
    enabled: true
    opener: xdg-open
    modifier: Ctrl
    underline_hover: true
```

### C Config

```c
gst_config_set_module_config_bool(config, "hyperlinks", "enabled", TRUE);
gst_config_set_module_config_string(config, "hyperlinks", "opener", "xdg-open");
gst_config_set_module_config_string(config, "hyperlinks", "modifier", "Ctrl");
gst_config_set_module_config_bool(config, "hyperlinks", "underline_hover", TRUE);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `opener` | string | `"xdg-open"` | Command used to open URIs |
| `modifier` | string | `"Ctrl"` | Modifier key required for click-to-open |
| `underline_hover` | boolean | `true` | Underline hyperlink spans on mouse hover |

Valid `modifier` values: `"Ctrl"`, `"Shift"`, `"Alt"`, `"Super"`

## Mouse Bindings

| Event | Condition | Action |
|-------|-----------|--------|
| Left click | Configured modifier held | Open URI under cursor |
| Mouse motion | Always | Update hover underline state |

## Escape Sequences

### Open a Hyperlink

```
ESC ] 8 ; <params> ; <uri> ST
```

Begins a hyperlink span. All subsequent text is part of this link until it is closed. The `<params>` field is a colon-separated list of `key=value` pairs (the `id=` parameter is used to associate spans across lines).

### Close a Hyperlink

```
ESC ] 8 ; ; ST
```

Ends the current hyperlink span (both params and URI are empty).

## Usage

Programs can emit hyperlinks directly:

```bash
# Simple hyperlink
printf '\e]8;;https://example.com\e\\Click here\e]8;;\e\\'

# With an id parameter (for multi-line links)
printf '\e]8;id=mylink;https://example.com\e\\This link\e]8;;\e\\'
printf '\e]8;id=mylink;https://example.com\e\\spans two lines\e]8;;\e\\'
```

### Supported Programs

Many programs emit OSC 8 hyperlinks natively:

- **ls** (coreutils 8.28+) -- `ls --hyperlink=auto`
- **gcc** -- error messages with source file links
- **grep** (3.7+) -- `grep --hyperlink=auto`
- **systemd** -- journal output
- **various Rust tools** -- cargo, ripgrep, etc.

## Notes

- The module maintains a URI deduplication table so that repeated URIs (common when the same link appears on many lines) share a single string allocation.
- Hyperlink spans are tracked by row and column range. When the terminal scrolls, spans on old rows are discarded.
- The `id=` parameter allows a single logical hyperlink to span multiple non-contiguous cells (e.g. a filename that wraps across lines).

## Source Files

| File | Description |
|------|-------------|
| `modules/hyperlinks/gst-hyperlinks-module.c` | Module implementation |
| `modules/hyperlinks/gst-hyperlinks-module.h` | Type macros and struct declaration |
