# Userland fonts

Status: **planned by the owner, 2026-10-17; an agent implements it (owner, not
yet assigned to one).** The decisions below are accepted. Each task still starts with a short proposal of its open details;
nothing here authorizes code.

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
6. **The caller draws; no callbacks.** The library hands out positioned glyphs
   and the program's own loop draws them, so any framebuffer or pixel format
   works without the library calling back into the program:

   ```c
   struct font_glyph glyph;
   const char *cursor = text;
   int32_t pen_x = x;

   while (font_next_glyph(font, &cursor, &pen_x, &glyph)) {
     blit_coverage(&screen, pen_x + glyph.left, baseline - glyph.top,
         glyph.coverage, glyph.width, glyph.height, glyph.stride, colour);
   }
   ```

   A glyph's coverage is an 8-bit mask, 0 for no ink and 255 for full ink,
   independent of any pixel format. For the common case the library also ships
   one helper built on the same loop, which blends in linear light into an 8-bit
   XRGB buffer:

   ```c
   font_draw_xrgb8888(font, pixels, stride, width, height, x, baseline, text, colour);
   ```

   Names and signatures here are illustrative; task 1's proposal settles them.

## Hosted Clang

`libfont` and its header are exported by `make sdk` like the other userland
libraries, so the hosted payload producer copies them into each bundle's
`sysroot://`. A program on Pyxis then links with `-lfont`. The installed Clang
only gains it with the next hosted payload. The [clangd milestone](clangd-neovim.md)'s
payload does not wait for `libfont` (owner, 2026-10-17), so `libfont` arrives
with a later payload update.

## Tasks

- [ ] **1. libfont.** Vendor stb_truetype with its pin and licence, write the
  library and its header in pyxis-userland, export it through the SDK, and
  render a test string in QEMU.
- [ ] **2. Fonts.** Fetch the pinned font releases with SHA-256 through the
  download cache, stage the chosen styles under `boot://share/fonts/` with
  their licences, and record the size.
- [ ] **3. Example and hosted check.** A short example in
  `boot://share/examples/` drawing each family, built natively with hosted
  Clang and `-lfont` on vela.

## Open details for the proposals

- API names, error results, cache bounds and how a program frees fonts.
- The helper's blending assumes today's 8-bit XRGB framebuffer with an sRGB
  transfer; coverage masks stay format-independent so deeper colour formats can
  get their own helpers.
- Invalid UTF-8 and missing glyphs (a replacement glyph or a box).
