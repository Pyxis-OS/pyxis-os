# Remote multiplexer resize qualification

## Revisions and setup

Pre-code baseline: Pyxis `94d0320d`, userland
`dca986b9581dea7bdad449c43b86f18edb7bb6c5`, ports `df3210b0`.
Initial implementation measurements used Pyxis `cef3b8c1` and userland
`781c480`. Interleaved pairs 1–2 and pending-HOST inspection used the same changes
rebased onto main `a0906f2f` / userland `ee9ccae`, at Pyxis `0126c258` and
userland `955b35f`. Pair 3 and final smoke used Pyxis `1d53c367`, based on
`a48ef0b3`, and userland `0a3113b`, based on `24f3140`, preserving the newly
merged scheduler fix and Remote child-launch configuration. The matched staging
configuration still kept the baseline's launch setting on both images.
Ports stayed unchanged. Ordinary `make -j16 image` and
`make -C tools remote` passed with builder `pyxis-llvm23.1.3-49e2c1a`.
No compiler-container rebuild, kernel mechanism, new klog line or committed
probe/test hook was needed.

QEMU 10.2.2, Q35, nested KVM, host CPU, four CPUs, 512 MiB, OVMF, standard VGA,
VirtIO SCSI CD, RNG and network; loopback NAT TCP 24831 → guest 2323.
Timed runs had no attached debugger or competing task build. Separate GDB
snapshots inspected native geometry, groups, readiness and backing. Both images
used the same ignored staging configuration: Remote `remote_multiplexer = true`,
ordinary local startup and initial 120×40 geometry. Packaged configurations stay
opt-out. Neovim checks separately enabled Remote `launch = true`, as required by
its bundle; HOST checks added a limited virtio-fs export with one 32 KiB fixture.
Reverse checks used an explicit beacon and loopback discovery forwarding.

## Plain launch and output

The same saved baseline client and inputs were used on both images:

```sh
# launch-input.txt: 1,024 lines of echo -n, then exit.
/usr/bin/time -f %e pyxis-remote --machine --no-shell-echo \
  --columns 120 --rows 40 127.0.0.1 24831 < launch-input.txt
# output-input.txt: the following Lua command, then exit.
# lua -e 'for i=1,1024 do print(string.rep("x",120)) end'
/usr/bin/time -f %e pyxis-remote --machine --no-shell-echo \
  --columns 120 --rows 40 127.0.0.1 24831 < output-input.txt
```

Initial before samples: 14.26–14.29 s for launches and 2.14–2.34 s for the
123,904-byte output workload. Initial after samples: 13.58–14.69 s and
2.45–2.59 s. Final interleaved pairs, with one workload at a time:

| Pair | 1,024 launches before / after (s) | Output before / after (s) |
| --- | --- | --- |
| 1 | 13.43 / 13.87 | 2.18 / 2.20 |
| 2 | 13.64 / 16.52 | 2.58 / 2.60 |
| 3 | 13.29 / 13.74 | 2.50 / 2.16 |

All runs reported 1,024 successful child results (or one Lua child), builtin
exit success and normal, drain-complete FINAL. Pair 2's slower launch sample
did not repeat. The shared nested-VM host also ran other guests; these ranges
do not isolate small costs or prove native performance. Inspection found no new
plain-session allocation, frame or wait interest; the unnegotiated path adds
only conditional checks. The isolated 21% slower pair is retained rather than
claimed as an improvement or a consistently reproduced feature regression.

## Latency, bytes and backing

Instrumented `socat -v` PTY captures and `ss -tinp` distinguish guest TCP bytes
from host terminal output. At idle 120×40, plain startup received 147 TCP bytes
and emitted 800 host bytes. Mux received 6,207 before and 6,231 after: the new
initial geometry frame adds exactly 24 bytes. Host startup remained 83,329
bytes because the identical initial applied geometry does not repaint.

