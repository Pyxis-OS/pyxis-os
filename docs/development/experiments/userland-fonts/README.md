# Userland font task 1 qualification

2026-10-18; nested QEMU, not native hardware or a hosted-Clang qualification.
Parent `f489b56818a6e838aa60c39a026d03045e29dd26`, userland
`3ff5d934a14185267ab1514e35bc7039d69ae1ed`, ports
`3ec6e2d40d8a4147a6993b850d7f4bf7b28b7b1a`; compiler container
`pyxis-llvm23.1.3-abc00d0`. Subsequent parent changes only record these results.

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
TTF exercised; CFF support and allocation-failure unwinding were source-reviewed,
not native/hostile-font/fault-injection qualified. Finite channel-order comparisons
are not a comprehensive colour-management qualification. No font discovery,
shaping or native vela result is claimed.

Raw caller sources/binaries, screenshots, GDB transcripts and build logs remain
at `/shared/present/libfont-20261018/`; no raw captures or font files are committed.
