# Shared-memory ownership and withdrawal

Programs can declare native region creation authority, share a fixed RAM buffer
through ordinary capabilities, and map it read-only or writable in independent
processes. Creator exit and handle close alone do not revoke recipients. A
successful explicit CUT withdraws mappings derived through that branch.

The [interface](../interfaces/shared-memory.md) defines operations, SDK/libc
helpers, declared grants, errors and lifetime. The opt-in
[shared-min example](../../userspace/examples/shared-min/README.md) demonstrates
acknowledged buffer reuse and joined worker cleanup. These paths preserve
[VM ownership and retirement](vm-ownership.md); they do not change malloc or
provide POSIX mappings, shared libc mutexes, GPU/DMA or compositor integration.

## Owned storage

[Region backing](../../kernel/mm/shared.c) owns a fixed page vector of eager,
zeroed WB RAM and a retained origin account. It needs neither physically
contiguous pages nor a permanent kernel alias. Origin metadata keeps the space
and non-reused process-list key, not the creator process or its VM. Aliases and
transfers retain backing without duplicating or moving its physical charge.

[Shared views](../../kernel/mm/shared_view.c) belong to the target process and
hold their backing, persistent source-lineage registration and cleanup storage
before exposure. MAP enrolls under the same lineage guard as CUT: CUT either
prevents publication or includes that view. Registrations retain logical holder
grants, never controller authority. RETARGET affects future resolution, not
already-mapped backing. View identities are process-local and non-reused.

Physical backing, virtual views, shared metadata and private page tables have
separate accounting. Empty private table ancestors can remain charged until
ordinary VM destruction. Accounting is not a hard quota or reservation of core
RAM; [the accepted budget limit](https://git.internal/PyxisOS/pyxis-os/issues/1655)
tracks when to revisit isolation.

## Withdrawal and retirement

CUT schedules pre-reserved owned work on the existing fixed RAM actors. Its
callback does not allocate, wait or mutate a target VM under the forwarder
guard. The queue guard is released before entering an external process-VM
transaction; one process mutation is owned at a time.

[External VM admission](../../kernel/process_vm.c) closes root entry, joins a
preceding mutation/reply and drains actual dispatched roots and short mapping
borrowers before PTE changes. With PCID/PGE disabled, departure/reload and
non-reused translation generations close stale access; a shared-kernel flush is
not a substitute for private-root drainage. Pending work yields ownership rather
than parking a RAM actor behind a live process.

Explicit CUT success is an unmap fence. One absolute one-second observation
budget covers the operation, not each target; it does not guarantee syscall
return by that time. Elected uncertainty stays OUTCOME_UNKNOWN, requests process
termination and quarantines uncertain backing until reboot. A self-targeting
revoker completes stopped cleanup from the kernel root without reopening its
uncertain private root. Independent branches and already-copied bytes remain
unaffected.

Actual CPU root/stack departure still precedes reclamation. If it cannot be
proved, execution records and WAIT remain pending. After safe departure and
detachment, process/group completion can finish while independently charged
quarantine remains. Late ACKs never make that backing reusable. This distinction
is separate from the general [VM retained-cleanup limit](https://git.internal/PyxisOS/pyxis-os/issues/1514).

Last-controller close queues the same withdrawal without sleeping; CLOSE is not
an unmap fence. Final-task departure queues shared-alias removal before ordinary
process cleanup can destroy the VM; the retained view pin is released after that
removal. Final region destruction transfers reclamation
to its preparation CPU's RAM actor, with cleanup attribution retained until
bounded physical and metadata freeing actually finishes. Queue acceptance does
not mean memory was freed.

## Qualification and remaining limits

The [record](../development/experiments/shared-memory.md) separates ordinary
builds, source inspection, 1/4-CPU QEMU behavior, debugger/accounting observations
and native vela results. The native shared-min run passes input counts through
20 MiB with joined cleanup; its latency includes Remote dispatch and launch.
The current native controls supply no fresh A/B ratio or independent boot range.

Native CUT/races, injected lost CPU/ACK and allocation failures, generation
exhaustion and a complete stale-physical-frame-reuse proof remain
[deferred qualification](https://git.internal/PyxisOS/pyxis-os/issues/1656).
The successful finite-budget uncertainty/quarantine checks are recorded; they
are not lost-CPU simulation. Further fault-injection scope needs separate approval.
