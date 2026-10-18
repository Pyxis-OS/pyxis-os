# Userland font task 1 qualification

2026-10-18; nested QEMU, not native hardware or a hosted-Clang qualification.
Parent `f489b56818a6e838aa60c39a026d03045e29dd26`, userland
`3ff5d934a14185267ab1514e35bc7039d69ae1ed`, ports
`3ec6e2d40d8a4147a6993b850d7f4bf7b28b7b1a`; compiler container
`pyxis-llvm23.1.3-abc00d0`. Subsequent parent changes record results; the review follow-up below names its
new code revision.

## Inputs and build

The baseline parent `2ed89a24` / userland `00370cb` had no scalable userland
font library. Terminal bitmap rendering is unchanged, so there is no equivalent
before/after terminal benchmark. Cold/warm observations below describe the new
path only.

The exact pinned stb archive was fetched from the owner's cache and its hash
verified before vendoring; see [provenance](../../../../userspace/third_party/stb_truetype/UPSTREAM.md).
Fresh `make -j8 image` and SDK export passed with no compiler rebuild. A temporary
native caller linked against that SDK with `-lfont`; it and its input font are
outside the repository, not the task-3 example or task-2 shipped assets.

Trusted input: local Fedora `liberation-sans-fonts-2.1.5-15.fc44.noarch`,
Liberation Sans Regular, SIL OFL 1.1, 410712 bytes, SHA-256
`76d04c18ea243f426b7de1f3ad208e927008f961dc5945e5aad352d0dfde8ee8`.
The font and licence were exposed through a read-only virtio-fs server/root.

## QEMU and debugger results

Q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF, standard VGA, live archive;
read-only host root. From the Read-only tab:

```text
host://font-caller.pxe > tmp://font-report.txt
```

The caller completed with zero failed checks: two sizes (24/48 px), metrics,
AV kerning and explicit origins, strict invalid UTF-8 replacement, missing-glyph
replacement, stable END/unsupported-control/budget failures, clipping, swapped
RGB/BGR shifts, unused bits, row padding and guard preservation. Cache/scratch
budgets of one byte returned LIMIT. Rendering showed both sizes, baseline guides,
replacement boxes and intentional top/left clipping; Escape restored the terminal.

Read-only GDB hardware breakpoints, with no inferior function calls, observed:

- Two live sizes after caller face close: face references 2; 24-pixel cache
  29 entries / 6410 requested bytes; 48-pixel cache 21 entries / 11929 bytes.
- First size destruction reduced face references to 1; the surviving size still
  rendered. Last destruction reached `free` for the copied font bytes, then face.
- A second caller run with a two-entry cache requested A, B, A, C: the live list
  was C (glyph 38), A (36), with B evicted, consistent links and exactly two
  entries / 577 requested bytes. It also completed with zero failed checks.

## Cold/warm observations

Five pairs drawing the same 44-byte line at 24 px with default budgets, identical
background reset outside the timed call and cache clear before each cold call.
Clock reads bracket the whole library draw (layout, raster/cache and blending).
GDB attached after these samples, during cleanup, not during timing.

| Sample | Cold, microseconds | Warm, microseconds |
| --- | ---: | ---: |
| 1 | 240.40 | 151.35 |
| 2 | 239.48 | 148.91 |
| 3 | 271.11 | 165.75 |
| 4 | 237.67 | 152.83 |
| 5 | 239.33 | 148.79 |

These short nested-VM observations are not a throughput guarantee. Limits:
The initial run exercised TTF; allocation-failure unwinding was source-reviewed,
not native/hostile-font/fault-injection qualified. Finite channel-order comparisons
are not a comprehensive colour-management qualification. No font discovery,
shaping or native vela result is claimed.

Raw caller sources/binaries, screenshots, GDB transcripts and build logs remain
at `/shared/present/libfont-20261018/`; no raw captures or font files are committed.

## Review follow-up, 2026-10-18

Parent `df57abab3cd78ad909815c1c22561f25c3b73192`, userland
`cebad1145849f23bf2633c7c50cc5a85c6d42a91`; same compiler and QEMU setup.
Fresh image/SDK and temporary caller builds passed. The repeated caller completed
with zero failed checks and the same rendered frame, including the existing
metrics, kerning, UTF-8, cache, clipping and cleanup checks.

