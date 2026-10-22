# Native Go parallel qualification

2026-10-22, Pyxis base `82b516d1`, Go runtime `cc9b5caad384` (merged task 1),
Go 1.27.3/amd64-v1, no cgo. [Fork PR #2](https://git.internal/PyxisOS/pyxis-go/pulls/2)
updates its profile documentation only; runtime and compiler inputs are unchanged.
No kernel, ABI, userspace, ports or filesystem change was needed for task 2.

## Configuration and commands

Nested QEMU/KVM, `-machine q35 -cpu max`, four/eight vCPUs, one thread per core,
2 GiB, OVMF, standard video, VirtIO RNG/network and a read-only VirtIO-SCSI boot
CD (the host's AHCI CD-ROM crashes before Caelum). The guest selected KVM clock,
with stable flags on every CPU and the shared monotonic floor. These are
correctness observations, not native measurements or a parallel-speed claim;
they are not comparable to task 1's original HPET startup timings.

Use the [Go build guide](../../go-toolchain.md), the pinned target sources
and ordinary standalone programs. No external modules or test framework:

```sh
GOOS=pyxis GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off \
  /absolute/go-prefix/bin/go build -trimpath -o parallel.pxe parallel.go
QEMU_DISPLAY=none CPUS=4 MEMORY=2G ACCEL=kvm VIRTIO_NET=1 make run
```

Temporary programs/captures remain at `/shared/present/go-task2/`. They ran from
boot-archive bundles declaring memory/manage, threads/manage, clock/read+sleep
and random/read. Worker/parking/retirement programs ran as ordinary shell
commands. For stop/exhaustion cases, a native parent held the raw image, launched
one fresh group, passed explicit streams/services and `GOMAXPROCS=4`, drained
output concurrently and observed actual process/group completion within a
30-second failure bound. No launch was retried. The normal optional hello and
image remain unchanged.

The unchanged task-1 adapter supplied the baseline and all successful cases;
there is no before/after runtime change or claimed speedup. GDB observations
were separate from the ordinary runs. A follow-up image build and four-CPU
regression used base `f495ad9f` (inherited userland `af86ac836432`); no kernel
VM/thread implementation changed between these bases.

## Results

| Workload | Observed result |
| --- | --- |
| Four Ps on four/eight vCPUs | Sixteen workers processed 512 channel jobs with a mutex-protected total. Buffered/unbuffered channels completed. Each job checked recursive 2 KiB stack frames, a 64 KiB stack array and a reachable 128-node graph with 1 KiB payloads across yields/GC. All checks passed; 35–37 collections completed. Dynamic 1/8/2/4-P changes also passed. |
| Eight Ps | Eight locked workers completed arithmetic loops on four/eight-vCPU guests. A four-CPU GDB snapshot found distinct FS contexts belonging to one Go process active on all CPUs; CPUs 0 and 2 were simultaneously in `main.main.func1` (`0x472080..0x472160`), at `0x4720df` and `0x4720dc`. Function bounds came from the P1F's existing pclntab, without a debug ELF. |
| Native timed/infinite parking | With one P, an ordinary goroutine called the existing runtime's `notetsleepg`: a timed wait expired; an infinite wait completed after `notewakeup`. Another goroutine progressed and performed repeated GC, confirming P handoff during the blocked native address wait. |
| M retirement | Sixteen batches of eight goroutines returned while locked to their native threads: 128 turnovers, GC between batches, successful subsequent CREATEs. This exceeds the 64 live/retiring-task cap cumulatively. Native completion gates stack/FS release and M reclamation; no Go-side exit flag substitutes for it. Collection remains lazy, before later CREATE. |
| Whole-process stop | A process ran allocating/GC workers and a locked native-wait goroutine. A separate GDB observation found ten native tasks and linked pending address waits before termination. The parent requested termination and observed `PROCESS_TERMINATED`, status zero, then group completion. A later hello exited normally. |
| Thread admission exhaustion | Eighty simultaneously locked workers exceeded native admission: `thread create`, status 11 (LIMIT), traceback, actual exit 2. The parent drained 42,872 diagnostic bytes and observed group retirement; a later hello succeeded. |
| Physical-memory exhaustion | Four workers retaining up to 4 GiB in the 2 GiB guest failed with `memory backing update`, status 10 (NO_MEMORY), traceback and actual exit 2. The parent drained 7,595 bytes, observed process/group retirement, then launched hello successfully. |

The runtime-note/timer probes use private runtime symbols with
`-ldflags=-checklinkname=0`; they qualify existing scheduler hooks, not a public
native-call interface. Full `time` imports still reach the unfinished `syscall`
package, and general file/time adapters remain task 3. The simple worker and
retirement programs need no such linker override.

## Limits

Default P count and `NumCPU()` remain one; explicit parallelism is not an affinity
or CPU entitlement. Native admission currently allows 64 live/retiring tasks,
including the initial task, sysmon and additional blocked Ms. Required backing
or CREATE failure is process-fatal rather than a promise of unlimited parallelism.

Go preemption remains cooperative. Call-free loops can indefinitely delay a
runnable goroutine or stop-the-world GC; native hardware faults stop the process.
Diagnostic writes retain their P under runtime locks and can block behind output
backpressure. This record does not change those accepted limits or qualify
native-hardware concurrency, race detection or asynchronous preemption.
