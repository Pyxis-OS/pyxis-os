# Terminal SGR costs and qualification

For the [SGR follow-up](../../../wip/neovim-groundwork.md#terminal-sgr-follow-up)
and [terminal sequence table](../../../userland/terminal.md#tty-output-controls).

## Revisions and method

Baseline preceded implementation on main `2f34bb30`, userland `fbce73b`.
Matched runs use rebuilt control `441fcd4a` / userland `3bd6c21` and changed
`85a4e8a4` / userland `e742ccb`; both pin ports `be901cf`, filesystem
`b427df29` and lwIP `a1aadb91`. Later integration includes upstream runtime
changes; the figures below describe these frozen revisions.
The final image is built at `562533ad`, incorporating main `775502e4`,
userland `1162d729` and main's ports `f510e212`; tab/pane colour and plain
launch checks pass again. The SGR patch is unchanged after rebasing.

Ordinary `make -j16 image` uses builder `pyxis-llvm23.1.3-49e2c1a`.
QEMU 10.2.2 with the documented AHCI fix: q35, nested KVM, four CPUs, 8 GiB,
standard VGA 1280×800, display off; tab 160×48, one pane 160×46.
Both fixtures add the same fifth Mux space. Product configuration is unchanged.

The uncommitted console workload reconstructs byte counts from
[terminal-profile](../terminal-profile/README.md), whose original source is
unavailable. The same binary runs on both sides (SHA-256
`2349da985f3f65298ed14ad9715b224de3afe91a36514e4256b5e21602ddc165`).
Native monotonic timing brackets libc writes with short-progress handling;
preparation, prefill and results are excluded.

| Command | Timed work |
| --- | --- |
| `terminal-bench scroll LABEL` | 5,000 coloured lines; 550,000 bytes |
| `terminal-bench redraw LABEL` | 200 frames of 24 positioned rows; 468,607 bytes |
| `terminal-bench region LABEL` | 2,000 bottom-margin writes, rows 2–23; 192,017 bytes |
| `terminal-bench alternate LABEL` | 200 enter/leave pairs; 3,200 bytes |
| `terminal-bench rows LABEL` | 2,000 insert/delete-line pairs, rows 2–23; 12,016 bytes |

Three interleaved before/after boots give three samples per workload/layer.
CPU windows are 8 seconds of whole-QEMU ticks at 100 Hz, including background
work and queue drain. No debugger, build or extra client runs during timing.
Tab elapsed includes rendering; pane elapsed mainly measures queue acceptance.
Screenshots check drain; all 60 byte/write-count records match expectations.
Raw records and captures remain local.

## Results

Median (range), milliseconds; CPU columns are host ticks per window.

| Workload | Before ms | After ms | CPU before | CPU after |
| --- | ---: | ---: | ---: | ---: |
| Tab scroll | 2459.149 (2408.725–3061.561) | 2452.070 (2450.992–2971.162) | 354 (336–427) | 345 (334–460) |
| Tab redraw | 45.589 (45.175–46.001) | 46.624 (46.471–46.694) | 99 (91–104) | 85 (81–106) |
| Tab region | 463.575 (452.467–471.312) | 459.423 (456.060–465.686) | 142 (138–151) | 131 (129–140) |
| Tab alternate | 222.222 (216.678–230.163) | 227.852 (225.564–234.451) | 129 (124–136) | 118 (118–121) |
| Tab rows | 1089.696 (1083.766–1115.103) | 1099.639 (1094.310–1100.952) | 269 (269–279) | 268 (257–278) |
| Pane scroll | 97.879 (97.269–107.053) | 101.093 (95.382–102.165) | 115 (100–127) | 97 (96–109) |
| Pane redraw | 15.085 (10.627–36.303) | 9.839 (9.766–14.741) | 120 (95–168) | 89 (87–90) |
| Pane region | 2.760 (2.746–3.126) | 3.644 (3.572–3.727) | 109 (102–121) | 87 (83–90) |
| Pane alternate | 0.036 (0.036–0.052) | 0.036 (0.034–0.113) | 147 (102–151) | 86 (82–89) |
| Pane rows | 0.038 (0.037–0.120) | 0.038 (0.037–0.119) | 95 (93–96) | 85 (84–87) |

Tab scroll ranges overlap widely; the first pair's 21% slowdown did not repeat.
Redraw consistently rises 2.3%; alternate-screen median rises 2.5% and row-copy
0.9%, with overlapping ranges. Pane region acceptance rises 0.88 ms / 32%,
while CPU windows fall. Queue acceptance and background CPU do not establish
an isolated rendering regression or improvement. No measured cause is claimed.

Source inspection: whole-screen scroll moves 3,850,240 raster bytes unchanged;
cell copy rises 22,560 → 90,240 bytes, raising total copy volume 1.75%.
The 22-row region has the same ratio. Plain glyphs skip synthetic styling;
larger cells and colour resolution add work without attributing timings.

## Backing

Read-only GDB snapshots verify dimensions and full 1,024-row history at stride
160. Requested extents exclude allocator overhead.

| Backing | Before, bytes | After, bytes |
| --- | ---: | ---: |
| One kernel TTY, two screens | 46,080 | 184,320 |
| Mux pane, two screens | 58,880 | 176,640 |
| Mux history | 655,360 | 1,966,080 |
| History width array | 8,192 | 8,192 |
| Mux frame and previous frame | 61,440 | 184,320 |
| Mux total in this one-pane fixture | 783,872 | 2,335,232 |

Matched quiet snapshots with five TTYs and one pane show allocated frames
13,561 → 14,071: **510 pages / 2,088,960 bytes**. Capacity differs by three
pages, so the free-frame decrease alone overstates allocation growth.
Kernel heap live bytes rise 2,238,512 → 2,932,264. Pooling, slack and rounding
separate requested bytes from page growth; this is not peak resize memory.
Creation/resize retain eager allocation and rollback; failure unwinding was
inspected, not injected.

## Qualification and limits

Tab and pane PNG pixels/GDB verify all indices, representative RGB, independent
style clears, reverse, saved cursors and alternate roundtrips. Colon, truncated,
out-of-range and 17-parameter groups leave the preceding style unchanged.
Selection preserves styles; clipboard inspection verifies glyph-only copy.
Paste after focus changes was unavailable and is not qualified.

A separate one-CPU, 256 MiB GTK/VirtIO-GPU run verifies tab/pane grow/shrink,
cropping, blank exposure and alternate-screen restoration. Interactive remote
PTY controls verify palette/RGB/style output and malformed rejection;
`xfer` transfers 7,909 bytes unchanged (SHA-256
`abd59f20f579ba8c2ff9c7e33a8caf5bfd7da601a66daad42ac78951f20a8bac`).
This checks emitted controls, not the host font. Host saved-cursor/alternate
support remains outside its presentation subset.

These are nested-VM results, not native ThinkPad timings or panel qualification.
Quiet backing is fixture-specific; allocation rejection was not forced.
