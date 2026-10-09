# Program image and initial stack capacity

Status: owner-assigned priority 0 from [hosted Clang](hosted-clang.md),
proposal only, 2026-10-09. The three defaults below await acceptance; no capacity
implementation is authorized by this note. Base: main `0a9946de`, including
merged [process-lifetime task 1](https://git.internal/PyxisOS/pyxis-os/pulls/612).

## Inspected constraints

The [SDK linker](../../userspace/linker.ld) starts at `0x400000`.
[Common loading](../../kernel/user/load.c) reserves an unmapped guard at
`0x7ff000`, then eagerly backs a 1 MiB stack at `0x800000`. A conventional
contiguous image has 4 MiB minus one page below the guard; this is a collision
limit, not a P1F file-size limit. The [P1F loader](../../kernel/image.c) preserves
BSS, zeroes rounded backing, rejects overlap and unwinds an unpublished load.

[User VM](../../arch/x86_64/paging.c) already spans `[4096, 2^47)`; it has no
separate 1 GiB search limit. P1F allows page-aligned segment addresses from
`0x1000`; `0x400000` is a linker convention, not a required minimum. Boot and
ordinary/batch launches share the loader but independently compute the stack top
in [launch.c](../../kernel/user/launch.c) and [spawn.c](../../kernel/user/spawn.c).

The pinned LLVM `49e2c1a` P1F writer supports 64-bit segment memory extents, and
the pinned TCC writer's independent `INT_MAX` layout budget accommodates the
proposed range. Neither format writer has a 4 MiB image bound or stack metadata.
The [hosted-Clang investigation](hosted-clang.md#resources-and-native-limits)
measured an approximately 86 MiB stripped Linux Clang proxy and inspected its
8 MiB stack expectation. Those are not native mapped-image, BSS or stack-usage
measurements. This proposal adds no measurements or compiler port.

## Proposed contract

Keep fixed-address P1F segments and the SDK link base. Cap the **image span** at
256 MiB: last page-rounded segment memory end minus the first segment start,
including BSS, trailing page padding and gaps between segments. This bounds
sparse images as well as backed bytes; serialized payload size is a separate
quantity. Preserve the existing valid minimum address and canonical-address
checks. Validate overflow, span and stack/guard collision before backing pages.
For an ordinary image beginning at `0x400000`, the maximum rounded end is
`0x10400000`. The 256 MiB value is an initial loader admission policy, not a new
P1F encoding limit or a guarantee that physical allocation will succeed.

Place the initial stack near the top of the canonical lower half:

| Region | Proposed addresses |
| --- | --- |
| Initial stack, 8 MiB, RW and non-executable | `[0x7fffff7ff000, 0x7ffffffff000)` |
| Reserved, unmapped lower guard, one page | `[0x7fffff7fe000, 0x7fffff7ff000)` |

The one-past stack top is canonical and aligned; `2^47` itself is unsuitable for
the existing user-entry check. Reserve both ranges through VM, retaining eager
zeroed backing and process ownership until destruction. Existing allocation and
startup placement must see those reservations. Reject colliding image segments;
leave other valid fixed-address placements intact. The common loader returns
the actual stack top with the loaded process/entry, and both launch paths consume
it rather than reconstructing layout. Keep one authority for sizes/placement.

Use a **fixed 8 MiB initial stack in this task**, including boot init and small
commands. This adds 7 MiB of eager backing per process, plus high-address page
tables, and can materially increase launch/exit cost and concurrent memory use.
It avoids new P1F fields, launcher parameters or per-program policy in this slice.
A per-image declaration is the alternative when that cost justifies coordinated
P1F/LLD/TCC changes; a launcher override instead requires a native launch-policy
contract covering boot and ordinary callers. Neither is silently added here.
No automatic growth or demand paging is proposed.

Later [threads](threads.md) cannot concurrently reuse the initial stack; they
need their own disjoint stack ranges. Their allocation/guard policy remains with
that milestone. High initial placement provides no thread API, TLS layout or stack arena
reservation now. Existing unpublished-load failure releases the inactive VM,
backing and reservations; failures after process creation use ordinary process
destruction, and submitted cleanup follows task 1's lifetime. No partially loaded
process may be published.

[Installed executable capture](../../include/abi/launcher.h) remains capped at
16 MiB of serialized file bytes in HOST/NPFS. Raising it is a separate staging
budget/peak-memory task: captured bytes coexist with image backing and the stack,
and batches retain earlier prepared children while capturing the next image.
Archive/RAM qualification can exercise
larger payloads, but does not establish that an 86 MiB executable can launch from
the installed pool. A small file with large BSS can exercise mapped capacity
through installed capture without bypassing its existing bound.

## Owner decisions

1. **Layout and image admission:** default high initial stack at the addresses
   above and a 256 MiB page-rounded image-span ceiling, preserving custom P1F
   placement and collision rejection. Placing the stack just above each image
   keeps mappings lower but couples stack placement to every image's extent.
2. **Initial stack policy:** default fixed 8 MiB, eager, with no growth or
   per-program field/parameter. Per-program sizing avoids charging small commands
   that full capacity, at the cost of a format or launch-policy change. A P1F field
   needs both writers and an owner-built compiler container; the fixed default
   needs kernel/SDK integration only, with no compiler rebuild.
3. **External staging:** default keep 16 MiB here and assign its larger budget
   separately. Changing it now would combine VM capacity with captured-file peak
   memory and batch-admission policy. Neither option makes hosted Clang a port.

## Validation after acceptance

- [ ] Capture an exact-main baseline before implementation: repeat the existing
  1,024-child launch/exit batches and allocation workloads at one/four CPUs from
  [task 1 qualification](../development/experiments/threads-task1/README.md), with
  matched devices, memory, compiler, logging, workloads and repeated samples.
- [ ] Build a temporary native P1F executable whose mapped extent exceeds the old
  window, with approximately 86 MiB combined initialized data/BSS as a capacity
  fixture, not a native Clang estimate. Execute it and inspect its actual segment
  extents, initialized bytes, zero-filled BSS, permissions, disjoint stack and
  reserved unmapped guard using interactive QEMU/GDB. Exercise boot and ordinary
  launches; inspect startup/private allocations as well as final exit cleanup.
- [ ] Qualify separate admission boundaries: page-rounded BSS/span above 256 MiB,
  sparse over-span placement and stack/guard collision must reject without
  publication. Inspect invalid-second-image batch rollback and partial backing
  unwind; distinguish runtime observations from unforced allocation-failure
  inspection. Check the unchanged installed-file capture boundary separately.
- [ ] Repeat matched launch/exit costs and record variation, eager-stack memory
  charges and BSP cleanup before/after. Discuss material regressions with the
  owner rather than silently reducing backing or adding growth. Temporary inputs
  and debugger captures stay local; add no test/boot automation or benchmark
  infrastructure. Stop with the focused implementation PR for review.

This delivery stops at the proposal. No compiler port or public threads are part
of the capacity task, and no dependency pin or container input changes here.
