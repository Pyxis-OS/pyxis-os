# Selected-image capture qualification

Recorded on 2026-10-09 for [program bundles](../../../wip/program-bundles.md#selected-image-admission).
These are manual nested-KVM measurements and read-only debugger observations.
Temporary native fixtures, image variants and raw captures remain local in
ignored `build/capture-*`; no test, fault-injection or benchmark infrastructure
and no kernel log lines were added.

## Inputs and commands

The baseline precedes implementation: main
`11d35fa68e311f5a0b4a37ef85e5e2821c035e6e`, using kernel/SDK/userland/ports
artifacts from exact-head [CI #1562](https://git.internal/PyxisOS/pyxis-os/actions/runs/1562).
Pins remain userspace `0c690289`, ports `8bff5dcd`, fs `b427df29`, lwIP `a1aadb91`.
After code is `ed92d70c06e01b2a15599c116ead11a963422616`; its executable body was
built before committing, with only a request-ownership comment amended afterward.
ELF SHA-256 is `6fa01be0d46b05d1630ef70b60082ab4ab13ca5fb048805b8deb794c044d06ef`.
Ordinary `make -j16 image` rebuilt kernel, SDK, all ports and userland with the
existing `pyxis-llvm23.1.3-49e2c1a` builder. No dependency, compiler-container or
external-source input changed. Effective kernel configuration values match.

Timing and session census use QEMU 10.2.2, Q35, nested KVM, `-cpu max`, 512 MiB,
UTC RTC, one socket, one or four cores, one thread per core, standard VGA
1280x800, modern VirtIO SCSI CD/RNG/network and fresh matching raw OVMF variables.
The [process-lifetime QEMU command](../threads-task1/README.md#inputs-and-configuration)
applies with `build/capture-baseline` / `build/capture-after` paths. Only staged
configuration enables the Dev multiplexer and Remote launcher. Logging is
`info`, memory profiling is off; GDB is detached while timing.

After 16 warmup children, time three fresh remote sessions per CPU count using
the [existing FIFO/client command](../program-bundles-task1/README.md#inputs-and-matched-launch-comparison),
substituting these capture paths. Guest input is:

```text
lua -e 'for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'
exit
```

For session backing, select Dev, split twice with `Ctrl+B`, `%`, then run
`head -c 1 | cat` in one pane. Inspect idle, waiting pipeline, and restored prompt
after typing `a` without Enter. Read-only GDB deduplicates the process tree and
sums owned `RANGE_BACKED` pages; borrowed display ranges, page tables and kernel
backing are excluded. PMM allocations provide a separate whole-guest census.

Large-image qualification uses four CPUs and 1 GiB, with the same kernel/SDK.
HOST uses `-cpu max` and a read-only VirtIO-FS export; NPFS/RAM use `-cpu host`.
NPFS attaches a read-only GPT disk containing a structurally checked 768 MiB pool,
8 MiB journal and the same fixtures. RAM uses native `cp` from HOST into `tmp://`.
Those device/configuration differences make absolute backend peaks incomparable;
no large-image timing comparison is claimed.

## Matched plain-launch and resident costs

| 1,024-child whole session | Before samples (s) | After samples (s) | Median before / after (s) |
| --- | --- | --- | --- |
| 1 CPU | 3.09, 3.09, 3.09 | 3.10, 3.07, 3.09 | 3.09 / 3.09 |
| 4 CPUs, initial | 4.16, 3.18, 3.45 | 3.98, 4.08, 4.01 | 3.45 / 4.01 |
| 4 CPUs, alternating fresh-boot control | 3.20, 3.16, 3.16 | 3.09, 3.13, 3.16 | 3.16 / 3.13 |

The initially higher four-CPU after median prompted a matched alternating
control with no concurrent build or other guest. All samples completed 1,024
children, exited zero and reported `drain=complete`. The control ranges overlap;
there is no consistent observed plain-launch regression. These short intervals
include startup, Lua, transport, exit and drain, rather than isolated loading;
they establish neither statistical confidence nor a speedup.

| Four-CPU session | Processes | Owned backing before / after (MiB) | PMM allocated before / after (MiB) |
| --- | ---: | ---: | ---: |
| Mux plus three shells | 4 | 6.34375 / 6.34375 | 48.5546875 / 48.5546875 |
| Waiting short pipeline | 6 | 8.546875 / 8.546875 | 50.85546875 / 50.85546875 |
| Restored prompt | 4 | 6.34375 / 6.34375 | 48.55859375 / 48.55859375 |

Both retain 13 heap pools totaling 3.25 MiB, with zero retired arena bytes and
equal live allocation counts. After live heap bytes increase by 328/352/328
bytes respectively. Allocatable PMM capacity is two frames lower after
(107,544 versus 107,546); ELF LOAD page counts are unchanged and initrd grows
5,120 bytes. The capacity difference is reported without a causal attribution.

## Native images and peak backing

An ordinary SDK-linked native probe has an additional initialized read-only
P1F segment, filled with `0x5a`; it checks its first/last 4 KiB and exits zero.
Host inspection independently checks exact serialized size, ordered disjoint
page-rounded segments, legal permissions, entry and mapped span. These fixtures
exercise actual serialized payload, rather than large BSS or trailing padding.

| Fixture | Serialized bytes | Page-rounded mapped span (bytes) |
| --- | ---: | ---: |
| Small | 131,072 | 159,744 |
| 127 MiB | 133,169,152 | 133,197,824 |
| Exactly 128 MiB | 134,217,728 | 134,246,400 |
| Over ceiling | 134,217,729 | 134,246,400 |

HOST and NPFS ran small, 127 MiB and exact-ceiling images; RAM ran small and
repeated exact-ceiling images. All executed the sentinels and exited zero.
For HOST/NPFS, prepared exact-ceiling children had 32,775 image pages, 256 stack
pages and two startup pages: 129.03515625 MiB of disjoint owned backing, with
one reserved unmapped guard page. Capture simultaneously owns another 128 MiB.

A HOST launch alongside a native 64 MiB memory holder peaked at 94,595 allocated
PMM frames (369.51171875 MiB), versus 28,722 before. The exact capture release
returned 32,768 frames; after child exit, allocations returned to 28,722.
NPFS warm peak was 76,504 frames, returning 32,768 immediately on capture release
and reaching 10,627 after exit. RAM peak was 111,106 frames, including its
separate 32,768-frame source file; it returned all capture frames immediately.
RAM's first large capture retained 64 shared kernel page-table frames after
child exit; repetition reused them. Kernel heap pools remained 3.25 MiB.

## Failure rollback

Over-ceiling images returned `CALL_LIMIT` on all three backends with empty
capture descriptors and no capture-frame allocation. Ordinary native memory
holders then left fewer than 32,768 free frames. GDB inspected actual partial
capture allocations, before source reads/copy or destination loading:

| Backend | Mapped capture prefix at exhaustion (pages) | Result |
| --- | ---: | --- |
| HOST | 17,214 | `CALL_NO_MEMORY`; all prefix frames returned |
| NPFS | 16,919 | `CALL_NO_MEMORY`; all prefix frames returned |
| RAM | 16,413 | `CALL_NO_MEMORY`; all prefix frames returned |

Each VM reservation unwound and descriptor stayed empty. NPFS separately
reclaimed ordinary cache/metadata chunks under pressure; those changes were
accounted apart from the capture prefix. RAM's source remained readable and
its busy operation ended exactly once on success, LIMIT and NO_MEMORY.

Failed two-stage batches also discarded their first prepared child and
provisional observer before any publication. HOST covered both over-ceiling
and allocation failure: 293 child pages returned in each, plus 16,921 partial
capture pages in the allocation-failure batch. NPFS over-ceiling rollback
returned 293 child pages; RAM returned 309. Batch count became zero and PMM
returned to its pre-batch value. Subsequent small launches succeeded under
pressure. Removing RAM sources separately returned their owned data frames.

This qualifies the accepted native capture ceiling and the inspected failure
paths, not ZIP, Clang, mutable-source snapshot coherence, exhaustive I/O/cancel
failures or physical hardware. Shared page-table and filesystem-cache lifetimes
remain distinct from capture-data ownership. Task-owned QEMU, debugger,
VirtIO-FS and remote-client processes were stopped.