Two before echo samples took 3.92–5.11 ms plain and 8.94–9.04 ms mux from input
to first host output. After samples took 5.70–7.13 ms plain; a fresh,
debugger-free mux took 8.35–9.60 ms over three samples. Host echo output was
164–179 bytes plain and 115–117 mux. An earlier mux pair reached 52–60 ms near
debugger/PTY activity; it is retained as variation, not used to claim a speedup.
These are instrumented nested-VM observations, not LAN or display acknowledgments.

GDB counted private mapped backing and global allocated physical frames, with
4 KiB pages; heap pools/history can retain allocations after shrink. An idle
120×40 mux stayed at 730 pages (2.85 MiB), its root shell at 341 pages, and the
daemon at 309 pages before/after. One two-pane mux grown to 512×256 used 4,314
pages (16.85 MiB), retained after shrinking. With two eight-pane mux connections
(400×80 and 120×40) plus two plain connections, global allocated frames were
26,609 (103.94 MiB). Growing the large mux through 12×5 to 512×256 peaked at
28,244 frames (110.33 MiB); that mux used 5,803 pages (22.67 MiB), versus 4,171
before resize. These observed peaks are not admission guarantees or quotas.

## Interactive and failure qualification

- Initial applied geometry precedes terminal records. Shrink/grow sequences
  120×40 → 30×8 → 1×1 → 200×60 and up to 512×256 preserved panes, restored
  layouts, split cwd and digit focus. Explicit 80×24 ceilings held at larger
  host sizes, followed 60×20 shrink, ignored transient 0×0, and restored 80×24.
- With explicit launch delegation, Neovim's TUI and same-image embedded child
  survived 60×20 → 400×80, insertion, Tab,
  `:wq` and a subsequent file read. RGB foreground/background, styles and
  supported single-cell UTF-8 (`é`, `─`, `│`) remained visible.
- Two mux connections with eight panes each coexisted with two successful plain
  sessions; a third mux was refused. Tiny geometry retained hidden panes and
  digit selection; growth restored them. Closing all clients reaped all groups
  and returned the daemon to listener-only readiness.
- A busy output pane and stopped host-reader proxy saturated bounded queues.
  SIGWINCH still observed changes. One resize remained outstanding and later
  sizes coalesced; after resuming the reader, the final 12×5 geometry applied.
  Native output reached its existing 65,536-byte bound. An extreme stalled
  close exhausted the existing five-second transport deadline and reported a
  missing FINAL; the daemon still completed native group cleanup.
- Reverse discovery connected with initial geometry, resized, split, echoed
  and closed with normal, drain-complete FINAL, then resumed discovery.
- Final rebased smoke repeated initial negotiation, 1×1 shrink, 200×60 growth,
  queued split retention, cwd inheritance and explicit close. GDB observed the
  applied outer generations, restored pane geometry and listener-only cleanup.
- A debugger paused only the virtio-fs data worker, leaving its control thread
  running. A published HOST read had `outstanding = 1` and `submitted = true`.
  GDB extended that one request's deadline, then induced an outer user fault.
  The sealed/stopping group remained incomplete with its child still present
  while the request was pending; transport expiry did not release admission.
  Resuming the worker completed the read and reclaimed the group; the daemon
  returned to listener-only readiness. This qualifies pending cleanup ordering,
  not unmodified HOST timeout duration or physical-host failure recovery.

Code inspection and independent review checked strict frame lengths/options,
request IDs, bounds, acknowledgment ordering, parser continuity across resize,
identical-geometry no-op and signal-pipe lifetime. Native resize acknowledges
the outer attachment, not mux redraw or host display. Already queued host bytes
cannot be clipped retroactively after physical shrink. Per-pane/screen resize
rollback is not a connection-wide allocation transaction; failure ends mux and
retains supervision through cleanup.

Plain/machine live resize, reattachment, pointer/clipboard and pane-aware transfer
remain outside this task.

## Owner native qualification

On 2026-10-11 the owner reported native horse/ThinkPad qualification on main
`a59a4ef3`: Neovim edit/save/quit in panes, a TCC hello world and instant live
resize. This closes the remote-multiplexer milestone. “Instant” is the owner's
functional observation, not a timed native latency measurement; failure/admission
and pending-HOST evidence above remains nested QEMU/GDB evidence.
