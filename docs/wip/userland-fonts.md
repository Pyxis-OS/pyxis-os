# Userland fonts

Status: **task 1 delivered for review; task 2 proposal below awaits the owner,
2026-10-18.** No task-2 implementation is authorized by this proposal.

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

## Task 2 proposal — pending owner acceptance

Ship the nine already accepted styles as unchanged static TrueType files. The
following release pins and byte counts were checked through the owner's cache
on 2026-10-18; this was source inspection, not guest rendering or image staging.
All nine have `glyf`/`loca` outlines and supported Unicode cmaps, without CFF2 or
variable-font axes. No font compiler, patcher, subsetting or renaming is needed.

| Family | Proposed release | Exact selected filenames | Font bytes |
| --- | --- | --- | ---: |
| CaskaydiaCove Nerd Font | [Nerd Fonts v3.5.1](https://github.com/ryanoasis/nerd-fonts/releases/tag/v3.5.1) | `CaskaydiaCoveNerdFont-{Regular,Bold,Italic,BoldItalic}.ttf` | 11,562,896 |
| IBM Plex Sans | [@ibm/plex-sans@1.1.0](https://github.com/IBM/plex/releases/tag/%40ibm/plex-sans%401.1.0) | `IBMPlexSans-{Regular,Bold}.ttf` | 401,372 |
| IBM Plex Serif | [@ibm/plex-serif@2.0.0](https://github.com/IBM/plex/releases/tag/%40ibm/plex-serif%402.0.0) | `IBMPlexSerif-{Regular,Bold,Italic}.ttf` | 665,660 |

Select the plain `CaskaydiaCoveNerdFont` files, not the separate `NerdFontMono`
or `NerdFontPropo` variants. The accepted family has monospaced text; some icons
can occupy more than one cell. This does not change terminal rendering. The Plex
files come from each archive's `fonts/complete/ttf/`, not its variable or subset
packages. Preserve the upstream family names and filenames.

### Source pins and cache

These exact cached archives are available, with measured SHA-256:

- [CascadiaCode.tar.xz](https://github.com/ryanoasis/nerd-fonts/releases/download/v3.5.1/CascadiaCode.tar.xz), 3,592,604 bytes:
  `ae598e9401e2846aa3ee364513715de0490b844a4b54f0768991f45f23aa8369`.
- [ibm-plex-sans.zip](https://github.com/IBM/plex/releases/download/%40ibm/plex-sans%401.1.0/ibm-plex-sans.zip), 9,921,777 bytes:
  `fb365d910566e6d199cc2c15579a7dd9a267128e18431a394ed81f1970c69200`.
- [ibm-plex-serif.zip](https://github.com/IBM/plex/releases/download/%40ibm/plex-serif%402.0.0/ibm-plex-serif.zip), 9,272,432 bytes:
  `c006b095c47b919c6c8d78319e9e628745033f5db94507c1c9dfe4b35dd51f1e`.

The cache URLs replace `https://github.com/` with
`https://repo.internal/repository/raw-github/` and retain the complete encoded
path. Downloaded bytes, including notices, came only from that cache. Builds
must verify the pins and fail clearly on cache absence or mismatch, never fetch
upstream or silently move to a newer release. Nerd notice sources use its
v3.5.1 tag, commit `b894ea7803af6aade63d60a4381e006098ec9c4d`, with source URLs
and individual notice hashes recorded alongside the recipe.

### Packaging and licences

Use a data-only recipe in pyxis-ports, reusing its existing archive/checksum and
extra-source machinery. Stage only the chosen nine font files, their notices
and a small provenance/style list; do not store font binaries in Git. The parent
assembles this shared data into every ordinary live/installed archive under:

- `boot://share/fonts/caskaydia-cove/`
- `boot://share/fonts/ibm-plex-sans/`
- `boot://share/fonts/ibm-plex-serif/`

Keep per-family licence/provenance files beside the fonts, with an icon-notice
subdirectory for CaskaydiaCove. Small verbatim notices may be vendored in the
recipe with their exact upstream pins/hashes. Record them in `LICENSING.md`.
The full-release input is not a reason to stage unwanted weights or variants.
Fonts stay read-only and are read through the caller's existing `boot://` root;
there is no new grant, font service, discovery API or default-font setting.
The SDK exports the library/header, not these runtime font files. Existing
hosted bundles remain unchanged until their later payload refresh.

Retain the Cascadia base OFL 1.1 and both Plex OFL 1.1 texts, copyright and
reserved-name statements. The already patched CaskaydiaCove release supplies
its upstream renamed family; we do not create another modified font. Nerd icons
have additional terms; describing every embedded glyph as OFL would be wrong.
Preserve the pinned Nerd root licence, release README, glyph-source table and
source notices:

| Component | Notice terms to retain |
| --- | --- |
| Codicons; Font Awesome icons | CC BY 4.0 |
| Devicons, Font Awesome Extension, Octicons, Seti/original, Powerline Extra/Symbols, IEC power symbols | MIT |
| Hack extra glyphs | MIT and Bitstream Vera notices |
| MaterialDesign | Apache 2.0 text and the upstream Pictogrammers statement |
| Pomicons; Weather Icons | OFL 1.1, including the reserved-name notice where present |
| Font Logos | Unlicense/public-domain dedication |

The [pinned glyph table](https://github.com/ryanoasis/nerd-fonts/blob/v3.5.1/src/glyphs/README.md),
[root licence](https://github.com/ryanoasis/nerd-fonts/blob/v3.5.1/LICENSE) and
[licence audit](https://github.com/ryanoasis/nerd-fonts/blob/v3.5.1/license-audit.md)
are the attribution/provenance starting point. Font Logos's table label
“unlicensed” refers to its actual
[Unlicense](https://github.com/lukas-w/font-logos/blob/v1.3.0/LICENSE), not missing
permission. Font Awesome Extension has no separate original licence file in
its tagged tree; its MIT classification comes from the pinned Nerd upstream
metadata. Preserve that declaration and provenance without claiming a separately
verified original-author grant. The Hack notice retains Bitstream attribution
rather than reducing it to the glyph table's abbreviated MIT label.

### Size and qualification

The nine files total **12,629,928 bytes (12.045 MiB)**. Collected notices add
94,098 bytes, for 12,724,026 bytes before the small authored provenance files
and newc path/header/padding overhead. This corrects the plan's rough 11 MB
estimate if the owner accepts these pins. Use **13 MiB as a planning allowance**,
not a new runtime/format limit or a validator frozen to today's release sizes.
The uncompressed archive retains these bytes in RAM; compressed source downloads
(22,786,813 bytes in all) do not reduce that residency. Font relocation to the
pool remains the already recorded later direction.

After acceptance: publish the ports data recipe first, then the parent pin and
archive integration. Build the default image and report exact staged/archive
byte deltas and member hashes. In QEMU, open all nine installed paths through
`libfont`, render their styles at two sizes, check nominal Latin/kerning and a
documented supplementary-plane Nerd icon, and exercise missing-glyph replacement.
Use the task-1 temporary caller pattern, not a new installed test framework.
Licence/provenance presence and byte-identical font copies are part of packaging
qualification. No hosted-Clang/native vela claim until task 3.

### Owner decision

**Pending; recommended default:** accept these three exact release pins, the
nine unchanged files with the complete notices/provenance above, and the measured
12.724 MB subtotal / 13 MiB planning allowance. Families and styles are already
accepted and are not being reopened. No task-2 code or image changes before the
owner answers.

After task 2 the owner can open each installed family/style by its ordinary
`boot://share/fonts/` path from a `libfont` graphics program, without supplying a
host font directory. Task 3 still supplies the example and hosted/native check.
