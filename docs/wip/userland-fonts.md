# Userland fonts

Status: **task 1 delivered for review, 2026-10-18.** Tasks 2–3 retain their
own short proposal gates.

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

- [x] **1. libfont.** Vendored pinned stb_truetype, implemented the library/header
  and SDK export, and rendered a trusted test font in QEMU. See the
  [implemented interface](../userland/fonts.md) and
  [qualification](../development/experiments/userland-fonts/README.md).
- [ ] **2. Fonts.** Fetch the pinned font releases with SHA-256 through the
  download cache, stage the chosen styles under `boot://share/fonts/` with
  their licences, and record the size.
- [ ] **3. Example and hosted check.** A short example in
  `boot://share/examples/` drawing each family, built natively with hosted
  Clang and `-lfont` on vela.

## Task 1 — accepted 2026-10-18, implemented

The owner accepted all three task-1 defaults:

1. **Explicit face/size handles and `enum font_result`.** Faces own copied file
   bytes, sizes retain faces, and positioned glyphs separate bitmap origins from
   the successor pen. Metrics and pen coordinates use checked signed 26.6 pixels.
2. **Caller-visible bounded budgets and an LRU cache per size.** Initial defaults
   are 32 MiB font bytes, 256 entries / 2 MiB cache allocations, 4 MiB scratch
   and 512-pixel height. Coverage is borrowed until the next glyph-producing
   operation on that size, cache clear or destruction.
3. **Replacement and single-line runs.** Invalid UTF-8 consumes one byte as
   U+FFFD; missing glyphs use visible replacement ink or an outline box. Controls
   refuse without advancing. One clipped XRGB8888 helper blends in linear light
   with explicit channel shifts. No shaping, DPI scaling or terminal changes.

The [reference](../userland/fonts.md) defines the full ownership, error, budget,
positioning and drawing contract. The pinned stb_truetype 1.26 commit is
`2c980bb59875b0d32144a71867fbdebb2f77cd20`; its exact archive was served by the
owner's download cache, SHA-256
`9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515`.
The vendored MIT notice and adaptations are exported with the SDK. Fonts must
remain trusted; allocation budgets do not harden the parser or bound CPU/stack.

After task 1 the owner can build a native graphics program with `-lfont` and
render a caller-selected trusted font. Shipped family/style assets and the
hosted-Clang/native vela example remain tasks 2 and 3; an existing hosted payload
must be rebuilt to gain the library. No compiler-container rebuild is needed.
