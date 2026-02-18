# Kitty Graphics Module

Inline image display using the Kitty graphics protocol.

## Overview

The kittygfx module implements the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/), allowing programs to display images directly in the terminal. This enables image previews in file managers (e.g. yazi, ranger), inline plots in data analysis tools, and other graphical content within the terminal.

The module intercepts APC (Application Program Command) escape sequences that carry image data, manages an image cache with LRU eviction and configurable memory limits, supports chunked multi-part transfers, and renders image placements as overlays on the terminal surface.

## Configuration

### YAML

```yaml
modules:
  kittygfx:
    enabled: true
    max_total_ram_mb: 256
    max_single_image_mb: 64
    max_placements: 4096
    allow_file_transfer: false
    allow_shm_transfer: false
```

### C Config

```c
gst_config_set_module_config_bool(config, "kittygfx", "enabled", TRUE);
gst_config_set_module_config_int(config, "kittygfx", "max_total_ram_mb", 256);
gst_config_set_module_config_int(config, "kittygfx", "max_single_image_mb", 64);
gst_config_set_module_config_int(config, "kittygfx", "max_placements", 4096);
gst_config_set_module_config_bool(config, "kittygfx", "allow_file_transfer", FALSE);
gst_config_set_module_config_bool(config, "kittygfx", "allow_shm_transfer", FALSE);
```

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable the module |
| `max_total_ram_mb` | integer | `256` | Total memory limit for all cached images (MB) |
| `max_single_image_mb` | integer | `64` | Maximum size of a single decoded image (MB) |
| `max_placements` | integer | `4096` | Maximum number of active image placements |
| `allow_file_transfer` | boolean | `false` | Allow loading images via file path (`t=f`, `t=t`) |
| `allow_shm_transfer` | boolean | `false` | Allow loading images via shared memory (`t=s`) |

## Usage

Once enabled, programs that support the Kitty graphics protocol can display images automatically. No keybindings are needed.

### Supported Programs

- **yazi** - File manager with image previews
- **ranger** - File manager (with kitty backend)
- **timg** - Terminal image viewer
- **matplotlib** - Python plotting (with kitty backend)
- **chafa** - Image-to-terminal converter (with kitty protocol support)
- Any program that implements the Kitty graphics protocol

## Architecture

The module is built from three components:

```
gst-kittygfx-module.c    GObject module shell, GstEscapeHandler + GstRenderOverlay
gst-kittygfx-parser.c    APC key=value parser (GstGraphicsCommand)
gst-kittygfx-image.c     Image cache, upload accumulator, placement tracking, decode
```

### GObject Interfaces

The module implements two interfaces:

