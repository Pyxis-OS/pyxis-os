# blurgtext

The opt-in `blurgtext` 0.2.1 package runs CallumDev's upstream text-rendering demo over
SDL2/OSMesa softpipe. It retains the demo's text, layout, colours, styled spans,
UTF-16 cursor, SVG emoji and CJK fallback sample. Escape exits.

```text
pkg install blurgtext
blurgtext
```

The program depends on the single [fonts package](fonts.md#installed-collection).
It reads the exact Roboto 2.137 and EmojiOne Color 1.000 demo fonts from
`pkg://fonts`, and its native system-font backend discovers CJK and other
coverage there. It reads no host font path. Noto Sans CJK SC supplies the Chinese
fallback; unlike macOS CoreText, it uses the explicit package's regional glyphs.

The port keeps upstream's real bundled FreeType 2.15.0 and HarfBuzz 15.0.0,
accepted (chronium, 2026-10-24). It does not use Pyxis libfont in their place.
The exact source archives, hashes, licences and ordered patches are in the
[port recipe](../../ports/blurgtext/README.md). Reusable `freetype-dev` and
`harfbuzz-dev` packages remain later work; their verified pins and build profile
are recorded in the qualification record. Upstream 0.2.1 fixes the stock
system-font cache-entry lifetime issue reported as #7. The dependency pins and
three platform/build/demo patches are unchanged; no port-local core workaround
is added.

## Display and grants

The demo requests its upstream 800×600 GL canvas. SDL can aspect-fit that canvas
to other native content sizes; use matching content geometry for a 1:1 capture.
The bundle requests an 8 MiB stack and memory, threads, clock, display and
keyboard grants. Pointer is optional. It uses native SDL input/presentation and
the existing C++ runtime, without a new kernel interface.

The Pyxis sysfonts backend scans only its granted `pkg://fonts` and immediate
family directories. Matching uses ASCII-case-insensitive names, real italic preference
then nearest OS/2 weight, and character coverage. Generic sans-serif, serif and
monospace map to Noto Sans, Noto Serif and Noto Sans Mono. Unknown families fall
back to Noto Sans; missing characters search the remaining installed faces.
Regular-only faces can be emboldened for bold requests, without changing the
canonical Regular cache. Upright fonts are never reported as italic.

Font metadata uses a separate FreeType library, released by backend destruction.
The implementation bounds scans to 64 family directories, 512 font files,
32 MiB per font and 1,024-byte paths; these are profile settings, not font-format
limits. It accepts single-face TTF/OTF, including CFF and SVG OTF; collections
need an indexed loading API and are refused. Missing/invalid data or scan failure
fails enablement. These are trusted packaged fonts, not a hostile-font sandbox.

## Build and limits

```sh
make blurgtext-package REPOSITORY=/path/to/repository
```

This opt-in target requires a clean checkout, builds the pinned port and packs a
program for that build's ABI. Ordinary images do not build or embed the demo or
font assets. No compiler-container rebuild is needed.

The bundle retains upstream/dependency licences, provenance, local patches and
covered modified sources. FreeType uses upstream blurgtext's stb PNG shim;
EmojiOne uses its FreeType SVG hooks and plutosvg. No dynamic loader, fontconfig,
host font scan or PlutoVG mmap-based font-cache loader is supplied. Core font
manager/cache code and the public header remain upstream. The Pyxis backend
uses existing internal loading functions without modifying the family cache
during callbacks; demo startup, presentation, pacing and cleanup follow upstream. Separate
font-library packages and a wider font/discovery profile remain outside this port.

The [QEMU qualification](../development/experiments/blurgtext/README.md) records
the actual 1:1 upstream demo, native package launch and macOS reference comparison.
