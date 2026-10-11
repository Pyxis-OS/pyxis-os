# Remote multiplexer tasks 1–2 qualification

## Revisions and configuration

Pre-code baseline: Pyxis `69047eee`, userland `d560fbb`, ports `df3210b0`.
Implementation qualification used userland `2dacca2`; its broker/renderer changes
were rebased without conflicts. Final source build and smoke checks used Pyxis
`da6d2175` (based on `d124a053`) and userland `e75691ed`, preserving main's
published `842b079` hosted-Clang support. Ports stayed unchanged. Builder:
`pyxis-llvm23.1.3-49e2c1a`; ordinary `make -j16 image` and `make -C tools remote`
passed. No compiler-container rebuild or new klog lines.

QEMU 10.2.2, Q35, nested KVM, host CPU, four CPUs, 512 MiB, OVMF, standard VGA,
VirtIO SCSI CD, RNG and network, NAT loopback TCP 24821 → guest 2323; no HOST
mount or installed disk. Timed workloads had no attached debugger or competing
task build. Separate GDB snapshots inspected native groups, readiness and
private backing. Mux checks used an ignored staging configuration with
`remote_multiplexer = true` on Remote and `multiplexer = true` on Development;
packaged live/installed configurations remain opt-out. Initial geometry was
120×40, with an additional 400×80 admission/backing check.

## Matched plain costs

Use the same saved baseline host client and input on each image:

```sh
# launch-input.txt contains 1,024 lines of echo -n, then exit.
/usr/bin/time -f %e pyxis-remote --machine --no-shell-echo \
  --columns 120 --rows 40 127.0.0.1 24821 < launch-input.txt
# output-input.txt contains this Lua command, then exit:
# lua -e 'for i=1,1024 do print(string.rep("x",120)) end'
/usr/bin/time -f %e pyxis-remote --machine --no-shell-echo \
  --columns 120 --rows 40 127.0.0.1 24821 < output-input.txt
```

Pre-code launches took 13.00–13.26 s; the 123,904-byte output workload took
2.48–2.50 s. Later interleaved before/after pairs, with one workload at a time:

| Pair | 1,024 launches before / after (s) | Output before / after (s) |
| --- | --- | --- |
| 1 | 14.01 / 13.98 | 2.31 / 2.36 |
| 2 | 14.38 / 14.49 | 2.15 / 2.58 |
| 3 | 16.53 / 14.80 | 2.57 / 2.16 |

Pair 1's after image used rebased `ae0fd9e`; pairs 2–3 used final `e75691ed`.
The intervening changes affect uncertain broker errors, not the plain path.
All launch records reported 1,024 successful child results, builtin exit success
and normal/drain-complete FINAL. Output runs reported successful Lua and FINAL.
An overlapping-build sample was excluded. These ranges show no consistent
plain-path regression; they do not establish an improvement or native throughput.

After matched workloads, daemon private backing was 304 pages before and 309
after (+20 KiB, 4 KiB pages). Global allocated frames were 11,600 and 11,553,
respectively; upstream SDK/image changes and heap caching also affect these
whole-system counters, so their difference is not a feature allocation cost.

## Presentation and backing

On final 120×40 images, idle startup received 147 TCP bytes for a plain prompt
and 6,207 for mux, measured by Linux `ss -tinp`. Host terminal output through
`socat -v` was 800 and 83,329 bytes respectively: the current host renderer emits
positions per cell, so host output is a separate expansion from guest wire bytes.
Two single-character echo samples received 92/94 wire bytes plain and 63/63 mux.
Instrumented input-to-first-host-output was 5.62–7.29 ms plain and 7.62–8.39 ms
mux; last output was 13.99–16.12 and 8.60–9.38 ms. One idle-cwd split produced
its first host output after 27.98 ms, finishing its 4,048-byte host-output burst
at 84.87 ms. This includes host logging and loopback/nested-QEMU costs, not
physical display acknowledgment or a LAN latency guarantee.

One final 120×40 remote mux used 730 private pages (2.85 MiB), plus 341 pages
for its root shell. At two mux connections, eight panes each (120×40 and
400×80), plus two plain connections, daemon readiness used 30 interests; each
mux used 17. Mux private backing was 1,896 and 5,312 pages (7.41 and 20.75 MiB),
including retained history widths after splits. Whole-system allocated frames
peaked at 29,324 (114.55 MiB); after disconnect they were 15,707 (61.36 MiB).
All remote groups were reclaimed; kernel heap pools remained cached. These are
observed backing counts, not quotas or maximum-geometry admission promises.

A manual wire peer stopped OUTPUT reads with a 4 KiB host receive buffer.
The unmerged blocking-renderer control `c2316fa` parked inside console output;
a queued split left one pane. The new renderer retained a partially accepted
packet and created two additional panes with that receive window closed.
Both controls returned the daemon to listener-only state after disconnect.
Input, pane draining and supervision therefore continue behind output stalls;
synchronous launch and the existing cwd snapshot timeout can still delay them.

## Functional and failure checks

Interactive pyxis-remote and separate GDB inspection checked initial HELLO
geometry, vertical/horizontal splits, BSP/equal layouts, stable digit focus,
relative file sharing and a seven-directory cwd chain retaining actual grants.
A busy pane fell back to `tmp://` after the existing snapshot budget. History
browsing, bracketed host paste, supported UTF-8/RGB/styles and OSC discard worked;
stock xfer refused before protocol emission. Two mux connections with eight
panes each coexisted with two plain connections; a third mux request was refused.
Opt-out mux requests were rejected and ordinary remote/local startup continued.

Normal root exit/dismissal, explicit host close, stopped-reader disconnect and
a debugger-induced outer user fault reclaimed tracked groups. Manual debugger
checks also covered expiry before launch, result-grant delivery failure, and a
final ambiguous launcher reply injected after a successful child launch. The
latter two ended mux with `CALL_OUTCOME_UNKNOWN`, without a new split retry;
all remote groups disappeared and the daemon returned to listener-only wait.
No failure hook or probe code is committed. Private captures stay local.

Native horse/ThinkPad results, reverse-mode mux, live remote resize, HOST-delayed
cleanup, maximum geometry and native pointer/clipboard are not qualified here.
Live resize remains [task 3](../../wip/remote-multiplexer.md#tasks), unassigned.
