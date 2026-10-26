# GPT discovery

Caelum scans each inventoried [block device](block-storage.md) during boot and
publishes a partition-map snapshot per device through
[`gpt_get_snapshot()`](../../include/kernel/gpt.h). Discovery reads metadata only.
It does not repair GPT, choose a filesystem, mount partitions, grant access or
provide a partition I/O wrapper or userspace ABI.

VirtIO scans start immediately after their workers are created. USB candidates
are discovered asynchronously, so their snapshot capacity is reserved before AP
startup. A coordinator waits for terminal USB discovery (including partial
inventories), then uses one shared scratch buffer to scan each candidate through
ordinary block tickets. Individually READY USB media can publish maps while
aggregate discovery remains incomplete. USB maps are observations, not grants.
Configured GUID authority can select a unique observed USB/VirtIO match for
native mounting; installer raw USB access remains deferred. Mount selection
waits for all terminal boot scan results, including unrelated failed/unsupported
candidates.

## Execution and lifetime

`gpt_prepare()` reserves bounded scratch per device before AP startup. After the
block worker and scheduler are created, `gpt_start()` launches a BSP scan task
for each prepared device.
The task reads through the ordinary `block_submit()`, `block_wait()` and
`block_collect()` interface. Reads respect the device's transfer limit. A full
request queue causes a short sleep and retry within the scan deadline.

One absolute 30-second deadline covers scan I/O and admission waits. A timed-out
read ticket is abandoned; the block driver retains any outstanding DMA ownership.
The block driver's separate device watchdog still applies. The scanner frees
scratch when it publishes its result, including on failure. Snapshot storage
remains allocated for the boot; raw-writer release can replace its contents.

Call `gpt_get_snapshot(device)` on the BSP with interrupts disabled, outside
IRQ/fault entry. It returns `NULL` while initial discovery or a rescan is pending.
A published view is borrowed only until the next scheduling point; copy needed
fields before sleeping. Primary and backup copy statuses remain available for
diagnosis. There is no hotplug or general userspace rescan interface.

`gpt_rescan(device)` runs on a BSP kernel task with interrupts enabled after
initial discovery. Its caller must exclude all mounts and raw mutation on that
device until publication. It allocates fresh scratch, rereads both copies and
replaces the snapshot, including on failure. The
[installer raw claim](installer-authority.md) supplies this exclusion and rescans
before releasing its claim. Rescanning performs no writes or GPT repair.

## Supported layout and checks

The format follows the [UEFI GPT specification](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html).

The scanner supports 512-byte and 4 KiB logical blocks, GPT revision 1.0 and at
most 256 declared partition entries. Each entry is at least 128 bytes with a
power-of-two stride. Each declared entry array is at most 64 KiB. These are
explicit implementation limits; exceeding a supported bound does not establish
that a disk is corrupt.

The protective MBR is a gate for exposing any map. It must have the MBR signature
and exactly one `0xee` entry starting at logical block 1. Normally it covers the
disk through its last block, with the size field saturated at `UINT32_MAX` when
necessary; the bounded larger-device exception below permits a shorter extent.
Other entries must be zero. On 4 KiB media, bytes after the first 512 bytes of
the MBR block must also be zero. Legacy and hybrid MBR partition types
are unsupported. A valid GPT copy cannot bypass an absent or invalid protective
MBR; the scanner never repairs it.

The primary header is read at logical block 1. After full primary validation,
its recorded alternate selects the backup; otherwise the backup is inspected at
the disk's final logical block. For each copy, validation checks:

- Signature, supported revision, header length and CRC32 with the checksum field
  zeroed, plus reserved fields and zero padding beyond the defined header.
- The header's own location and reciprocal alternate-header location, nonzero
  disk GUID, ordered usable range and array placement outside that range.
- Space for at least 16 KiB of entry-array reservation at both ends, expanded
  when the declared array is larger; array arithmetic and disk bounds.
- The declared array CRC32, entry count and stride, and zero extension bytes
  beyond the first 128 bytes of every entry. For each used entry, a nonzero unique
  GUID and ordered extent entirely inside the usable range. Reserved attribute
  bits are unsupported; type-specific attribute bits are preserved.
