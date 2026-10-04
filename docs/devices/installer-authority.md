# Installer disk authority

The live image has a separate `Install Pyxis` Limine entry. `BOOT_MENU_TIMEOUT`
is a build setting, default `0` for immediate normal boot. Build install media
with `BOOT_MENU_TIMEOUT=5` to show the menu for five seconds. Both entries are
generated for either value. The install entry adds `boot.install=1`:
Caelum selects native `app://init-install.pxe` on the primary workload CPU and
idle init on every other workload CPU. The BSP remains kernel-only on multicore
boots. Duplicate install options or values other than `1` fail boot.

Normal init, sessions and applications receive no raw-disk service. Install mode
gives only its primary trusted init the `disks`, `boot_kernel` and `boot_archive`
resources. Selecting an executable with the same name during normal boot does
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
for both. These assets are available through read-only `app`.

Native init opens `app://installer.pxe` and delegates only the disk service,
the two source files, private memory, input/output, read-only clock and randomness,
read-only `app`, read-only SYSTEM_INFO and standard streams. SYSTEM_INFO supplies
the live kernel build revision for display and the installed ESP record. It waits for the child and reports its
completion. It delegates no launcher, writable home, mount, host, network or
display authority. The packaged [native installer](../userland/installer.md)
implements consent, formatting, installation, read-back verification and
read-only Update candidate inspection. Update inspection uses only raw reads;
it never opens a volume or acquires a write claim.

## Inventory and raw access

Installer disk authority remains VirtIO-only; configured read-only USB mounting
uses the separate native GUID authority.

[`include/abi/disk.h`](../../include/abi/disk.h) defines two native protocols.
DISKS ENUMERATE requires ENUMERATE and takes a zero-based inventory index; it
returns NOT_FOUND at the end and refuses an incomplete inventory. IDs select
physical devices for this boot independently of mutable GPT GUIDs. Unsupported
and failed candidates remain listed. INFO reports preparation, geometry when
known, writable/flush/failure flags, mounted/claimed state and the last completed
GPT status/GUID. A GPT GUID is available only for healthy or degraded metadata.

DISKS OPEN requires OPEN and selects an ID plus READ_ONLY or READ_WRITE access.
READ_ONLY returns disk INFO/READ/MOUNT grants for inspection. It acquires no
exclusive claim and provides no snapshot against another raw writer. READ_WRITE
also grants WRITE/RELEASE and requires an operational writable device with flush
support and no latched write failure. Any existing raw claim or retained npfs
pool on that device returns BUSY, including a read-only pool or one with no live
handles. Successful raw claims block all mounts on the device.

READ and WRITE use byte offsets and nonzero lengths aligned to the logical block
size, within device bounds and at most `DISK_IO_MAX_BYTES` (4096). WRITE captures
input before queueing. READ returns all requested bytes only on success; a failed
WRITE may have changed a prefix. WRITE and FLUSH require a live claim. Successful
WRITE alone promises no durability; FLUSH is the durability point. Uncertain
published write/flush failures latch write failure until reboot, without retry or
rollback. Calls execute through the serial filesystem worker and existing block
tickets; no userspace buffer is lent to DMA.

RELEASE requires RELEASE, flushes and rescans GPT before ending the claim on all
aliases of that disk object. It returns any I/O/scan failure and still ends raw
mutation authority; failure does not roll back bytes. Last-object close queues
the same cleanup through the worker, logging failure. Use explicit RELEASE to
observe the result. INFO and READ remain available on the released object.

## Mounts, consent and limits

DISK OPEN_VOLUME requires MOUNT and uses the ordinary npfs partition/volume
request and validation path. It permits read-only directory rights, including
independently requested filesystem information. It waits for the selected
device's GPT scan and refuses a live raw claim. Closing the disk does not revoke
a returned root. The normal configured-GUID mount authority remains separate.

Mounted pools stay retained for the boot. Opening a volume for inspection can
therefore prevent a later raw-write claim on the same device even after every
root closes. There is no pool teardown or installer exception. Installer
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
number when several qualify, then requires typed `wipe`. The installer does not
expose USB raw access or USB write/flush support. AHCI, NVMe, hotplug,
physical-media qualification and power-loss validation remain outside this interface. See [block storage](block-storage.md) and [GPT discovery](gpt.md).
