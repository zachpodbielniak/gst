# Search Module

Interactive incremental text search with match highlighting.

## Overview

The search module provides an interactive search overlay for finding text in the terminal buffer. When activated, a search bar appears at the bottom of the terminal. As you type, all matches are highlighted in real-time. You can navigate between matches with Enter/Shift+Enter.

The module supports both plain text and regex search, with optional case sensitivity. Match highlighting uses configurable colors and alpha values for both the current match and all other matches.

## Configuration

### YAML

```yaml
modules:
  search:
    enabled: true
    highlight_color: "#ffff00"
    highlight_alpha: 100
    current_color: "#ff8800"
    current_alpha: 150
    match_case: false
    regex: false
```

### C Config

```c
gst_config_set_module_config_bool(config, "search", "enabled", TRUE);
gst_config_set_module_config_string(config, "search", "highlight_color", "#ffff00");
gst_config_set_module_config_int(config, "search", "highlight_alpha", 100);
gst_config_set_module_config_string(config, "search", "current_color", "#ff8800");
gst_config_set_module_config_int(config, "search", "current_alpha", 150);
gst_config_set_module_config_bool(config, "search", "match_case", FALSE);
gst_config_set_module_config_bool(config, "search", "regex", FALSE);
```

### Options

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `enabled` | boolean | `false` | | Enable the module |
| `highlight_color` | string | `"#ffff00"` | | Hex color for non-current matches (yellow) |
| `highlight_alpha` | integer | `100` | 0-255 | Opacity for non-current match highlights |
| `current_color` | string | `"#ff8800"` | | Hex color for the current match (orange) |
| `current_alpha` | integer | `150` | 0-255 | Opacity for the current match highlight |
| `match_case` | boolean | `false` | | Case-sensitive matching |
| `regex` | boolean | `false` | | Use regex patterns instead of plain text |

## Keybindings

### Activation

| Key | Action |
|-----|--------|
| `Ctrl+Shift+F` | Open search bar |

### While Search is Active

| Key | Action |
|-----|--------|
| Printable characters | Append to search query |
| `Backspace` | Delete last character from query |
| `Enter` | Jump to next match |
| `Shift+Enter` | Jump to previous match |
| `Escape` | Close search bar, clear highlights |

## Usage

1. Press `Ctrl+Shift+F` to open the search bar
2. Type your search query -- matches highlight as you type
3. Press `Enter` to jump forward through matches
4. Press `Shift+Enter` to jump backward
5. Press `Escape` to close

The search bar shows the query text and a match count indicator (e.g. `[3/17]` for match 3 of 17).

## Notes

- When the scrollback module is active, search covers both the visible screen and scrollback history.
- Regex search uses GLib's `GRegex` (PCRE-based). Invalid regex patterns are silently treated as literal text.
- Match positions are recalculated when the query changes or when terminal content changes (new output).
- The module consumes all keyboard input while the search bar is active -- normal terminal input is suspended until you press Escape.

## Source Files

| File | Description |
|------|-------------|
| `modules/search/gst-search-module.c` | Module implementation |
| `modules/search/gst-search-module.h` | Type macros and struct declaration |