- **GstEscapeHandler** - Intercepts APC escape sequences (`ESC _ G ... ESC \`) from the terminal's escape parser. The terminal dispatches all APC strings through the module manager; this module claims sequences whose first byte after `ESC _` is `G`.

- **GstRenderOverlay** - Draws image placements on top of (or behind) terminal text during each render cycle. Called by the renderer after line drawing, before the pixmap is flushed to the window.

### Data Flow

```
PTY read -> terminal escape parser -> APC dispatch -> module manager
  -> kittygfx_handle_escape()
    -> echo detection (queue match + heuristic)
    -> gst_gfx_command_parse()       [parser]
    -> gst_kitty_image_cache_process() [image cache]
      -> handle_transmit / handle_display / handle_query / handle_delete
    -> terminal "response" signal (if response needed)
    -> gst_terminal_mark_dirty() (if placements changed)

Render cycle:
  renderer draws dirty lines -> renderer dispatches overlays
    -> kittygfx_render()
      -> get visible placements (z-sorted)
      -> gst_render_context_draw_image() for each
    -> pixmap copied to window
```

## Protocol Support

### Actions

| Action | Key | Description | Status |
|--------|-----|-------------|--------|
| Transmit | `a=t` | Upload image data to cache | Supported |
| Transmit+Display | `a=T` | Upload and immediately place image | Supported |
| Display | `a=p` | Create new placement for cached image | Supported |
| Query | `a=q` | Probe for protocol support (responds OK) | Supported |
| Delete | `a=d` | Remove placements and/or image data | Supported |
| Animation frame | `a=f` | Animation frames | Not implemented |
| Animation control | `a=a` | Animation playback control | Not implemented |
| Composition | `a=c` | Composition mode | Not implemented |

### Pixel Formats

| Format | `f=` value | Description |
|--------|------------|-------------|
| RGB | `24` | 24-bit RGB (3 bytes/pixel), converted to RGBA internally |
| RGBA | `32` | 32-bit RGBA (4 bytes/pixel), used directly |
| PNG | `100` | PNG-encoded, decoded via stb_image |

RGB data (`f=24`) is converted to RGBA during decode by appending `alpha=255` to each pixel. All images are stored internally as RGBA regardless of source format.

### Transfer Methods

| Method | `t=` value | Config Flag | Description |
|--------|------------|-------------|-------------|
| Direct | `d` | (always on) | Base64-encoded image data in the APC payload |
| File | `f` | `allow_file_transfer` | Load from an absolute file path |
| Temp file | `t` | `allow_file_transfer` | Load from a temporary file |
| Shared memory | `s` | `allow_shm_transfer` | Load via POSIX shared memory |

Direct transfer is always available. File and shared memory transfers are disabled by default for security (see [Security](#security)).

### Delete Targets

Delete commands (`a=d`) use the `d=` key to specify what to remove. Lowercase targets remove placements only. Uppercase targets also free the underlying image data when no placements remain.

| Target | `d=` value | Description |
|--------|------------|-------------|
| All | `a` / `A` | All placements (default when `d=` is omitted) |
| By ID | `i` / `I` | Placements for a specific `image_id` |
| By number | `n` / `N` | Newest image with a specific `image_number` |
| At cursor | `c` / `C` | Placements intersecting the cursor position |
| At cell | `p` / `P` | Placements intersecting a specific cell (`x=`, `y=`) |
| At cell+z | `q` / `Q` | At cell with matching z-index |
| By ID range | `r` / `R` | Placements where `x <= image_id <= y` |
| At column | `x` / `X` | Placements intersecting a specific column |
| At row | `y` / `Y` | Placements intersecting a specific row |
| At z-index | `z` / `Z` | Placements with a specific z-index |

### Compression

Zlib-compressed payloads (`o=z`) are decompressed using `GConverterInputStream` wrapping a `GZlibDecompressor`. This streaming approach handles arbitrary compression ratios without fixed buffer size limits -- important because highly compressible images (solid colors, gradients) can exceed 100:1 ratios.

### Quiet Mode

The `q=` key controls response suppression:

| Value | Behavior |
|-------|----------|
| `0` | Send all responses (OK and errors) |
| `1` | Suppress OK responses, still send errors |
| `2` | Suppress all responses |

Programs like yazi use `q=2` to suppress all responses, keeping the PTY clean.

## Chunked Transfers

Large images are sent across multiple APC escape sequences using the `m=` (more) key. This is the standard transfer pattern used by programs like yazi.

### Protocol

1. **First chunk**: carries all control keys (`a=`, `f=`, `q=`, `s=`, `v=`, `c=`, `r=`, etc.) plus `m=1` (more data follows) and a base64 payload.
2. **Continuation chunks**: carry only `m=1` and a base64 payload. All other fields default to parser defaults (which differ from the first chunk's values).
3. **Final chunk**: carries `m=0` (or omits `m=`) and the last payload fragment. Triggers decode and placement.

### Implementation Details

The `GstKittyUpload` accumulator struct stores all first-chunk fields so they are available when the final chunk triggers image decode and placement creation. This is critical because continuation chunks only carry `m=` and payload -- the parser fills all other fields with defaults (`action='t'`, `format=32`, `quiet=0`, etc.) that would be wrong if used for the final response or placement.

Fields preserved from the first chunk:

- `action` - `'t'` vs `'T'` (transmit-only vs transmit+display)
- `quiet` - response suppression level
- `format` - pixel format (24, 32, or 100)
- `compression` - `'z'` for zlib
- `placement_id`, `src_x`, `src_y`, `crop_w`, `crop_h`
- `dst_cols`, `dst_rows`, `x_offset`, `y_offset`, `z_index`
- `cursor_movement`
- `image_number`, `width`, `height`

### Continuation Chunk ID Resolution

Per the kitty spec, continuation chunks may omit the `i=` key (image ID). The cache tracks `last_image_id` -- the ID from the most recent transmit command -- and reuses it for chunks where `image_id == 0` when an active upload exists. A new auto-assigned ID is only generated when no active upload is found.

### Decode Pipeline

When the final chunk arrives (`m=0`):

1. Null-terminate the accumulated base64 byte array
2. Base64-decode into raw bytes
3. Zlib decompress if `compression == 'z'` (streaming via `GConverterInputStream`)
4. Decode pixels:
   - `f=100` (PNG): stb_image decodes to RGBA
   - `f=24` (RGB): expand 3 bpp to 4 bpp (append `alpha=255`)
   - `f=32` (RGBA): copy directly
5. Check against `max_single` size limit
6. Evict LRU images if `total_ram` would exceed `max_ram`
7. Insert into image hash table
8. If action was `'T'`: create placement at current cursor position

## Image Cache

### Structure

```
GstKittyImageCache
  ├── images:      GHashTable (image_id -> GstKittyImage)
  ├── uploads:     GHashTable (image_id -> GstKittyUpload)
  ├── placements:  GList of GstImagePlacement
  ├── total_ram:   current decoded bytes
  ├── max_ram:     limit in bytes
  ├── max_single:  max single image bytes
  └── next_image_id / last_image_id
```

### LRU Eviction

Each `GstKittyImage` stores a `last_used` monotonic timestamp, updated on every lookup via `gst_kitty_image_cache_get_image()`. When a new image would exceed `max_ram`, the image with the oldest `last_used` timestamp is evicted. This continues until enough space is freed.

### Scroll Tracking

When the terminal scrolls, `gst_kitty_image_cache_scroll()` adjusts all placement row positions by the scroll amount. Placements that scroll far off the top (row < -1000) are removed. This keeps placements aligned with their original text content as the terminal scrolls.

### Alternate Screen

`gst_kitty_image_cache_clear_alt()` removes all placements when switching to or from the alternate screen buffer. Images remain in cache but their placements are cleared, matching kitty's behavior.

## Rendering

### Overlay Pipeline

The `kittygfx_render()` function runs during each terminal render cycle, after the renderer has drawn dirty text lines and before the pixmap is flushed to the window.

1. Query visible placements for the current viewport (rows 0 to terminal height)
2. Sort by z-index (lowest first -- negative z renders behind text)
3. For each placement:
   - Calculate pixel position from cell coordinates + offsets
   - Determine source crop region (or full image if no crop)
   - Calculate destination size from `dst_cols`/`dst_rows` (or pixel size if unset)
   - Clip to window bounds
   - Call `gst_render_context_draw_image()` to composite RGBA data

### Backend Compatibility

The module uses the abstract `GstRenderContext` API, making it backend-agnostic. It works with both:

- **X11**: Uses `XRender` for RGBA compositing (premultiplied BGRA conversion)
- **Wayland**: Uses Cairo `cairo_set_source_surface()` with an image surface

### Dirty State and Repaints

GST uses dirty-line tracking for efficient rendering -- only lines whose text content changed are repainted. Image overlays are drawn on top of the already-rendered text in the pixmap. This creates a specific challenge:

When a delete command removes placements, no terminal lines are marked dirty (the text hasn't changed). Without intervention, the renderer skips those lines and old image pixels persist in the pixmap from the previous frame.

To handle this, the module explicitly calls `gst_terminal_mark_dirty(term, -1)` after processing any delete command. This forces a full repaint so line backgrounds are drawn over the old image area before the overlay phase runs.

## Echo Detection

### The Problem

When the module writes a response (e.g. `\033_Gi=31;OK\033\\`) to the PTY master, the line discipline may echo it back if `ECHO` is enabled. The echoed data arrives as a new APC sequence. Without detection, it would be parsed as a transmit command (the parser defaults `action` to `'t'`), fail to base64-decode the status text, generate an error response, which echoes back again -- creating an infinite cascade. Characters from error messages (like `d` from "decode") leak to the child process as keypresses.

### Defense Layers

The module uses a two-layer strategy:

**Layer 1: Response Queue** (`sent_responses` GQueue, capped at 64 entries)

When a response is sent, its APC body (the content between `\033_G` and `\033\\`) is recorded in the queue. When a new APC arrives, its body is compared against all queued entries. An exact match means it is an echo -- the entry is consumed and the APC is silently discarded.

**Layer 2: Heuristic Fallback**

If the queue match fails (e.g., the response was already consumed or the queue wrapped), a structural heuristic identifies response-shaped APCs:

- The control data starts with `i=` (not `a=`, `f=`, `s=`, etc.)
- The payload after `;` is either `OK` or matches `E<UPPERCASE>:<message>` (e.g., `EINVAL:failed to decode image`)

The colon `:` in error codes is not in the base64 alphabet (`A-Za-z0-9+/=`), making this a definitive discriminator between error response payloads and legitimate base64 image data.

**Layer 3: PTY Write with Echo Suppression** (`gst_pty_write_no_echo()`)

As defense-in-depth, the PTY layer temporarily disables `ECHO` via `tcsetattr()` around response writes and calls `tcdrain()` before restoring it. This prevents most echoes at the kernel level. However, programs like yazi put the terminal in raw mode (ECHO already off), so this layer is primarily effective during shell-mode operation.

## Security

- **File transfers** (`t=f`, `t=t`) are disabled by default because they allow programs to make the terminal read arbitrary files from disk. Enable only if you trust the programs running in the terminal.
- **Shared memory transfers** (`t=s`) are disabled by default for similar reasons -- a program could make the terminal map arbitrary shared memory segments.
- The `max_total_ram_mb` limit prevents a malicious program from consuming excessive RAM by sending many large images.
- The `max_single_image_mb` limit prevents a single image from consuming excessive memory (important because compressed images can expand dramatically).
- The `max_placements` limit prevents excessive rendering overhead from thousands of placements.

## Source Files

| File | Description |
|------|-------------|
| `modules/kittygfx/gst-kittygfx-module.c` | GObject module: escape handler, render overlay, echo detection, lifecycle |
| `modules/kittygfx/gst-kittygfx-module.h` | Module type macros and struct forward declaration |
| `modules/kittygfx/gst-kittygfx-parser.c` | APC key=value parser, fills `GstGraphicsCommand` struct |
| `modules/kittygfx/gst-kittygfx-parser.h` | Parser types: `GstGraphicsCommand`, enums for actions/formats/delete targets |
| `modules/kittygfx/gst-kittygfx-image.c` | Image cache, upload accumulator, placement CRUD, decode pipeline, LRU eviction |
| `modules/kittygfx/gst-kittygfx-image.h` | Cache types: `GstKittyImage`, `GstKittyUpload`, `GstImagePlacement`, `GstKittyImageCache` |
| `modules/kittygfx/stb_image.h` | Embedded stb_image (PNG/JPEG decode, `STBI_ONLY_PNG`, `STBI_ONLY_JPEG`, `STBI_NO_STDIO`) |

## Not Yet Implemented

The following kitty graphics protocol features are not yet supported:

- **Animation**: Frame management (`a=f`), animation control (`a=a`), composition mode (`a=c`)
- **Unicode placeholders**: Virtual placement via U+10EEEE codepoints with `U=1` -- images track with text reflow
- **Cell-based placement tracking**: Automatic cleanup when cells under a placement are overwritten by text
- **Shared memory object naming**: The `t=s` transfer path is gated by config but the actual shm_open logic is not implemented
