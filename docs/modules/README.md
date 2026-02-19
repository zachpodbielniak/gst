# GST Modules

Modules extend GST's functionality through a plugin system. Each module is a shared object (`.so`) that GST loads at startup. Modules can intercept keyboard events, modify rendering, handle URLs, and more.

## Quick Reference

### Core Modules

| Module | Description | Default |
|--------|-------------|---------|
| [scrollback](scrollback.md) | Scrollback history with keyboard/mouse navigation | Enabled |
| [transparency](transparency.md) | Window opacity with focus tracking | Disabled |
| [urlclick](urlclick.md) | Detect and open URLs | Enabled |
| [externalpipe](externalpipe.md) | Pipe screen content to external commands | Disabled |
| [boxdraw](boxdraw.md) | Pixel-perfect box-drawing characters | Enabled |
| [visualbell](visualbell.md) | Visual flash on terminal bell | Disabled |
| [undercurl](undercurl.md) | Curly underline rendering | Enabled |
| [clipboard](clipboard.md) | Auto-sync selection to system clipboard | Enabled |
| [font2](font2.md) | Fallback font preloading | Disabled |
| [keyboard_select](keyboard-select.md) | Vim-like keyboard text selection | Disabled |
| [kittygfx](kittygfx.md) | Kitty graphics protocol (inline images) | Disabled |
| [mcp](mcp.md) | MCP server for AI assistant integration | Disabled |

### Escape Sequence Modules

| Module | Description | Default |
|--------|-------------|---------|
| [notify](notify.md) | Desktop notifications via OSC 9/777/99 | Disabled |
| [dynamic_colors](dynamic-colors.md) | Runtime color palette changes via OSC 10/11/12/4/104 | Enabled |
| [osc52](osc52.md) | Remote clipboard access via OSC 52 | Disabled |
| [sync_update](sync-update.md) | Synchronized output (mode 2026) to eliminate flicker | Enabled |
| [shell_integration](shell-integration.md) | Semantic prompt zones via OSC 133 | Disabled |
| [hyperlinks](hyperlinks.md) | OSC 8 explicit hyperlinks with click-to-open | Disabled |

### Graphics and Rendering Modules

| Module | Description | Default |
|--------|-------------|---------|
| [sixel](sixel.md) | DEC Sixel graphics protocol for inline images | Disabled |
| [ligatures](ligatures.md) | HarfBuzz font ligature rendering | Disabled |

### Interactive Modules

| Module | Description | Default |
|--------|-------------|---------|
| [search](search.md) | Interactive scrollback text search with highlighting | Disabled |

## Enabling / Disabling Modules

In your YAML config (`~/.config/gst/config.yaml`):

```yaml
modules:
  scrollback:
    enabled: true
  transparency:
    enabled: false
```

In C config:

```c
gst_config_set_module_config_bool(config, "scrollback", "enabled", TRUE);
gst_config_set_module_config_bool(config, "transparency", "enabled", FALSE);
```

A module set to `enabled: false` is not activated at startup. Omitting the `enabled` key defaults to the module's built-in default (see table above).

## Module Search Path

Modules are loaded from `.so` files at startup. GST searches these directories in order:

1. `$GST_MODULE_PATH` (colon-separated directories)
2. `~/.config/gst/modules/`
3. `/etc/gst/modules/`
4. `/usr/share/gst/modules/`

All built-in modules are compiled into the build directory and loaded automatically.

## Writing Custom Modules

See the [Module System Developer Guide](module-system.md) for how to write your own modules, including:

- Module entry points and lifecycle
- Available interfaces (hook points)
- Priority system
- Building and testing
- Minimal example with Makefile
