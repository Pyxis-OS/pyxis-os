# Program images and initial stacks

The owner accepted a high initial stack with a reserved guard, a 256 MiB
image-span ceiling and unchanged 16 MiB installed-executable capture in
[#613](https://git.internal/PyxisOS/pyxis-os/pulls/613) on 2026-10-09. After
reviewing the 8 MiB default's costs, the owner retained the plain executable's
1 MiB eager stack in [#617](https://git.internal/PyxisOS/pyxis-os/pulls/617)
on the same date. Boot and ordinary launches use the same loader policy. The
[qualification report](../development/experiments/program-capacity/README.md)
records native boundary checks, cleanup, matched launch costs and session memory.

## Image admission

[P1F loading](../../kernel/image.c) accepts fixed, page-aligned segment addresses
in the canonical lower half, starting at `0x1000`. The
[SDK linker](../../userspace/linker.ld) convention remains `0x400000`; it is not
a required minimum. Segments must be ordered and disjoint after page rounding,
readable, and never writable and executable together. The entry must be inside
an executable segment's unrounded memory extent. Existing payload-size,
overflow and canonical-address checks still apply.

The maximum **image span** is 256 MiB: the last page-rounded segment memory end
minus the first segment start. This includes BSS, trailing page padding and gaps
between segments, bounding sparse layouts as well as backed bytes. An SDK image
beginning at `0x400000` may end at `0x10400000` after rounding. The ceiling is
loader admission policy, separate from serialized file size and P1F's encoding
capacity; it does not guarantee that physical allocation succeeds.

Span and stack/guard overlap checks cover every segment before any VM or image
backing is allocated. Other valid fixed placements remain available, including
the final user page above the stack. An inactive private VM owns the eagerly
zeroed destination frames, including BSS and page padding. Payload copying uses
a borrowed kernel scratch alias; final segment permissions apply before launch.
The caller's active address space does not change during loading.

## Initial stack and ownership

[Common process loading](../../kernel/user/load.c) owns the stack placement and
size. An omitted `launch_request.initial_stack_bytes` (zero) selects 1 MiB,
including boot init and ordinary small commands. An explicit request must be
page-aligned and between 1 and 8 MiB inclusive; invalid requests fail before
image capture, without clamping. The default layout is:

| Region | Addresses and backing |
| --- | --- |
| Initial stack | `[0x7fffffeff000, 0x7ffffffff000)`, 1 MiB eager zeroed backing, user RW, non-executable |
| Lower guard | `[0x7fffffefe000, 0x7fffffeff000)`, one reserved unmapped page |

A bundle may request larger eager backing through its validated manifest; the
base becomes `0x7ffffffff000 - initial_stack_bytes`, with the reserved guard one
page below it. The top stays fixed. Single, batch and space-creation launches
use the same admission and common loader; boot passes zero.

The initial one-past stack top, `0x7ffffffff000`, is canonical and 16-byte
aligned. The common loader returns that actual top with the process and entry;
[boot](../../kernel/user/launch.c) and [ordinary/batch](../../kernel/user/spawn.c)
launch consume it. There is no duplicate layout calculation in those paths.
Both ranges participate in VM allocation, so startup/private allocations cannot
consume the guard or stack. Image segments overlapping either range reject.

The loader clears outputs before work. Image-loading failures destroy the
inactive VM and release scratch reservations and any partial backing. Guard,
stack or process-creation failures also destroy the inactive VM. Subsequent
startup/grant/task-preparation failures destroy the unpublished process; a failed
batch discards its earlier prepared children before publication. Normal retirement
reclaims task storage on the BSP, then destroys process-owned backing and
reservations before publishing the final process-control result. See
[process lifetime](../interfaces/processes.md) and [BSP ownership](smp.md).

The guard catches accesses into its page, but a large stack adjustment can skip
it. There is no stack growth or demand paging. P1F contains no stack-size field.
Later [threads](../wip/threads.md) need their own disjoint guarded stack ranges;
this layout reserves no thread stack arena and adds no public thread or TLS API.

## File capture and costs

[Selected executable capture](../../include/abi/launcher.h) limits HOST, NPFS
and RAM files uniformly to 128 MiB of serialized bytes per image. There is no
global concurrent-capture budget; physical exhaustion still rejects. Immutable
boot archive bytes are loaded in place, without a new copy. The independent
256 MiB mapped-span and stack collision checks apply to every backend. A small
serialized file with large BSS can use much more destination backing. Captured
bytes coexist with eager image backing and stacks, and a batch retains earlier
prepared children while capturing its next image.

[Capture ownership](../../include/kernel/user/image_capture.h) is an address,
serialized size and page-rounded backing size. The BSP eagerly backs whole
RW/NX kernel pages, reads/copies into them, loads the image, then releases their
data frames and virtual reservation on success or failure. Partial allocation
unwinds the mapped prefix; an empty descriptor owns nothing. Shared kernel
page-table ancestors keep their ordinary VM lifetime. Only the BSP accesses the
buffer: APs transfer the descriptor through request metadata, so reused capture
mappings have no remote translations to retire. RAM capture keeps its source
FILE busy across the BSP copy and ends that operation exactly once.

Over-ceiling requests return `CALL_LIMIT`, allocation failures `CALL_NO_MEMORY`,
and I/O errors retain their existing status. A failed batch publishes no children
and discards earlier prepared children and provisional observers. The
[capture qualification](../development/experiments/program-bundles-capture/README.md)
records matched launch/session costs, 128 MiB native images, simultaneous backing
and debugger inspection of real allocation failure. Hosted Clang remains unported.

The experimental 8 MiB eager stack added 7 MiB per live process relative to
1 MiB, plus page-table costs. Matched nested-KVM qualification measured an exact
28 MiB increase in owned backing for mux plus three shells, and 42 MiB with two
pipeline children. Complete 1,024-launch sessions increased from median 3.06 to
10.53 seconds at one CPU and 3.50 to 11.30 seconds at four CPUs. These intervals
include session startup, transport and final drain, not isolated loader latency.
Those costs prompted the owner's return to the implemented 1 MiB default.

The [unpacked bundle path](../wip/program-bundles.md) now carries a bounded
initial-stack parameter in the launch ABI. Kernel, SDK and in-tree launchers
must use the coordinated layout; this changes no P1F field, SDK link address
or compiler container. Plain programs keep 1 MiB. An 8 MiB bundle stack meets
that capacity request only; it does not port Clang.
The [bundle qualification](../development/experiments/program-bundles-task1/README.md)
records the matched plain-launch comparison and manual admission/cleanup checks.
