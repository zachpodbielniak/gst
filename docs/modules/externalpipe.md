# External Pipe Module

Pipe visible terminal screen content to an external command.

## Overview

The externalpipe module collects all visible text from the terminal screen and pipes it to a shell command via stdin. This is useful for extracting text from the screen for processing -- for example, piping to a URL selector, a clipboard tool, or a search script.

## Configuration

### YAML

```yaml
modules:
  externalpipe:
    enabled: true
    command: "grep -oP 'https?://\\S+' | head -1 | xclip -selection clipboard"
```

### C Config

```c
gst_config_set_module_config_bool(config, "externalpipe", "enabled", TRUE);
gst_config_set_module_config_string(config, "externalpipe", "command",
    "grep -oP 'https?://\\S+' | head -1 | xclip -selection clipboard");
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `command` | string | `""` | Shell command to pipe screen content to |

## Keybindings

| Key | Action |
|-----|--------|
| `Ctrl+Shift+e` | Pipe screen content to the configured command |

## Usage

1. Set the `command` option to the command you want to receive the terminal text
2. Press `Ctrl+Shift+e` to trigger
3. All visible terminal text is collected and written to the command's stdin
4. The command runs asynchronously via `/bin/sh -c`

### Examples

**Copy all URLs to clipboard:**

```yaml
modules:
  externalpipe:
    enabled: true
    command: "grep -oP 'https?://\\S+' | xclip -selection clipboard"
```

**Open URL selector with dmenu:**

```yaml
modules:
  externalpipe:
    enabled: true
    command: "grep -oP 'https?://\\S+' | sort -u | dmenu -l 10 | xargs xdg-open"
```

**Save screen content to a file:**

```yaml
modules:
  externalpipe:
    enabled: true
    command: "cat > /tmp/terminal-dump.txt"
```

## Notes

- The command receives all visible terminal lines via stdin, then stdin is closed
- Wide character placeholders (dummy cells for double-width characters) are skipped
- The command is wrapped with `/bin/sh -c` so pipes and shell features work
- The module does nothing if `command` is empty
