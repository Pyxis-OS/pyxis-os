# Program images and initial stacks

Chronium accepted request-sized launch storage, file-sized immutable image
capture, variable-size atomic batches and explicit stack sizes bounded by the
address layout on 2026-10-30
([limits audit WP3](../interfaces/limits.md#launcher-capacity)). Boot, ordinary
launches and space creation share the loader policy. Allocation failure remains a real
capacity refusal; no guessed RAM fraction replaces the removed size ceilings.

## Image admission

[P1F loading](../../kernel/image.c) accepts fixed, page-aligned segment addresses
in the canonical lower half, starting at `0x1000`. The SDK linker convention
remains `0x400000`; it is not a required minimum. Segments must be ordered and
disjoint after page rounding, readable, and never writable and executable
together. The entry must lie inside an executable segment's unrounded extent.
Payload bounds, arithmetic overflow and canonical-address checks remain.
A P2F image launches as the same P1F image: its symbol section must exactly end
the file and is never mapped or read beyond its two size fields.

There is no 256 MiB total-span policy. Gaps between segments consume no physical
backing. Every segment must still fit the user address space and avoid the
selected stack and guard; these layout checks precede destination backing.
An inactive private VM owns eagerly zeroed frames, including BSS and page
padding. Payload copying uses the preparation CPU's scratch alias; final
permissions apply before publication. No process executes from a writable
loader alias or a live source-file mapping.

## Initial stack and ownership

[Common process loading](../../kernel/user/load.c) owns placement and size.
A zero `launch_request.initial_stack_bytes` selects the unchanged 1 MiB default.
An explicit request is positive, page-aligned and must leave room for the lower
guard within the user address layout. No 1 MiB minimum or 8 MiB maximum remains.
Malformed alignment returns BAD_REQUEST; an unrepresentable stack/guard span
returns CALL_LIMIT with stack-address detail. There is no clamping.

| Region | Default addresses and backing |
| --- | --- |
| Initial stack | `[0x7fffffeff000, 0x7ffffffff000)`, 1 MiB eager zeroed user RW/NX |
| Lower guard | `[0x7fffffefe000, 0x7fffffeff000)`, one reserved unmapped page |

Bundles select larger or smaller eager backing through their manifest. The base
is `0x7ffffffff000 - initial_stack_bytes`; the reserved guard is one page below.
Single, batch, space-creation and boot launches use the same common loader.
The top is canonical and 16-byte aligned. Stack and guard reservations prevent
later startup or private allocations from occupying those ranges.

**Automatic stack growth is deferred.** Plain programs still receive 1 MiB and
can fault when they exhaust it. A large stack adjustment can skip the one-page
guard. MEMORY_RESERVE/COMMIT does not authorize VM mutation from fault handlers;
no demand paging, stack-fault recovery or new thread-stack policy is introduced.

Failures destroy the unpublished process and its backing through the existing
preparation/retirement owners. Normal retirement reclaims task storage on its
departure CPU's fixed RAM actor, then process-owned backing on the recorded RAM
owner before publishing completion. Device/session hooks retain their owners.
See [process lifetime](../interfaces/processes.md) and
[VM ownership](vm-ownership.md#preparation-and-ram-reclamation).

## File capture and costs

HOST, NPFS, FAT32 and RAM executable capture allocates page-rounded backing from
the actual selected file size. There is no 128 MiB per-image ceiling or aggregate
capture quota. Immutable boot archive images load in place without a copy.
Physical allocation or VM storage can still fail with CALL_NO_MEMORY. A large
BSS needs destination backing beyond the serialized bytes. Snapshot and child
backing coexist; a batch retains earlier prepared children during later capture.
Removing the policy ceilings does not eliminate those memory costs.

[Capture ownership](../../include/kernel/user/image_capture.h) retains one owned
snapshot through every loader read. The file/I/O producer prepares RW/NX kernel
backing under bounded shared-owner admission, releases that owner before I/O,
and transfers one immutable read phase to caller-CPU preparation. RAM and native
filesystem capture serialize their source operation through copying; ownership
ends exactly once. HOST accepts positive short reads and rejects observed size
changes or premature EOF, but equal before/after lengths do not prove that a
concurrently modified host file was coherent. Do not modify it in place while
loading. Source errors are not retried.

Partial allocation unwinds its mapped prefix. AP-read captures, or captures
retired away from their producer CPU, require confirmed shared-range retirement
before VA/backing reuse. Unconfirmed retirement retains backing and cleanup
attribution until reboot; late ACKs cannot reclaim it. Existing shared kernel
page-table ancestors retain their ordinary lifetime.

## Capture, startup and refusal

Launch capture owns request-sized arrays/strings; startup metadata and writable
argv use measured, page-rounded mappings. The native environment has its own
[admission policy](../interfaces/environment.md); its names/text are not charged
a second time against an artificial startup budget. Metadata validation and
known policy/encoding refusal precede image payload capture. Every batch stage's
metadata is preflighted first; images prepare sequentially before one publication.

CALL_LIMIT includes structured reason and known requested/maximum quantities.
CALL_NO_MEMORY reports actual allocation failure; malformed inputs retain
BAD_REQUEST and invalid user ranges BAD_BUFFER. No available-RAM estimate is
promised as a reservation. Failed preparation publishes no runnable child and
preserves source grants. Shell redirects, stdio flushes and provider effects are
not rolled back; an untrustworthy publication reply is OUTCOME_UNKNOWN and must
not be retried automatically. See [batch launch](../interfaces/processes.md#batch-launch)
and the [limit diagnostic ABI](../interfaces/abi.md#limit-diagnostics).

The [WP3 comparison](../development/experiments/launcher-capacity/README.md)
records matched QEMU launch, pipeline and executable-copy costs. Historical
[stack](../development/experiments/program-capacity/README.md) and
[capture](../development/experiments/program-bundles-capture/README.md)
qualification describe the earlier chosen ceilings and why the default stack
returned to 1 MiB. Their timings are historical, not qualification of WP3.

## Mapping images into a running process

`MEMORY_MAP_IMAGE` maps a P1D or P2D image (`include/pxe/p1d.h`) from a FILE
into the calling process; `MEMORY_SEAL_IMAGE` makes its RELRO segments
read-only once. The dynamic loader uses them; `include/abi/memory.h` has the
call contract. Accepted in the
[dynamic-linking milestone](../wip/dynamic-linking.md#loading-and-authority).

- **Authority.** The FILE needs READ and, for a library, `FILE_RIGHT_IMPORT`,
  which a file gets only from a directory with `DIRECTORY_RIGHT_IMPORT`. A dynamic
  executable needs `FILE_RIGHT_PROGRAM`, which only a launch grant adds to a
  readable file. IMPORT cannot map an executable and PROGRAM cannot map a
  library, so launching a loader never imports a library from a directory
  without IMPORT.
- **Sources.** Package views (`pkg://` and command views) carry IMPORT when the
  packages root does. Native and RAM npfs volume roots can be opened with it,
  and boot init's RAM directory holds it so that a RAM volume root can ask for
  it. The SDK view, `tmp://`, HOST and FAT32 roots never carry it.
- **Capture and checks.** The file is captured as a launch image is, then
  checked by the shared container rules: the header, segment order, W^X,
  RELRO segments read-write and not executable, the dynamic section inside one
  read-only segment, the entry rules and, for P2D, a symbol section ending the
  file. The kernel never reads the dynamic tables.
- **Placement.** An executable maps at its fixed addresses; any occupied range
  fails the call with BAD_REQUEST. A library maps at a base the kernel chooses,
  with the gaps between its segments reserved too.
- **Mappings.** Each segment gets fresh zeroed frames, its file bytes copied
  through the kernel alias while the process's threads are quiesced, and its
  final permissions before the call returns. The pages count as committed
  private memory. Nothing releases them before process exit, and failure
  unwinds every mapping made by the call.
- **Seal.** `MEMORY_SEAL_IMAGE` turns the image's RELRO segments read-only. A
  second seal, or an identity that is not one of the process's images, is
  BAD_REQUEST. No operation makes the pages writable again.

