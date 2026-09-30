# Native filesystem kernel adapter

Caelum links the pinned read-only filesystem core and provides policy-approved
native directory/file objects through one serial BSP worker. This implements
backing preparation, objects and init integration, tasks 3–5 of the
[native mount milestone](../wip/native-readonly-filesystem.md). Native objects use
the existing directory/file protocols. Native executable capture uses the same
worker. Mount configuration and
acquisition are described in the task-5 integration below; startup performs no
automatic volume probe.

## Build and ownership

[Build rules](../../kernel/fs/build.mk) compile the seven read-only core sources
with kernel code model, no red zone, general registers only, freestanding compiler
headers and no builtin/libc dependency. Construction and whole-image checking
remain host operations. Kernel bundle provenance includes the filesystem pin and
local changes. No compiler-container rebuild is needed.

The [internal interface](../../include/kernel/fs/native.h) accepts a caller-owned
`nativefs_job` in shared kernel storage. A `NATIVEFS_ROOT` job supplies the GPT disk
GUID in on-disk byte order, one-based GPT entry number, counted volume name,
trusted nonzero principal and exact requested root rights. Root rights must
include LOOKUP; unknown bits are invalid and known mutation bits are read-only.
The worker opens the selected backing and acquires a SUBTREE view under persistent
policy, with a ceiling containing only lookup, listing and file read plus metadata.
It requires the complete requested mask. Knowledge of principal or object IDs
confers no authority, and ownership does not bypass policy.

Trusted asynchronous submission is BSP/IF=0 and lends the entire record until
COMPLETE. The caller retains every input reference and does not access the record
while queued/active. Rejected admission leaves the record unchanged. Successful
ROOT or LOOKUP transfers one owned `kernel_object` reference in `job.object`;
failure transfers none. Detach that output before resetting/reusing the record.
CAPTURE transfers owned launch staging through `job.captured` and its byte
length through `job.count`; detach that buffer before releasing the request.
No completion registry is retained. The internal ROOT inputs are embedding
authority, not an application interface for selecting a principal.

Ordinary directory/file calls use a typed `nativefs_request` in the user task's
provisioned BSP request storage. The handler captures input and checks user
buffers, then submits an ordinary BSP service request and waits. The executor
forwards ownership to the filesystem worker without running core calls or
sleeping for I/O itself. A live capability keeps the input node alive during the
uninterruptible wait. Only shared kernel storage crosses to the worker: no user
pointer, capability-table entry or AP stack loan. Completion clears the node,
request and queue loans before waking the task. The handler consumes successful
outputs and releases request storage; lookup releases it before capability-table
growth and child installation.

Startup creates one BSP kernel task which parks until work arrives. Only that
worker performs pool/volume/view operations or invokes the core allocator, with
interrupts enabled except around short allocator, block and queue sections. It
waits normally for GPT and block completion without holding a lock. Kernel jobs
submit directly to its queue, never to the synchronous user-request executor.

The worker reuses the same pool for the single device's same GPT entry/extent
and shares volumes by retained ID. New extents reserve their selected pool ID
before volume publication. A clone on another partition returns
`CALL_ALREADY_EXISTS` even on the same disk. The reservation lasts through final
close, so a later open of the clone can succeed after the first instance drains.
A failed preparation releases only its own state; there is no idle pool cache.

Each native node embeds its directory or file wrapper, owns one core view and
retains one adapter volume reference. Children retain the volume directly rather
than retaining a parent. Native files have no in-memory data or busy ownership;
native directories have no in-memory entry list. Final object destruction on
BSP/IF=0 transfers the node through `nativefs_retire`, without allocating or
freeing the embedded wrapper separately. The worker closes the view, drops the
volume reference and frees the node. Final volume retirement then closes the
volume and pool. Retirement carries execution-group cleanup attribution and
remains charged until storage is actually freed. A busy backing close retains
storage, pool identity and pending cleanup; subsequent real work retries it.

## Rights and ordinary calls

Capability copies may share a retained view with a wider ceiling. Every request
still carries the particular caller's exact OS rights; the worker checks them
against the node's acquired rights and the operation's requirements. Child rights
must be contained in that caller's attenuated grant. Lookup never reacquires
policy under the bootstrap principal, so sharing a view cannot restore withheld
bits.

