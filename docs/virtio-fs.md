# Virtio-fs filesystem backend and host setup

The optional virtio-fs device provides a `host://` root through native
directory/file capabilities. Init opens the selected export and delegates its
root through the session launcher to the shell and ordinary children. Existing
`ls` and `cat` use it without PCI, VirtIO or FUSE knowledge. Read-only grants
can also launch native executables from host files. Read-write grants support
file/directory creation, file writes and resize, removal and file
rename/replacement. Ordinary boot needs neither the device nor the daemon.

The supported platform is QEMU Q35 with firmware-assigned PCI resources, modern
VirtIO PCI transport and split queues. See [PCI discovery and resources](pci.md)
for MCFG/ECAM, BAR ownership and MSI-X routing. Only `VIRTIO_F_VERSION_1` is
negotiated; legacy transport, packed queues, indirect descriptors and DAX are
unsupported. There is no host overlay or change to `app://`. The
[development overlay](wip/host-development-overlay.md) remains a separate idea.

## Start the host service

The exercised host combination is Linux, QEMU 10.2.2 and virtiofsd 1.14.0.
The guest targets FUSE 7.38 with no optional FUSE features. Install virtiofsd
through your host distribution if needed; it is often separate from QEMU and
may live at `/usr/libexec/virtiofsd`. The build does not download or start it.

Choose a directory to export. In one terminal, run the daemon in the foreground:

```sh
export_dir="$HOME/pyxis-share"
socket_dir="${XDG_RUNTIME_DIR:-/tmp}/pyxis-virtiofs-$(id -u)"
mkdir -p "$export_dir"
(umask 077; mkdir -p "$socket_dir")
chmod 700 "$socket_dir"

unshare -Ur -- /usr/libexec/virtiofsd \
  --shared-dir "$export_dir" \
  --socket-path "$socket_dir/fs.sock" \
  --sandbox namespace \
  --inode-file-handles=never \
  --no-announce-submounts \
  --rlimit-nofile=0
```

Change the executable path if your distribution installs it elsewhere. `unshare`
comes from util-linux. This command requires unprivileged user namespaces; the
host user appears as root inside that namespace without gaining host-root
access. The daemon can access only files that the launching host user can access.
Use the same host user for QEMU so it can connect to the socket. The private
socket directory restricts access to that user.

This starts a writable development export. Add `--readonly` to the daemon
command to restrict the service independently of guest grants, including the
development init's read-write grant. The host user's file permissions also
apply. `--inode-file-handles=never` uses file descriptors rather
than privileged filesystem handles. Submount announcements are disabled because
the guest does not negotiate them. `--rlimit-nofile=0` keeps the inherited host
file-descriptor limit; it does not set the limit to zero.

## Connect QEMU

In another terminal, from the Pyxis repository:

```sh
socket_dir="${XDG_RUNTIME_DIR:-/tmp}/pyxis-virtiofs-$(id -u)"
make run CPUS=4 VIRTIO_FS_SOCKET="$socket_dir/fs.sock"
```

Usual toolchain, firmware, display and accelerator overrides still apply.
`make debug` accepts the same socket option. The script checks that the socket
exists and adds a shared memfd RAM backend plus a modern `vhost-user-fs-pci`
device tagged `pyxis-host`. The memfd size follows `MEMORY`. Socket paths must
not contain commas, which delimit QEMU options.

Successful initialization logs the prepared queue addresses followed by a FUSE
session-ready message. Default init selections start shells on CPUs 1 and 2
when present; Super+Right selects their tabs. Each interactive init mounts the
same export before session handoff. The development profile on CPU 1 delegates
read-write grants; the read-only profile on CPU 2 delegates read-only grants.
Both shells start in the shared RAM-backed `home://` directory, so use an
explicit `host://` path or `cd host://` to reach the export.
From the shell, try:

```text
ls host://
cat host://hello.txt
cd host://
ls .
```

