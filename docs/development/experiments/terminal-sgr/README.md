# Terminal SGR costs and qualification

Qualification for the [terminal SGR follow-up](../../../wip/neovim-groundwork.md#terminal-sgr-follow-up)
and the shared [`pyxis` sequence table](../../../userland/terminal.md#tty-output-controls).
This changes retained cells and SGR interpretation in the kernel TTY, mux and
interactive host remote client. It adds no kernel logging or benchmark facility.

## Configuration and method

Initial baseline was captured on main `2f34bb30`, userland `fbce73b`, before
implementation. Main subsequently merged working-path/environment and cursor
inventory work. The interleaved comparison uses a rebuilt control at main
`441fcd4a`, userland `3bd6c21`, against `85a4e8a4`, userland `e742ccb`. Both pin
ports `be901cf`, filesystem `b427df29` and lwIP `a1aadb91`.

The final integration also incorporates subsequently merged upstream
stdio/descriptor, session-lifetime, math and Lua/luv work. The SGR patch is
unchanged after the dependency rebase. The matched cost and memory figures
below belong to the frozen revisions above; they are not new measurements of
the later upstream session/runtime changes.

Ordinary `make -j16 image` builds use the cached Pyxis Clang 23.1.3 builder,
`pyxis-llvm23.1.3-49e2c1a`. No compiler container rebuild is needed.
QEMU 10.2.2 with the documented AHCI fix runs q35, nested KVM, four CPUs,
8 GiB, standard VGA at 1280×800, display off. The tab is 160×48 cells and
one mux pane is 160×46. Both staged images add the same uncommitted fifth Mux
space; its init starts the text provider and session without configuring the
network again. Product boot configuration is unchanged.

The scratch console workload reconstructs the byte counts in the earlier
[terminal-profile experiment](../terminal-profile/README.md); the original
scratch source is unavailable, so these are fresh controls, not a reproduction
of its numerical results. Exactly the same linked workload binary is staged
on both sides: SHA-256
`2349da985f3f65298ed14ad9715b224de3afe91a36514e4256b5e21602ddc165`.
It prepares bytes before timing, uses libc `write` with short-progress handling,
and brackets output with the native monotonic clock. Preparation, prefill,
footer and result-file append are outside elapsed time.

| Command | Timed work |
| --- | --- |
| `terminal-bench scroll LABEL` | 5,000 coloured 100-character lines, 550,000 bytes |
| `terminal-bench redraw LABEL` | 200 frames of 24 positioned 79-character rows with erase-to-end, 468,607 bytes |
| `terminal-bench region LABEL` | 2,000 bottom-margin writes in rows 2–23, 192,017 bytes |
| `terminal-bench alternate LABEL` | 200 enter/leave pairs after untimed screen prefill, 3,200 bytes |
| `terminal-bench rows LABEL` | 2,000 insert/delete-line pairs in rows 2–23 after untimed prefill, 12,016 bytes |

Rounds alternate control and changed-image boots: b2 a2 b3 a3 b4 a4. Each boot
runs one sample of every workload in the tab and then one in the pane, giving
three samples per workload, layer and revision. Images reuse the same VM,
firmware variables, devices and boot delay. Each command also has an 8-second
whole-QEMU CPU window, in host ticks at 100 Hz. No debugger, extra guest client,
build or second qualification VM runs inside those windows. Native timing
records are pulled through the existing remote client between rounds; all
commands complete and the connection drains before reset.

Tab elapsed time includes kernel rendering. Pane elapsed time mostly measures
producer acceptance into the bounded session queue; its CPU window includes
later mux rendering and other guest/QEMU work. Screenshots after each window
check that output drained. The unchanged binary reports the expected byte and
short-write counts; its footer deliberately homes the cursor, so the next
prompt can erase the samples. Visual checks use the captured colour bytes
through `cat` without that footer instead.

## Results

Median and range in milliseconds; CPU is median and range of host ticks over
the 8-second window. Each entry has three samples.

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

All 60 timed records and their CPU windows are in [samples.csv](samples.csv).

The first scroll pair was 2,459 → 2,971 ms, but the next was 3,062 → 2,452 ms
and the third 2,409 → 2,451 ms. The initial 21% slowdown did not repeat; the
three-sample ranges overlap widely and establish no consistent scroll change.
They do not exclude a small regression. Redraw is consistently slightly slower
in these three pairs: median **+2.3%**. Alternate-screen median is **+2.5%**
within overlapping ranges; row-copy median is **+0.9%**. These small costs are
recorded without claiming a measured cause.

Pane region acceptance is about **0.88 ms / 32% slower** at the median, while
its CPU windows are lower. Those clocks measure bounded-queue progress rather
than isolated rendering, so the figures do not establish a rendering slowdown
or a CPU improvement. Other pane ranges overlap; redraw controls include a
36 ms sample. Whole-QEMU background variation is visible in the CPU windows.

Source inspection: one-row whole-screen scroll moves 3,850,240 raster bytes
plus cell data increasing from 22,560 to 90,240 bytes. Total copy volume rises
**1.75%**; the 22-row region has the same ratio. Plain glyphs skip synthetic
style processing. Larger cell writes and shared colour-resolution calls add
work, but this inspection does not attribute the timing differences.

## Backing

Read-only GDB snapshots outside timing validate actual dimensions, allocation
pointers, cell sizes and the full 1,024-row history at stride 160. These are
requested buffer extents, excluding allocator overhead:

| Backing | Before, bytes | After, bytes |
| --- | ---: | ---: |
| One kernel TTY, two screens | 46,080 | 184,320 |
| Mux pane, two screens | 58,880 | 176,640 |
| Mux history | 655,360 | 1,966,080 |
| History width array | 8,192 | 8,192 |
| Mux frame and previous frame | 61,440 | 184,320 |
| Mux total in this one-pane fixture | 783,872 | 2,335,232 |

The matched quiet post-workload snapshots at b2/a2 have all five framebuffer
spaces and the one pane running, with the result-pull connection closed.
Allocated physical frames increase from 13,561 to 14,071: **510 pages,
2,088,960 bytes**. Allocatable capacity differs by three pages between boots;
the 513-page free-frame decrease therefore includes that reservation difference.
Kernel heap live block bytes increase from 2,238,512 to 2,932,264. Heap pool
high-water bytes are separate from live backing. Requested buffer growth and
allocated-page growth differ because of allocator slack, pooling and rounding;
these snapshots do not isolate every allocation or measure peak resize memory.

All buffers remain eagerly allocated at creation/resize. Mux history retains
the maximum width seen; a wider resize stages new history and both screens
before publication. Kernel resize stages every TTY and selection row before
committing. Failure unwinding was inspected; no allocation failure was injected.

## Interactive checks

In a tab and pane, the colour sample shows all 256 indices, off-palette RGB,
independent bold/italic/underline clears, reverse, and saved-cursor and alternate
roundtrips. PNG pixels confirm the Aardvark first 16, cube endpoints and greys;
GDB finds all 256 stored background indices and RGB foreground `(7,91,173)` /
background `(43,17,67)` in both the pane and outer TTY. Indexed operand 7 and
RGB component 7 leave reverse off. Colon, truncated indexed, index 256, RGB
component 256 and 17-parameter sequences retain the preceding green combined
style as a whole. Saved styles retain those same flags.

Left-drag selection preserves styled glyph shapes in both terminals. Copy
publishes the selected glyph bytes, independently verified in the bounded
clipboard items; no colour tags enter the copied text. Paste after focus changes
was unavailable in this fixture and is not qualified by this task.

A separate GTK/X11 VirtIO-GPU run uses KVM, one CPU, 256 MiB and fresh firmware
variables. Tab and pane colour samples survive grow/shrink, with cropping and
blank newly exposed cells. With vi's alternate screen active, further growth
and shrink preserve the hidden primary; quitting restores its styled cells.
Observed guest sizes include 1000×753, 600×423, 1050×773 and 700×473. The
separate VM and debugger are stopped before timed runs.

The new interactive remote client runs with a real controlling PTY. Its emitted
RGB matches every cube/grey background and sampled Aardvark entries, its emitted
attributes match the styled words and independent clears, and all five invalid
groups retain the prior style. This verifies emitted controls, not the host
font's appearance. Host saved-cursor/alternate-screen support is unchanged;
the host client does not implement the full native sequence table.
`xfer send boot://colour-probe.bin` transfers 7,909 bytes through that client,
with identical SHA-256
`abd59f20f579ba8c2ff9c7e33a8caf5bfd7da601a66daad42ac78951f20a8bac`;
transfer controls are intercepted rather than presented as terminal text.

## Limits

These are nested-VM results, not ThinkPad timings or native panel qualification.
CPU windows include baseline presentation and guest background work. Quiet
memory snapshots describe this fixture, not a per-pane resident-memory guarantee
or a peak. Resize allocation rejection was reviewed, not forced at runtime.
