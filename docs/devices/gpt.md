# GPT discovery

Caelum scans the selected [block device](block-storage.md) once during boot and
publishes an immutable partition-map snapshot through
[`gpt_get_snapshot()`](../../include/kernel/gpt.h). Discovery reads metadata only.
It does not repair GPT, choose a filesystem, mount partitions, grant access or
provide a partition I/O wrapper or userspace ABI.

## Execution and lifetime

`gpt_prepare()` reserves bounded scratch before AP startup. After the block
worker and scheduler are created, `gpt_start()` launches one BSP kernel task.
The task reads through the ordinary `block_submit()`, `block_wait()` and
`block_collect()` interface. Reads respect the device's transfer limit. A full
request queue causes a short sleep and retry within the scan deadline.

One absolute 30-second deadline covers scan I/O and admission waits. A timed-out
read ticket is abandoned; the block driver retains any outstanding DMA ownership.
The block driver's separate device watchdog still applies. The scanner frees
scratch when it publishes its result, including on failure. The snapshot remains
allocated and immutable for the boot.

Call `gpt_get_snapshot()` on the BSP with interrupts disabled, outside IRQ/fault
entry. It returns `NULL` while scanning and a pointer to the final snapshot
after publication. There is no wait interface, rescan, hotplug or runtime map
replacement. The primary and backup copy statuses remain available for diagnosis.

## Supported layout and checks

The format follows the [UEFI GPT specification](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html).

The scanner supports 512-byte and 4 KiB logical blocks, GPT revision 1.0 and at
most 256 declared partition entries. Each entry is at least 128 bytes with a
power-of-two stride. Each declared entry array is at most 64 KiB. These are
explicit implementation limits; exceeding a supported bound does not establish
that a disk is corrupt.

The protective MBR is a gate for exposing any map. It must have the MBR signature
and exactly one `0xee` entry starting at logical block 1 and covering the disk
through its last block, with the size field saturated at `UINT32_MAX` when
necessary. Other entries must be zero. On 4 KiB media, bytes after the first 512
bytes of the MBR block must also be zero. Legacy and hybrid MBR partition types
are unsupported. A valid GPT copy cannot bypass an absent or invalid protective
MBR; the scanner never repairs it.

The primary header is read at logical block 1 and the backup at the disk's final
logical block. For each copy, validation checks:

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

## Published results

Only `GPT_HEALTHY` and `GPT_DEGRADED` carry a partition map. All other results
have zero selected copy and no exposed partitions.

| Status | Meaning |
| --- | --- |
| `GPT_HEALTHY` | Valid protective MBR and two valid, agreeing GPT copies. The primary supplies the map. |
| `GPT_DEGRADED` | Valid protective MBR and exactly one valid copy; the other is absent or invalid. The surviving map is read-only. |
| `GPT_AMBIGUOUS` | Both GPT copies validate independently but disagree. No map is selected. |
| `GPT_ABSENT` | No protective MBR and neither GPT header is present. |
| `GPT_INVALID` | Metadata is malformed, the protective MBR gate fails, or neither copy can supply a valid map. |
| `GPT_UNSUPPORTED` | A legacy/hybrid MBR, unsupported GPT revision, bounds, attributes or device geometry prevents interpretation. |
| `GPT_UNAVAILABLE` | There is no usable block device at scan startup. |
| `GPT_NO_MEMORY` | Scratch allocation or scan-task creation failed. |
| `GPT_IO_ERROR` | A required block read could not complete successfully. |
| `GPT_TIMED_OUT` | The scan's deadline expired during admission or read completion. |

A read timeout or I/O error prevents map publication even if the other copy is
valid. Unsupported metadata also prevents degraded fallback. Degraded discovery
is specifically for one valid copy paired with an absent or invalid copy; it
does not treat an unexamined or uninterpretable copy as evidence of agreement.

The snapshot records disk geometry and GUID, inclusive usable bounds, selected
copy (`1` primary, `2` backup) and used partitions in entry order. Each partition
retains its one-based entry number, type and unique GUID, first logical block,
block count, raw attributes and all 36 UTF-16 name code units. GUID bytes retain
GPT/EFI on-disk encoding; names have no implied terminator or namespace meaning.
Unknown partition type GUIDs are metadata, not a reason to invent a handler.

## Authority and accepted limits

Healthy metadata is a prerequisite for future partition writes, not sufficient
authority to perform them. Degraded maps are read-only. The snapshot records
discovery health; callers must separately check current block-device availability,
writability and the write-failure latch. Later transport failure does not rewrite
the immutable snapshot.

There is no raw-block write gate: trusted kernel raw-block clients must preserve
GPT headers, arrays and the protective MBR throughout the boot. External mutation
or a raw client rewriting metadata can invalidate the snapshot. There is no
coherent live-update or repair protocol. GUIDs, attributes and names grant no
capabilities or filesystem policy. See the
[accepted discovery limits](../technical-debt.md#gpt-snapshot-and-profile-limits).

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

Degraded or disagreeing copies, malformed metadata, I/O/timeout failures and
allocation failures have code inspection only. Deliberately damaged images,
fault injection, automated tests and boot/output automation were not used.
These observations do not establish physical-hardware behavior, power-loss
recovery or owner-host performance.
