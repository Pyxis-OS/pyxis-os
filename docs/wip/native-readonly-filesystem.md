# Native read-only filesystem mounts

Status: selected next milestone, 2026-09-30. This records agreed scope and
implementation tasks; no kernel mount or new ABI is implemented by this document.
Resolve the task-1 contract before starting implementation. Update each checkbox
in the PR that delivers that task.

## Completion point

Create a populated Pyxis pool image with the existing host tools, attach it as a
GPT partition through virtio-blk, and have trusted init mount one volume under a
chosen namespace binding. Ordinary `ls`, `cat` and executable loading must work
through the returned directory/file capabilities. Fastfetch then reports the
accessible mount and accurately labeled capacity information.

This uses the existing bootloader, kernel and initrd. A disk is optional; an
unconfigured boot keeps the existing archive/RAM/HOST sessions. Native writes,
recovery and installation remain separate milestones.

## Existing foundation

- [Block storage](../devices/block-storage.md) supplies bounded ticketed I/O and
  waits; [GPT discovery](../devices/gpt.md) supplies a boot-time partition map.
- The [shared filesystem core](../devices/filesystem-readonly.md) opens committed
  states, acquires policy-bounded views, traverses directories and reads files.
  Its [interfaces](../../fs/docs/core.md) and [format](../../fs/docs/format.md)
  remain authoritative. Reuse this core, not a second kernel format parser.
- Existing [filesystem objects](../interfaces/filesystem-mutations.md),
  [init/session handoff](../userland/init.md) and namespace bindings provide the
  application-facing model. The current mount capability selects the HOST export;
  it does not yet select a block partition or native volume.
- [BSP requests](../kernel/bsp-service-requests.md) separate request ownership from
  scheduling. The filesystem adapter must also obey [memory](../kernel/memory.md)
  and [SMP](../kernel/smp.md) rules.

## Agreed selection and authority

Partition, volume and namespace selection belong in init. Illustrative syntax,
not an implemented command or a frozen parser spelling:

```sh
mount --partition 1 --volume system --read-only data://
```

The mount capability identifies the authorized disk. The partition number selects
an entry on that disk, not an arbitrary global device. Resolve the volume name
once when opening and retain pool/volume identities. A name being reused must not
retarget an existing mount. Namespace binding names such as `data://` are not
stored in the filesystem or derived automatically from volume names.

The kernel binds a configured bootstrap principal to trusted init's mount
authority. Applications cannot nominate a different principal in a request;
there is no `--principal` argument or authority derived from a volume's owner.
The core must evaluate persistent grants within the trusted root, scope and
rights ceiling before returning a root view. Knowing principal/object IDs is
neither authentication nor access. The supplied image and trusted boot setup
must agree on the intended principal; mismatches fail acquisition.

Init chooses what to mount within its authority, then delegates the resulting
root through session handoff. Children need ordinary directory/file grants to
browse and read, not mount or raw-block authority. Closing mount authority must
not revoke independently retained roots. This introduces neither a login system
nor a user-directory/PCA implementation. The longer-term
[identity direction](users-and-authority.md) still applies.

## Read-only integration boundaries

- Bound all reads to the selected GPT partition and translate the core's 4 KiB
  filesystem blocks through the device's logical sector geometry. Use existing
  transfer limits and exact-read/error contracts; do not expose raw I/O to apps.
- Keep the underlying image unchanged while open. Read-only guest access alone
  does not prevent host writes. Development instructions must prohibit concurrent
  host mutation and use read-only QEMU attachment. No live host refresh, hotplug
  or writable co-mount is supported.
- Preserve the core's selected-generation views and degraded-read selection
  rules. A successful open validates bounded candidates/root metadata, not the
  whole image. Ordinary operations validate traversed structures; a full check
  remains a distinct host operation. Never turn a media, unsupported-format or
  resource-limit error into an empty directory or successful short backing read.
- The core is serial and its handles cannot be copied. Kernel object references
  must retain the backing pool, volume and view for outstanding operations and
  delegated copies. Cancellation/exit must release queued work and retained
  references only when ownership permits; cleanup must not race an active read.
- Synchronous core callbacks may need to wait for block completion. Run that work
  in a suitable BSP task context; do not block an IRQ handler, hold a lock across
  a wait, or park the existing interrupt-disabled service executor inside the
  core. Keep core allocation on the BSP with bounded memory accounting.
- Map native directory lookup/enumeration, file length and offset reads onto held
  core rights. Persistent acquisition and subsequent capability delegation must
  not accidentally widen each other's rights. In particular, file read authority
  does not silently grant the core's distinct metadata rights.
- Mutation attempts must fail without disk writes, whether denied by the grant
  or rejected as read-only by the backend. No write/flush path, COW allocator,
  timestamp updates, repair or format migration is introduced.
- Existing archive, RAM, HOST and user-provider behavior stays intact. Reuse the
  existing file/directory protocols where their contracts fit; no generic VFS
  redesign or new backend framework for this milestone.

## Filesystem information and Fastfetch

Expose a bounded observation associated with an authorized mounted filesystem,
not an ambient list of every disk or another space's mounts. Namespace labels
come from the caller's bindings; pool and volume names/IDs remain separate.

Volumes share pool storage, grow under guarantees and limits, and have no simple
fixed partition-sized capacity. Distinguish pool usable capacity, per-volume
charged bytes, guarantees and any configured upper limit. Do not sum shared pool
capacity once per volume or present pool-wide free space as a volume's writable
allowance. Read-only access does not imply zero free physical space.

