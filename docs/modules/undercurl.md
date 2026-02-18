# Undercurl Module

Curly underline rendering support.

## Overview

The undercurl module enables rendering of curly (wavy) underlines in the terminal. This decoration style is used by some programs (notably Neovim and other editors) to indicate spelling errors, diagnostics, or other inline annotations with a distinctive wavy line beneath the text.

Without this module, curly underline escape sequences fall back to a standard straight underline.

## Configuration

### YAML

```yaml
modules:
  undercurl:
    enabled: true
```

### C Config

```c
gst_config_set_module_config_bool(config, "undercurl", "enabled", TRUE);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable curly underline rendering |

## Usage

The module works automatically when enabled. Programs that emit curly underline escape sequences (SGR extended underline style) will display wavy underlines instead of straight ones.

### Common Programs That Use Undercurl

- **Neovim** - Spell checking, LSP diagnostics
- **Vim** (with terminal support) - Spell checking
- **Various terminal applications** using SGR extended underline sequences

## Notes

- This is a rendering feature toggle -- it has no keybindings or interactive behavior
- When disabled, curly underline sequences render as standard straight underlines