- No duplicate unique GUIDs or overlapping extents among used entries. A zero
  type GUID identifies an unused entry.

Both copies are inspected independently. Healthy metadata requires agreement on
disk GUID, header size, usable range, entry count and stride, and a byte-for-byte
comparison of the declared arrays. Comparing array CRCs alone is insufficient.
Two individually valid but differing copies are ambiguous; neither wins by
position or preference.

### Images copied onto larger devices

A copied image may record its backup before the physical device end and/or
retain a shorter
protective MBR. The exception requires a valid primary, a valid reciprocal
backup at the primary's recorded alternate, and matching declared arrays and
header fields. The protective MBR remains structurally strict; its extent must
cover the primary array and every used partition, or have the standard
`UINT32_MAX` saturated coverage. A header found at the physical end when the
recorded alternate is earlier refuses this exception rather than choosing
between layouts. All reads remain bounded by the existing scan deadline.

The scanner publishes the primary as read-only `GPT_DEGRADED`, with one warning
naming `sgdisk --move-second-header`. Never repair or expand partitions automatically.
Bad CRCs, disagreement, overlaps, unsupported or unexamined metadata do not
qualify for this exception. Existing full-sized single-copy degraded recovery
remains unchanged. NPFS refuses writable initial opens and upgrades of degraded
maps with `READ_ONLY`, before pool opening or writable journal recovery (owner
decision, 2026-10-14). Filesystem rights never override degraded discovery.

## Published results

Only `GPT_HEALTHY` and `GPT_DEGRADED` carry a partition map. All other results
have zero selected copy and no exposed partitions.

| Status | Meaning |
| --- | --- |
| `GPT_HEALTHY` | Full protective MBR coverage and two valid, agreeing GPT copies at the device endpoints. The primary supplies the map. |
| `GPT_DEGRADED` | A surviving full-sized copy, or two agreeing copies with smaller-image placement/MBR coverage under the exception above. Read-only map. |
| `GPT_AMBIGUOUS` | Valid copies disagree, or a competing valid header exists at the physical end beyond the recorded alternate. No map is selected. |
| `GPT_ABSENT` | No protective MBR and neither GPT header is present. |
| `GPT_INVALID` | Metadata is malformed, the protective MBR gate fails, or neither copy can supply a valid map. |
| `GPT_UNSUPPORTED` | A legacy/hybrid MBR, unsupported GPT revision, bounds, attributes or device geometry prevents interpretation. |
| `GPT_UNAVAILABLE` | There is no usable block device at scan startup. |
| `GPT_NO_MEMORY` | Scratch allocation or scan-task creation failed. |
| `GPT_IO_ERROR` | A required block read could not complete successfully. |
| `GPT_TIMED_OUT` | The scan's deadline expired during admission or read completion. |

A read timeout or I/O error prevents map publication even if the other copy is
valid. Unsupported metadata also prevents degraded fallback. Ordinary full-sized
degraded discovery is for one valid copy paired with an absent or invalid copy. The larger-device exception requires two valid agreeing
copies; neither path treats an unexamined or uninterpretable copy as evidence of
agreement.

The snapshot records disk geometry and GUID, inclusive usable bounds, selected
copy (`1` primary, `2` backup) and used partitions in entry order. Each partition
retains its one-based entry number, type and unique GUID, first logical block,
block count, raw attributes and all 36 UTF-16 name code units. GUID bytes retain
GPT/EFI on-disk encoding; names have no implied terminator or namespace meaning.
Unknown partition type GUIDs are metadata, not a reason to invent a handler.

## Authority and accepted limits

Healthy metadata is a prerequisite for writable filesystem mounts, not sufficient
authority to perform them. Degraded maps are read-only. The snapshot records
discovery health; callers must separately check current block-device availability,
writability and the write-failure latch. Later transport failure does not rewrite
the published snapshot.

