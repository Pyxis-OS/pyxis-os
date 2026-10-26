# Userland font rendering

`libfont` renders caller-selected trusted TrueType/CFF1 fonts into 8-bit grayscale
coverage masks. It is a small wrapper in userland around a pinned, vendored
[stb_truetype](https://github.com/nothings/stb/blob/master/stb_truetype.h)
(public domain or MIT; TrueType and CFF outlines). The kernel terminal keeps its
Bizcat bitmap font; scalable terminal fonts remain a
[later direction](../wip/later-os-directions.md#neovim-follow-ups), since font
parsing does not belong in the kernel. There is no discovery service, shaping,
hinting, bidi, normalization, cross-font fallback or DPI scaling. The `fonts`
package carries the fixed font collection listed below.

Build against the exported SDK with `-lfont` and the ordinary native runtime
libraries. The public interface is [font.h](../../userspace/include/font.h).
The SDK includes the library, header and upstream notices. A program built on
Pyxis with [hosted Clang](../development/hosted-tools.md) links it the same way;
see [Hosted Clang](#hosted-clang). No compiler-container rebuild is needed.

The [font-min example](../development/edit-build-run.md#try-the-pointer-example)
opens each shipped style, draws a line with the helper and one by hand, and
follows the pointer.

## Ownership and errors

`font_open(path, options, &face)` uses the caller's ordinary read authority,
retains a private copy of the bytes and closes the file. A collection's face is
selected by `face_index`, default zero. File extent changes during reading fail.
This is not a filesystem snapshot or permission to read another path.

Each `font_size_create(face, pixels, options, &size)` retains the face. The caller
may then close its face reference; the size remains usable. Destroying the last
size releases the bytes if the caller has closed the face. Closing/destroying a
null pointer is harmless. Callers serialize use and lifetime of shared objects.

Operations return `enum font_result`: OK, END, BAD_ARGUMENT, IO_ERROR, BAD_FONT,
UNSUPPORTED, NO_MEMORY or LIMIT. CFF2 and unsupported bitmap/SVG-only glyph
representations report UNSUPPORTED; missing `maxp` is BAD_FONT, rather than
using an invented glyph-count bound. END is iterator exhaustion. Creation clears its
output handle and unwinds on failure. A failed run step clears its output and
preserves its cursor, pen and previous glyph; the caller can explicitly retry.
Cache eviction can still occur. The library does not log.

The pinned [stb source and adaptations](../../userspace/third_party/stb_truetype/UPSTREAM.md)
use the MIT licence option. **Fonts must be trusted.** Outer format checks and
bounded allocations do not harden upstream's internal offsets, recursive
composites or CFF decoding. BAD_FONT is best-effort recognition, not a malicious-
file safety guarantee. Heap-scratch budgets do not bound parser CPU or stack use.

## Budgets and coverage

| Setting | Default | Scope |
| --- | ---: | --- |
| Font bytes | 32 MiB | One face's copied file |
| Cached entries | 256 | Each size, including blank/replacement glyphs |
| Cache bytes | 2 MiB | Each size's requested glyph allocations, including entry metadata |
| Scratch bytes | 4 MiB | Each size's live rasterization heap scratch, including adapter metadata |
| Maximum requested height | 512 pixels | Each size's configurable admission setting |

`font_open_options_default` and `font_size_options_default` initialize complete
options. Passing NULL selects defaults; explicit zero budgets are invalid.
Open has only a file-byte budget: font initialization does not allocate scratch.
Budgets never grow silently. A glyph that cannot fit returns LIMIT; an actual
allocation failure returns NO_MEMORY. Font tables use signed-int offsets, so a
file larger than `INT_MAX` also returns LIMIT. Checked dimensions and signed
26.6 coordinates impose their representation bounds independently of settings.
There is no global face/size quota; their objects and font copies use caller RAM.

The per-size cache is keyed by glyph ID, with least-recently-used eviction.
Coverage is borrowed until the next glyph-producing operation on that size,
including a failed operation, cache clear or destruction. Copy it to retain it.
Separate sizes have independent caches. The mask is byte coverage, zero for no
ink and 255 for full ink, with dimensions and byte stride independent of pixels.

## Positions and text

Metrics, advances, kerning and run pen/baseline coordinates use signed **26.6
pixels**. A requested size is ascent-to-descent height, excluding line gap.
`font_get_metrics` returns ascent, descent, line gap and nominal line advance.
`font_get_glyph` accepts a Unicode scalar; `font_get_kerning` accepts glyph IDs.
A glyph's integer bitmap origin relative to its pen is `(left, -top)`.

`font_run_begin` borrows counted UTF-8 and a live size. `font_run_next` applies
kerning before producing the current glyph's explicit integer `x,y`, then
advances the successor pen. Fractional positions are floored for the bitmap;
there is no subpixel-positioned raster cache. Draw at the returned origin,
not at the already advanced `run.pen_x`.

Invalid UTF-8 consumes one invalid byte as U+FFFD. A missing glyph uses visible
U+FFFD, then visible glyph zero, then a small outline box with nonzero advance.
`FONT_GLYPH_BOX` identifies that synthetic glyph; it has no kerning. A supported
blank glyph such as space remains blank. C0/C1 controls and Unicode line/paragraph
separators return UNSUPPORTED without advancing. Callers split lines and choose
tab stops themselves. Other text receives only nominal glyphs and the kerning
supported by stb's font-table reader.

## XRGB helper

`font_draw_xrgb8888` uses the same run loop. Its descriptor carries writable
capacity, dimensions, byte stride and three distinct byte-aligned RGB shifts
(0, 8, 16 or 24). Nonempty surfaces need aligned storage and enough capacity
through the last visible pixel. Empty surfaces draw nothing but still process
the run and report text/glyph errors. Text and live library objects must not
overlap writable pixel storage.

The colour argument is opaque `0xRRGGBB` sRGB; a nonzero high byte returns
BAD_ARGUMENT before drawing. Coverage is linear opacity;
foreground and background are decoded, blended in linear light and encoded
back to sRGB. Unused destination bits are preserved. Signed clipping precedes
address arithmetic. Invalid descriptors cause no drawing; a later glyph error
may leave a rendered prefix. Alpha destinations, LCD subpixels and deeper colour
formats need caller composition, not this helper.

For example, with a validated caller-owned surface and explicitly selected path:

```c
struct font_face *face = NULL;
struct font_size *size = NULL;
enum font_result result = font_open(path, NULL, &face);
if (result == FONT_OK) {
  result = font_size_create(face, 32, NULL, &size);
}
font_close(face);
if (result == FONT_OK) {
  static const char text[] = "Hello, Pyxis";
  result = font_draw_xrgb8888(size, &surface, 20 * 64, 50 * 64,
      text, sizeof(text) - 1, 0xf0f0f0);
}
font_size_destroy(size);
```

## Installed collection

The `fonts` data package supplies these unchanged TrueType/OpenType files.
Configure a network repository and run `pkg install fonts` on live or installed
systems; live installations last until reboot. Each path is chosen explicitly by the
caller; this is shared read-only data, not a discovery service or default-font
selection. Shells and plain programs read it as `pkg://fonts`; a bundle lists
`"packages": ["fonts"]` in its manifest, as `font-min` does, and a missing
package refuses launch.

| Directory | File | Style |
| --- | --- | --- |
| `caskaydia-cove/` | `CaskaydiaCoveNerdFont-Regular.ttf` | Regular |
| `caskaydia-cove/` | `CaskaydiaCoveNerdFont-Bold.ttf` | Bold |
| `caskaydia-cove/` | `CaskaydiaCoveNerdFont-Italic.ttf` | Italic |
| `caskaydia-cove/` | `CaskaydiaCoveNerdFont-BoldItalic.ttf` | Bold Italic |
| `ibm-plex-sans/` | `IBMPlexSans-Regular.ttf` | Regular |
| `ibm-plex-sans/` | `IBMPlexSans-Bold.ttf` | Bold |
| `ibm-plex-sans/` | `IBMPlexSans-Italic.ttf` | Italic |
| `ibm-plex-sans/` | `IBMPlexSans-BoldItalic.ttf` | Bold Italic |
| `ibm-plex-serif/` | `IBMPlexSerif-Regular.ttf` | Regular |
| `ibm-plex-serif/` | `IBMPlexSerif-Bold.ttf` | Bold |
| `ibm-plex-serif/` | `IBMPlexSerif-Italic.ttf` | Italic |
| `ibm-plex-serif/` | `IBMPlexSerif-BoldItalic.ttf` | Bold Italic |
| `noto-sans/` | `NotoSans-Regular.ttf` | Regular |
| `noto-sans/` | `NotoSans-Bold.ttf` | Bold |
| `noto-sans/` | `NotoSans-Italic.ttf` | Italic |
| `noto-sans/` | `NotoSans-BoldItalic.ttf` | Bold Italic |
| `noto-serif/` | `NotoSerif-Regular.ttf` | Regular |
| `noto-serif/` | `NotoSerif-Bold.ttf` | Bold |
| `noto-serif/` | `NotoSerif-Italic.ttf` | Italic |
| `noto-serif/` | `NotoSerif-BoldItalic.ttf` | Bold Italic |
| `noto-sans-mono/` | `NotoSansMono-Regular.ttf` | Regular |
| `noto-sans-mono/` | `NotoSansMono-Bold.ttf` | Bold |
| `roboto/` | `Roboto-Regular.ttf` | Regular |
| `roboto/` | `Roboto-Bold.ttf` | Bold |
| `roboto/` | `Roboto-Italic.ttf` | Italic |
| `roboto/` | `Roboto-BoldItalic.ttf` | Bold Italic |
| `roboto/` | `Roboto-ThinItalic.ttf` | Thin Italic |
| `roboto/` | `Roboto-Black.ttf` | Black |
| `noto-sans-cjk-sc/` | `NotoSansCJKsc-Regular.otf` | Upright Regular, CFF outlines |
| `emojione-color/` | `EmojiOneColor.otf` | Colour SVG emoji |

The `font-min` example draws only the first nine styles.

Pins: Nerd Fonts v3.5.1, IBM Plex Sans 1.1.0, IBM Plex Serif 2.0.0, and the
static hinted TrueType files of the `notofonts/latin-greek-cyrillic` releases
NotoSans-v2.015, NotoSerif-v2.015 and NotoSansMono-v2.014 (Noto Sans Mono has no
italic styles; chronium, 2026-10-21). The
[data recipe](../../ports/fonts/README.md) records archive SHA-256 pins, exact
member hashes and full licence/provenance details. It selects the plain NerdFont
variant, not Mono/Propo; text is monospaced but some icons have wider advances.
There is no subsetting, patching or renaming. Programming ligatures and other
shaped substitutions remain separate characters under the nominal run API.

Each family has its applicable original licence and a `source.txt` containing source
release, styles, byte counts and hashes. CaskaydiaCove also retains the full
mixed icon-source notices under `icons-notices/`; these include CC BY 4.0, MIT,
Apache 2.0, OFL, Bitstream Vera and Unlicense, not a blanket OFL claim. Font Awesome
Extension's MIT classification comes from pinned Nerd metadata; a separate
original-author licence file was not independently verified.

The collection has thirty fonts totaling 41,705,932 bytes, plus original notices
and generated provenance. The former 13 MiB planning allowance covered only the
first nine fonts; these bytes live in the package store, outside the boot archive.
[Package preparation](../../packages/README.md) retains all notices and provenance.

The additions accepted (chronium, 2026-10-24) remain in this single package:
Roboto 2.137 is the six unchanged files from blurgtext's pinned demo, under
Apache-2.0. Noto Sans CJK SC 2.004 is a single-face OpenType/CFF font under OFL-1.1;
its Simplified Chinese regional glyphs provide CJK coverage. Adobe EmojiOne Color
1.000 is the exact demo font, under MIT, with an OpenType SVG table; it requires
a renderer with SVG font hooks. These are not libfont coverage claims: its stb
profile does not render colour SVG. Plex's added italics come from the existing
pinned releases. Noto Sans Mono has no upstream italic and stays upright; the
selected CJK face is also upright. No face is renamed or synthetically slanted.

Font files are runtime data and are not copied into a compiler sysroot.

## Hosted Clang

`libfont` and its header are in `pyxis-dev`, so in `sdk://`, and a program on
Pyxis builds with `-lfont`.

Native result (chronium, 2026-10-21): on vela, an installed system (0.0.7a), the
`clang-hosted` package rebuilt from `81313e0e` built
`boot://share/examples/font-min.c` with `-lfont`. With the `fonts` package
installed (version `nerd3.5.1-plexsans1.1.0-plexserif2.0.0-noto2.015-notomono2.014`)
the built `font-min` ran and drew.

The current `boot://share/examples/font-min.c` copy is included only in
`DISTRIBUTION=full` development images; the default core archive omits it.

## Limits

- Fonts must be trusted; the parser is not hardened (see above).
- The `fonts` data lives in the package store.
- The Nerd Fonts icon sets carry their own licences, recorded with the font.
  Programming ligatures render as separate characters without shaping.
- The native check above was one build and run of the example on vela; no timing
  was recorded.
