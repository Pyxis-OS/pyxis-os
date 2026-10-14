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
`share/installer/BOOTX64.EFI`, `share/installer/limine.conf.template` and Limine's
license. The template is the source menu configuration, not the generated
normal-boot command line or menu timeout; it contains separate placeholders
for both. These assets are available through read-only `boot`.

`init-install` opens `boot://installer.pxe` and creates the `install` space
with it as the first process. It delegates only the disk service, the two
source files, private memory, read-only clock and randomness, read-only `boot`,
read-only SYSTEM_INFO and, when it holds `power`, that resource narrowed to the
RESTART right for the [restart offer](../userland/installer.md#restart). The kernel adds the space's own console, keyboard,
pointer, display and title grants and the standard streams. SYSTEM_INFO supplies
the live kernel build revision for display and the installed ESP record.
It also explicitly delegates an owned WRITE-only copy of its existing Caelum
console as the private environment capability `installer_diagnostics`.
Only this trusted installer child receives
it; there is no ordinary-program inheritance or writable LOG resource. Phase
records/coalesced counters enter the existing kernel log ring/serial path.
A failed diagnostic write disables that observer independently of confirmed I/O.
`init-install` drops the space factory, waits for the installer and reports its
completion on the Caelum tab. It delegates no launcher, `tmp`, mount, host or
network authority. The packaged [native installer](../userland/installer.md)
implements consent, formatting, installation and
[system updates](../userland/system-updates.md). Update candidate inspection uses
raw reads without mounting a pool. Printed plans convey no authority; planning/cancel/dry-run use only raw
read-only inspection and no retained target mounts or persistent replay. After
explicit `wipe`/`update` confirmation, the shared executor acquires the same
exclusive write claim as install and rechecks eligibility, the selected
layout and consumed source/target state, refusing stale plans before any writes. It then releases that claim, writes the new programs into the pool's
`bin` volume through the disk handle, claims only the ESP partition and writes
it. Flush, release and read-back verification precede the read-only system-root
reopen.

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

DISKS OPEN requires OPEN and selects an ID plus READ_ONLY or READ_WRITE access.
READ_ONLY returns disk INFO/READ/MOUNT grants for inspection. It acquires no
exclusive claim and provides no snapshot against another raw writer. READ_WRITE
also grants WRITE/RELEASE and requires an operational writable device with flush
support and no latched write failure. Any existing raw claim, pending admission or active npfs/FAT mount on that
device returns BUSY, including a read-only instance with no live handles. READ_WRITE also waits for the selected device's GPT scan; inventory
sealing does not require every GPT scan to finish. Successful raw claims block overlapping mount admissions.

During [NVMe coexistence task 1](../wip/nvme-coexistence.md), NVMe
READ_WRITE OPEN and whole-disk CLAIM additionally read every logical block and
require every byte to be zero. Missing GPT metadata or zeroed headers alone do
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

CLAIM requires WRITE and re-establishes raw mutation authority on a disk object
whose claim was released. Partition zero claims the whole disk under READ_WRITE
opening's rules. A GPT entry claims only that partition: no other claim may
exist on the device and its physical extent must not overlap a pending admission or active npfs/FAT
mount, while disjoint partitions stay mounted. WRITE is then confined to the partition,
and mounts of it are refused.

NVMe partition CLAIM is refused during task 1, including on an object that
previously held and released a whole-disk claim. Task 3 replaces this temporary
blank-media-only policy with partition-first coexistence authority. Every later
NVMe whole-disk claim repeats the entire zero scan: a proof is never cached
across writes, release or mounts. Existing VirtIO and USB claim rules are
unchanged.

RELEASE requires RELEASE and flushes. For a whole-disk claim it then rescans GPT;
a partition claim cannot have changed the table and skips the rescan. It ends
the claim on all aliases of that disk object. It returns any I/O/scan failure and still ends raw
mutation authority; failure does not roll back bytes. Last-object close queues
the same cleanup through the worker, logging failure. Use explicit RELEASE to
observe the result. INFO and READ remain available on the released object.

## Mounts, consent and limits

The shared [extent registry](fat32-mounts.md#exclusion-and-retirement) reserves
physical ranges before probing; comparisons do not depend only on GPT ordinals.
`DISK_OPEN_FAT32` requires MOUNT and returns a read-only root plus a bound
retirement control. READ alone cannot admit a native view.

DISK OPEN_VOLUME requires MOUNT and uses the ordinary npfs partition/volume
request and validation path. It permits read-only directory rights, including
independently requested filesystem information, and mutation rights when the
disk object also holds WRITE, which only READ_WRITE opening grants. It waits for
the selected device's GPT scan and refuses a whole-disk claim or a claim on
that partition. CREATE_VOLUME requires MOUNT and WRITE and appends a live volume
with an empty root to the pool in one committed transaction, with an identity
from the kernel's entropy source; the installer uses it to add `bin`. Closing the disk does not revoke
a returned root. The normal configured-GUID mount authority remains separate.

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
