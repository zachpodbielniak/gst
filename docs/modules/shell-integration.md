# Shell Integration Module

Semantic prompt zones via OSC 133 with visual markers and keyboard navigation.

## Overview

The shell integration module tracks semantic zones in the terminal output using the OSC 133 protocol. Shells emit these markers to identify where prompts, commands, and their output begin and end. This enables:

- **Prompt navigation** -- jump between prompts with `Ctrl+Shift+Up/Down`
- **Visual markers** -- colored indicators in the left border at each prompt
- **Exit code display** -- failed commands are marked in red (configurable)

The module requires shell-side configuration to emit OSC 133 markers. Most modern shells support this natively or via plugins.

## Configuration

### YAML

```yaml
modules:
  shell_integration:
    enabled: true
    mark_prompts: true
    show_exit_code: true
    error_color: "#ef2929"
```

### C Config

```c
gst_config_set_module_config_bool(config, "shell_integration", "enabled", TRUE);
gst_config_set_module_config_bool(config, "shell_integration", "mark_prompts", TRUE);
gst_config_set_module_config_bool(config, "shell_integration", "show_exit_code", TRUE);
gst_config_set_module_config_string(config, "shell_integration", "error_color", "#ef2929");
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `mark_prompts` | boolean | `true` | Draw colored markers at prompt rows |
| `show_exit_code` | boolean | `true` | Color markers red when the previous command failed |
| `error_color` | string | `"#ef2929"` | Hex color for failed-command markers |

## Keybindings

| Key | Action |
|-----|--------|
| `Ctrl+Shift+Up` | Jump to previous prompt |
| `Ctrl+Shift+Down` | Jump to next prompt |

## Escape Sequences

The module handles OSC 133 zone markers:

| Sequence | Meaning |
|----------|---------|
| `ESC ] 133 ; A ST` | Prompt start |
| `ESC ] 133 ; B ST` | Prompt end / command start |
| `ESC ] 133 ; C ST` | Command output start |
| `ESC ] 133 ; D ; <exit_code> ST` | Command finished with exit code |

### Zone Lifecycle

```
[A] prompt text [B] user command [C]
command output here...
[D;0]    <-- exit code 0 (success)
[A] next prompt [B] ...
```

## Shell Setup

### Bash

Add to your `~/.bashrc`:

```bash
PS0='\[\e]133;C\a\]'
PS1='\[\e]133;A\a\][\W]\$ \[\e]133;B\a\]'
PROMPT_COMMAND='printf "\e]133;D;%s\a" "$?"'
```

### Zsh

Add to your `~/.zshrc`:

```zsh
precmd() {
    print -Pn "\e]133;D;$?\a"
    print -Pn "\e]133;A\a"
}
preexec() {
    print -Pn "\e]133;C\a"
}
# Emit B after the prompt is drawn
PS1="%~ %# %{\e]133;B\a%}"
```

### Fish

Fish 3.4+ has built-in support. Enable it with:

```fish
function fish_prompt
    printf '\e]133;A\a'
    # your prompt here
    printf '\e]133;B\a'
end
function fish_preexec --on-event fish_preexec
    printf '\e]133;C\a'
end
function fish_postexec --on-event fish_postexec
    printf '\e]133;D;%s\a' $status
end
```

## Notes

- Zone rows are adjusted as lines scroll out of the visible area (the module connects to the `line-scrolled-out` signal).
- The prompt navigation keys scroll the terminal view to bring the target prompt into view, similar to how `Shift+PageUp` scrolls through history.
- If the scrollback module is not active, prompt navigation only works within the visible screen.

## Source Files

| File | Description |
|------|-------------|
| `modules/shell_integration/gst-shellint-module.c` | Module implementation |
| `modules/shell_integration/gst-shellint-module.h` | Type macros and struct declaration |
