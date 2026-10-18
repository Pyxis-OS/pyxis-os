# Userland font rendering

`libfont` renders caller-selected trusted TrueType/CFF1 fonts into 8-bit grayscale
coverage masks. The kernel terminal keeps its bitmap font. There is no discovery
service, shaping, hinting, bidi, normalization, cross-font fallback or DPI scaling.
The image also carries the fixed font collection listed below.

Build against the exported SDK with `-lfont` and the ordinary native runtime
libraries. The public interface is [font.h](../../userspace/include/font.h).
The SDK includes the library, header and upstream notices. Existing hosted-Clang
bundles gain it only when rebuilt with that SDK; installing this source does not
change an already exported hosted sysroot. No compiler-container rebuild is needed.

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

Every ordinary live/installed image includes these unchanged static TrueType
files beneath `boot://share/fonts/`. Each path is chosen explicitly by the
caller; this is shared read-only data, not a discovery service or default-font
selection. No extra grant is needed beyond the existing `boot://` read root.

| Directory | File | Style |
| --- | --- | --- |
| `caskaydia-cove/` | `CaskaydiaCoveNerdFont-Regular.ttf` | Regular |
| `caskaydia-cove/` | `CaskaydiaCoveNerdFont-Bold.ttf` | Bold |
| `caskaydia-cove/` | `CaskaydiaCoveNerdFont-Italic.ttf` | Italic |
| `caskaydia-cove/` | `CaskaydiaCoveNerdFont-BoldItalic.ttf` | Bold Italic |
| `ibm-plex-sans/` | `IBMPlexSans-Regular.ttf` | Regular |
| `ibm-plex-sans/` | `IBMPlexSans-Bold.ttf` | Bold |
| `ibm-plex-serif/` | `IBMPlexSerif-Regular.ttf` | Regular |
| `ibm-plex-serif/` | `IBMPlexSerif-Bold.ttf` | Bold |
| `ibm-plex-serif/` | `IBMPlexSerif-Italic.ttf` | Italic |

Pins: Nerd Fonts v3.5.1, IBM Plex Sans 1.1.0 and IBM Plex Serif 2.0.0. The
[data recipe](../../ports/fonts/README.md) records archive SHA-256 pins, exact
member hashes and full licence/provenance details. It selects the plain NerdFont
variant, not Mono/Propo; text is monospaced but some icons have wider advances.
There is no subsetting, patching or renaming. Programming ligatures and other
shaped substitutions remain separate characters under the nominal run API.

Each family has its original OFL notice and a `source.txt` containing source
release, styles, byte counts and hashes. CaskaydiaCove also retains the full
mixed icon-source notices under `icons-notices/`; these include CC BY 4.0, MIT,
Apache 2.0, OFL, Bitstream Vera and Unlicense, not a blanket OFL claim. Font Awesome
Extension's MIT classification comes from pinned Nerd metadata; a separate
original-author licence file was not independently verified.

The collection is 33 files / 12,731,749 bytes: 12,629,928 font bytes, 94,098 bytes
of original notices and 7,723 bytes of provenance. The owner accepted 13 MiB as
a planning allowance, not a hard runtime/format limit. These bytes remain in the
uncompressed boot archive until the later shared-data relocation to the pool.

The minimal in-image C SDK includes `libfont.a` for explicit `-lfont` linkage;
font files themselves are runtime data and are not copied into a compiler
sysroot. Already exported hosted-Clang bundles still need their SDK refreshed
before linking libfont; an on-device Clang/example qualification is separate.