| OS grant | Core rights in the corresponding view |
| --- | --- |
| Directory LOOKUP | `PFS_DIR_LOOKUP` |
| Directory ENUMERATE | `PFS_DIR_LIST` |
| Directory READ_FILES | `PFS_FILE_READ | PFS_FILE_METADATA` |
| File READ | `PFS_FILE_READ | PFS_FILE_METADATA` |

Root and child directories use SUBTREE scope; files use OBJECT scope. Lookup
first derives and closes a zero-right OBJECT view to check the child's kind,
then derives the final view with exactly the requested rights and scope. Both
passes use held lookup authority. This turns a kind mismatch into
`CALL_WRONG_TYPE` without requesting inappropriate rights or a fresh policy grant.
Native component names are limited to 255 bytes and validated by the core,
including UTF-8; longer lookup names return `CALL_LIMIT`.

FILE_READ uses held read authority; FILE_SIZE uses the metadata half of the READ
bundle and returns only byte length. Both require the calling capability's READ
bit. A persistent grant that allows read but withholds metadata cannot acquire
this complete bundle and returns `CALL_DENIED`. READ remains bounded by `FILE_READ_MAX_BYTES` and uses explicit offsets.
Unknown rights bits return `CALL_BAD_REQUEST`; valid mutations return
`CALL_READ_ONLY` after ordinary authority and argument checks, including an empty
WRITE. A normal root grants no mutation bits, so mutation calls through it are
usually denied by their ordinary rights checks. Existing archive, RAM and HOST
behavior is unchanged. Executable capture reads metadata and bytes through the
held file view, under the same caller READ check and absolute job deadline. It
rejects empty files, limits staging to 16 MiB and requires the full recorded
length; errors transfer no buffer. Complete staging transfers to the launch
capture, which frees it on BSP after loading or on failure/stop. Its separately
owned allocation is outside core/adapter caps, matching HOST capture's bound.
There is no new aggregate staging cap across simultaneous callers.

Enumeration returns names and kinds, with no child handle or metadata authority.
It fetches one stateless core page using the opaque continuation in
`cursor.position`. `cursor.generation` is a nonzero, monotonically assigned
wrapper identity; the zero pair starts. Zero identity with nonzero position is
invalid, and a different nonzero identity returns DIRECTORY_CHANGED. Counter
exhaustion returns `CALL_LIMIT` without wrapping. Fresh lookup of the same
directory may create a different identity; copies of one wrapper share it.
Continuation interpretation remains bound to the held immutable core view.

Cursors retain no server-side state and may be replayed or forked independently.
ENTRY copies a complete NUL-terminated name and advances the cursor.
BUFFER_TOO_SMALL reports the required size while preserving the input cursor
and name buffer. END copies no name and is repeatable. Failures publish no name
or reply. READ failures likewise publish no bytes or count, even if the core
proved a prefix before failing.

## I/O, budgets and errors

Geometry is `floor(partition_sectors / sectors_per_filesystem_block)`. Checked
partition-relative ranges are translated into device sectors without requiring a
4 KiB-aligned partition start on 512-byte media. Trailing partial filesystem
blocks are ignored. A reader has no write/flush callbacks. Each callback accepts
at most 16 filesystem blocks and splits at the actual device transfer limit,
including limits smaller than a filesystem block. One ticket is outstanding at a
time, and each collected read must supply every requested byte.

Admission allows 32 active/queued jobs, shared by kernel submissions and ordinary
user requests. User admission and its deadline begin at BSP request publication,
including time in the executor FIFO. The shared core payload cap is 8 MiB;
adapter payload is capped at 1 MiB with at most 1,024 live pool, volume and node
wrappers combined. Embedded directory/file storage and temporary catalog storage
are charged to the adapter cap; nodes receive no separate allowance. Deferred
retirements retain their charges until freed. The core's own accounting
distinguishes cap exhaustion (`CALL_LIMIT`) from allocator refusal below the cap
(`CALL_NO_MEMORY`). Alignment headers/padding are measured separately from core
payload; TLSF rounding, task stacks and caller-owned job or provisioned user
request storage remain outside these payload caps.

