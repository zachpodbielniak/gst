# GST Keybindings

## Default Keyboard Bindings

| Key | Action | Description |
|-----|--------|-------------|
| `Ctrl+Shift+c` | `clipboard_copy` | Copy selection to clipboard |
| `Ctrl+Shift+v` | `clipboard_paste` | Paste from clipboard |
| `Shift+Page_Up` | `scroll_up` | Scroll up (3 lines) |
| `Shift+Page_Down` | `scroll_down` | Scroll down (3 lines) |
| `Ctrl+Shift+Page_Up` | `scroll_top` | Jump to top of scrollback |
| `Ctrl+Shift+Page_Down` | `scroll_bottom` | Jump to bottom (live output) |
| `Ctrl+Shift+Home` | `scroll_top` | Jump to top of scrollback |
| `Ctrl+Shift+End` | `scroll_bottom` | Jump to bottom (live output) |
| `Ctrl+Shift+plus` | `zoom_in` | Increase font size |
| `Ctrl+Shift+minus` | `zoom_out` | Decrease font size |
| `Ctrl+Shift+0` | `zoom_reset` | Reset font size to default |

## Default Mouse Bindings

| Button | Action | Description |
|--------|--------|-------------|
| `Button4` | `scroll_up` | Scroll up (mouse wheel up) |
| `Button5` | `scroll_down` | Scroll down (mouse wheel down) |
| `Shift+Button4` | `scroll_up_fast` | Fast scroll up (~10 lines) |
| `Shift+Button5` | `scroll_down_fast` | Fast scroll down (~10 lines) |

## Module-Added Keybindings

Some modules add their own keybindings. These are not part of the configurable keybinds system -- they are handled directly by each module.

| Key | Module | Description |
|-----|--------|-------------|
| `Ctrl+Shift+u` | [urlclick](modules/urlclick.md) | Open first detected URL |
| `Ctrl+Shift+e` | [externalpipe](modules/externalpipe.md) | Pipe screen to external command |
| `Ctrl+Shift+Escape` | [keyboard_select](modules/keyboard-select.md) | Enter keyboard selection mode |

## Available Actions

These are all the actions you can bind to keys or mouse buttons:

| Action Name | Description |
|-------------|-------------|
| `clipboard_copy` | Copy the current selection to the system clipboard |
| `clipboard_paste` | Paste from the system clipboard |
| `paste_primary` | Paste from the X11 primary selection |
| `scroll_up` | Scroll up by a few lines (default 3) |
| `scroll_down` | Scroll down by a few lines (default 3) |
| `scroll_top` | Jump to the top of the scrollback buffer |
| `scroll_bottom` | Jump to the bottom (return to live output) |
| `scroll_up_fast` | Scroll up quickly (~10 lines) |
| `scroll_down_fast` | Scroll down quickly (~10 lines) |
| `zoom_in` | Increase font size |
| `zoom_out` | Decrease font size |
| `zoom_reset` | Reset font size to the configured default |

## Customizing Keybindings

### YAML

Define keybindings in the `keybinds:` section. When you define any keybindings in YAML, the entire default set is replaced -- so include all bindings you want.

```yaml
keybinds:
  "Ctrl+Shift+c": clipboard_copy
  "Ctrl+Shift+v": clipboard_paste
  "Shift+Insert": paste_primary
  "Shift+Page_Up": scroll_up
  "Shift+Page_Down": scroll_down
  "Ctrl+Shift+Page_Up": scroll_top
  "Ctrl+Shift+Page_Down": scroll_bottom
  "Ctrl+Shift+Home": scroll_top
  "Ctrl+Shift+End": scroll_bottom
  "Ctrl+Shift+plus": zoom_in
  "Ctrl+Shift+minus": zoom_out
  "Ctrl+Shift+0": zoom_reset
```

Mouse bindings work the same way in the `mousebinds:` section:

