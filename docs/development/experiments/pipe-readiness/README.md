# Pipe readiness task 1 qualification

Recorded 2026-10-10 for [Neovim task 1](../../../wip/neovim-libuv.md#tasks).
These are nested-KVM observations, not physical-host performance. Scratch
fixtures, screenshots, serial output and debugger captures remain under ignored
`build/pipe-baseline`, `build/pipe-after` and `build/pipe-qualification`.
No consumer, benchmark infrastructure, klog line or CI job was added.

## Inputs and configuration

Before: exact-main `dc91a4c6c904e63e3d9bc4e137a079df57a1b268`, using verified
kernel/SDK/userspace/ports bundles and image from [CI #1634](https://git.internal/PyxisOS/pyxis-os/actions/runs/1634).
Its userspace is `8cbd9f87bdc74d587d1b4574aa5293c629628be0`.
After: native code measured at `aa48107a1e9fba0fd5bbb6465c8d7f4363c258af`,
rebased onto merged plan #650 as `a48a78e32c1c043335314cae4e85cbde2108d012` with identical kernel, ABI and
userspace trees; userspace `5aede1c22f932e6469c5e6a813975d4da4182785`
([PR #192](https://git.internal/PyxisOS/pyxis-userland/pulls/192)).
Ports remain `19fb10b05549cf132f567d33a9e7f097cb7e9956`, fs
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. Builder:
`pyxis-llvm23.1.3-49e2c1a`; no compiler-container rebuild.

An ordinary `make -j16 image` rebuilt kernel, SDK, userland and the image;
the final notification-only kernel rebuild used
`make -j16 image PREBUILT="sdk userspace ports"` after ABI identity checking.

QEMU uses Q35, nested KVM, `-cpu host`, 256 MiB, one or four CPUs, standard
VGA, OVMF raw CODE/VARS, VirtIO RNG and modern VirtIO networking over the user
backend. Timed runs have no HOST mount, disk, profiler or attached debugger.
The same VM alternates baseline and after ISO using a QMP medium change and
reset, retaining its OVMF variables. Every workload starts from a fresh RAM boot
and runs in the local Development space via session handoff. No remote-session
handoff or configuration alteration is part of the measurement.

## Interleaved blocking-pipe costs

A baseline was captured before implementation. After the final guard change,
each workload was repeated twice against each image, interleaving before and
after. These final runs supplement the initial baseline. Each invocation uses
the existing iobench warmup plus five measured, verified 1 MiB passes:

```text
session bin://iobench.pxe pipe --buffer 4096 --rounds 5
session bin://iobench.pxe pipe --buffer 64 --rounds 5
```

Acceptance measures producer descriptor writes; completion measures the
consumer's CALL acknowledgement. Completion is end-to-end batch latency,
including scheduling and acknowledgement, not individual transfer or wakeup
latency. Content verification and EOF/reaping are outside the timed interval.
The clock-call cost is reported by iobench and is not subtracted.

Values are per-invocation medians with sample minimum–maximum, in milliseconds.
Each row has five verified samples; bytes written and consumed are 1,048,576,
with no transfer errors or short operations. Positive short progress is
qualified separately below.

| CPUs | Buffer | Image/run | Acceptance ms | Completion ms |
| --- | --- | --- | --- | --- |
| 1 | 4096 | Before 1 | 1.336 (1.331–1.338) | 1.702 (1.696–1.703) |
| 1 | 4096 | After 1 | 1.337 (1.327–1.445) | 1.714 (1.692–2.452) |
| 1 | 4096 | Before 2 | 1.332 (1.327–1.346) | 1.697 (1.696–1.738) |
| 1 | 4096 | After 2 | 1.332 (1.323–1.338) | 1.704 (1.694–2.430) |
| 1 | 64 | Before 1 | 7.583 (7.279–8.292) | 7.960 (7.887–8.679) |
| 1 | 64 | After 1 | 7.424 (7.116–8.210) | 7.853 (7.681–8.569) |
| 1 | 64 | Before 2 | 7.587 (7.335–8.430) | 8.024 (7.955–8.797) |
| 1 | 64 | After 2 | 7.240 (7.130–8.061) | 7.840 (7.694–8.540) |
| 4 | 4096 | Before 1 | 0.326 (0.282–1.649) | 0.774 (0.725–2.575) |
| 4 | 4096 | After 1 | 0.520 (0.314–1.298) | 0.973 (0.737–2.074) |
| 4 | 4096 | Before 2 | 0.397 (0.216–0.525) | 0.783 (0.671–0.928) |
| 4 | 4096 | After 2 | 0.359 (0.301–0.875) | 0.800 (0.741–1.744) |
| 4 | 64 | Before 1 | 4.950 (4.572–5.215) | 5.351 (5.120–5.578) |
| 4 | 64 | After 1 | 4.703 (4.635–7.109) | 5.011 (4.938–7.760) |
| 4 | 64 | Before 2 | 4.721 (4.593–4.814) | 5.140 (4.981–5.237) |
| 4 | 64 | After 2 | 4.637 (4.529–5.344) | 5.046 (4.929–5.655) |

At median acceptance/completion elapsed time, the one-CPU 4096-byte runs give
before 749–751 / 588–589 MiB/s and after 748–751 / 583–587 MiB/s.
For 64-byte runs, before gives 132 / 125–126 MiB/s and after 135–138 /
127–128 MiB/s. Four-CPU 4096-byte results give before 2,517–3,070 /
1,277–1,292 MiB/s and after 1,924–2,786 / 1,027–1,250 MiB/s; 64-byte results
give before 202–212 / 187–195 MiB/s and after 213–216 / 198–200 MiB/s.
The one-CPU final costs are near baseline; four-CPU variation overlaps and
does not establish a speedup or an absence of smaller regressions.

An intermediate implementation notified both readiness workers on every pipe
transition even with no pipe interests. Its one-CPU 4096-byte completion median
was 13.578 ms versus 1.702 ms before. The final guard counts admitted requests
before their initial scan and suppresses those idle notifications. Pair-lock
ordering and the remembered notification preserve wake-before-park. The table
uses only the final guarded implementation.

## Manual behavior and debugger qualification

A warning-clean native scratch program was compiled as P1F and as an ELF for
symbols; all loaded segments and entry bytes matched. It is served read-only
through HOST only for qualification, with shared memfd RAM and VirtIO-FS.
This is not a shipped viewer or a Neovim/libuv port.

State checks on one and four CPUs reported zero mismatches: empty/full live
try operations return WOULD_BLOCK with no transfer; making 13 bytes of room
allows a 13-byte short write; a three-byte enqueue yields a three-byte short
read. Queued tail bytes survive final-writer closure and drain before zero EOF.
Copies keep their direction open until the last copy closes. Last-reader
closure reports WRITE_CLOSED and a nonempty write reports ENDPOINT_CLOSED.
Zero-size calls still succeed after closure. Closure-only masks, two distinct
pipe interests, attenuated rights, invalid masks, whole-list rejection and
unchanged raw error outputs matched the ABI.

A four-CPU writer fills the queue and waits with a positive deadline. A child
reading 13 bytes wakes WRITABLE; the subsequent 64-byte try-write reports
exactly 13 bytes and fills the queue again. The second wait wakes WRITE_CLOSED
when that child closes the last reader, and a nonempty try-write reports
ENDPOINT_CLOSED with zero progress. Real child completion is successful,
all expectations match, and GDB observes zero remaining pipe registrations.

A child-output viewer waits on PIPE, console input and process completion,
drains with try-read until WOULD_BLOCK, and prints six child chunks 700 ms
apart. On one and four CPUs it remains responsive to manually typed keys,
handles idle deadlines, then observes real pipe EOF and successful child
completion. A four-CPU variant adds a silent native TCP connection to a local
host peer, including with TCP before PIPE in the interest array. Each pipe
wake reports zero TCP events and the viewer completes normally. Pressing q
terminates and actually waits for the child; this reports PROCESS_TERMINATED,
not fabricated success. These normal runs reported zero mismatches.

GDB inspection on four CPUs stopped the ordinary and network workers after
publishing their wait records and releasing their notification locks, before
calling task_wait_sleep_until. An untimed scratch variant busy-reads the clock
before its first try-write, so the AP producer can progress with the BSP frozen
without relying on BSP timer service. Debugger scheduler locking lets that AP
write while the BSP remains before park. In both workers, the genuine pipe
write changes count from zero to 20 and reaches task_wait_wake; the worker is
still unparked, task->wait is NULL, and notification becomes true. Resuming
only the BSP takes the notified early return before publishing a task wait or
switching stacks. No injected kernel calls or memory/register changes are used.
These deliberate debugger interleavings are separate from timed measurements
and normal viewer success; their wall-clock deadlines can expire while stopped.

Active ordinary and mixed-TCP requests have one pipe registration each. On
completion, GDB sees the count return from one to zero, watches_pipe cleared,
and retained interests and caller cleared before bsp_request_complete wakes
its caller. A separate child blocked in a ten-second pipe readiness wait is
terminated while the parent retains the open writer. GDB sees stop_requested,
no ready bytes, CALL_ENDPOINT_CLOSED cancellation and the same registration
and reference cleanup; the parent receives the genuine PROCESS_TERMINATED
result. The one-CPU run also completes this stop/reap case without mismatches.

No kernel panic was observed. Physical hardware, copied-endpoint contention
fairness and exhaustive interleavings are not qualified by these runs. Existing
blocking pipelines and mux are unchanged: shell pipelines already use native
blocking transfers, and mux uses terminal endpoints. A relay or live child
viewer can use the new operations; converting less or adding a shipped viewer
is a later consumer task.
