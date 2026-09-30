# Native read-only filesystem mounts

Trusted init mounts an explicitly selected GPT partition and Pyxis volume, then
passes ordinary directory/file grants to applications. Native `ls`, `cat`, file
reads, executable capture and Fastfetch Disk use the shared read-only core. The
milestone is complete; writable transactions, recovery, FUSE and installation
remain separate work. Default boot requires no disk or mount configuration.

The [core and host tools](filesystem-readonly.md), [kernel adapter](filesystem-native-adapter.md),
[directory ABI](../interfaces/directories.md), [init configuration](../userland/init.md#native-disk-configuration-and-mounting)
and [Fastfetch reference](../userland/fastfetch.md#native-disk-observations) contain
the detailed interfaces. The pinned filesystem repository owns the authoritative
[format](../../fs/docs/format.md) and [core lifetimes](../../fs/docs/core.md).

## Configure and delegate

Prepare a new populated image with the existing host tools, place it in an
explicit GPT extent and check it before attachment; see the
[disposable disk instructions](filesystem-native-adapter.md#prepare-a-disposable-disk).
Set both `MOUNT_DISK=<canonical-GPT-GUID>` and
`MOUNT_PRINCIPAL=<nonzero-32-hex-ID>` for image assembly. Both absent disables
native mount authority; malformed, partial or duplicate boot configuration fails.
The principal must have persistent grants in the chosen image. A trusted init
can then mount and hand off a binding:

```sh
#!app://shell.pxe
mount --partition 1 --volume system --read-only data://
session app://session.pxe
```

Attach with `VIRTIO_BLK_IMAGE=/absolute/path/disk.raw VIRTIO_BLK_READONLY=1`.
Keep the image unchanged while any pool is open: read-only guest attachment does
not prevent host writes. Live refresh, hotplug and writable co-mounting are not
supported. The existing compiler is sufficient; ordinary kernel/SDK/userland/ports
and image builds use the pinned dependencies.

The GUID selects a disk; it authenticates neither the disk nor its contents.
Ambiguous/unsupported device discovery fails rather than choosing a first match.
The current block profile accepts one candidate device. The mount operation
waits for GPT, checks the configured GUID and selects the one-based GPT entry,
not a packed index. The GPT type is not a filesystem selector; the explicitly
selected used extent must contain a supported Pyxis superblock. No other
partition is probed on failure.

All trusted inits share one configured bootstrap principal and read-only ceiling.
`MOUNT_OPEN_VOLUME` takes the partition number, counted volume name and exact root
rights, with no caller principal, namespace label or raw-device address. The
worker resolves the name once, retains pool/volume identity and generation, and
performs policy acquisition within that volume root. Ownership or knowledge of
an ID supplies no bypass. Reusing a name cannot retarget an existing mount.

`--optional` suppresses only missing mount authority: disabled configuration or
confirmed hardware absence. A present but unusable device retains a failing
authority. Wrong selectors, invalid media, denied policy and resource failures
remain errors. HOST keeps its separate mount operation and optional behavior.

The shell reserves a destination before acquisition and publishes it only on
success. Duplicate root/service names fail without replacing a binding. The
binding name is caller namespace data, independent of the volume name. At most
16 selected roots, including app/home/HOST, fit the existing 64 KiB startup
capture profile; overflow fails explicitly and closes unpublished roots.

Handoffs carry an explicit selected directory list with actual rights and
transport masks. They do not scan all capabilities or inherit mount authority.
Ordinary applications cannot reacquire permissions withheld by their launcher,
even though trusted inits share a principal. Restricted launchers must also
restrict their working-directory grants. See [session handoff](../userland/shell.md#session-handoff).

## Rights, identity and lifetime

Root acquisition requires LOOKUP and the complete requested policy rights.
Directory LOOKUP and ENUMERATE map to core lookup and list. OS READ_FILES/File READ
bundle core file read **and metadata**, preserving byte reads and length queries.
A persistent read grant without metadata fails with DENIED. Descendant lookup
uses held views and each caller's attenuated rights, never fresh bootstrap
acquisition. Mutation requests fail through ordinary authority checks or as
READ_ONLY; the adapter supplies no write or flush path.

Mount OBSERVE and directory FILESYSTEM_INFO are separately delegable rights;
observation grants no content or policy access. Root observation requires mount
OBSERVE, while ordinary root policy acquisition still applies. `--no-info` omits
observation. An acquired grant can be attenuated to observation alone, and child
observation can be requested only from a parent that holds it.

Repeated mounts of the same device/partition extent share one backing instance.
A second extent containing the same pool ID returns ALREADY_EXISTS, even on the
same disk or with a different generation. The serial worker reserves identity
before root publication and retains it through in-flight operations and final
retirement. Only after actual close can another extent with that identity open.
Every mount performs its own policy acquisition; sharing never widens authority.

One BSP worker owns all core calls and final closes. Nodes retain their views and
volumes; children can outlive parent directories and mount authority. Published
requests retain their objects until completion. Group stop waits for the existing
uninterruptible loan to return; it does not free active work. Deferred retirement
closes views before volumes and pools, retains cleanup attribution and charges,
and keeps busy backing state reserved. There is no idle pool cache.

Enumeration uses the existing two-word cursor: a boot-unique wrapper identity
and an opaque core continuation, not a pool generation or ordinal. The zero pair
starts; another wrapper identity returns CHANGED. Short name buffers preserve the
cursor, END is repeatable, and failures publish no partial name or next cursor.
Tokens may be replayed, forked or abandoned without retaining per-enumeration
state, allocation or references. The core validates rooted paths in the held
view and seeks to the successor without rescanning the returned prefix. Tokens
grant no authority; page success is not whole-list count/uniqueness reconciliation.
See [stateless continuation](../../fs/docs/core.md#stateless-directory-continuation).

## I/O, limits and errors

Checked reads stay inside the chosen partition. The core sees complete 4 KiB
blocks over 512-byte or 4 KiB sectors; trailing partial blocks are unused and a
512-byte-sector partition need not begin on a 4 KiB boundary. Transfers split at
device limits and require exact successful backing reads. File output is staged:
errors publish no partial bytes/count, and only successful EOF clamping produces
a short read. Native executable capture uses the same held READ rights and worker.

| Shared resource | Bound |
| --- | --- |
| Active plus queued native operations | 32; BUSY on saturation |
| Live core payload across pools/views | 8 MiB; LIMIT |
| Adapter payload / live wrappers, including retirements | 1 MiB / 1,024; LIMIT |
| Worker backing reads | One ticket; callbacks at most 16 filesystem blocks (64 KiB) |
| Operation deadline | One absolute, cooperative 30-second deadline from publication |
| Executable staging | 16 MiB per image, outside core/adapter caps |

Allocation refusal below a cap is NO_MEMORY. These caps exclude task/request
storage, stacks, heap overhead/rounding and mapped-pool slack. Executable staging
has no additional aggregate budget across callers. They are not a total memory
or hard CPU-time bound, nor a promise that every valid image fits. Measured inputs
and revisit conditions are in [technical debt](../technical-debt.md#native-mount-design-limits).

Each operation starts with an empty backing-error context. The first precise
adapter/device failure refines only that operation's generic core I/O result;
it cannot override corruption, denial or limits, or leak into another request.
The context is unbound before completion. Important public distinctions are:

| Condition | Result |
| --- | --- |
| Invalid public arguments / wrong kind | BAD_REQUEST / WRONG_TYPE |
| Missing partition, volume, child or wrong disk GUID | NOT_FOUND |
| Concurrent duplicate pool identity on another extent | ALREADY_EXISTS |
| Insufficient held or persistent authority | DENIED |
| Authorized valid mutation against native backing | READ_ONLY |
| Profile cap / allocator failure / queue saturation | LIMIT / NO_MEMORY / BUSY |
| Backing or operation deadline | TIMED_OUT |
| Unsupported device/format or unavailable transport | UNAVAILABLE |
| Invalid/corrupt media, backing failure or short backing completion | IO |

Bounded diagnostics preserve GPT/core/device detail. Unsupported, resource and
operational failures are not reasons to select an unexamined fallback generation.
Opening validates candidate/root metadata; ordinary calls validate traversed
structures. Neither substitutes for the separate host whole-image checker.

## Scoped information

The directory query copies retained filesystem type, read-only state, independent
GPT/filesystem degradation, pool/volume IDs, volume name, selected generation and
`(pool_blocks - 2) * 4096` allocatable bytes. It adds no traversal, disk reads or
policy reacquisition. Capacity excludes the two superblocks but includes shared
metadata and reserves; it is neither fixed volume size nor writable allowance.

Usage, free bytes, charged bytes, guarantees, quotas and percentages remain
unavailable. Recorded counters are not globally reconciled by ordinary opening.
Fastfetch reports only selected native roots with observation rights, labels
shared capacity, and preserves null/unset values for unavailable fields. Multiple
bindings/volumes with equal pool IDs share capacity and must not be summed.
Binding labels come from the caller, never the filesystem query.

## Combined workflow validation

Closure was exercised on 2026-09-30 at merged Pyxis `ea47d47`, filesystem
`017996b`, userland `3b9ba3f`, ports `5760a7a` and lwIP `a1aadb9`. Ordinary
`make -j16 image fs-tools build/tools/pyxis-remote` passed with target GCC 16.2.0,
host GCC 16.2.1 and CMake 4.4.3. Later init-only assembly reused those verified
bundles. No implementation or dependency pin changed for closure.

A fresh 64 MiB pool imported the current native cat/date executables, a welcome
file and shell script, and 805 Linux headers. It occupied a 68 MiB GPT disk at
512-byte sector 2,049. Before attachment, both retained generation-1 states
passed host checking: two volumes, 845 objects, 843 directory entries, 809 extents
and two grants. Recursive extraction of both volumes matched their source trees
byte for byte; this separately checks payloads that the structural checker does
not checksum.

Interactive QEMU 10.2.2 with the documented AHCI fix used four CPUs (`max`),
256 MiB, nested KVM, Fedora OVMF and read-only virtio-blk. The remote run also
used virtio-net and a private virtio-fs export. Trusted init mounted `system` as
`data://` and `mirror://`, `headers` as `headers://`, and another `system` root
as `private://` with `--no-info`, then used the ordinary session/remote handoffs.
The existing remote client exercised:

- root and nested header enumeration, relative reads after `cd`, and repeated
  reads through independent bindings;
- native cat/date executable launches and native cat reading a nested header;
- guest copies of the welcome file, two nested headers and the native cat image,
  all matching their source bytes on the host;
- denied native mkdir/overwrite attempts and failed mount reacquisition by the
  ordinary shell, followed by successful reads;
- archive reads, RAM copy/read-back, HOST output capture, and Fastfetch text/JSON.

Fastfetch reported the three observable bindings, one shared pool ID, distinct
volume IDs, generation 1 and 67,100,672 allocatable bytes (63.99 MiB). Usage
remained null; the no-info root was absent while its file reads succeeded.
An ordinary foreground shell-script invocation failed for its documented lack
of launcher authority. A separate configured local boot used the supported
`session data://read-welcome` handoff: the native script and its cat child exited
zero, as did init and Fastfetch. This preserves the existing launcher boundary.

After the remote commands drained, GDB found the same mounted baseline as before:
2,080 core payload bytes, 2,528 adapter bytes and seven wrappers, with zero admitted
requests and no operation context or pending retirements. Peak requested storage
was 323,872 core bytes (324,539 including adapter allocation headers/padding) and
106,808 adapter bytes. These separate peak counters need not coincide and exclude
other kernel/launch allocations; they are not total heap or performance measures.

After the local handoff finished and all roots closed, core payload, core heap
requests, adapter payload, wrappers and admitted jobs were zero. Pool/volume lists,
request/retirement queues and operation context were empty. The ordinary packaged
image was then restored without mount configuration and booted without a disk.
All seven Fastfetch data modules completed without JSON errors; Disk returned an
empty result and the normal text diagnostic. Archive reads, RAM copy/read-back
and HOST listing worked. GDB found zero native allocations, wrappers, admitted
jobs or pools/volumes. The detached disk SHA-256 remained unchanged. All validation
VMs, debuggers, remote clients and host daemons were stopped.

Earlier [adapter validation](filesystem-native-adapter.md#historical-task-3-validation)
records 4 KiB sectors, duplicate extents, queue saturation, continuation behavior,
policy denial and observation attenuation. [Fastfetch validation](../userland/fastfetch.md#native-disk-validation)
records detailed formatting and no-observation cases. These results do not claim
physical-hardware coverage, owner-host timings, forced device/timeouts/OOM, maximum
directory depth or later generations. Those remain source-reviewed or unexercised
as stated in the linked evidence and [technical debt](../technical-debt.md#native-mount-design-limits).
No new tests, fault injection, permanent probes or boot/output automation were added.