```yaml
mousebinds:
  "Button4": scroll_up
  "Button5": scroll_down
  "Shift+Button4": scroll_up_fast
  "Shift+Button5": scroll_down_fast
```

### C Config

In C config, keybindings are additive by default -- they append to the existing set. To replace all bindings, clear first.

```c
G_MODULE_EXPORT gboolean
gst_config_init(void)
{
    GstConfig *config = gst_config_get_default();

    /* Add a single extra binding (keeps all defaults) */
    gst_config_add_keybind(config, "Shift+Insert", "paste_primary");

    /* Or: replace ALL keybindings from scratch */
    gst_config_clear_keybinds(config);
    gst_config_add_keybind(config, "Ctrl+Shift+c", "clipboard_copy");
    gst_config_add_keybind(config, "Ctrl+Shift+v", "clipboard_paste");
    /* ... add more as needed ... */

    /* Mouse bindings work the same way */
    gst_config_clear_mousebinds(config);
    gst_config_add_mousebind(config, "Button4", "scroll_up");
    gst_config_add_mousebind(config, "Button5", "scroll_down");

    return TRUE;
}
```

**C API functions:**

| Function | Description |
|----------|-------------|
| `gst_config_add_keybind(config, key_str, action_str)` | Add a keyboard binding |
| `gst_config_add_mousebind(config, key_str, action_str)` | Add a mouse binding |
| `gst_config_clear_keybinds(config)` | Remove all keyboard bindings |
| `gst_config_clear_mousebinds(config)` | Remove all mouse bindings |
| `gst_config_lookup_key_action(config, keyval, state)` | Look up action for a key event |
| `gst_config_lookup_mouse_action(config, button, state)` | Look up action for a mouse event |

## Key String Format

Key strings use the format `Modifier+Modifier+KeyName`.

### Modifiers

| Modifier | Description |
|----------|-------------|
| `Ctrl` | Control key |
| `Shift` | Shift key |
| `Alt` | Alt key (Mod1) |
| `Super` | Super/Windows key (Mod4) |
| `Hyper` | Hyper key |
| `Meta` | Meta key |

Modifier names are case-insensitive (`ctrl`, `Ctrl`, and `CTRL` all work). You can also use `Control` instead of `Ctrl`, or `Mod1` instead of `Alt`, or `Mod4` instead of `Super`.

### Key Names

Key names are X11 keysym names. Common examples:

| Key Name | Key |
|----------|-----|
| `a`-`z` | Letter keys |
| `0`-`9` | Number keys |
| `Page_Up`, `Page_Down` | Page up/down |
| `Home`, `End` | Home/end |
| `Insert`, `Delete` | Insert/delete |
| `Return`, `space` | Enter/space |
| `Escape` | Escape |
| `Tab` | Tab |
| `plus`, `minus` | Plus/minus |
| `F1`-`F12` | Function keys |

### Mouse Button Names

| Button | Description |
|--------|-------------|
| `Button1` | Left click |
| `Button2` | Middle click |
| `Button3` | Right click |
| `Button4` | Scroll wheel up |
| `Button5` | Scroll wheel down |
| `Button6` | Scroll wheel left |
| `Button7` | Scroll wheel right |
| `Button8` | Extra button 1 |
| `Button9` | Extra button 2 |

## Technical Notes

- **Lock bit stripping**: NumLock, CapsLock, and ScrollLock states are automatically stripped when matching keybindings, so bindings work regardless of lock key state.
- **Shift+letter normalization**: When you bind `Shift+a`, it is automatically converted to `Shift+A` (uppercase keysym) for correct X11 matching.
- **YAML replaces defaults**: If you define a `keybinds:` section in YAML, all default keybindings are replaced. If you omit the section entirely, defaults are used.
- **C config appends**: `gst_config_add_keybind()` adds to existing bindings. Use `gst_config_clear_keybinds()` first if you want to replace everything.