Every admitted operation receives one absolute 30-second deadline. GPT readiness,
block admission retry, completion waits and successive core calls use that same
deadline. Block saturation sleeps in bounded one-millisecond intervals; a timed-out
waiter abandons its ticket after returning, leaving unresolved DMA with the driver.
The deadline is cooperative, not a hard bound on core computation. If the deadline
expires after producing an object, the worker drops that reference before
publishing the timeout; no failed call installs a child handle.

One fresh operation context records the first precise backing error. Only
`PFS_IO` consults it when mapping the result; corruption, denial and LIMIT cannot
inherit a device error. The context is unbound before completion. The completed
job preserves its own core status/backing result for diagnosis. A missing volume
or partition is NOT_FOUND; absent/invalid filesystem or GPT metadata is IO.
Unavailable/unsupported transport or format is UNAVAILABLE. Optional mounting
only suppresses absence of the authority itself. A configured authority preserves
present-device preparation failures, which are never treated as optional absence.

## Prepare a disposable disk

These commands use existing host tools and create new regular files. The example
principal is illustrative and does not establish a trusted boot identity. Never
modify or replace an attached image while a pool is open, including through a
second host tool. Read-only guest attachment does not prevent external writers.

```sh
make -j16 fs-tools
build/fs-tools/mkpyxisfs --image /tmp/native-pool.raw --size 64MiB \
  --volume headers --source /usr/include/linux \
  --owner 0a32efc079ed4c7bab58e224cf119315 \
  --volume empty --owner 0a32efc079ed4c7bab58e224cf119315
truncate -s 68M /tmp/native-disk.raw
sgdisk --clear --set-alignment=1 --new=1:2049:+64M \
  --disk-guid=12345678-1234-4567-89ab-0123456789ab /tmp/native-disk.raw
dd if=/tmp/native-pool.raw of=/tmp/native-disk.raw bs=512 seek=2049 \
  conv=notrunc status=none
build/fs-tools/pyxisfs-inspect --image /tmp/native-disk.raw \
  --gpt-partition 1 --sector-size 512 check
sha256sum /tmp/native-disk.raw
make debug ACCEL=tcg CPUS=4 VIRTIO_BLK_IMAGE=/tmp/native-disk.raw \
  VIRTIO_BLK_READONLY=1
```

Select matching OVMF paths for the host as in the repository README. The partition
starts at sector 2049 deliberately; it is not aligned to a filesystem block.
The GPT type is not used to select or authenticate the filesystem.

For 4 KiB sectors, `guestfish --blocksize=4096 --format=raw -a IMAGE` can initialize
GPT and add a 64 MiB partition at sectors 256–16639. Copy the pool with `dd bs=4096
seek=256 conv=notrunc`. Use `--sector-size 4096` for host inspection and launch
QEMU with `logical_block_size=4096,physical_block_size=4096` on the virtio-blk
device. The ordinary launcher uses the device's default 512-byte sector profile.

## Manual debugger operation

