# Installer disk authority

The live image has a separate `Install Pyxis` Limine entry. `BOOT_MENU_TIMEOUT`
is a build setting, default `0` for immediate normal boot. Build install media
with `BOOT_MENU_TIMEOUT=5` to show the menu for five seconds. Both entries are
generated for either value. The install entry's command line is
`init=boot://init-install.pxe boot.install=1`. Duplicates warn and the last
value wins; `0` disables install authority, and an invalid final value warns and
leaves it disabled.

Boot init, space inits, sessions and applications receive no raw-disk service on
a normal boot. Install mode gives the install entry's boot init, `init-install`,
the `disks`, `boot_kernel` and `boot_archive` resources. Selecting an executable with the same name during normal boot does
not supply these grants. Other init resources retain their ordinary contracts.

## Boot sources and handoff

`boot_kernel` is the original Limine-loaded ELF file, not the running kernel
image. `boot_archive` is the entire original uncompressed newc archive, including
trailer and padding. Both are native FILE objects with READ only. The adapter
copies physical extents, validates their complete page-rounded reservations
and preserves the original ELF frames as kernel-reserved memory. Before AP
startup the kernel maps borrowed ELF frames supervisor read-only/NX; the archive
reuses its existing immutable mapping. No payload copy or boot-file Limine
pointer survives the adapter. Views and physical storage remain valid for the kernel lifetime;
closing a FILE frees its wrapper, not the source bytes.

The [boot manifest](../../boot/initrd.lua) also packages
`share/installer/BOOTX64.EFI` and Limine's license through read-only `boot`.
The installer generates the installed menu from an explicit normal/rescue
allowlist with the selected disk binding and three-second timeout; it never
imports entries or authority-bearing command lines from the live menu.

