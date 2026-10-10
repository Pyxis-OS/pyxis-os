# Native libuv task 4 qualification

Recorded 2026-10-10 for [Neovim task 4](../../../wip/neovim-libuv.md#task-4-preflight).
These are nested-KVM measurements and manual observations, not owner-hardware
results. Raw captures and scratch consumers remain in ignored
`build/libuv-baseline`, `build/libuv-after`, `build/libuv-qualification` and
`build/libuv-console-qualification`. No benchmark infrastructure, in-tree tests, CI jobs or
klog lines were added.

## Revisions and configuration

The before-code baseline used exact main `7766dae0` and verified
[CI #1666](https://git.internal/PyxisOS/pyxis-os/actions/runs/1666) artifacts.
Main then merged terminal/USB work. The final interleaved comparison below uses
main `4236efc7927a6229e49aaf8680c04fc3f5fe912c` from
[CI #1678](https://git.internal/PyxisOS/pyxis-os/actions/runs/1678), with userspace
`51bcb56b` and ports `7c33f3ff`, against task kernel/code `0e3ecd28`, userspace
`3dafe88fe57b0dadeaed5f85a5146459000d1a35` ([#196](https://git.internal/PyxisOS/pyxis-userland/pulls/196))
and ports `ede19861` ([#83](https://git.internal/PyxisOS/pyxis-ports/pulls/83)).
The final ports head `4326582` adds the synchronous libc mkstemp wrapper; it
changes neither the timed programs nor their kernel/runtime paths. An ordinary
`make -j16 image` rebuilt the submitted dependencies and image using the existing
`pyxis-llvm23.1.3-49e2c1a` builder; no compiler-container rebuild.

Integration refresh: endpoint readiness/network debugger main `036f3287` was
merged at `83d3e535`, with userland rebased to published `fbce73bf` (contains
main `6e12b5f4`) and ports unchanged at `4326582`. The ordinary image rebuild
passed, followed by one/four-CPU relay smoke checks: six child chunks, timers,
interleaved typed input and normal completion. Main `3d73637e` was then merged,
preserving the Renoir poll-cost work. The matched timings below remain tied to
the earlier revisions; they were not repeated for these integration merges.

Timed QEMU 10.2.2: Q35, nested KVM, `-cpu host`, 256 MiB, one or four CPUs,
standard VGA/headless, matching raw OVMF CODE/VARS, VirtIO RNG and modern
VirtIO networking over the user backend. No HOST mount, disk, profiler or
attached debugger in timed VMs. Each CPU configuration alternates before/after
ISO via QMP medium change and reset, preserving its OVMF variables. Fresh RAM
boots and identical commands; local Development uses session handoff for pipes.

## Matched existing workloads

A remote machine client sends 1,024 separate `echo -n` commands then `exit`;
`/usr/bin/time -f %e` records the whole session. Each sample has exactly 1,024
successful child completions, then shell exit and `drain=complete`. Two samples
per image/CPU alternate before/after; medians and ranges are seconds:

| CPUs | Before | After |
| --- | --- | --- |
| 1 | 21.21 (18.53–23.89) | 18.80 (18.39–19.20) |
| 4 | 14.51 (14.28–14.73) | 15.09 (14.97–15.21) |

Four-CPU whole-session time is about 4% higher in these two pairs; the ranges do
not overlap. One-CPU ranges overlap widely. This includes transport, command
framing, startup and final group drain, rather than isolated loader latency.
The echo image grows from 71,147 to 72,539 serialized bytes with the descriptor
bridge, but retains 24 eagerly backed image pages and a 96 KiB mapped span;
its stack remains 1 MiB. These observations neither attribute the four-CPU
increase nor establish an absence of smaller regressions or a speedup.

Existing iobench uses one warmup and five verified samples per invocation:

```text
session bin://iobench.pxe pipe --buffer 4096 --rounds 5
session bin://iobench.pxe pipe --buffer 64 --rounds 5
iobench read boot://share/iobench-small.bin --bytes 32768 --buffer 4096 --rounds 5
```

Pipe acceptance times producer descriptor writes; completion includes the
consumer acknowledgement. It is batch latency, not individual wake latency.
Every sample transfers/verifies 1 MiB, with no shorts or transfer errors and
correct EOF/reaping. Clock calibration is not subtracted. Median (range), ms:

| CPUs | Buffer | Before acceptance / completion | After acceptance / completion |
| --- | --- | --- | --- |
| 1 | 4096 | 1.352 (1.335–2.301) / 1.722 (1.698–2.677) | 1.357 (1.321–1.545) / 1.768 (1.698–1.938) |
| 1 | 64 | 7.991 (7.216–10.305) / 8.052 (7.397–11.459) | 7.431 (7.142–8.088) / 7.829 (7.741–8.448) |
| 4 | 4096 | 1.741 (0.227–4.364) / 2.383 (0.616–5.003) | 1.389 (0.829–2.348) / 1.456 (1.301–3.235) |
| 4 | 64 | 5.280 (4.879–5.879) / 5.654 (5.331–7.071) | 5.033 (4.718–5.405) / 5.520 (5.156–5.885) |

The 32 KiB read has eight full descriptor reads plus EOF each pass. Before/after
complete medians are 0.112/0.113 ms at one CPU (ranges 0.112–0.131 / 0.113–0.119)
and 0.113/0.113 ms at four CPUs (0.112–0.123 / 0.113–0.114). Pipe/file ranges
overlap; nested scheduling variation is substantial at four CPUs.

## Manual behavior and read-only debugger inspection

The shipped `boot://share/libuv/uv-relay.pxb` returns normally on one/four CPUs,
relaying six chunks 700 ms apart while its 200 ms timer and console input remain
responsive. Four-CPU typed input interleaves with chunks/timers. Its manifest
requests pipe creation explicitly; its plain child receives no pipe creator.

Scratch native consumers, using an optional HOST export, observed two async
callbacks: three initial sends coalesced, then a send from the first callback
ran on the next turn. UV_RUN_ONCE fired an already-due repeating timer once in
about 1.2 ms, rather than waiting for its next 100 ms repeat. Mutex busy/recursive
depth, once/key state and sole-thread equality matched the one-thread contract;
thread creation/join and PID access returned unsupported errors.

Synchronous file vectors wrote/read `abcdef`; fstat carried actual RAM metadata
validity. A callback-style open/truncate returned UV_ENOSYS and left those bytes
intact. Directory enumeration and cleanup succeeded. Final-port mkstemp created
one exclusive named file through libc, then close/unlink succeeded with actual
random/clock grants. A missing-image spawn stayed closable and restored admission.
Opening a 32nd stream returned UV_ENOSPC with its descriptor still caller-owned;
spawning with 31 reserved streams rejected before publication (observer zero).
Closing all handles left zero active handles, requests and interests.

Normal exit -1, a deliberate user fault, and native termination reported native
reasons 1, 2 and 3 respectively, status -1 and term_signal zero. Read-only GDB at
the fault callback saw an actual completed PROCESS observer, native result
FAULTED/status zero, no waiters and a cleared process-storage link; the libuv
callback retained reason 2/status -1/signal zero. Missing-spawn cleanup showed
observer/admission zero, empty loop queues and zero pipe readiness registrations.
No debugger function injection or kernel memory/register mutation.

Bounded terminal qualification used an isolated staged init (a scratch boot
init successor needs terminal/create); stock payloads and tracked configuration
were preserved. Fifteen 4096-byte writes plus a 3840-byte short write filled the
64 KiB record queue. The next try returned EAGAIN and no writable readiness.
GDB saw output_count=65536 and no blocking writer registration. A libuv write
stayed queued while a timer drained one 4112-byte record, then completed once
and closed cleanly. Final EOF followed 17 records/69,376 data bytes. Empty input,
END_INPUT and hangup returned the expected EAGAIN, EOF, PEER_FIN, WRITE_CLOSED,
ERROR and EPIPE outcomes. Failed adoption preserved ownership; successful
adoption consumed it. A real pipeline retained 18 libc read-ahead bytes after
the native queue reported only peer closure, and descriptor try-read delivered
those exact bytes. FILE try-read returned ENOTSUP without moving its cursor.

No kernel panic observed. This is bounded path qualification, not exhaustive
race, mux/remote-libuv or physical-host validation. Existing consumers were not
converted. The source-only early Neovim pool audit and remaining consumer/API
gaps are in the milestone and adapter references; no Neovim/luv runtime claim.
