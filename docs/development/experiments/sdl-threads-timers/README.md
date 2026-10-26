# SDL threads and timers qualification

Threads task 9, first consumer: SDL workers, timers and cross-thread event
wakeup. The owner accepted the [contract](../../sdl2.md#threads-and-timers) on
2026-10-13. These are manual nested-QEMU results; no native run is claimed. No
kernel, ABI, SDK or libc change was made: the baseline and changed images
carry the same kernel (`caelum.elf` SHA-256
`3ca4050735c6456795bf2ee7e932ad393c6890ff3b8e9efe2e81572389ebc404`).

## Revisions and configuration

- Pyxis main `0e95af3a`, userland `c472de7d`, builder
  `pyxis-llvm23.1.3-4c23a20`, SDL 2.32.10 `5d249570`.
- Before: ports `f93becb9` (threadless SDL). After: ports `5a1f1d3` (this task),
  later rebased onto ports main without SDL changes as `c09023d`.
- QEMU 10.2.2 (with the local AHCI fix), nested KVM in Claude's Fedora VM, Q35,
  `-cpu max`, 2 GiB, standard VGA at 1280x800 (1280x768 SDL content area),
  VirtIO RNG, no NIC or disk, `-display none`. Input arrived through QMP. One
  and four CPUs, separate boots.

Three local programs were staged as `.pxb` bundles declaring `memory`, `clock`
(read, sleep), `threads`, `pipe`, `display`, `keyboard` and optional `pointer`,
so the before and after images grant the same authority:

- upstream `test/checkkeysthreads.c`, unmodified (SHA-256 `abc1f3f4…`, as in
  the [event-wait qualification](../../sdl2-event-wait-qualification.md));
- [`sdl-delay.c`](../sleep-wake-granularity/sdl-delay.c), the existing
  `SDL_Delay(16)` frame workload;
- a local threads probe printing mutex/semaphore cost, create/join time, a
  10 ms `SDL_AddTimer` period over 199 intervals, push-to-`SDL_WaitEvent`
  latency over 50 pushes from a thread at 20 ms spacing, and exit modes.

The probe, bundles and images stay local; no test recipe, library or CI change
was added.

## Before

The threadless build refused everything the probe asked for, on one and four
CPUs: `SDL_CreateSemaphore` failed ("SDL not built with thread support"),
`SDL_CreateThread` failed, and so did `SDL_INIT_TIMER` and `SDL_AddTimer`.
`SDL_CreateMutex` succeeded and its locks did nothing. The earlier SDL docs
said semaphores also succeeded; they did not.

## Matched results

`sdl-delay` mean frame time per run (ms), five runs per boot. The baseline image
was booted twice, before and after the changed image, to measure boot-to-boot
variation:

| CPUs | Baseline boot 1 | Changed | Baseline boot 2 |
| --- | --- | --- | --- |
| 4 | 18.180–18.269, median 18.248 | 17.703–17.767, median 17.715 | 17.646–17.725, median 17.679 |
| 1 | 17.609–18.464, median 18.439 | 17.387–17.422, median 17.402 | 17.407–17.573, median 17.460 |

The difference between the two baseline boots (0.57 ms on four CPUs, 0.98 ms
on one CPU, almost all in the drawing "work" time) is larger than the
difference between the changed image and either baseline. No frame-time change
from real mutexes is measurable here.

Whole-QEMU host CPU (user + system from `/proc/PID/stat`, `CLK_TCK` 100, 100%
is one host core) with no input. This includes the unchanged presenter and
devices:

| CPUs | State | Before | After |
| --- | --- | --- | --- |
| 4 | Shell idle, 20 s | 15.20% | 14.70% |
| 4 | `checkkeysthreads` in `SDL_WaitEvent`, 3×30 s | 15.43, 16.16, 16.60% | 18.83, 14.90, 14.20% |
| 1 | Shell idle, 20 s | 9.30% | 7.20% |
| 1 | `checkkeysthreads` in `SDL_WaitEvent`, 3×30 s | 9.66, 10.00, 8.67% | 7.60, 7.27, 10.30% |

Before, the consumer's ping thread could not start. After, it pushes ten
events at 1–2 s intervals, each waking the blocked main loop. On four CPUs the
first interval overlapped those pushes; the one-CPU intervals started after
them. Idle blocking is unchanged within this noise.

Changed-image probe results:

| Measure | 4 CPUs | 1 CPU |
| --- | --- | --- |
| Mutex lock+unlock, uncontended | 23.1 ns | 23.0 ns |
| Semaphore post+trywait | 46.1 ns | 49.3 ns |
| Thread create+join | 12.44 ms (one sample) | 2.65–4.59 ms over 10, first 3.78 ms |
| 10 ms timer period | mean 10.389, 9.240–11.143 ms | mean 10.276, 9.257–11.372 ms |
| Push to `SDL_WaitEvent` return | mean 1217 µs, 720–3217 µs | mean 1103 µs, 967–2748 µs |

Create+join includes libc's eager guarded 1 MiB stack and TLS backing and the
native thread lifecycle. The single four-CPU sample came from an earlier probe
revision and is not a distribution. Intervals below 10 ms come from SDL's millisecond
scheduling; all 200 callbacks arrived. Wake latency
covers the sibling's pipe write, kernel readiness, main-thread wakeup, SDL's
pumps and queue removal, measured by `SDL_GetPerformanceCounter`.

## Authority, fallback and exit checks

On four CPUs:

- Running the probe's inner `.pxe` by path (plain launch, no threads grant):
  `SDL_AddTimer` failed with `pthread_create() failed: Operation not
  permitted`, and exit was orderly. Plain `checkkeysthreads` printed its
  thread failure and blocked at 14.43% whole-QEMU CPU over 30 s, as before.
- A bundle granting threads but not `pipe` used the polling fallback: 101.16%
  over 30 s, with all ten thread events still delivered. This is the
  documented cost of omitting `pipe/create`.
- Probe exit modes, using an `atexit` handler as the evidence:
  - `lazy`: `SDL_AddTimer` started the thread without `SDL_INIT_TIMER`, and
    `SDL_Quit` joined it. The handler ran.
  - `detach`: `SDL_DetachThread` set its error, the thread finished, and exit
    was forced. The handler did not run.
  - `noquit`: the timer was left running and exit was forced. The handler did
    not run.

  Both forced exits are the [recorded limit](https://git.internal/PyxisOS/pyxis-os/issues/1302).
  `SDL_THREAD_PRIORITY_HIGH` returned -1 ("That operation is not supported")
  and `NORMAL` returned 0.

## Debugger inspection

GDB was attached to QEMU's stub on the four-CPU changed image. It used a
debugger ELF of `checkkeysthreads` linked with `-Wl,--oformat=elf` from the
same inputs, with `may-call-functions off`. A hardware breakpoint at
`PYXIS_SendWakeupEvent` stopped in the ping thread, called from
`SDL_PeepEvents(SDL_ADDEVENT)`:

- `fs_base` was `0x117000`, the sibling's TLS;
- the wakeup reader and writer handles were `0x100000016` and `0x100000017`,
  with `wakeup_needed` set.

A breakpoint at `wait_many`, conditioned on the same CR3, then stopped in the
main thread (`fs_base` `0x4000`) inside `PYXIS_PumpEvents`. It held four
interests: display `WAIT_RESIZED` at generation 1, keyboard and pointer
`WAIT_READABLE`, and the pipe reader `WAIT_READABLE`.

## Limits

These runs used nested QEMU and are not native timings. Thread creation was
measured with ten samples on one CPU only. The no-pipe and plain-program paths
were checked only on four CPUs. DevilutionX and Quake were not rerun: they are
plain programs with no threads grant, so they still refuse thread creation and
only gain real (uncontended) mutexes. Audio callbacks are the next, separate
consumer step.