Before exposing usage, specify which counters are verified, merely recorded in
the selected generation, or unavailable. Bounded root validation does not
establish global allocation totals. Do not run a whole-image checker on every
Fastfetch invocation or manufacture a Linux-shaped used/total percentage. The
first useful display can report the binding, filesystem type, read-only status
and pool capacity; add usage only where its evidence and meaning are explicit.

Use the existing [Fastfetch port](../userland/fastfetch.md) as the final consumer:
add the native Disk adapter and only the shared-source changes its real platform
boundary needs. Preserve upstream formatting, allocation and diagnostics. Do not
implement arbitrary other disk backends or optional upstream discovery features.
Local-IP display is a separate future follow-up using existing networking
facilities; it does not expand this storage milestone.

## Contracts to settle in task 1

These details remain open, rather than implicitly approved implementation choices:

- How trusted boot configuration supplies the principal and selects the disk
  authority; explicit behavior when either is absent. Keep deployment values out
  of hard-coded per-CPU authority rules.
- The native mount request/rights shape alongside existing HOST mounting; exact
  rights mapping for root acquisition, lookup, enumeration, metadata and file
  reads. Define which authority permits filesystem information and its disclosure
  of shared-pool capacity/accounting.
- Worker ownership, bounded request/memory limits, cancellation and deadlines;
  repeated mounts of the same pool/volume, shared instances and final cleanup.
  Do not import host-checker memory defaults into a 256 MiB kernel unchecked.
- How selected read-only/degraded state, unsupported formats, media errors and
  denied access map to existing call errors. Missing optional hardware must be
  distinguishable from a configured mount failing acquisition or validation.
- The init command spelling, namespace/session forwarding, binding limits and
  required versus optional mount behavior. No silent fallback to another disk,
  partition, volume or principal.

## Focused PR tasks

1. [ ] **Settle the native mount contract.** Inspect the pinned core and current
   block, object, init and namespace interfaces. Resolve the questions above with
   a concrete rights/lifetime/error mapping and agreed runtime bounds. Record the
   first displayable capacity fields. This is a design PR, not a broad framework
   implementation or a repeat of the filesystem-format design.
2. [ ] **Link the core and implement bounded block/memory adapters.** Integrate the
   pinned freestanding library into Caelum with kernel-appropriate flags; add
   partition-bounded reads, memory accounting and the agreed BSP worker ownership.
   Document manual preparation of a disposable GPT disk using existing host tools.
   Verify opening the selected pool/volume through normal boot and debugger
   inspection without a permanent diagnostic application or automatic probe.
3. [ ] **Provide read-only directory/file objects.** Implement policy-approved
   acquisition, enumeration, lookup, length and offset reads through shared-core
   views, including retained object lifetimes and failure cleanup. Enforce the
   agreed rights mapping and read-only backend errors. Keep existing backends
   working. Share internal mount preparation needed by this and the next task;
   do not publish unusable placeholder APIs.
4. [ ] **Mount and delegate from init.** Add the agreed mount ABI/library/command
   support and namespace/session forwarding. Init chooses partition, volume and
   binding through disk-scoped authority. Exercise `ls`, `cat` and launching an
   executable from the disk, plus absent disk, wrong selector, policy denial and
   rejected writes. Default boot needs no development disk.
5. [ ] **Expose scoped filesystem information.** Implement the settled bounded
   query and library interface. Document field units, shared-pool meaning,
   verification status, unavailable fields and observation authority. Read queries
   must not trigger a full consistency scan or acquire additional authority.
6. [ ] **Adapt Fastfetch Disk.** Add the minimal native adapter, explicit resource
   forwarding if needed, recipe/pin changes and normal image integration. Exercise
   local/remote display, redirected text/JSON and absent disk/query authority.
   Preserve unavailable-value and upstream error behavior; do not broaden the port.
7. [ ] **Validate the combined workflow and close the milestone.** Rebuild and boot
   normally with and without the disk; inspect content against the source import,
   nested traversal, executable loading, delegated restrictions, repeated opens
   and process cleanup. Use existing host checking tools before attachment and
   confirm the image is unchanged afterward. Record actual coverage and limits,
   then move this WIP to the devices references and update the index.

Use ordinary builds, interactive QEMU and debugger inspection. The existing
remote client is suitable for command work; use screenshots only for presentation
checks that need them. No new tests, self-tests, fault injection, CI or boot/output
automation are authorized by this plan. Existing CI must be checked for submitted
revisions; distinguish host-only checks, nested KVM results and owner-hardware runs.

## Repository ownership and deferred work

Pyxis owns kernel adapters, ABI, SDK and image integration; pyxis-fs owns the
format/core and its host tools. Userland owns wrappers and mount/session commands;
ports owns Fastfetch. Publish dependent commits/PRs before updating gitlinks and
state merge order. Ordinary ABI/library changes need a new SDK, not a compiler
container rebuild. Assess any actual new host/toolchain requirement separately.

Writable transactions, recovery, reclamation, live-generation semantics, unmount
or hotplug administration, Linux FUSE, NVMe, installation, on-disk boot and a full
identity broker remain outside this milestone. It does not prove power-loss
recovery, file-data integrity checksums or production-data safety. The existing
[format/host limits](../technical-debt.md#filesystem-host-prototype-limits) remain
visible; carry any newly accepted implementation limits into technical debt.
