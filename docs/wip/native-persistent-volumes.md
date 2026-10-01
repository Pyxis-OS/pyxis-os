# Native persistent volumes

Status: agreed follow-on milestone scope, 2026-09-30; implementation waits for
the [writable core and recovery milestone](writable-filesystem-core.md). Detailed
native authority and lifetime mappings are task 1 decisions, not frozen APIs.

Completion means editing and compiling a source file on a native Pyxis volume,
explicitly syncing it, rebooting, and reading/running the persisted results.
A separately restricted session must remain unable to mutate that volume.
The existing bootloader/kernel/initrd path remains in use; no installer, FUSE
adapter or NVMe driver is required.

## Integration boundary

Extend the existing [native filesystem adapter](../devices/filesystem-native-adapter.md)
and its serial BSP worker. Preserve BSP allocation/VM ownership and existing
request publication, cancellation and deferred cleanup rules. Do not add a
second filesystem implementation or perform backing I/O in interrupt context.
Before linking or enabling the shared writer, resolve the
[kernel-stack prerequisite](../technical-debt.md#writable-filesystem-kernel-stack-prerequisite):
the reviewed private edit/validation/codec path exceeds the current 16 KiB
kernel-task stack. Prefer caller-reserved workspace for large temporaries and
verify the full nested path including outer worker/adapter frames.

Trusted init selects partition, volume and namespace binding. Writable mounting
and observation remain explicit authority choices; ordinary programs cannot
reacquire rights omitted by a launcher. Retain the configured bootstrap principal
and persistent acquisition model, parent-controlled creation ownership and the
absence of any owner bypass. Authentication, policy administration and per-user
home namespace design are separate work.

Multiple mounts inside the same kernel must use a coherent shared writer/backing
instance. Read-only capabilities restrict operations without secretly exposing
a stale snapshot. Refuse independent host/guest access to changing media. Task 1
settles read-only-to-writable mount transitions and exact attachment policy; a
later writable request must not widen an existing grant.

Map writes, resize, creation, removal and regular-file rename to existing native
protocols where their contracts fit. Explicitly reconcile differences in rights:
the core separates write, resize, checkpoint and replacement while current OS
interfaces bundle some operations. Do not silently add persistent permissions
merely to preserve an existing OS grant spelling.

File/directory sync uses the agreed volume checkpoint. Document that it can
persist sibling changes and which held rights authorize the request. Closing or
libc flushing is not the public durability boundary. Preserve short-write and
uncertain-outcome semantics from the core; never turn a failed required flush
into success or a guessed no-progress result.

Live handles preserve identity across rename and unlink and observe committed
updates. Enumeration must detect invalidated continuation rather than treat a
token from an old tree as a position in a new one. Executable capture needs a
coherent operation view. Group termination cannot free state borrowed by active
operations or discard necessary orphan/recovery bookkeeping.

## Focused tasks

1. [ ] **Settle native writable contracts.** Review the completed core and
   existing directory/file/mount ABI. Agree exact authority bundles, sync scope,
   mount-mode coexistence, live-handle/continuation/capture rules and errors.
   Identify any narrowly necessary ABI changes before implementation.
2. [ ] **Enable explicit writable mount acquisition and delegation.** Extend
   trusted init/mount configuration and worker attachment. Preserve defaults and
   existing read-only grants unless explicitly changed; exercise restricted
   session handoff without hard-coded CPU authority.
3. [ ] **Integrate mutation and lifetime.** Requires resolution of the writer
   stack prerequisite above before enabling these paths in Caelum. Wire existing operations to the
   shared core, including bounded progress, namespace changes, retained unlinked
   objects and deferred final cleanup. Validate coherent views across separately
   held grants and enumeration invalidation.
4. [ ] **Integrate checkpoint and failure reporting.** Map file/directory sync
   to the agreed volume checkpoint and propagate device/flush/uncertain outcomes.
   Document refusal and recovery workflow, with no silent repair or successful
   placeholder operations.
5. [ ] **Validate the persistent development loop and close the milestone.**
   Use Kilo and TCC to create/edit/build/run through a native binding, sync,
   stop and reboot normally, then compare source and execute the saved program.
   Exercise read-only denial, same-volume rename/replacement, retained handles,
   cleanup and default no-disk/HOST regressions. Check detached media with the
   host tools. Record limits and convert this plan into a concise reference.

Validation uses ordinary builds and interactive QEMU/remote/GDB work on disposable
images. Report nested-KVM configuration and exact dependency revisions. Crash
recovery evidence comes from the explicitly scoped core validation; this milestone
does not independently authorize kernel fault injection, new CI or boot automation.
An orderly reboot demonstration alone is not a power-loss test.

Publish any dependency PRs before pinning them in Pyxis and state merge order.
The final image must use the reviewed shared core, rather than substitute host
tool behavior for native filesystem integration.
