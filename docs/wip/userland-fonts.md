# Userland fonts

Status: **task 1 assigned to codex-epsilon, owner 2026-10-18; proposal below,
awaiting decisions before implementation.** The milestone decisions remain
accepted. Tasks 2–3 retain their own short proposal gates.

## Goal

Graphics programs draw antialiased text from TrueType fonts through a small
userland library, including programs built on Pyxis with
[hosted Clang](../development/hosted-tools.md). The kernel terminal keeps its
Bizcat bitmap font; scalable terminal fonts remain a
[later direction](later-os-directions.md#neovim-follow-ups), since font parsing
does not belong in the kernel.

## Accepted decisions (owner, 2026-10-17)

1. **Userland library only.** Terminal fonts are out of scope.
2. **stb_truetype.** The rasterizer is a pinned, vendored
   [stb_truetype](https://github.com/nothings/stb/blob/master/stb_truetype.h)
   (public domain or MIT), wrapped by a small `libfont` in pyxis-userland. It
   handles TrueType and CFF outlines, without hinting or shaping. It is not
   hardened against malicious font files, so the library loads fonts only from
   paths the caller chooses; this limit is recorded with the library.
3. **Fonts and styles.** All three families are OFL-licensed:

   | Family | Use | Styles |
   | --- | --- | --- |
   | CaskaydiaCove Nerd Font (Cascadia Code with Nerd Fonts icons) | Monospace | Regular, Bold, Italic, Bold Italic |
   | IBM Plex Sans | Sans | Regular, Bold |
   | IBM Plex Serif | Serif | Regular, Bold, Italic |

   The Nerd Fonts icon sets carry their own licences (Font Awesome, Material
   Design Icons, Octicons, Codicons and others), recorded with the font. Without
   shaping, Cascadia's programming ligatures render as separate characters.
4. **Location.** The fonts are installed read-only under
   `boot://share/fonts/`, where shared data lives today, about 11 MB in all.
   They move to the pool with the rest of the archive's shared data later
   ([rescue set debt](../technical-debt.md#rescue-set-programs)).
5. **Library shape.** A program opens a font by path, chooses the pixel size
   itself (never scaled from DPI), and draws UTF-8 text into its own pixel buffer
   with grayscale antialiasing. The library keeps a glyph cache per size and
   reports metrics: ascent, descent, line gap, advance and kerning from the
   font's tables. There is no shaping and no font-discovery service. Nerd icons
   are ordinary code points.
6. **The caller draws; no callbacks.** The library supplies positioned glyphs
   with 8-bit coverage masks (0 for no ink, 255 for full ink), independent of
   framebuffer format. The caller draws them in its own loop. One optional
   helper blends opaque sRGB text in linear light into an 8-bit XRGB buffer.
   The task 1 proposal below settles names, positioning and ownership; the
   glyph's origin must be separate from the already-advanced successor pen.

## Hosted Clang

`libfont` and its header are exported by `make sdk` like the other userland
libraries, so the hosted payload producer copies them into each bundle's
`sysroot://`. A program on Pyxis then links with `-lfont`. The installed Clang
only gains it with the next hosted payload. The [clangd milestone](clangd-neovim.md)'s
payload does not wait for `libfont` (owner, 2026-10-17), so `libfont` arrives
with a later payload update.

## Tasks

- [ ] **1. libfont — assigned 2026-10-18; proposal awaiting acceptance.** Vendor stb_truetype with its pin and licence, write the
  library and its header in pyxis-userland, export it through the SDK, and
  render a test string in QEMU.
- [ ] **2. Fonts.** Fetch the pinned font releases with SHA-256 through the
  download cache, stage the chosen styles under `boot://share/fonts/` with
  their licences, and record the size.
- [ ] **3. Example and hosted check.** A short example in
  `boot://share/examples/` drawing each family, built natively with hosted
  Clang and `-lfont` on vela.

## Task 1 proposal (not accepted)

Inspected main `7e97b3a0`, pinned userland `26e7d910`. No implementation or
font assets are included in this proposal. Three decisions remain:

1. **API and ownership — default: explicit face/size handles and status results.**
   Public `font.h` and `libfont.a` mirror the existing `libterm` runtime/SDK
   export. `font_open(path, options, &face)` reads a private owned copy through
   the caller's ordinary read authority and closes its FILE; it creates no
   authority or service. Each `font_size_create(face, pixels, options, &size)`
   retains the face. `font_close` drops the caller's face reference;
   `font_size_destroy` frees its cache and drops its reference, freeing font
   bytes after the last reference. A size remains usable after face close.
   Callers serialize mutable operations/lifetime on shared objects.

   `font_get_metrics` reports ascent, descent, line gap and nominal line
   advance. `font_get_glyph` reports coverage, dimensions/byte stride, signed
   bitmap offsets, glyph ID and advance; `font_get_kerning` accepts glyph IDs.
   Metrics, kerning and the pen use signed 26.6 pixel units with checked widened
   arithmetic; bitmap positions are integer pixels. Requested pixel height is
   ascent-to-descent height, without DPI scaling or line gap.

   A counted UTF-8 `font_run` borrows text and a live size. `font_run_begin`
   initializes its baseline, pen and previous glyph; `font_run_next` returns a
   `font_positioned_glyph` with explicit bitmap `x,y` plus coverage/metrics.
   Kerning adjusts this glyph's origin before the run advances its next pen.
   The caller draws at returned `x,y`, never at the updated pen.

   Use `enum font_result`: `FONT_OK`, `FONT_END`, `FONT_BAD_ARGUMENT`,
   `FONT_IO_ERROR`, `FONT_BAD_FONT`, `FONT_UNSUPPORTED`, `FONT_NO_MEMORY`,
   `FONT_LIMIT`. END means iterator exhaustion, not failure. Output handles
   start null; failed creation unwinds everything. A failed run step clears its
   output and leaves cursor/pen unchanged, permitting an explicit retry. No
   library logging. BAD_FONT is best-effort recognition, not a security promise.
   This is slightly more API than a single open/draw call, but shares font
   bytes across sizes and makes failures and positioning visible.

2. **Budgets and cache lifetime — default: caller-visible bounded settings.**
   Start with a 32 MiB font-file budget; per-size defaults of 256 cached
   entries and 2 MiB total cache allocations; a 4 MiB live rasterization-scratch
   budget; maximum requested height 512 pixels. These are initial configurable
   library options, not font-format/ABI limits or global quotas. No arbitrary
   cap on the number of live faces/sizes: each is charged to caller memory.
   Validate options, products, strides and coordinates before allocation.

   Cache by glyph ID for that size with least-recently-used eviction, counting
   empty/missing glyphs and metadata too. A glyph exceeding the configured
   cache/scratch budget returns LIMIT; actual allocation failure is NO_MEMORY.
   Never silently raise a budget. Audit stb's allocation-failure paths and use
   its allocator hooks to bound scratch, including failure cleanup; detecting
   allocation failure must not produce successful blank text. The coverage
   pointer is borrowed only until the next glyph-producing operation on that
   size (including a failed one), cache clear or destruction. Immediate drawing
   needs no pin/release API; retaining coverage requires a caller-owned copy.
   Different sizes have independent caches. The tradeoff is caller copying for
   retained glyphs rather than longer-lived borrowed pointers.

3. **Text and blending — default: replacement, single-line layout and one helper.**
   Invalid UTF-8 consumes one invalid byte and substitutes U+FFFD; reject
   overlong encodings, surrogates, truncated encodings and values above
   U+10FFFF. A missing scalar uses U+FFFD, then glyph zero; if neither supplies
   visible replacement ink, draw a small outline box with a nonzero advance.
   A supported blank glyph such as space stays blank. No cross-font fallback,
   normalization, bidi, ligatures or combining-mark placement: nominal font
   glyph/advance/kerning only. CR/LF/tab and other controls return UNSUPPORTED
   without advancing; callers split lines and choose tab stops explicitly.

   `font_draw_xrgb8888` uses that same run loop, with a destination descriptor
   containing capacity, dimensions, byte stride and the three channel shifts.
   Honor the native display's distinct byte-aligned 8-bit RGB shifts, rather
   than assuming one channel order. Clip signed glyph origins before address
   arithmetic. Coverage is linear opacity; decode opaque foreground/background
   sRGB, blend in linear light, then encode. Preserve unused bits. No alpha
   destination, LCD subpixel colour, shaping or broader colour management.
   Validate the descriptor before drawing; a later glyph failure may leave a
   rendered prefix and returns its real error, with no atomic-draw promise.

### Delivery after acceptance

1. Pin and vendor `stb_truetype.h` with its MIT option and provenance in
   pyxis-userland, build `libfont`, and publish the dependency PR first. Candidate
   pin from upstream HEAD inspected 2026-10-18:
   `2c980bb59875b0d32144a71867fbdebb2f77cd20` (header version 1.26).
   The owner mirror/cache must serve
   [this exact archive](https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz)
   before vendoring; no upstream fallback. Record SHA-256 and the licence from
   the mirrored source. [Upstream documentation](https://github.com/nothings/stb/blob/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_truetype.h)
   explicitly excludes malicious-font safety; the wrapper's budgets do not
   harden its unchecked offsets. No new kernel or compiler-container work.
2. Pin the published userland head and export the archive/header/licence through
   the SDK. Qualify a caller-selected trusted font through a temporary host
   read-only root in QEMU, recording the font version/hash and licence; do not
   ship the task 2 font collection early. Draw metrics/kerning, replacement,
   clipping and the RGB helper against a contrasting background, then inspect
   live objects and cleanup in GDB. Use ordinary builds and interactive checks,
   not a new test/benchmark framework. Establish cold/warm rendering/cache
   observations for this new path; no existing terminal path changes.

After task 1 the owner can: build a native graphics program with `-lfont` and
render a caller-selected trusted font in QEMU. Shipped family/style assets and
the hosted-Clang/native vela example remain tasks 2 and 3.
