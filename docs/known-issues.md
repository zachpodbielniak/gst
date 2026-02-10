# Known Issues

## Yazi auto-triggers 'd' (delete) key on startup with kittygfx module

**Status:** Open
**Affects:** kittygfx module (kitty graphics protocol)
**Symptom:** Opening yazi in GST immediately triggers the delete confirmation dialog, as if 'd' was pressed.
**Reproduction:** Launch GST, run `yazi` in a directory with files. The "delete this file?" prompt appears without user input.

### Background

This appeared after fixing APC buffer corruption (commit afd2d30 and subsequent work). The original fix added an early-return in `term_strhandle()` for APC (`str_type == '_'`) sequences so `term_strparse()` would not replace `;` with `\0` in the buffer before the kittygfx module receives it. That fix resolved garbled yazi image previews, but exposed this secondary issue: kittygfx now generates responses that get echoed back.

### Root cause analysis (in progress)

The PTY slave has ECHO enabled (set by the shell). When the kittygfx module writes a response (e.g. `\033_Gi=31;OK\033\\`) to the PTY master, the line discipline echoes it back to the master's read buffer. The terminal re-parses the echoed data as a new APC command.

The echoed response `Gi=31;OK` has only the `i` key — no explicit `a=` key — so the parser defaults `action` to `'t'` (transmit). The module tries to base64-decode the status text "OK" as image data, fails, and generates an error response containing `EINVAL:failed to decode image`. This error response echoes back, creating an infinite cascade. Characters from error messages (like 'd' from "decode"/"failed") leak to yazi as keypresses.

### Attempted fixes (all failed)

1. **cfmakeraw on forkpty** (`src/core/gst-pty.c`): Passed `cfmakeraw()`-initialized termios to `forkpty()`. Disabled OPOST/ONLCR (broke NL->CRLF, prompt displaced) and ICRNL (Enter key stopped working). Terminal accepted no input.

2. **Global ECHO disable** (`src/core/gst-pty.c`): Disabled ECHO via `tcsetattr` on master fd after `forkpty()`. Shell inherited ECHO=off as baseline and never re-enabled it — typed characters became invisible.

3. **Targeted write_no_echo** (`src/core/gst-pty.c`): Added `gst_pty_write_no_echo()` that toggles ECHO off around each response write. Race condition — Linux processes PTY master writes asynchronously via work queue; by the time the line discipline processes the data, ECHO has already been restored.

4. **Module-level echo detection** (`modules/kittygfx/gst-kittygfx-module.c`): Added check in `kittygfx_handle_escape()` to detect echoed responses by format (`i=<number>;<status>` with no comma before `;`). The check correctly identifies the pattern but the 'd' key issue persists — suggesting the cascade or character leak may occur through a different mechanism than hypothesized.

### Current state of modified files

- `src/core/gst-pty.c` — has `gst_pty_write_no_echo()` function (defense-in-depth)
- `src/core/gst-pty.h` — declaration for `gst_pty_write_no_echo()`
- `src/main.c` — `on_terminal_response()` uses `gst_pty_write_no_echo()`
- `modules/kittygfx/gst-kittygfx-module.c` — echo detection guard in `kittygfx_handle_escape()`
- `src/core/gst-terminal.c` — APC early-return in `term_strhandle()` (from original fix, working)
- `tests/test-escape.c` — APC semicolon preservation test (from original fix, passing)

### Next steps to investigate

- Add stderr debug logging to `kittygfx_handle_escape()` and `on_terminal_response()` to capture exact data flow when yazi opens — verify whether the echo cascade is actually happening or if the 'd' comes from elsewhere.
- Compare raw PTY I/O between GST and kitty terminal using `strace -e read,write -p <pid>` to see what data actually traverses the master fd.
- Check whether yazi's kitty graphics detection sequence or the DA/DSR response handshake produces unexpected output that the terminal misroutes.
- Investigate the `put_char()` STR state machine (lines 2491-2513 in `gst-terminal.c`) — the ESC handling during string accumulation differs from st: GST calls `term_strhandle()` immediately on ESC, while st sets `ESC_STR_END` and defers to `\` arrival via the control code handler. This state difference may cause bytes between `ESC` and `\` of the ST terminator to leak.
