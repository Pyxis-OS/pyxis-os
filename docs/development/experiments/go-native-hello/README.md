# Native Go hello

Original task-1 qualification, 2026-10-21. Kernel foundation: Pyxis `4f69383668` on `07752b7f`,
[PR #1083](https://git.internal/PyxisOS/pyxis-os/pulls/1083).
Go fork: `f2799d1e559f460989ae94bfd8f21e85ef44c4c1`, based on Go 1.27.3;
[fork PR #1](https://git.internal/PyxisOS/pyxis-go/pulls/1).
No userspace, ports, filesystem or lwIP pin changed. The parent integration adds
only the pinned cross-build recipe, optional hello bundle and image input.

## Build and behavior

The [cross-toolchain recipe](../../../../toolchain/build-go.sh) fetched the
exact fork commit from the internal mirror and completed a fresh Linux host
bootstrap. The host's Linux hello still built and ran. The
[hello recipe](../../../../toolchain/build-go-hello.sh) built the bundle with
`GOOS=pyxis GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0`, `-trimpath` and no module or
upstream fallback. The LLVM builder was `pyxis-llvm23.1.3-3d69e83`; both ordinary
and opt-in image assembly built without changing that container:

```sh
toolchain/build-go.sh /absolute/go-prefix /absolute/go-work
toolchain/build-go-hello.sh /absolute/go-prefix "$PWD/build/gohello.pxb"
make -j16 image GO_HELLO="$PWD/build/gohello.pxb"
```

Nested QEMU/KVM used one and four vCPUs, 2 GiB RAM, `-cpu max`, OVMF, standard
video, VirtIO RNG/network and a firmware VirtIO-SCSI CD-ROM. Stock host QEMU's
AHCI CD-ROM crashed before Caelum; changing the firmware boot device avoided it.
The guest selected direct 64-bit HPET (10 ns period), with no invariant TSC.
Both CPU configurations printed `hello from Go on Pyxis` and exited zero.
A separate one-P native program passed stack growth, GC across live 8 MiB
allocations, explicit panic/recover and three locked-thread turnovers. Native
completion, rather than a Go-side exit flag, gates context reclamation.
Multi-P concurrency and native hardware startup were not qualified.

The original measured recipe's hello SHA-256 is
`e212d8f4cce1c7a2aa37e78bf4beb6858897ed5dadfb2d66459b7d82c5ccc55c`.
It is direct P1F, with RX/R/RW segments and an omitted zero-filled BSS tail;
internal pclntab/moduledata remain loadable. Linker/assembler checks also refused
bad alignment/overlap/span/entry, dynamic imports, external/PIE/interpreter and
amd64/v2 output. These structural checks used Linux runtime objects for format
inspection, separately from the real Pyxis target execution above.

## Startup

A temporary native helper held the raw executable FILE, created a fresh group
and output pipe outside the timed window, then timestamped immediately before
LAUNCH. It passed memory/manage, threads/manage, clock/read+sleep and random/read,
`GOMAXPROCS=1` and the pipe writer for both supplied outputs. First-output time
ends after the first positive native pipe read. Retirement time ends after a
successful immutable PROCESS_WAIT result. Both include executable capture;
neither includes a host Remote transport round trip. The helper observes/seals
each group and closes endpoints between launches. Five seconds bounds a failed
sample; no sample hit it. No debugger/profiler ran during measurement.

Three batches on one four-vCPU boot used the pinned `-trimpath` binary, each
with one discarded warmup and 11 measured launches. Each launch produced the
expected 23-byte hello, exited zero and completed process/group retirement.
P95 below uses nearest rank; with 11 samples it is the maximum.

| Batch | First output min / p50 / p95 (ms) | Retirement min / p50 / p95 (ms) |
| --- | --- | --- |
| 1 | 9.399 / 10.255 / 10.971 | 9.922 / 10.905 / 11.593 |
| 2 | 9.513 / 10.421 / 11.307 | 10.087 / 10.941 / 11.859 |
| 3 | 9.541 / 10.443 / 13.887 | 10.074 / 11.083 / 14.814 |

These warm boot-archive results fit the 250 ms nested-KVM engineering budget.
They include pipe readiness/observation and clock-call cost. They do not predict
cold media I/O, native startup, another machine or a larger Go program. HPET is
explicitly part of this configuration, not a kvmclock/TSC result.

## Independent debugger snapshot

A separate untrimmed build of the same runtime implementation
(`24737b02e9dda1362a815f3e64f5a6646b707e7ec5d8c3ef4183ba69395ab42b`)
was stopped at main.main using its existing Go PC table. No debug ELF, converter
or loader symbol table was added. It had three native tasks and five pinned
guarded identities (initial FS plus two native M stack/FS pairs). FS−8 held
the running g pointer. Total reserved user VM was 1,165.445 MiB, including
1,219,567,616 bytes in 13 sparse private identities and 163,840 guarded bytes;
total backed user VM was **4,804 KiB**, including image, startup and initial
stack. Sparse extents numbered 34, with 9,608 total wait-token bytes. This is a
stopped observation, not a timing sample or a native memory promise.

Raw logs, temporary probe sources, artifacts and debugger scripts remain outside
the repository at `/shared/present/go-task1/`. The
[sparse VM record](../go-private-vm/README.md) covers zeroing, wait withdrawal,
guarded pins and real physical-exhaustion rollback. Remaining profile and retained
infrastructure limits are in the [build guide](../../go-toolchain.md) and
[memory reference](../../../kernel/memory.md#sparse-memory-limits).

## Review follow-up, 2026-10-22

Fork `d372764c495662c244a049a67a51014611fb16b4`, with parent based on main
`d37d1ded` (including #1083), passed a fresh exact-pin cross-toolchain build,
`go test go/build -run '^TestDependencies$' -count=1`, normal image build and
opt-in hello image assembly. That recipe hello SHA-256 is
`2843bb304360dea63bb04196bbb4f4413309546c556ce7c15866f8e8a8bd4c78`.

One- and four-vCPU QEMU passed console and redirected FILE hello, the existing
stack/GC/recover/M-turnover probe, and full 262,149-byte FILE and pipe output.
The pipe reader initially waited: the 64 KiB pipe was full, the child incomplete;
subsequent drainage recovered every byte and the child exited zero. Blocking
writes replace the earlier lossy try-write path. Scavenging split-NO_MEMORY
retains and clears the affected pages; entropy QUEUE_FULL retries share a five-
second deadline. Those two failure branches are source-inspected, not injected.

The same native startup helper ran three batches (one discarded warmup and
11 samples each) on four vCPUs/2 GiB. This is a **new nested-KVM configuration**:
current main selected KVM clock on all four CPUs, with a shared monotonic floor,
instead of the original HPET clock. No debugger/profiler ran. All 33 measured
launches produced 23 bytes and retired with exit zero:

| Batch | First output min / p50 / p95 (ms) | Retirement min / p50 / p95 (ms) |
| --- | --- | --- |
| 1 | 4.207 / 4.356 / 4.838 | 4.386 / 4.523 / 4.982 |
| 2 | 4.097 / 4.613 / 4.921 | 4.297 / 4.777 / 5.136 |
| 3 | 4.483 / 4.630 / 5.181 | 4.630 / 4.798 / 5.421 |

These fit the 250 ms nested engineering budget. The changed clock and host
configuration prevent attributing the difference from the original table to
runtime changes. Native startup remains unqualified. Evidence stays outside
the repo at `/shared/present/go-task1/fork-review-20261022/`.

Pre-g diagnostic follow-up (`cc9b5caad38412e50db4052be16c486b4c8c2098`): reused the unchanged Linux host tools
and rebuilt the Pyxis target. Four-vCPU QEMU passed normal hello and six negative
launches: omit threads, clock or random, each with console and pipe stderr. Each
named the missing capability and status 12, then retired with exit 2. The bounded
formatter uses static scratch, no g/TLS/heap/clock/locks, and fits the normal
nosplit stack budget. No new timing samples were taken for this error-path fix.
