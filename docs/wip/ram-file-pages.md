# Page-backed RAM files

Status: **proposal, 2026-10-09,** assigned to Claude as technical-debt paydown.
It fixes [contiguous RAM-file backing](../technical-debt.md#contiguous-ram-file-backing)
and the RAM-file part of the
[never-reused kernel heap arena](../technical-debt.md#never-reused-kernel-heap-arena).
Three [decisions](#decisions) need the owner before code.

## Problem

A RAM FILE owns one kernel heap buffer:
- **Growth copies.** The buffer doubles as the file grows; each replacement
  holds the old and the new buffer and copies the contents.
- **Pools stay.** Heap pools are never returned, so memory freed by a deleted
  file stays with the heap.

Baseline on main `d673303` (QEMU 10.2.2, nested KVM, 4 CPUs, 8 GiB), checked
with `fastfetch`'s allocated memory:

| Step | Allocated |
| --- | --- |
| Boot, then `iobench` on 1 MiB RAM files | 48 MiB |
| After receiving 64 MiB with `tcp 10.0.2.2 5002 > tmp://rx.bin` | 315 MiB |
| After `rm tmp://rx.bin` | 315 MiB |
| Two more receive-and-remove cycles | 315 MiB |

The receives took 11.6–13.5 s wall time, and every SHA-256 matched.

## Proposal

**Backing.** A RAM file keeps an index of physical frames, one per 4 KiB page:
- frames come from the PMM, not the heap;
- the index itself is a heap array of frame addresses, grown geometrically, so
  a 64 MiB file needs a 128 KiB index;
- a zero entry is a hole, which reads as zeros.

**Access.** The kernel has no direct map of physical memory, so each page is
reached through the calling CPU's scratch slot, with interrupts off: map, copy
to or from user memory, unmap with a local `invlpg`. No shared kernel mapping
or TLB shootdown is involved.

**FILE operations.** The FILE protocol does not change.
- **READ** copies page by page; holes read as zeros.
- **WRITE** first allocates and zeroes every frame the range needs, then
  copies. If any allocation fails, it frees the frames it took and fails with
  NO_MEMORY, leaving the file unchanged. Writes still complete in full or fail
  unchanged.
- **RESIZE down** frees whole frames past the new size and zeroes the tail of
  the last page, so truncated bytes never reappear. Resize to zero also frees
  the index.
- **RESIZE up** depends on [decision 3](#decisions).

**Lifetime.** Final destruction frees every frame and the index on the
existing BSP retirement path. Nothing is retained after `rm` except the
heap's usual small-allocation pools.

**Other users of RAM file contents.**
- **Program launch** from a RAM file, for example `home://` on live boots,
  copies the image into a heap buffer during capture. Host and native
  filesystem images already go this way, and the loader keeps one contiguous
  input.
- **Screen capture** keeps its heap pixel buffer until the snapshot file
  exists, then copies it into pages and frees the buffer.

## Serialization and page lifetime

Moving growth off the BSP does not change what serializes a file:
- **Exclusive ownership.** Every RAM-file READ, WRITE and RESIZE, and a launch
  capture, already holds the file's exclusive `busy` ownership for the whole
  operation (`file_begin_operation` / `file_end_operation`). The BSP queue
  only ran the buffer replacement inside that ownership. The page index, the
  size and every frame are touched only by the owner.
- **Waiting.** A second operation waits in the file's FIFO as an ordinary
  sleeping task wait, not by spinning.
- **The spinlock.** The file's short spinlock (IF=0, never held across a wait,
  an allocation or a copy) guards only the hand-off of `busy` and that FIFO.
  It stays that way. No new lock is added, and no spinlock is held across a
  large read.

**Page lifetime.**
- **Shrinking and holes.** RESIZE down frees frames only while it owns `busy`,
  so no other operation can be copying from them. Frames are freed after the
  index entries are cleared.
- **Final destruction.** It runs from the existing object retirement on the
  BSP, which asserts that no operation owns the file. An operation in progress
  keeps the object alive through the caller's own capability: its single task
  is inside the syscall and cannot close that handle. Other processes closing
  their handles cannot drop the last reference.
- **Threads.** Once a process can run several threads, a file call must retain
  the object for its duration, a rule for the threads work.
- **Scratch mappings.** A copy's page is mapped only between the calling CPU's
  own map and unmap, with IF=0, so no other CPU ever holds that translation.

**No other BSP-owned state.** The caller's CPU uses only `pmm_alloc`/`pmm_free`,
`kmalloc`/`kfree` for the index, and its own scratch slots. Each is already safe
on any CPU with IF=0 ([SMP](../kernel/smp.md#memory-and-output-boundaries)). No
VM metadata, kernel VA range or page table outside the scratch window changes,
so the allocator and VM contracts stay as documented. Only the SMP reference's
sentence listing RAM-file backing among BSP requests changes.

**Lock order.** Frame and index allocation run with the file owned and its
spinlock released. Only the existing leaf heap and PMM locks are taken, and the
memory-pressure notification follows the documented order. A FILE payload is at
most 4096 bytes, so one READ or WRITE touches at most two pages and allocates at
most two frames. Each call's interrupts-off work is a few page copies. RESIZE
down and final destruction are the only steps proportional to file size: they
free one frame per page, and grow the index rarely and geometrically.

## Decisions

1. **Allocation on the writing CPU.** Default: yes.
   - RAM-file growth today is a BSP request, and the SMP reference lists it as
     one.
   - With pages, appending writes need new frames on almost every call. A BSP
     round trip each time would dominate, as the old BSP service delay did.
   - The PMM, the heap and the scratch slots are already safe on any CPU with
     interrupts off. Private memory allocation already works this way.
   - **Alternative:** keep growth on the BSP, reserving frames geometrically
     to limit round trips. That holds spare frames and keeps the queue delay.
2. **Retire the RAM FILE profile.** Default: yes.
   - `PROFILE_RIGHT_FILE` and `profile_file_begin/snapshot/end` count BSP buffer
     replacements: their capacity, copied bytes, queue and service times.
     Under decision 1 none of these exist.
   - Retiring them is a public ABI change: remove the operations and
     `struct profile_file_snapshot`, and update `iobench write/copy --profile`
     in userland together.
   - **Alternative:** keep the layout and redefine the fields for page
     allocation on the caller. That keeps a protocol that no longer describes
     anything worth measuring.
3. **Holes instead of reservation.** Default: holes.
   - RESIZE up and a write past the end leave unwritten pages as holes, so
     memory follows written data and a large RESIZE is cheap.
   - **Consequence:** RESIZE no longer reserves memory, so a later write inside
     the file's size can fail NO_MEMORY. Writes still never fail halfway.
   - **Alternative:** allocate and zero every page on RESIZE, keeping today's
     reservation behaviour at its memory cost.

## Measurement

Repeat the baseline above, matched, on main and on the change, three receives
each. Also run `iobench read` and `iobench write` (growing, 64 KiB buffer, and
`--prepared`) on `tmp://`, and record the boot log's heap line. The native
64 MiB receive into `tmp://` from the
[network throughput](../development/network-throughput.md#repeating-the-measurement)
runs is the owner's later check.