The installer can claim an unmounted device even when GPT is absent or invalid;
raw formatting does not require an existing healthy map. Claim release refreshes
discovery before mounts become available again. INFO during a claim describes
the last completed scan, not each raw write. External mutation remains
unsupported, and trusted kernel raw clients must preserve metadata or use the
same exclusion/rescan ordering. GUIDs, attributes and names grant no capabilities
or filesystem policy. See the
[accepted discovery limits](https://git.internal/PyxisOS/pyxis-os/issues/1414).

## Validation

The ordinary kernel build and image assembly passed (`make -j16`, with verified
unchanged SDK/userspace/ports bundles for the image). Manual boots used nested
KVM, 256 MiB RAM and QEMU 10.2.2 carrying the
[upstream AHCI boot fix](../development/qemu.md#ahci-cd-rom-crash-before-kernel-entry). Entropy
was enabled. Disposable 64 MiB images were partitioned and verified with host
`sfdisk` 2.41.3 and `sgdisk`; no custom format writer was added.

| Image and guest configuration | Observed result |
| --- | --- |
| 512-byte blocks, 256 GPT entries, writable attachment, four CPUs | Healthy map; two partitions at block 2048/count 8192 and block 16384/count 32768. |
| 4 KiB blocks, 128 GPT entries, read-only attachment, eight queue descriptors, one CPU | Healthy map; two partitions at block 256/count 1024 and block 2048/count 4096. |
| Valid 512-entry GPT, four CPUs | Unsupported copies, zero published partitions; ordinary boot continued. |
| Blank image, one CPU | Absent GPT, zero published partitions; ordinary boot continued. |
| No block device, four CPUs | GPT unavailable, zero published partitions; ordinary boot continued. |

GDB inspection of both healthy scans matched disk/partition GUIDs, logical
geometry, usable bounds, names and extents against host-tool output. The 512-byte
case also preserved a type-specific attribute at bit 60. Each scan completed
five ordinary block reads using the sleeping client interface, left no requests
outstanding and published only after selecting the validated map. Scratch was
released after publication. Whole-image SHA-256 hashes matched before and after
all attached-image boots, including the writable attachments.

### Larger-device recovery (2026-10-14)

The kernel/image build passed at `3c445d94`, using unchanged dependency bundles
from baseline main `30fb8888`. Manual QEMU 10.2.2 boots used nested KVM, four
CPUs, 1 GiB RAM, a writable VirtIO block device with 512-byte blocks and a
VirtIO-SCSI boot CD to avoid the host's stock AHCI CD-ROM crash. Host `sgdisk`
created a 1 GiB GPT image with a 128 MiB NPFS partition at LBA 2048. The clean
pool contained `data` with `hello.txt`; enlarging the image to 2 GiB preserved
its original metadata. A temporary, uncommitted native init probe used the
existing disk-scoped mount authority to request writable admission, read the
file, and request a writable upgrade after the read-only open.

| Image / kernel | Observed result |
| --- | --- |
| Enlarged 2 GiB image / baseline `30fb8888` | `GPT_INVALID`, no partitions; both mount requests refused. |
| Same image / `3c445d94` | `GPT_DEGRADED`, both copies valid, primary selected; original usable range 34..2097118 and partition extent preserved. One relocation warning; file read succeeded; initial writable open and read-only-pool upgrade returned `READ_ONLY`. |
| Original full-sized 1 GiB control / `3c445d94` | `GPT_HEALTHY`; file read and writable admission succeeded. |
| Backup relocated to the 2 GiB end, original short PMBR retained / `3c445d94` | `GPT_DEGRADED`; read succeeded, both writable requests returned `READ_ONLY`. |
| Original 1 GiB layout copied over a different 2 GiB GPT, retaining its valid end backup / `3c445d94` | `GPT_AMBIGUOUS`, zero partitions; mount requests refused. |

GDB confirmed the snapshots and NPFS pool state: degraded pools stayed
read-only; the healthy control was writable; the ambiguous case opened no
pool. Whole-image SHA-256 hashes were unchanged across all boots, including
the writable attachments. QEMU/GDB jobs were stopped afterward.

Full-sized single-copy recovery, malformed metadata, I/O/timeout failures and
allocation failures remain source-inspected only. No fault-injection or new
test framework was added. These observations do not establish native hardware
behavior, power-loss recovery or owner-host performance.
