# Known Issues

No critical known issues at this time.

## Resolved Issues

### Yazi image preview broken with kittygfx module (RESOLVED)

**Status:** Fixed
**Affects:** kittygfx module (kitty graphics protocol)

When using yazi (terminal file manager) with the kittygfx module enabled, multiple symptoms appeared:

1. **'d' key auto-triggered on startup** -- yazi's delete confirmation dialog appeared without user input
2. **Error text visible in yazi UI** -- `Gi=537;EINVAL:failed to decode image` rendered as text
3. **Jump to last entry** -- yazi jumped to the bottom of the file list (as if Shift+G was pressed)
4. **Images rendered but old images persisted** -- switching to a smaller image left the previous larger image's pixels on screen

### Root Causes and Fixes

The issue had multiple interacting root causes, each requiring a separate fix:

**1. PTY Echo Cascade**

When the module wrote a response (e.g. `\033_Gi=31;OK\033\\`) to the PTY, the line discipline echoed it back if `ECHO` was enabled. The echoed APC was re-parsed as a transmit command, failed to decode the status text as base64, generated an error response, which echoed again -- creating an infinite loop. Characters from error messages (`d` from "decode", `G` from response prefix) leaked to yazi as keypresses.

*Fix:* Three-layer echo detection: response queue matching, structural heuristic (error codes contain `:` which is not in the base64 alphabet), and `gst_pty_write_no_echo()` for kernel-level echo suppression.

**2. Broken Zlib Decompression**

The original decompression used `g_converter_convert()` with a fixed-size output buffer and a retry loop that re-fed already-consumed input, corrupting the decompressor state. This caused every zlib-compressed image to fail decoding.

*Fix:* Rewrote decompression to use `GConverterInputStream` wrapping a `GZlibDecompressor` -- the streaming approach handles arbitrary compression ratios without fixed buffers or state corruption.

**3. Chunked Transfer ID Assignment**

Continuation chunks (with `image_id == 0`) were auto-assigned new IDs instead of reusing the first chunk's ID. Each chunk became a separate incomplete upload that never finalized.

*Fix:* Added `last_image_id` tracking to the cache. Continuation chunks reuse the active upload's ID when `image_id == 0`.

**4. First-Chunk Fields Lost on Continuation**

The `quiet`, `action`, `placement_id`, pixel format, and all display parameters were only present in the first chunk. Continuation chunks carried parser defaults (e.g. `quiet=0` instead of `quiet=2`, `action='t'` instead of `action='T'`). Using these defaults for the final response and placement creation produced wrong behavior: OK responses sent when they should be suppressed, placements not created because action appeared to be transmit-only.

*Fix:* All first-chunk control keys are stored in the `GstKittyUpload` accumulator struct and used when the final chunk triggers decode and placement.

**5. Quiet Flag Logic Inverted**

Success responses used `cmd->quiet != 1` which incorrectly sent OK for `q=2` (suppress all). Per the spec: `q=0` sends all, `q=1` suppresses OK, `q=2` suppresses everything.

*Fix:* Changed to `saved.quiet == 0` for success responses.

**6. Stale Image Pixels After Delete**

Yazi's image transition pattern sends `a=d,d=A` (delete all) before uploading a new image. The delete correctly removed placements from the cache, but the terminal's dirty-line tracking didn't know about it -- the text under the old image hadn't changed, so those lines weren't redrawn. Old image pixels persisted in the pixmap.

*Fix:* After processing delete commands, the module explicitly calls `gst_terminal_mark_dirty(term, -1)` to force a full repaint.

### Not Yet Implemented (Kitty Graphics)

- Animation support (`a=f`, `a=a`, `a=c`)
- Unicode placeholders (U+10EEEE with `U=1`)
- Cell-based placement tracking (auto-cleanup when text overwrites image cells)