`init-install` owns the `install` space handoff. A trusted mode selector inside
that space starts the chosen classic or coexistence installer with explicitly
narrowed disk rights. Both children receive ENUMERATE, OPEN_READ_ONLY, OPEN_POOL,
MOUNT_READ_ONLY and MOUNT_READ_WRITE. Classic additionally receives ERASE;
coexistence additionally receives APPEND_POOL. Neither child receives broad
OPEN, raw OPEN_PARTITION, launcher or space-factory authority. The handoff also
delegates the two source files, private memory, read-only clock and randomness, read-only `boot`,
read-only SYSTEM_INFO and, when it holds `power`, that resource narrowed to the
RESTART right for the [restart offer](../userland/installer.md#restart). The kernel adds the space's own console, keyboard,
pointer, display and title grants and the standard streams. SYSTEM_INFO supplies
the live kernel build revision for display and the installed ESP record.
It also explicitly delegates an owned WRITE-only copy of its existing Caelum
console as the private environment capability `installer_diagnostics`.
Only the trusted install handoff delegates it; there is no ordinary-program
inheritance or writable LOG resource. Phase
records/coalesced counters enter the existing kernel log ring/serial path.
A failed diagnostic write disables that observer independently of confirmed I/O.
`init-install` waits for completion and reports it on the Caelum tab. The
installer receives no `tmp`, generic mount, host or network authority. The
implemented kernel interfaces below do not establish integration of the packaged
[native installer](../userland/installer.md) or its
[Update executor](../userland/system-updates.md); that task-3 work is pending.
Printed plans convey no authority. Planning, Cancel and dry-run inspect through
raw reads without retaining target mounts, writing data or persisting replay.

## Inventory and raw access

Installer disk authority covers retained VirtIO, USB BOT/SCSI and NVMe candidates.
USB write claims require the same per-device C.1 qualification as writable native
mounts: known write-protection-clear media and a successful real blocking cache
synchronization. Configured native mounting uses its separate GUID authority.
NVMe writes are disabled by default and require the explicit `nvme.write=1` boot
option. This gate does not itself grant a raw claim.

[`include/abi/disk.h`](../../include/abi/disk.h) defines two native protocols.
DISKS ENUMERATE requires ENUMERATE and takes a zero-based inventory index; it
returns NOT_FOUND at the end. ENUMERATE and OPEN wait within the operation
deadline for boot discovery to seal the retained candidate IDs. They refuse
lost registry records or incomplete VirtIO candidate bookkeeping, but accept
sealed observed USB inventory when coverage is partial, such as an unsupported
EHCI controller or uninspected hub descendants. Unseen disks cannot be listed
and may conceal other eligible targets. IDs select physical devices for this
boot independently of mutable GPT GUIDs. Unsupported and failed retained
candidates remain listed, as do protected and unqualified USB disks. INFO
reports preparation, geometry when known, writable/flush/failure flags,
mounted/claimed state and the last completed
GPT status/GUID. A GPT GUID is available only for healthy or degraded metadata.

DISKS OPEN selects an ID plus READ_ONLY or READ_WRITE access. Its explicit
OPEN_READ_ONLY issuer right permits only READ_ONLY; OPEN permits either access
mode. READ_ONLY returns disk INFO/READ/MOUNT grants, with no exclusive claim or
snapshot against another raw writer. READ_WRITE also grants WRITE/RELEASE and
requires an operational writable device with flush support and no latched write
failure. Any existing raw claim, pending admission or active npfs/FAT mount on
that device returns BUSY, including a read-only instance with no live handles.
Writable admission waits for the initial GPT scan; inventory sealing alone does
not mean every scan finished.

Ordinary whole-disk READ_WRITE OPEN and CLAIM zero enforce the same GPT policy
on every backend: allow an absent GPT, or a healthy GPT containing only the
native-pool partition type. An ESP or any other partition type returns DENIED,
including through Read the room. Degraded, ambiguous, invalid or unsupported
metadata also refuses ordinary whole-disk admission. Destructive classic
installation uses the separate reviewed erase grant below; marker names cannot
bypass the kernel policy.

Ordinary NVMe whole-disk admission additionally reads every logical block and
requires every byte to be zero. Missing GPT metadata or zeroed headers alone do
not prove blank media. Nonzero data returns DENIED; invalid geometry, partial
reads, I/O failure or deadline expiry also refuse the claim. The scan uses a
heap scratch buffer of at most 64 KiB, bounded by the device transfer limit, and
the same operation deadline as admission, without extending it between reads.
Its cost scales with namespace capacity; a large
blank disk may exceed the current 30-second deadline and remain unavailable for
raw writes. No throughput or native hardware qualification is implied.
The serial filesystem worker remains occupied during the scan, so other queued
filesystem requests can also expire while a claim is being checked.

READ and WRITE use byte offsets and nonzero lengths aligned to the logical block
size, within device bounds and at most `DISK_IO_MAX_BYTES` (4096). WRITE captures
input before queueing. READ returns all requested bytes only on success; a failed
WRITE may have changed a prefix. WRITE and FLUSH require a live claim. Successful
WRITE alone promises no durability; FLUSH is the durability point. Uncertain
published write/flush failures latch write failure until reboot, without retry or
rollback. Calls execute through the serial filesystem worker and existing block
tickets; no userspace buffer is lent to DMA.

CLAIM requires WRITE and a released claim. An ordinary whole-disk object may
claim partition zero under the same GPT/blank rules as writable OPEN, or one GPT
partition on transports permitting ordinary partition claims. The ordinary NVMe
path still refuses partition claims; explicitly bound opening below supplies
NVMe partition authority. Every ordinary NVMe whole-disk claim repeats the zero
scan. No proof is cached across writes, release or mounts.

The shared extent registry permits only one raw claim per device. A partition
claim excludes overlapping pending or live mounts while permitting disjoint
mounted partitions. Writes remain inside its physical extent; a mounted extent
cannot be reclaimed through a second alias or GPT ordinal.

RELEASE requires RELEASE and flushes. For a whole-disk claim it then rescans GPT;
a partition claim cannot have changed the table and skips the rescan. It ends
the claim on all aliases of that disk object. It returns any I/O/scan failure and still ends raw
mutation authority; failure does not roll back bytes. Last-object close queues
the same cleanup through the worker, logging failure. Use explicit RELEASE to
observe the result. INFO remains available on released objects. Ordinary
unbound objects retain READ; bound pool and raw-partition objects require a
live claim for READ as well as WRITE, including retained GPT-metadata reads.
After release, READ returns DENIED until a successful binding-validated CLAIM.
The live extent exclusion prevents a table change during an admitted read.

## Reviewed erase and GPT append

DISKS REVIEW_ERASE requires the ERASE issuer right; REVIEW_APPEND requires
APPEND_POOL. Both bind a boot-scoped device ID, expected geometry and the
comparable GPT disk identity, and capture exact bytes before Review: the
MBR sector, both complete header sectors and each bounded rounded
entry array, including its own sector padding. Temporary exclusion covers
capture; no raw claim survives preparation. Initial GPT discovery must finish
before preparation. INFO on the returned object reports retained geometry/GPT
facts, while READ overlays the retained metadata on live reads of other sectors.
It is a metadata snapshot, not a snapshot of filesystem contents or source files.

Erase review can retain safely framed absent or semantically damaged metadata,
including bad CRCs or disagreeing copies. An absent header references no array.
Unbounded framing, unsupported capture capacity and relocated backup layouts
refuse capture. Append review requires a healthy, agreeing GPT. These capture
limits do not change ordinary read-only GPT discovery policy.

Review objects permanently grant INFO/READ and only their matching consume
operation. They have no MOUNT, WRITE, RELEASE or CLAIM authority. BEGIN_ERASE
checks the exact confirmation bytes `wipe`; obtaining typed consent and
rendering the reviewed plan remain the trusted UI's responsibility. It consumes
the erase review across all aliases, takes whole-device exclusion and compares
every retained metadata byte and the geometry before returning a separate
already claimed writer. It performs no erase itself. The writer grants
INFO/READ/WRITE/RELEASE, cannot mount volumes, and can never CLAIM again after
RELEASE. Its explicit destructive authority can include foreign OS partitions;
it is not delegated to coexistence execution.

APPEND_POOL consumes an append review and the chosen native-pool slot, GUID and
extent. Under whole-device exclusion it compares the captured metadata, checks
that the slot is unused and the extent is within usable bounds and disjoint
from every existing partition, then updates the backup copy before the primary.
Only the chosen entry and required array/header CRC fields change; existing
entries, other header bytes, per-copy padding, disk identity and the MBR are
preserved. Flushes and exact read-back of both copies precede publication. This
does not format the pool or write the ESP, and the two-copy update is not
crash-atomic.

Return objects, capability reservations, derived grants, candidate buffers and a
device-owned quarantine record are prepared before append effects. Success
returns an already claimed pool DISK, transferring the held exclusion directly
to the partition without a new allocation, device probe or exclusion gap. The
reply retains the selection, phase, possible-effects flag and failed block where
known, including on a worker failure. A flush has no single failed block. When
effects are possible and the result is uncertain, the device-owned record keeps
whole-device exclusion until reboot even after every handle closes. No automatic
rollback, deletion, retry or second append occurs. A lost reply is outcome
unknown to the caller. A spent review object requires new preparation and Review;
quarantined media additionally requires a new boot or owner recovery tools.

## Bound partition authority

DISKS OPEN_POOL requires its own OPEN_POOL issuer right and the native-pool GPT
type. It opens an existing healthy partition directly for formatting or Update,
without first acquiring a whole-disk writer. Admission compares both GPT copies
with the selected disk/partition GUIDs, type, slot, extent, attributes and device
geometry. APPEND_POOL returns the same kind of bound object for a newly added
entry. The immutable selection survives RELEASE, aliases and later CLAIM; zero
or a neighbouring partition is denied. Worker-side volume admission also checks
that selection. Pool READ and WRITE are extent-confined, and pool objects cannot
open FAT views. OPEN_POOL supplies the existing-pool Update path without
appending another entry.

DISKS OPEN_PARTITION has a separate raw qualification issuer right. It binds a
healthy selected partition in the same way, permitting raw mutation only inside
that extent. It also retains the captured GPT metadata for read-only access
outside the partition, so the strict FAT bootstrap can inspect the protective
MBR and both GPT copies. A request containing any other outside sector is denied
in full; it never falls back to a live outside read. This qualification authority
is delegated to neither production installer child. Classic can initialize a
new ESP while holding its explicit destructive erase writer; subsequent mounted
work uses the native view interfaces.

The coexistence ESP uses explicitly delegated native FAT RW views through
DISKS OPEN_FAT32. They grant no raw write, format or erase authority. Compare
the bound control's OBSERVE identity/geometry with the reviewed plan before
mutation. The selected-view path supports both Install and Update; executor and
native payload staging integration remain pending. These primitives establish
no native NVMe write qualification; writes remain disabled until the explicit
boot option enables the device.

## Mounts, consent and limits

The shared [extent registry](fat32-mounts.md#exclusion-and-retirement) reserves
physical ranges before probing; comparisons do not depend only on GPT ordinals.
`DISK_OPEN_FAT32` requires MOUNT and returns an explicitly selected RO/RW native
view plus its bound retirement control; RW also requires WRITE even when root
rights are attenuated to read-only. The mount-only DISKS OPEN_FAT32 requires
MOUNT_READ_ONLY or MOUNT_READ_WRITE, with RW admission requiring the latter.
READ alone cannot admit a native view.

DISK OPEN_VOLUME requires MOUNT and uses the ordinary npfs partition/volume
request and validation path. It permits read-only directory rights, including
independently requested filesystem information, and mutation rights when the
disk object also holds WRITE. It waits for the selected device's GPT scan and
refuses a whole-disk claim or a claim on
that partition. CREATE_VOLUME requires MOUNT and WRITE and appends a live volume
with an empty root to the pool in one committed transaction, with an identity
from the kernel's entropy source; the installer uses it to add `bin`. Closing the disk does not revoke
a returned root. Bound disk objects cannot select another partition through
either operation. The normal configured-GUID mount authority remains separate.

Mounted pools stay retained for the boot. Opening a volume for inspection can
therefore prevent a later whole-disk or overlapping partition claim even after every
root closes. FAT instances instead support explicit drained retirement. There is no pool teardown or installer exception. Installer
sequencing follows the owner-accepted current direction: inspect consent through
raw reads and the format library, reading COMMITTED journal images as an
in-memory overlay without modifying the disk. The installer looks up regular
root `SAFE_TO_WIPE` markers in every live volume. Rejected consent leaves the disk
untouched. See the
[accepted limits](../technical-debt.md#installer-authority-and-retained-pools).

Disk capabilities authorize operations, not selection of a safe target. The raw
kernel service does not check `SAFE_TO_WIPE`, authenticate contents or interpret
partition names as consent. Target preparation and explicit consent belong to
the trusted installer. The installer selects the sole eligible disk automatically or asks for a disk
number when several qualify, then requires typed `wipe` for install or `update`
for Update. Partial USB coverage does not change these selection and consent
rules; a sole eligible observed disk is not proof that no other disk exists.
USB raw access follows the same claims, bounded I/O, ordered flushes, explicit
release and GPT rescan as VirtIO. Normal boots still grant no raw-disk service.
AHCI, hotplug and power-loss validation remain outside this interface.
[USB installation](usb-installation.md) records the native ThinkPad installation.
See [block storage](block-storage.md) and [GPT discovery](gpt.md).