New finite checks passed: a Liberation copy with its `maxp` directory tag removed
returned BAD_FONT with a null face; Fedora Noto Sans CJK VF 2.004 (CFF2 collection,
SHA-256 `d3d8256cdec8dbcb3552284bc6b20c734dd60c2ee9df83b5758e34807c4bac32`)
returned UNSUPPORTED with a null face; Cantarell 0.301 Regular CFF1
(`a92f23cdd2b428b228aae94f2f25cf64a5d2c1287a4a3d638a3866fe52a2613c`)
still opened and rasterized A. A colour with a nonzero high byte returned
BAD_ARGUMENT and left the entire destination untouched. Open options now expose
only the file-byte budget; initialization allocates no scratch.

The correctness rerun's five cold/warm draw observations ranged 0.233–0.560 /
0.153–0.389 ms, with most samples near the earlier range. This was not a
controlled performance comparison; no hot-path optimization is claimed.
Screenshots and build logs use the `review-` prefix in the raw-capture directory.

## Font collection, task 2

2026-10-18. Data-only packaging; owner accepted the exact pins/styles/notices
and 13 MiB planning allowance in #962. Release and member pins live in the
[ports recipe](../../../../ports/fonts/metadata.lua), not a second expected-value
list. The final member check runs inside the recipe after copying and before
writing provenance.

Build: existing `pyxis-llvm23.1.3-3d69e83` container, `make -j8 image`, fresh SDK
and ordinary recipe builds. Source for final SDK/archive checks:
parent `75283c372992fcbfe61def7da6c59f2ebdc8c466`, ports
`7604c85dbdf7fd0e0cbcd0d162e5109f4d582e0e`, userland
`cebad1145849f23bf2633c7c50cc5a85c6d42a91`. The nine-style rendering run used
parent `5d7f6485` / ports `39d6b49`; subsequent code changes only added member
verification and the missing guest SDK archive, preserving font/notice bytes.

All three archive hashes and nine original font member hashes matched. The
staged set is exactly 33 files: nine fonts (12,629,928 bytes), 21 original notices
(94,098) and three generated provenance files (7,723), total **12,731,749 bytes**.
Both Plex ZIPs and the flat Nerd tar were extracted through the existing runner;
its default tar path was also exercised by the normal image's other recipes.
No upstream fallback, font compilation, patching, subsetting or renaming.

The matched size control repacked the same final tree with and without only
`share/fonts/`, using the image's sorted file list and reproducible newc options.
The full repack hash matched the built archive. With fonts: **101,843,456 bytes**;
without: **89,102,336**; delta **12,741,120**, including headers and padding.
This measures resource cost without attributing unrelated main changes or SDK
library bytes to the collection. The 13 MiB figure remains planning guidance.

Interactive QEMU: Q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF, standard VGA,
live archive, Read-only tab. The temporary task-1 caller pattern was reused;
only its executable came from a read-only host root. Every font path was the
installed `boot://share/fonts/...` path. Each of the nine styles rendered at
24/48 px and completed with zero failed checks (metrics/kerning, replacement,
invalid UTF-8, explicit limits, clipping, channel order and cleanup). All four
Nerd styles also produced a non-replacement glyph with coverage for U+F0001;
Plex showed its missing-glyph replacement as expected.

Final SDK wiring: an uninstalled, temporary C link check was compiled inside
QEMU using `tcc host://font-link.c -lfont -o tmp://font-link.pxe`. Running it
opened installed Plex Sans Regular and rasterized A (`font-link: ok glyph=33`).
Creating a directory beneath `boot://share/fonts/` was refused with permission
denied. The minimal guest SDK now contains the already built `libfont.a`;
this does not qualify the future hosted-Clang payload or native hardware.

No example was authored or shipped by this task; sonnet2 owns it. No new test
framework, benchmark or kernel instrumentation was added. Raw screenshots,
caller/source hashes, recipe logs and archive controls remain at
`/shared/present/libfont-task2-20261018/`; selected UI captures are also in
`/shared/present/screenshots-keep/font-collection/`. Qualification jobs stopped.
