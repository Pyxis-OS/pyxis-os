# Native Go files and time

2026-10-22: Pyxis `0ae18f44`, Go runtime `30881d22af17` (pyxis-go #3),
Go 1.27.3/amd64-v1, no cgo. No kernel/ABI or dependency-submodule change.
The merged task-2 target cannot compile the same ordinary `fmt`/`os`/full `time`
program; its syscall and poll definitions were unfinished. Task 3 implements
native adapters, not a Unix syscall layer.

## Configuration and commands

Nested QEMU/KVM, q35, `-cpu max`, one/four vCPUs, 2 GiB, OVMF, standard video,
VirtIO RNG/network and read-only VirtIO-SCSI boot CD (host AHCI CD-ROM crashes
before Caelum). The guest selected KVM clock with stable flags on every CPU.
These results are nested-VM observations, not native hardware measurements, and
are not comparable to task 1's original HPET startup timings.

Use the [build guide](../../go-toolchain.md) with the pinned fork:

```sh
GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off \
  GOOS=pyxis GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  /absolute/go-prefix/bin/go build -trimpath -o files.pxe files.go
QEMU_DISPLAY=none CPUS=4 MEMORY=2G ACCEL=kvm VIRTIO_NET=1 make run
```

Ordinary temporary Go programs ran from archive bundles with explicit runtime
memory/thread/clock/random grants and pipe/create where needed, plus their normal
boot/tmp roots. Source, commands and raw captures stay in
`/shared/present/go-task3/`; no test framework or boot automation was added.
The default image build passed. Linux `fmt`/`os`/`time` cross-build also passed,
checking shared build-tag/helper changes.

## Results

| Check | Observed result |
| --- | --- |
| Files and paths, one/four vCPUs; one/four Go Ps | Independently generated 20,037-byte contents, chunk boundaries, ReadAt/WriteAt with unchanged sequential cursor, partial EOF counts, seek, zero-filled gaps, shrink/growth, sync, rename, directory enumeration, retained cwd across rename, subtree boundary and normalized descriptive cwd passed. Missing/read-only roots, text-as-authority and unsupported append/Root operations refused. |
| Pipes and deadlines | 24,031-byte transfer preserved bytes through buffered EOF; empty input stayed pending. Close woke pending reads/full-pipe writes with closed errors. A timed-out write retained its confirmed prefix, which drained with exact bytes/count. Peer closure stayed an I/O error. Deadlines expired, extended, cleared and moved earlier without stranded goroutines. |
| Readiness bounds | Registration 32 returned native LIMIT after 31 application handles; closing one permitted another. A separate GDB snapshot with 200 same-pipe reader goroutines found three native tasks, all parked, rather than one M per waiter. |
| Supplied streams | FILE input/output and byte-pipe input preserved exact data. `cat`'s native FILE value flattened correctly to the archive's 661 bytes. FILE-backed stdout/stderr aliases interleaved ordinary fmt and runtime print/panic without overwritten output. Panic exited 2 and the following shell command ran. |
| Authority and time | A bundle without pipe/create returned a missing-grant error from os.Pipe while Sleep/AfterFunc, ordinary output and wall-time availability still worked. Bucharest winter/summer offsets were +02/+03, configured time.Local was Bucharest, explicit UTC worked, and invalid named TZ failed rather than silently becoming UTC. Timer reset, tickers and 24 concurrent sleeping goroutines completed. |
| Whole-process cleanup | Existing native parent stopped a four-P process with 200 pipe-reading goroutines on a one-vCPU guest, observed PROCESS_TERMINATED and actual group retirement, then launched hello successfully. |

## Startup

The existing native launch-to-first-pipe-output/process-retirement probe used
one discarded warmup then 11 samples per arm in A–B–A–B order, four vCPUs, one
Go P, the same parent/kernel/clock and the same 23-byte hello. A is task 2's
`97250a740851` (runtime unchanged from `cc9b5caad384`); B is task 3's runtime.
No profiler/debugger ran during measurements.

| Arm | First-output median (min–max), ms | Retirement median, ms |
| --- | --- | --- |
| A1 | 4.243 (4.142–4.337) | 4.422 |
| B1 | 4.425 (4.206–5.389) | 4.597 |
| A2 | 4.227 (4.155–5.560) | 4.409 |
| B2 | 4.724 (4.234–5.626) | 4.910 |

B's medians increased by 0.18–0.50 ms; ranges overlap. This includes the new
runtime poller/init path and image/loader cost, not an isolated poller cost.
Every sample stayed below 5.7 ms, within the accepted 250 ms nested p95 budget;
there is no speedup claim or native startup result.

## Limits

31 registered handles and 64 live/retiring native tasks remain explicit limits;
larger fan-in and accepted thread sizing are prerequisites before networking.
FILE I/O releases the P but can occupy an M while waiting for storage. Go
preemption remains cooperative. Exported providers/FILE values, forward-only
STREAM, atomic append, os.Root and process execution are outside this slice.
Missing metadata is exposed through native validity flags. Locked runtime
diagnostics can still block their P. Wall-clock unavailability is checked in
source but was not induced in QEMU; the native boot clock was available.
