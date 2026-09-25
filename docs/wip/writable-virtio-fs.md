# Persistent development through writable virtio-fs

Status: agreed milestone direction and short-write contract. This document
plans focused implementation PRs; it does not authorize implementing all tasks
at once. Discuss unresolved interface choices before starting their task, and
check off each task in the PR that completes it.

## Result and boundary

From the development shell, edit a C file in Kilo, compile it with TCC, and run
the native executable directly from `host://`. The second shell sees the same
export through read-only grants: it can read those files but cannot modify them.
After restarting Pyxis, the source and executable remain readable and runnable.

The [read-only backend](../virtio-fs.md), [filesystem mutation protocols](../filesystem-mutations.md)
and [per-CPU init selection](../init.md) are already implemented. Extend those
paths without changing CPU pinning, BSP allocation ownership or the sole FUSE
worker. Keep request buffers bounded and device-owned storage separate from
caller mappings. Transport concurrency and throughput tuning are not this task.

No overlays, replacement of `home://`, disk format, virtio-blk, guest account
system, symlink traversal, hard-link creation, directory moves, recursive
removal or cross-filesystem moves. Reconnection, unmount and guest data caching
remain deferred. Space titles are a separate small PR after this milestone.

## Authority and init policy

Init is trusted setup code. Each selected init keeps the full available
bootstrap grants; CPU numbers do not impose authority ceilings. Init chooses
which host-directory rights to delegate through session handoff. Neither
interactive shells nor their ordinary children inherit mount authority.

The development script will explicitly select a read-write host root. The
read-only script will select read-only access to the same export. A mount
selection restricts the returned grants; it must not toggle the shared backend
between globally writable and read-only states. Opening another root cannot
widen existing grants. Descendant lookups, retained working-directory handles,
session handoff and child launches must preserve the chosen rights. Do not
infer rights from the `host://` spelling or from a CPU index.

The opt-in host daemon still limits access independently. A read-only daemon
or inaccessible host file must not be bypassed. Guest requests initially use
the existing single host-service identity; this does not map Pyxis users to
host accounts. New-file mode/identity policy needs an explicit decision in the
creation task. Do not introduce placeholder user IDs or permission models.
See the [authority checkpoint](users-and-authority.md): the prototype boundary
does not settle persistent per-user homes, cross-user sharing or authentication.

Archive-only boots remain supported. `--optional` continues to tolerate only
an absent mount resource, not a failed operation. Finalize mount syntax and
what a writable request can establish about a host-read-only export before
implementation; do not probe writability by creating a disposable file or
silently fall back to read-only access.

## Write progress and failures

Permit short native file writes. A successful nonempty write returns a positive
count no larger than the supplied extent; callers advance the offset and input
by that count and submit only the remaining suffix. A zero-byte write still
validates authority and arguments, then succeeds without extending the file. No caller may spin on successful zero progress for
a nonempty request or accept a count larger than its request.

RAM-backed writes can continue completing in full. Replace the public
all-or-nothing promise rather than maintaining a second ABI or increasing a
version field. Audit libpyxis, libc and direct native callers together. Preserve
known progress and stream errors when a helper performs several native calls;
C element-count returns must still obey their own contract.

A timeout or lost reply can occur after the host performed a mutation. Reporting
failure does not prove that the file or directory was unchanged. Do not fabricate
a successful count, roll back unrelated host changes, or automatically replay
WRITE, creation, removal or rename. Define caller-visible status semantics for
this uncertainty before wiring mutations through the worker. Keep ordinary host
errors distinct from malformed replies and transport failure. Disk-full/quota
errors must not be mistaken for kernel heap exhaustion.

There is no multi-call transaction. Host processes and other spaces may change
the tree between requests. Existing non-atomic stdio append remains a documented
limitation; short-write handling must not accidentally claim atomic append.

## Synchronization and executable loading

A completed write, flushing a C stream, and requesting backing-storage
synchronization are different operations. Add explicit native file and directory
synchronization plus a small userspace command. Closing a capability or calling
`fflush` must not silently promise crash durability. Define how synchronization
errors reach callers; final-reference cleanup cannot be the only place to report
them. Directory synchronization matters for created, removed and renamed names.
The host filesystem/storage ultimately determines the durability of its reply.