Create `hello.txt` in the exported host directory first. The prompt tracks the
new working directory and children inherit it. In the read-only session,
writes fail with DENIED even while the development session can mutate the same
export. Native executables can be launched from readable host files; the loader
captures an owned image first. See [processes](processes.md#implemented-userspace-launch)
for its size limit, errors and host-change contract.

Exit QEMU with Ctrl-a x in its serial terminal, or use the monitor's `quit`.
Stop the daemon with Ctrl+C if it remains running. After **both** have stopped,
remove any leftover socket before starting a fresh pair:

```sh
rm -f "$socket_dir/fs.sock"
```

Start a new daemon for each QEMU run. Do not remove a socket belonging to a live
service. Omitting `VIRTIO_FS_SOCKET` returns to the ordinary archive-only boot.

If `unshare` reports `Operation not permitted`, the host disallows this namespace
setup; enable it through your host's policy or use an administrator-managed
virtiofsd service with the intended access. If QEMU reports connection refused,
check the daemon terminal and remove a stale socket only after confirming that
the daemon stopped.
If the daemon reports too many open files later, increase its inherited file
limit before launch. No socket option weakens the daemon's export permissions.

## Persistent development walkthrough

With the writable daemon above and the default init selections, CPU 1 can use
Kilo, TCC, `mkdir`, `rm`, `rmdir` and `mv` in the export. CPU 2 sees the live
tree through read-only grants; its attempted mutations fail even when the host
daemon permits writes. Neither CPU number nor the `host://` name sets authority:
the selected trusted init delegates the root grants. See
[init selection](init.md#boot-selection) and the
[future user/authority checkpoint](wip/users-and-authority.md).

In CPU 1's shell, create a directory and edit a source file there:

```text
cd host://
mkdir work
cd work
kilo hello.c
```

Enter this program, save with Ctrl-S and quit Kilo with Ctrl-Q:

```c
#include <stdio.h>

int main(void)
{
  puts("Hello from host storage");
  return 0;
}
```

Compile and run it in the same shell, then request synchronization for both
files and the affected directories:

```text
tcc hello.c -o hello.pxe
./hello.pxe
kilo hello.c
tcc hello.c -o hello.pxe
./hello.pxe
sync host://work/hello.c host://work/hello.pxe host://work host://
```

On the second Kilo visit, change the message, save and quit before rebuilding.
In CPU 2's shell, `cat host://work/hello.c` reads the same source. Attempts to
`mkdir host://work/blocked`, remove or rename that source with `rm` or `mv`, or
save an edit in Kilo fail under the read-only grant. Stop QEMU and the daemon
as described above, start a fresh daemon and QEMU pair using the same export
directory, then read and run the saved files again:

```text
cat host://work/hello.c
host://work/hello.pxe
```

This is a manual persistence workflow, not a power-loss durability guarantee.
Each sync covers only its named file or directory, and the host service and
storage determine what an acknowledgement means. No write probe or read-only
fallback occurs at mount time. An absent device is tolerated by `--optional`;
an operational mount failure stops init.

New regular files request mode `0644` under the existing host-service identity,
with no guest chmod or umask API. Creation is exclusive: an existing name returns
ALREADY_EXISTS rather than replacing or truncating it. Resize is a separate
operation. Host permissions and a daemon's `--readonly` remain independent limits.
A successful save or close does not promise durable storage. Use explicit file
and directory synchronization when the host service's durability contract
matters; see [synchronization](#synchronization).

## Queue and worker contract

`virtio_fs_pci_prepare` allocates and maps both queues on the BSP before AP
startup. Each owns a contiguous 20 KiB physical extent: one page of ring storage,
an 8 KiB request buffer and an 8 KiB reply buffer. Mappings are kernel-only,
read/write, non-executable, ordinary write-back RAM. Device MMIO remains uncached.
CPU virtual addresses are never used as descriptor addresses.

Each split queue selects 16 descriptors, or the device's smaller supported
power-of-two size; fewer than two is unsupported. One direct descriptor chain
may be in flight per queue. There is no indirect-descriptor or event-index
negotiation. Ordinary requests and high-priority FORGET requests are serialized
by the sole worker. FORGET has no writable descriptor or FUSE reply; its used-ring
completion still returns ownership of the request buffer.

Submission publishes the request and descriptors before the available index,
then uses a full ordering barrier before inspecting device notification
suppression and sending the 16-bit queue notification. Driver interrupt
suppression stays clear. Completion observes the used index before consuming
its entry and reply. It validates the index advance, descriptor head and written
length against the outstanding buffer; a malformed completion stops the device.
These barriers rely on coherent x86 DMA and ordinary write-back RAM, without an
IOMMU. Buffers remain device-owned until completion or a confirmed reset.

After task initialization, `virtio_fs_pci_start` creates one BSP kernel worker.
The worker enables bus mastering, sets `DRIVER_OK`, then unmasks MSI-X entry zero
and the function. It sends one real `FUSE_INIT` request on the ordinary queue.
The FUSE layer checks the reply length, request identity, error, protocol version
and feature flags. Request/reply data is copied through the owned DMA buffers;
no private task stack is published to the device.

The worker inspects both queues and device status, then sleeps for interrupt
activity or the five-second monotonic request deadline. IRQ entry only remembers
activity and detaches/wakes its waiter. The BSP-local check/publication handoff
runs with IF=0 and preserves interrupts arriving before parking. The worker runs
with IF=1 otherwise; presentation and userspace continue to be scheduled.
After successful INIT it services native requests and deferred object cleanup,
sleeping when neither work nor device activity is pending. There are no
background polling requests.

## FUSE client contract

`include/kernel/virtio/fs.h` exposes root acquisition, LOOKUP, GETATTR, OPEN,
READ, CREATE, WRITE, size-only SETATTR, MKDIR, UNLINK, RMDIR, RENAME2,
OPENDIR, READDIR, FSYNC/FSYNCDIR, RELEASE/RELEASEDIR and reference release.
These functions run only on the existing BSP transport worker with interrupts
enabled and no held locks. They neither allocate nor accept userspace pointers.
The native backend below owns their records and performs allocation with BSP
interrupts disabled, outside transport waits.

The caller supplies zeroed, stable node and open records. Each successful LOOKUP
owns one host lookup reference, even if another record has the same node ID.
Local node retains do not acquire more host references. An open retains its node;
its close sends RELEASE or RELEASEDIR before dropping that retain. Final node
release sends one FORGET on the high-priority queue. The implicit root has no
acquired lookup reference. Node storage must outlive all of its opens. Close and
final put consume their local records even if the session has failed.

Lookup accepts one component of at most 255 bytes, excluding NUL, slash, `.` and
`..`. Names need not be UTF-8. Only regular files and directories can be opened;
symlinks are not followed and special files are unsupported. READDIR still
reports their kinds, including unknown types, and skips dot entries. Reads use
explicit offsets, transfer at most 4 KiB and preserve short reads and EOF. Each
writable descriptor is sized to that operation's maximum reply, including its
header. Protocol scratch is static to this worker, outside the kernel task stack.

WRITE submits at most 4 KiB of data, further limited by FUSE `max_write`;
headers occupy additional transport storage. Nonempty success reports a positive
count, possibly short. A zero negotiated write limit rejects nonempty writes.
Offsets and resulting sizes must fit the host's signed 64-bit file range.
SETATTR changes only size using the write handle. Its reply need not equal the
requested size: another host writer can change the file before attributes are
captured. Neither operation promises a transaction across requests.

CREATE returns one owned lookup and one write-only open, both retained by the
new native wrapper. If returned open semantics are unsupported, cleanup releases
the open and lookup but leaves the created name alone: deleting it could remove
a concurrent replacement. Errors never imply that the host tree is unchanged.

READDIR returns a checked batch of at most 4 KiB. Consume it with
`virtio_fs_directory_next`; `VIRTIO_FS_END` means the batch is exhausted, whereas
`batch.end` means the server returned EOF. Resume using `batch.next_cookie`,
which includes skipped dot entries. Cookies are opaque and belong to that open
directory: zero starts or restarts enumeration, and other values must not be
incremented, ordered or reused with another open.

There is no guest data or attribute cache. Host edits can affect later requests;
a read-only export is not an immutable tree. Enumeration is not a snapshot and
cannot reliably detect external changes. Native enumeration therefore promises
a live view and reports `DIRECTORY_CHANGED` only for detected invalidation.
RAM directories retain their existing generation checks.

Wire errors become `enum virtio_fs_result`; Linux errno values do not escape the
client. Ordinary errors such as a missing or inaccessible file leave the session
usable. Truncated/inconsistent replies, invalid directory records, transport
failures and failed RELEASE/FORGET cleanup stop it. Directory creation, removal,
regular-file rename and synchronization use host FUSE operations. Symlink
traversal, reconnection and unmount remain unimplemented.

Storage errors retain their meaning through the native layer: ENOSPC becomes
NO_SPACE, EDQUOT becomes QUOTA, and EFBIG becomes FILE_TOO_LARGE. ENOMEM remains
NO_MEMORY. Read-only, already-existing and nonempty-directory errors also retain
their native equivalents rather than becoming generic I/O errors.

A published mutation without a trustworthy completion returns OUTCOME_UNKNOWN
(libc EIO): this includes transport loss, timeout, malformed reply or impossible
write count. Pre-submission failures and preparatory lookup/open failures retain
their ordinary status. Structurally valid host error replies retain their mapped
error. No failure promises rollback, and no uncertain mutation is automatically
retried.

`mkdir` requests mode 0755 under the existing host-service identity. There is
no guest chmod or umask interface. Creation is exclusive and has no rollback.
MKDIR returns an owned lookup; if a host replacement changes its type before
the reply, that lookup is retired and creation reports a type mismatch. No host
name is deleted to undo it.

Removal accepts native `FILE`, `DIRECTORY` and `ANY` requests, but preflight
supports only regular files and directories; file removal uses UNLINK and
directory removal uses RMDIR. Rename preflight supports only regular files and
uses host atomic RENAME2, with its no-replace flag when requested. There is no
delete-then-rename or local existence-check fallback. A host that lacks no-replace support returns its
error. Replacement requires REMOVE and CREATE on the destination parent, and
REMOVE on the source parent. No-replace rename to the same name follows the host
operation and can return ALREADY_EXISTS rather than acting as a RAM same-entry
no-op.

Preflight and mutation are separate host requests. The host can change names or
types between them, so these checks cannot guarantee the affected object or
type; even a directory or symlink can replace a preflighted entry. The guest
does not follow symlinks. Open native file handles retain their host objects
after unlink or replacement, subject to ordinary host permissions. This is a
live view, with no snapshot or durability promise. Automatic creation of missing
parents, recursive deletion, directory moves and cross-filesystem moves are
unsupported.

## Synchronization

`file_sync(handle)` requests full synchronization of a file's data and metadata;
`directory_sync(handle)` requests synchronization of that directory's entries.
Both return the operation's status to the caller. File sync requires WRITE
authority. Directory sync requires either CREATE or REMOVE authority. A
read-only grant therefore returns DENIED. The immutable archive backend does
not support synchronization.

The RAM backend accepts both operations as no-ops; success does not mean data is
persistent. Host file sync sends FUSE FSYNC with flags zero, and host directory
sync sends FSYNCDIR with flags zero through the existing worker. Files are
opened lazily with write access for file sync and read access for directory
sync. Preparatory open failures are ordinary errors. Once a sync request is
submitted, a missing or malformed reply reports OUTCOME_UNKNOWN because the
host may have completed it. Host errors are returned to the caller; uncertain
requests are not retried automatically.

The `sync` utility accepts explicit file and directory paths, processes them in
argument order, continues after errors and exits unsuccessfully if any target
fails. With no arguments it prints usage; successful operation is quiet. It
resolves each path when invoked, so a concurrent rename can make a path name a
different object. Code that needs object identity should call the libpyxis
helpers on the already-held handle. Sync applies only to the named object; it
does not sync a filesystem globally. `fflush` and closing a file handle retain
their existing behavior and do not request synchronization, and libc exposes no
`fsync` API.

For publishing a replacement, write the temporary file and sync it through the
same open handle while that identity is known, rename it to the final name, then
sync the parent directory. If the rename crosses directories, sync both parent
directories. A host acknowledgement establishes only what that host service and
its storage promise. Concurrent host changes prevent treating the sequence as a
snapshot or a multi-call transaction.

## Native directory and file objects

`kernel/fs/hostfs.c` connects the client to the existing directory and file
protocols. Each successful native lookup creates an independently owned wrapper
with one FUSE lookup reference. It acquires independent read-only and write-only
open handles lazily as needed; size uses fresh GETATTR. A write never requires
host read permission, nor does a read require write permission. Copying or
granting a capability retains the same native object. Closing a parent does not invalidate
children. No host path, node ID or wire structure becomes an application request.

File reads return at most 4 KiB per call, including when the supplied buffer is
larger; callers must handle short reads. Native directory cursors are opaque and
scoped to the directory object. A retained open directory gives their host
cookies a stable lifetime without a shared enumeration position. Restart with
a zero cursor. A too-small name buffer preserves the input cursor and returns
the required size. Enumeration reports file, directory, symlink, other and
unknown kinds; lookup still accepts only file/directory requests and never
follows symlinks. Libpyxis validates replies without assuming numeric cursor
increments. External changes may alter results, including after a prior EOF.

Rights checks precede dispatch and missing rights return DENIED. The existing
`mkdir`, `rm`, `rmdir` and `mv` commands operate on the host when their mounted
root grants the required rights. Host rename captures both names before parking
and borrows the live parent capabilities while the caller waits; the worker
receives no caller-private pointers. Host I/O errors use CALL_IO (libc EIO); unavailable sessions
and timeouts have their existing distinct statuses. Native executables and
interpreters can be launched from host files through owned image capture; the
loader never executes from live host mappings. See [the launch contract](processes.md#implemented-userspace-launch).

A caller captures inputs in a record embedded in shared task metadata, publishes
it under the scheduler queue lock, then notifies the BSP after unlocking. This
wakes an idle BSP even if publication misses its queue sweep. The caller saves
its wait pointer before publication and blocks through the existing
wake-before-park contract; a BSP caller sends no self-IPI. The BSP scheduler only
forwards queued records; blocking FUSE work runs in the transport worker. All
user-buffer validation and copies happen on the caller's CPU. The worker never
switches to a caller's root or accesses its private stack. Its reply and any new
owned object are fully published before wakeup, and it never touches the record
after waking its owner. Requests hold no spinlock across a transport wait.

Creation exclusively lends the parked caller's capability table to the BSP
worker. It allocates the wrapper and installs the result handle before sending
CREATE or MKDIR. The caller cannot use that handle until wakeup. Failure closes
the private handle and retires any acquired FUSE ownership; success has no remaining
fallible local allocation. Write bytes are captured before parking, never read
from a private mapping by the worker.

There is one task per process and no external cancellation: a blocked caller
cannot close its capability or exit until the operation finishes. Its capability
keeps the source object alive. Failure to install a returned child releases its
owned reference. Process exit closes remaining capabilities normally. Final
object reaping transfers the wrapper to a worker cleanup queue without sleeping
or allocating. That worker sends RELEASE/RELEASEDIR followed by FORGET before
freeing local storage. Copies held elsewhere keep the object alive independently.

## Caller-scoped transfer profiling

The profile capability's independent `PROFILE_RIGHT_HOST` collection counts native
host READ/WRITE work, including lazy OPEN transport under those operations.
`iobench --host-profile` exposes separate scheduler/worker queues, worker service,
transport and caller-resumption intervals with native/transport counts. It does
not collect metadata-only operations, sync, deferred cleanup or remote callers.
Failed published transport requests have a separate interval ending before reset
recovery; valid used-ring completion does not imply FUSE success. Disabled
collection adds no clocks or allocation. See the [ABI](../include/abi/profile.h)
and [measured attribution limits](io-reliability-attribution.md#host-profiling-and-attribution-limits).

The nested-KVM measurements show strong profiling perturbation; retain matching
unprofiled controls. Transport elapsed combines guest and host scheduling with
device/daemon/backing service and cannot isolate any one component or CPU time.

## Init mount and delegation

The default init script is:

```text
#!app://shell.pxe
mount --optional --read-write host
session app://session.pxe --configure-network
```

When the selected modern virtio-fs device is present, native init or its script
interpreter receives a `host_mount` resource. This mount object is authority over
that one boot-lifetime export; knowing the device tag supplies no authority.
Presence is recorded before preparation, so a failed device is never confused
with an absent one.

`MOUNT_OPEN_ROOT` is a synchronous request requiring `MOUNT_RIGHT_OPEN_ROOT`.
Its `mount_open_request` selects `MOUNT_ACCESS_READ_ONLY` (LOOKUP, ENUMERATE,
READ_FILES) or `MOUNT_ACCESS_READ_WRITE` (all directory rights). It returns one
owned directory handle. Libpyxis exposes `mount_open_root(mount, access, &root)`.
Access selects the returned grant, never a global mode on the shared export;
opening another root cannot widen existing handles. Read-write authorizes
mutation attempts, not a promise of host writability. Host/backend errors still
apply, with no write probe or silent read-only fallback. A validated zero-byte
write is a backend-free no-op. Missing required rights fail with DENIED first.
The packaged development init explicitly requests read-write access; the
read-only init requests read-only access. Both use `--optional` and leave the
initial working directory at `home://`.
The mount operation uses the existing object-call ABI and creates neither a
global namespace entry nor a kernel URI parser.

Mount requests arriving during initialization wait on the native request queue.
The worker's bounded FUSE INIT completes them, or its failure wakes them with an
error. Failed preparation, worker creation or initialization does not leave
callers parked indefinitely. Transport request deadlines are five seconds;
stopping/reset may take up to one additional second, plus scheduling delay.

The shell's `mount [--optional] [--read-only | --read-write] host` binds the
returned root locally, defaulting to read-only. The only
optional success is a missing `host_mount` resource: any call failure reports an
error and stops a script. An existing `host` binding is never replaced. Names and
handles in a caller-supplied `path_context` root set are borrowed; a NULL set
continues to resolve immutable startup bindings. The shell owns newly mounted
roots until exit, and changing its bindings does not change a retained cwd chain.

Session handoff and ordinary child launches query and preserve the actual
grants on the optional host root, app/home roots and each cwd handle. They never
forward mount authority. The session launcher forwards that root to its interactive shell, whose `cd host://` and
child launches retain the same navigation boundary and rights. Mount authority,
root directories and opened children have independent reference lifetimes;
init exiting does not revoke the copies it delegated.

No device means archive-only startup as before. An explicit bad socket fails
QEMU setup; a present device that cannot mount fails the init script rather than
silently continuing. Selecting `INIT=build/userspace/shell.pxe` provides a native
recovery shell without executing the mount command or session configuration.
There is no mount attachment below directories, unmount, reconnection, live
namespace replacement or overlay.

## Failure and lifetime

Before AP startup, failed preparation disables MSI-X and confirms reset before
unmapping and freeing queue storage. An unconfirmed reset retains the claim and
storage. DMA has not been enabled at that point.

A runtime timeout, malformed reply, unexpected completion, status change or
configuration-generation change makes the session unavailable. The worker masks
delivery, disables MSI-X and bus mastering, requests reset, and waits up to one
second while yielding between reset-status reads. It reports whether stopping
was confirmed. The worker remains available to reject pending/new operations
and retire local objects; device IRQ delivery stays disabled. Native submissions
and reaper cleanup still wake it through the BSP handoff. Static interrupt state
stays valid for a late APIC delivery.

Runtime failure retains the device claim, DMA storage and mappings until reboot,
even after confirmed reset. Shared kernel mappings must remain stable while APs
run; this slice does not add TLB shootdowns or reconnection. An idle disconnected
daemon may not generate a guest-visible event; disconnection is otherwise detected
through device status or a request deadline, not a heartbeat. See
[the recorded lifetime tradeoff](technical-debt.md#virtio-fs-runtime-resource-retention).

## Debugger inspection

Use `make debug` with the socket option and connect GDB to `build/caelum.elf`.
Break at `boot_start_cpus` to inspect `filesystem.hiprio` and `filesystem.request`
after preparation; DMA and interrupt delivery are still disabled there. Each
queue records its kernel mapping and physical base separately.

A hardware breakpoint on `virtio_fs_pci_interrupt` observes real MSI-X delivery.
The worker may already have a published waiter, or the interrupt may arrive
before it parks; both are normal. Inspect the used ring and reply buffer without
calling inferior functions under KVM. After initialization, inspect
`filesystem.session`, `filesystem.interrupt_wait` and the queue indices. The
worker should have no request in flight and should be sleeping. Read MMIO at its
individual register widths; see [PCI inspection](pci.md#inspection-and-next-step).

For manual client calls, use TCG (see [inferior calls](gdb.md)), allocate and zero
caller records at `boot_start_cpus` while allocation is still safe, and stop in
`filesystem_worker` immediately after successful INIT. Disable that breakpoint
before calling client functions: they may sleep for real device completions.
Use allocated storage for names too; GDB string arguments otherwise try to call
an unavailable `malloc`. Do not call the client from an arbitrary stopped task
or interrupt handler. After closing opens and putting nodes, both session
ownership counters should be zero and both queues should be idle. Retained
native objects may legitimately keep lookup references or open handles alive.
These are manual debugger calls, not boot-time probes.

Ordinary `ls host://` and `cat host://hello.txt` exercise startup delegation,
syscall dispatch, AP waits, BSP forwarding, worker I/O and deferred cleanup.
Inspect the session ownership counters after returning to an archive/RAM cwd
and closing children; a retained host root has no FUSE lookup reference.
No debugger-injected capabilities are needed.

References: [VirtIO 1.4 split queues and filesystem device](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf),
[Linux v6.3 FUSE 7.38 wire definitions](https://github.com/torvalds/linux/blob/v6.3/include/uapi/linux/fuse.h),
[virtiofsd 1.14.0 FUSE definitions](https://gitlab.com/virtio-fs/virtiofsd/-/blob/v1.14.0/src/fuse.rs),
[virtiofsd host setup](https://gitlab.com/virtio-fs/virtiofsd/-/tree/v1.14.0).
