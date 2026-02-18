# URL Click Module

Detect and open URLs from the terminal screen.

## Overview

The urlclick module scans the visible terminal text for URLs and opens the first match using a configurable opener command. Press `Ctrl+Shift+u` to trigger URL detection -- the module finds URLs matching a regex pattern and opens the first one found with `xdg-open` (or your configured opener).

## Configuration

### YAML

```yaml
modules:
  urlclick:
    enabled: true
    opener: xdg-open
    regex: "(https?|ftp|file)://[\\w\\-_.~:/?#\\[\\]@!$&'()*+,;=%]+"
```

### C Config

```c
gst_config_set_module_config_bool(config, "urlclick", "enabled", TRUE);
gst_config_set_module_config_string(config, "urlclick", "opener", "xdg-open");
gst_config_set_module_config_string(config, "urlclick", "regex",
    "(https?|ftp|file)://[\\w\\-_.~:/?#\\[\\]@!$&'()*+,;=%]+");
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable the module |
| `opener` | string | `xdg-open` | Command to open URLs (receives the URL as an argument) |
| `regex` | string | (URL pattern) | Regular expression for matching URLs |

## Keybindings

| Key | Action |
|-----|--------|
| `Ctrl+Shift+u` | Scan screen for URLs and open the first match |

## Usage

1. Make sure a URL is visible on the terminal screen
2. Press `Ctrl+Shift+u`
3. The module scans all visible text for the first URL matching the regex
4. The URL is opened using the `opener` command

### Custom Opener

To open URLs in a specific browser:

```yaml
modules:
  urlclick:
    enabled: true
    opener: firefox
```

Or use a script for more control:

```yaml
modules:
  urlclick:
    enabled: true
    opener: ~/bin/open-url.sh
```

### Custom Regex

To match only HTTPS URLs:

```yaml
modules:
  urlclick:
    enabled: true
    regex: "https://[\\w\\-_.~:/?#\\[\\]@!$&'()*+,;=%]+"
```

The regex is compiled as a GLib `GRegex`. Changes to the regex in config cause recompilation on next activation.

## Notes

- The module collects text from all visible terminal lines, skipping wide character placeholders
- Only the first URL found is opened
- The opener command runs asynchronously and does not block the terminal
