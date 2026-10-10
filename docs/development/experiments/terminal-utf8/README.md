# Terminal UTF-8 qualification

For the [bounded repertoire](../../../userland/terminal.md#tty-output-controls)
and [owner assignment](../../../wip/neovim-groundwork.md#terminal-utf-8-first-slice).

## Method

Before-code baseline: main `917f6a8a`, userland `796087b`, ports `14347d5`.
After main's unrelated Neovim/libuv update, matched control is `02e91f13`,
userland `796087b`, ports `5e98894`; exact-head CI #1796 supplies verified
component artifacts. Changed source is `26c6a137`, userland `f90f109`, with the
same ports/fs/lwIP pins. Ordinary `make -j16 image` passes with the existing
`pyxis-llvm23.1.3-49e2c1a` builder; no compiler rebuild or new mirror.

QEMU 10.2.2, q35, nested KVM, CPU max, four cores/one thread each, 8 GiB,
UTC RTC, standard VGA 1280×800, VirtIO SCSI CD/RNG/network, fresh OVMF variables.
Both local fixtures add the same Mux space and Remote launch authority;
shipped configuration is unchanged. Tab is 160×48, pane 160×46.

Reuse the [SGR workload](../terminal-sgr/README.md#revisions-and-method) unchanged
(SHA-256 `2349da985f3f65298ed14ad9715b224de3afe91a36514e4256b5e21602ddc165`):
`terminal-bench scroll|redraw|alternate|rows LABEL`. Preparation and results are
outside the native monotonic write interval. Three interleaved control/changed
fresh-boot pairs repeat each workload in each layer. CPU windows cover eight
seconds of whole-QEMU ticks at 100 Hz, including queue drain and background work.
No build, debugger or extra client runs during timing. Pane elapsed mainly
measures queue acceptance. Raw captures, logs and manual fixtures remain local.

## Costs

Median (range), milliseconds; CPU columns are host ticks per eight-second window.

| Workload | Before ms | After ms | CPU before | CPU after |
| --- | ---: | ---: | ---: | ---: |
| Tab scroll | 2465.516 (2459.154–2489.081) | 2482.666 (2463.743–2504.025) | 346 (335–358) | 338 (337–340) |
| Tab redraw | 50.027 (49.907–50.062) | 50.194 (49.640–50.619) | 67 (59–96) | 66 (61–68) |
| Tab alternate | 229.146 (225.105–230.624) | 231.345 (227.736–239.730) | 102 (99–106) | 103 (98–106) |
| Tab rows | 1104.684 (1103.482–1110.549) | 1115.436 (1101.383–1115.747) | 257 (253–261) | 256 (256–261) |
| Pane scroll | 103.820 (99.142–111.762) | 94.251 (93.939–101.981) | 78 (77–111) | 73 (73–76) |
| Pane redraw | 10.671 (10.077–14.104) | 12.159 (9.253–13.772) | 68 (62–77) | 65 (62–69) |
| Pane alternate | 0.036 (0.036–0.037) | 0.036 (0.035–0.041) | 63 (57–81) | 67 (64–102) |
| Pane rows | 0.130 (0.120–0.163) | 0.037 (0.034–0.050) | 69 (62–85) | 67 (60–101) |

All 48 records have the expected byte counts and complete successfully.
Tab medians rise 0.3–1.0%, with overlapping ranges. The second alternate pair
is 4.6% higher, but the other two rise 0.3% and 1.2%; the three-pair median
increase is 1.0%. No large consistent tab regression is measured.

Pane redraw's median rises 1.488 ms / 13.9%, despite two of three paired
samples being lower after and overlapping ranges. Scroll/row acceptance
medians fall, also without establishing an isolated rendering improvement.
Whole-QEMU CPU windows include scheduling, composition and background work.
No isolated cause, equivalence or stable speedup is claimed. The shared codec
adds processing while cell and raster copy volumes remain unchanged.

## Manual qualification

Tab/pane pixels and read-only GDB confirm the 11 box glyphs, four arrows and
77 Latin-1 scalars, with 19 replacements for missing Latin-1. The distinct
outlined placeholder differs from `?`; `ÿ` keeps its real glyph. Unsupported
combining/CJK/supplementary scalars and malformed sequences have the expected
one-per-scalar/invalid-byte cell counts. A three-write vertical glyph decodes
once; interrupted prefixes and explicit fresh-line boundaries flush pending
bytes. `E0 80` displays two replacements while the program remains blocked,
before a newline or exit. Unicode survives the alternate-screen roundtrip.
Tree renders connected branches. Tab and mux Copy each publish the selected
23 UTF-8 bytes, marked non-ASCII; safe Paste refuses them without inserting text.
Copy and alternate-screen snapshots are at `de2f9600`; their paths are unchanged
by the strict-prefix refinement at `26c6a137`.

Interactive remote PTY output includes all 92 supported non-ASCII scalars;
fixture replacements total 39 and the split/fresh-line example totals six.
An impossible prefix is visible before input resumes the program. Both machine
output and an approved transfer preserve 509 bytes, SHA-256
`e207032606de8cb72adaf7efdcb2ce5e32229b1d7a505f0ccec3334dde3d6bf4`.
Machine exit is zero with complete draining; interactive close is acknowledged.
Host glyph appearance was not viewed.

Cells remain 12 bytes; each stream adds eight bytes of decoder state. Screen/
history copy volume, 1,024-row history and cell creation/resize backing/rollback
are unchanged by inspection. Byte-limit and allocation-failure paths are inspected, not forced.
Resize-state preservation is inspected. These are nested-VM measurements and
manual checks, not native ThinkPad performance or broader Unicode support.