Executable loading needs owned, stable bytes before calling the existing PXE
loader. Capture bytes through the host backend, validate the complete captured
image, and unwind storage and references on failure. The FUSE worker must never
access a caller's private pointers or mutate an active private address space.
Avoid holding locks across blocking reads or adding a loader-specific scheduler
framework.

An owned copy is stable after capture but is not a coherent host snapshot if the
source changes during capture. Size/metadata checks cannot prove otherwise.
Define bounds and the short-read/change contract before implementation. For the
initial workflow, require that an executable not be modified in place while
being loaded; recommend publishing replacements by atomic rename. Existing
handles retain their underlying file across replacement. Do not make program
launch execute from live host-backed mappings or claim snapshot isolation.

## Focused PR worklist

1. [ ] **Mount access and delegation.** Define explicit read-only/read-write
   selection in the native mount request and shell command. Audit host root,
   descendant and working-directory grants through both launch paths. Keep the
   backend's current lack of writes explicit until subsequent tasks provide it;
   do not publish a development profile that promises working writes early.

2. [ ] **Short-write contract and runtime consumers.** Replace the native
   all-or-nothing contract, specify counts and error/progress behavior, and
   update libpyxis/libc/direct callers. Decide transfer bounds and status mapping
   for host errors and uncertain mutation outcomes. Keep RAM behavior intact.

3. [ ] **File creation, write and resize.** Add the necessary FUSE operations
   and native backend dispatch. Settle new-file modes and open-handle access,
   preserve short progress, and account for protocol headers when bounding
   payloads. Capture write inputs before parking; handle returned-node/open
   ownership and failures explicitly. Exercise Kilo saves and TCC output on the
   host export through the supported mount path.

4. [ ] **Directory creation, removal and file rename/replacement.** Extend
   `mkdir`, `rm`, `rmdir` and `mv` through existing protocols and rights. Require
   host atomic operations for promised replacement/no-replace semantics; never
   emulate atomic replacement with delete-then-rename or no-replace with a racy
   existence check. Discuss name/type races with external host mutations and
   retained-handle behavior before finalizing the contract. Reject unsupported
   guarantees explicitly rather than pretending to enforce them locally.

5. [ ] **Explicit synchronization.** Define file/directory requests, required
   rights, runtime helpers and a small command with explicit target paths.
   Document ordering for saving a file and publishing a renamed replacement.
   Report errors separately from ordinary handle release; do not redefine libc
   `fflush` as a disk-sync operation or imply a global sync from a single file.

6. [ ] **Host-backed executable launch.** Settle capture limits and concurrency
   behavior, obtain owned bytes through the existing worker, and feed the
   current loader. Update any userspace image-inspection paths that assume
   in-memory backing. Preserve process ownership and failure unwinding. Run a
   host-backed executable produced by the guest TCC.

7. [ ] **Init profiles, persistence walkthrough and completion.** Finish the
   packaged development/read-only mount selections, document writable daemon
   setup and run the acceptance workflow below. Carry any remaining limitations
   into technical debt. Fold the completed implementation reference into
   `docs/virtio-fs.md`, remove this WIP plan and update links; Git retains the
   original task list.

## Ordinary validation

Use normal builds, QEMU boots and manual debugger inspection where useful; add
no tests, self-tests, fault injection or boot/output automation. Each PR should
exercise the operation it introduces with ordinary tools. The final walkthrough
uses a user-selected host directory, a writable daemon and both interactive
spaces:

- CPU 1 creates a directory, edits a C source file with Kilo, compiles it using
  TCC and runs the resulting `host://` executable.
- CPU 2 reads the source and output through its own root. Attempts to create,
  write, resize, remove or rename through its read-only grants fail without
  modifying host files. Children retain the same restrictions.
- Exercise file rename/replacement, removal, and retained open-file lifetime.
  The read-only view is live, not an immutable snapshot of CPU 1's files.
- Request synchronization for the files and affected directories, exit QEMU,
  restart the daemon/guest normally, then read and run the saved files again.
  A normal reboot check does not establish power-loss durability.
- Boot without the host device to retain the existing optional-mount behavior.

After this milestone, add space titles in their own PR: a bounded title for the
caller's own space, surviving init exit, set through a shell builtin such as
`title "Development"`. Retain fixed tab widths and existing navigation. Exact
text limits/encoding and title authority are decisions for that follow-up.
