# Private root epochs and concurrent flush initiators

Task 3 foundation, 2026-10-26. Implements part of the
[accepted invalidation design](../../../wip/vm-invalidation.md); shared kernel
VM ownership and its caller conversions are still pending. This foundation
keeps the general kernel VM's BSP assertion and private whole-process drainage.
It adds no ABI, grant, boot option or production diagnostic.

Every process receives a non-reused root incarnation, with a local generation
advanced only under drained mutation ownership. Dispatch captures the pair and
reserves its CPU in the residency bitmap. Departure clears the actual captured
pair only after switching to kernel CR3 on the permanent scheduler stack.
Incarnation/generation exhaustion refuses before mapping effects.

Shared invalidation uses permanent per-CPU requested/acknowledged epochs. The
global acquire/release issue chain orders each initiator's prior closure;
monotonic request mailboxes and ACKs can therefore cover an older request even
if its publisher is late. A larger integer alone would not prove this. Each
handler reloads its current CR3 and acknowledges without allocation, locks,
logging, transient pointers or a drain loop. Initiators retry outstanding
doorbells and sleep in 1 ms polls against one absolute caller-supplied deadline.
PCID and global pages remain disabled.

## Ordinary nested QEMU checks

Before code: accepted docs head `1d82c4c3`, kernel equivalent to main `5c96f016`.
After foundation: `2d4dd5a1`. Clang 23 builder
`pyxis-llvm23.1.3-3d69e83`, debug ceiling, q35/KVM, host CPU with invariant TSC,
4 GiB RAM, one and four CPUs, VirtIO graphics/network/RNG. No disk is mounted;
`nvme.write=0`. The same existing native packet archive/probes were used;
the one-CPU configuration confines Remote children to CPU 0, the four-CPU
configuration to CPU 1. All command lines, configuration/archive/kernel hashes,
probe outputs and build logs are retained at
`/shared/present/bsp-removal/task3/`.

One completed run per configuration before and after: eager allocation/release,
zeroing, sparse/self-unmap, context pins and unwind, sibling quiescence, parked
address/pipe operations, Go parallel/retiring native threads, stopped children,
and a VirtIO resize from 1280x800 to 1024x640 all pass. Resize confirms the
ordinary remote-flush path; its cell-copy diagnostic does not measure IPI cost.

| Nested run | Small 4 KiB | Large 1 MiB | Long control |
| --- | ---: | ---: | ---: |
| Before, 1 CPU | 3.057 ms | 4.877 ms | 64.476 ms |
| After, 1 CPU | 3.635 ms | 14.915 ms | 63.543 ms |
| Before, 4 CPUs | 3.268 ms | 4.959 ms | 62.517 ms |
| After, 4 CPUs | 2.759 ms | 15.865 ms | 61.891 ms |

These are single runs on a shared nested host, with no controlled performance
or native-speed claim. Earlier listener-startup preparation failures contain no
benchmark samples and are not counted as completed runs.

The first after-runs' short large-allocation case was slower inside allocation
service, while the long control and release service did not show that increase.
An additional matched four-CPU pair used `-mem-prealloc` on both kernels to move
host guest-RAM provisioning outside the measured operations:

| Preallocated RAM, 4 CPUs | Small | Large | Long control |
| --- | ---: | ---: | ---: |
| Before | 2.529 ms | 4.426 ms | 60.786 ms |
| After | 2.548 ms | 4.259 ms | 60.112 ms |

Both functional collectors and resize checks pass. The large-case increase was
not reproduced in this pair; this does not isolate host paging from every other
shared-host effect or establish a speedup. The original results remain above,
not replaced with the later pair.

## Concurrent initiators and root inspection

A separate private QEMU fixture starts independent processes on CPUs 1 and 2.
Each runs 32 ordinary all-online-CPU invalidations; the recorded maximum active
initiators is two and both complete. The fixture changes no PTE, drops no IPI
and suppresses/delays no acknowledgement. It is not merged, added to the ABI,
or placed in a native packet. Production sources were restored and rebuilt.
The patch, source, ELF hash, configuration, scripts and final passing record are
under `task3/concurrent/ordinary-two-initiators-gdb-final/` and its parent.
Earlier collector/GDB setup failures are recorded separately in `evidence.json`.

Read-only GDB hardware breakpoints establish on CPU 1:

- Before departure accounting: actual CR3 equals kernel root `0x62000`, captured
  pair is incarnation 2/generation 1, root count 1, residency mask `0x2`.
- Before the next private dispatch: roots 0, mask 0, same incarnation 2 and
  generation 2. Entry then reserves residency and reloads the private root.

These addresses are disposable guest values. Stopping under GDB is functional
inspection, not a latency measurement. Both concurrent actors, the ordinary
functional collector and the resize pass after detachment. All owned QEMU/GDB
processes were joined and stopped.

## Remaining qualification

Shared structural ownership, stable concurrent queries, safe scheduler
admission, retained uncertainty, actual shared range reuse/permissions and
consumer exclusion are not delivered by this foundation. Deliberately missed
or delayed acknowledgements still require chronium's explicit disposable-QEMU
authorization; none was injected. Native task 1/2 results do not qualify task 3.