Follow [GDB ownership](../development/gdb.md) and the
[block debugger rules](block-storage.md#manual-debugger-exercise), using TCG for
injected nonblocking calls. Break at `vm_get_stats` before scheduling, select BSP
and enable scheduler locking for calls. Allocate a job, check allocation success,
then fill it explicitly. The principal matches the disposable image's owner;
this debugger injection is trusted embedding authority:

```gdb
set $job = (struct nativefs_job *)kmalloc(sizeof(struct nativefs_job))
set *$job = {0}
set $job->operation = NATIVEFS_ROOT
set $job->disk.bytes = {0x78,0x56,0x34,0x12,0x34,0x12,0x67,0x45,0x89,0xab,1,0x23,0x45,0x67,0x89,0xab}
set $job->partition = 1
set $job->principal.bytes = {0x0a,0x32,0xef,0xc0,0x79,0xed,0x4c,0x7b,0xab,0x58,0xe2,0x24,0xcf,0x11,0x93,0x15}
set $job->rights = 7
set $job->count = 7
set $job->name = "headers"
p nativefs_submit($job)
```

The rights value 7 requests LOOKUP, ENUMERATE and READ_FILES. After CALL_OK,
disable scheduler locking and resume normal execution. Set a hardware breakpoint
at the worker's yield after completion (locate it with `list nativefs_worker`).
At that stop, require `job.state == NATIVEFS_JOB_COMPLETE` and inspect `job.status`
before consuming outputs. Success returns `job.object`, an owned root directory,
whose native node holds the view and volume. The volume's core/record and pool
diagnostics identify the selected generation. `core_peak`, `core_heap_peak`,
`adapter_peak`, `core_memory.used`, `adapter_used` and `wrapper_count` expose budget
accounting. `operation` must be NULL after completion. These are debugger
observations, not an application information interface.

Further nonblocking jobs can use a retained root node for LOOKUP or ENUMERATE and
a retained file node for READ or SIZE. Save each owned output and its node, detach
`job.object`, then reset the complete job, choose its operation and supply the
actual calling rights; LOOKUP also supplies the exact child rights,
kind and counted component name. ENUMERATE uses `cursor` and `count` as name
capacity. READ uses `offset` and `count` as read capacity; on success `count` is
the returned byte count. SIZE returns byte length in `offset`. Retain input objects
until completion; the worker clears `job.node` rather than releasing the caller's
reference. Every successful lookup output needs a separate release.

For the root-only example, at a safe stopped BSP boundary with IF=0 and scheduler
locking enabled, detach and release the owned object, then free the completed job:

```gdb
set $root = $job->object
set $job->object = 0
call object_release($root)
call kfree($job)
```

Restore IF and debugger scheduling, then resume so ordinary object reaping and
the worker can drain deferred nodes and backing state. Observe zero live
accounting after that cleanup completes. Release all retained children as well.
Do not inject `block_wait`, pool opens, view operations or other sleeping core
calls from GDB. No permanent diagnostic application or automatic probe is used.

## Historical task 3 validation

These results describe the merged backing-adapter implementation before native
object support. The measured job size and retained allocations below belong to
that revision; they are not measurements of the current object implementation.

On 2026-09-30, ordinary kernel and host builds passed with GCC 16.2.0 target and
GCC 16.2.1 host compilers. Image assembly used verified SDK/userland/ports bundles
from merged Pyxis `0d03ab8`, filesystem `017996b`, userland `c9ed311` and ports
`6ff8504`. Missing-core-source rejection and prebuilt-kernel reuse were checked.

The 64 MiB pool imported 805 real Linux headers, 838 populated-volume objects and
one empty-volume root. Existing host checking passed both committed states through
512-byte and 4 KiB GPT selection. Interactive QEMU 10.2.2 with the documented AHCI
fix, CPU `max`, 256 MiB, entropy, no HOST/network devices and read-only virtio-blk
ran the normal worker under TCG: four CPUs with 512-byte sectors, and one CPU with
4 KiB sectors. Both opened the selected pool and volume, then released all state.
The four-CPU run also checked an empty volume, repeated shared opens, cloned pool
rejection across two partitions, continued reservation after queuing final put,
successful clone open after final close, missing volume/partition, wrong disk GUID
and 32 accepted jobs followed by unchanged BUSY rejection of job 33. Both whole-disk
SHA-256 hashes were unchanged after shutdown.

| Measured requested storage | Bytes |
| --- | ---: |
| Kernel core peak during populated pool/catalog/volume opening | 255,336 |
| Kernel core peak including adapter alignment headers/padding | 255,566 |
| Kernel adapter peak, including temporary catalog output | 105,808 |
| Retained core pool + volume after opening | 1,048 |
| Retained adapter pool + volume after opening | 1,360 |
| Internal caller-owned job | 320 |
| Live core/adapter payload and wrappers after final release | 0 |

The two peak counters need not coincide; their sum is an upper bound on these
charged/requested allocations, not total heap usage. Each worker additionally
uses the existing 16 KiB kernel stack and 784-byte task record. TLSF overhead and
rounding, mapped heap pool slack, other kernel clients and caller job storage
remain additional costs; no total-kernel peak or owner-host timing is claimed.

Host GDB watchpoints on the same core's charged-memory counter, with an explicit
8 MiB limit, measured root-subtree acquisition at 248,360 bytes and nested
`can/error.h` object acquisition at 315,816 bytes. The root held file read+metadata
and directory lookup+list; the file held read+metadata. Both policy decisions
allowed the complete masks and both views closed successfully, returning charged
memory to zero. These are host policy/path measurements, not guest object lookup.
The initial bounds comfortably cover these inputs; they are not a capacity claim
for every valid image.

An ordinary four-CPU nested-KVM boot without a block device reached userspace
with GPT unavailable and the native worker idle. An initial debugger-injected
allocation under KVM faulted at GDB's stack return address; the manual calls above
were subsequently performed under TCG as required by the block-debugger guide.
This was not a native filesystem operation. All validation VMs/debuggers were
stopped afterward.

Timeout/device failures, actual allocator exhaustion, adapter-cap exhaustion,
BUSY-close recovery, sub-4-KiB device transfer limits and later filesystem
generations have source-review coverage only. No fault injection, new tests,
permanent probes, physical hardware or owner-host performance measurements were
used. Native mount configuration and executable capture remain task 5.

## Task 4 validation

On 2026-09-30, `make -j16` and image assembly passed for task 4 based on merged
Pyxis `0a5d895`, with the same dependency pins and verified SDK/userland/ports
bundles described above. No public ABI or dependency pin changed. Interactive
QEMU used four CPUs (`max`), 256 MiB, TCG, the documented AHCI fix, entropy and
read-only virtio-blk with 512-byte sectors and the same sector-2049 partition.
Normal archive-backed init/session programs also ran; HOST transport was not
attached or re-exercised in this run.

Manual asynchronous jobs acquired the root and `can/error.h`, checked its
7,087-byte size and initial bytes against the source, and read the file after
closing its parent directory. Enumeration resumed at the next entry, preserved
its cursor on a short name buffer, returned repeatable END, rejected an invalid
continuation with BAD_REQUEST and returned CHANGED for another wrapper's
identity. Abandoning enumeration retained no cursor object. A child lookup from
a grant lacking READ_FILES returned DENIED with no object; a root acquisition
under an ungranted principal returned PFS_DENIED/CALL_DENIED with no object.

To exercise the ordinary request path before the mount interface exists, GDB
loaded the existing initrd `cat.pxe` and `ls.pxe` into exclusively owned,
unsubmitted processes using the normal kernel loader/startup helpers. Each
received a native root binding through `capability_install`, a memory service
and separate console output/error handles, then ran normally on CPU 1. GDB did
not patch private mappings or a live capability table. `cat disk://can/error.h`
completed reads of 4,088, 2,999 and zero bytes at offsets 0, 4,088 and 7,087 and
exited 0. `ls disk://can` enumerated eight names through END and exited 0.
Debugger inspection confirmed the captured child rights and detached completion
records. These applications ran from the archive; native executable loading is
still task 5.

| Measured requested storage in the combined object run | Bytes |
| --- | ---: |
| Core payload peak | 312,192 |
| Core peak including allocation headers/padding | 312,744 |
| Adapter payload peak | 106,144 |
| One embedded directory/file node | 168 |
| Internal caller-owned job | 4,552 |
| Typed user request | 4,584 |
| Existing per-user-task request allocation (unchanged) | 4,928 |
| Live core/adapter payload and wrappers after final retirement | 0 |

Final retirement also left zero admitted jobs and no bound operation context.
The whole-disk SHA-256 was unchanged after both VMs stopped. The peak accounting
qualifications above still apply; this is not a total heap or timing measurement.
An initial nested GDB expression hit `arch_cpu_at`'s index assertion during manual
process setup; that VM was discarded. The successful run evaluated the space
selection separately and used ordinary scheduling for filesystem work. Debugger
setup errors are not filesystem validation passes. All VMs/debuggers were stopped.

The missing-metadata persistent-grant case, error-after-partial-read publication,
known mutation backend errors, stop-during-request and failure/limit cleanup
paths were reviewed in source rather than injected. Existing formatter grants do
not produce a read-without-metadata fixture. Timeout/device/allocator failures,
maximum directory depth and later generations retain the historical limits
above. No new tests, probes, boot automation or fault injection were added.

## Task-5 integration

Trusted init receives a `native_mount` capability when both bootstrap selectors
are configured and block hardware is present. The immutable authority contains
the configured disk GUID, bootstrap principal and block preparation result;
applications cannot supply a principal. `MOUNT_OPEN_VOLUME` captures a one-based
GPT entry number, a 1–255-byte volume name and the exact directory-rights mask.
The request has no namespace name or raw-device address. It requires mount
OPEN_ROOT and directory LOOKUP; unknown rights are invalid and mutation rights
are read-only. The worker validates the selected disk and persistent policy,
then the handler installs one independently retained root. Mount authority may
close while that root remains usable. HOST retains its separate OPEN_ROOT
operation; crossing the two backends returns BAD_OPERATION.

The library and shell validate selectors before optional-absence handling. The
shell reserves its binding slot/name before acquisition and closes unpublished
roots on failure. It forwards an explicit selected list of at most 16 roots
using each handle's actual rights and transport flags. Session and remote shell
handoffs preserve that list; they do not forward mount resources. Provider
profiles explicitly attenuate the roots and working directory they select.
See [init configuration](../userland/init.md) and [shell commands](../userland/shell.md).
Observation rights, filesystem information and `--no-info` remain task 6.

## Task-5 validation

On 2026-09-30, ordinary kernel, SDK, host-tools, userland and ports builds and
both default and configured image assembly passed from Pyxis `161d24e` plus
task 5, using userland `b50fd10` and unchanged filesystem/ports/lwIP pins.
The initial ports build lacked CMake on PATH; using the existing host CMake 4.4.3
installation resolved that environment issue. No compiler-container rebuild was
needed. Subsequent init/configuration-only image assembly used the verified
kernel/SDK/userland/ports bundles from those builds.

Interactive QEMU used four CPUs (`max`), 256 MiB and nested KVM with the documented
AHCI fix. A disposable 132 MiB GPT disk had 512-byte sectors and two cloned
64 MiB pool extents at sectors 2,049 and 133,129. The pool held `system` (fresh
native cat/date executables and a text file) and `headers` (host Linux headers).
Before attachment, the existing host checker accepted both retained committed
states: two volumes, 845 objects and 843 directory entries. This checks structure,
not payload checksums. Every guest attachment was read-only, without host mutation.

Trusted init mounted system twice and headers once, mounted a disposable HOST
export and handed the roots through session, remote services, remote server,
terminal and an ordinary remote shell. The existing remote client exercised:

- `ls data://`, nested header enumeration, native `cat` and relative reads after
  `cd data://share`, all successful;
- native `data://bin/cat.pxe` and `data://bin/date.pxe` launches, both exit 0;
- reading through the repeated `mirror://` mount;
- denied native mkdir and redirect/overwrite attempts;
- failure to reacquire a native root in the ordinary shell, which had no mount
  authority despite retaining delegated directory grants;
- archive reads, RAM copy/read, and HOST read/write/read-back/sync, all successful.

Separate trusted-init boots used `--optional` with present authority. A second
partition containing the same pool returned CALL_ALREADY_EXISTS after a successful
first mount; invalid partition 3 returned CALL_NOT_FOUND; an ungranted configured
principal returned PFS_DENIED/CALL_DENIED with no object. Each script exited 1.
GDB observed normal worker completions without injecting calls or changing guest
state. After failed scripts drained, core/adapter live bytes and admitted requests
were zero; clone and policy-denial runs also had zero wrappers and no operation
context. The clone run had no remaining pools or volumes.

With configuration retained but no block device, the same optional script exited
0. A restored packaged, unconfigured image also reached its normal userspace
sessions without a disk. The whole-disk SHA-256 remained unchanged after the
attached runs. The 4,592-byte typed native request still fits the existing
4,928-byte task request storage.

Wrong GUID, missing volume, malformed/duplicate bootstrap options, root/service
collision and root-count exhaustion have source-review coverage; this run did
not induce allocator/device/timeouts, unsupported hardware or duplicate devices.
Stop-during-capture and error-after-partial-read cleanup were reviewed in source.
No aggregate staging-memory or owner-host performance measurement is claimed.
All validation VMs, debugger sessions, remote clients and HOST daemon were stopped.
No new tests, permanent probes, fault injection or boot automation were added.
