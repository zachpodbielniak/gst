# Keyboard Select Module

Vim-like keyboard-driven text selection and navigation.

## Overview

The keyboard_select module provides a modal interface for navigating the terminal screen and selecting text without the mouse. Inspired by Vim's modal editing, it supports cursor movement, visual selection (character-wise and line-wise), incremental search, and yanking text to the clipboard.

Press the trigger key to enter selection mode. The terminal enters a modal state where all keys are consumed by the module until you exit.

## Configuration

### YAML

```yaml
modules:
  keyboard_select:
    enabled: true
    key: Ctrl+Shift+Escape
    show_crosshair: true
    highlight_color: "#ff8800"
    highlight_alpha: 100
    search_color: "#ffff00"
    search_alpha: 150
```

### C Config

```c
gst_config_set_module_config_bool(config, "keyboard_select", "enabled", TRUE);
gst_config_set_module_config_string(config, "keyboard_select", "key",
    "Ctrl+Shift+Escape");
gst_config_set_module_config_bool(config, "keyboard_select", "show_crosshair", TRUE);
gst_config_set_module_config_string(config, "keyboard_select", "highlight_color",
    "#ff8800");
gst_config_set_module_config_int(config, "keyboard_select", "highlight_alpha", 100);
gst_config_set_module_config_string(config, "keyboard_select", "search_color",
    "#ffff00");
gst_config_set_module_config_int(config, "keyboard_select", "search_alpha", 150);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `key` | string | `Ctrl+Shift+Escape` | Key to activate selection mode |
| `show_crosshair` | boolean | `true` | Show a crosshair at the cursor position |
| `highlight_color` | string | `#ff8800` | Selection highlight color (hex RGB) |
| `highlight_alpha` | integer | `100` | Selection highlight opacity (0-255) |
| `search_color` | string | `#ffff00` | Search match highlight color (hex RGB) |
| `search_alpha` | integer | `150` | Search match highlight opacity (0-255) |

## Keybindings

### Activation

| Key | Action |
|-----|--------|
| `Ctrl+Shift+Escape` | Enter keyboard selection mode (configurable) |
| `Escape` or `q` | Exit selection mode |

### Navigation (Normal Mode)

| Key | Action |
|-----|--------|
| `h` | Move left |
| `j` | Move down |
| `k` | Move up |
| `l` | Move right |
| `w` | Jump to next word |
| `b` | Jump to previous word |
| `e` | Jump to end of word |
| `0` | Jump to start of line |
| `$` | Jump to end of line |
| `g` then `g` | Jump to top of screen |
| `G` | Jump to bottom of screen |

### Selection

| Key | Action |
|-----|--------|
| `v` | Enter visual mode (character-wise selection) |
| `V` | Enter visual line mode (line-wise selection) |
| `y` | Yank (copy) selected text to clipboard and exit |
| `Escape` | Cancel selection and return to normal mode |

### Search

| Key | Action |
|-----|--------|
| `/` | Start forward search |
| `?` | Start backward search |
| `Enter` | Execute search |
| `n` | Next search match |
| `N` | Previous search match |

## Modes

The module operates as a state machine with these modes:

1. **Inactive** - Normal terminal operation. The module only listens for the trigger key.
2. **Normal** - Navigation mode. Move the cursor with vim-like keys.
3. **Visual** - Character-wise selection. Text between the anchor and cursor is highlighted.
4. **Visual Line** - Line-wise selection. Entire lines are highlighted.
5. **Search** - Incremental search. Type a pattern and press Enter to find matches.

## Usage

1. Press `Ctrl+Shift+Escape` to enter selection mode
2. Navigate with `h`, `j`, `k`, `l` (or `w`, `b`, `0`, `$`, etc.)
3. Press `v` to start selecting text (or `V` for whole lines)
4. Move the cursor to extend the selection
5. Press `y` to copy the selected text to the clipboard
6. You are returned to normal terminal operation

### Search and Select

1. Enter selection mode with `Ctrl+Shift+Escape`
2. Press `/` to start a forward search (or `?` for backward)
3. Type your search pattern
4. Press `Enter` to jump to the first match
5. Press `n`/`N` to cycle through matches
6. Press `v` to start selecting, then `y` to yank

## Notes

- When active, the module has high priority and consumes all key events -- the shell does not receive any input
- The cursor position, selection highlights, search matches, and mode indicator are rendered as overlays
- Search buffer has a maximum length of 256 characters
- Works with scrollback content if the scrollback module is also active
