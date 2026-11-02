# Coexisting with another operating system

Coexist installs Pyxis into a selected unallocated GPT gap while preserving the
existing partitions and using a healthy supported FAT32 ESP. It supports a disk
containing Fedora or Windows without treating their partition types, names or
ESP files as permission to overwrite them. See the [installer](installer.md),
[partition-bound authority](../devices/installer-authority.md),
[NVMe profile](../devices/nvme.md) and [FAT32 mounts](../devices/fat32-mounts.md).

## Review and execution

The planner reads the complete GPT and ESP evidence before consent. Both GPT
copies must agree. Coexist requires 512-byte logical sectors, a clean supported
FAT32 ESP, capacity for the payload/recovery publication, an available GPT slot
and a suitable free extent. FAT12/FAT16, ambiguous metadata and insufficient
space refuse in Review before writes. The pool-size default is the whole
selected gap; the reviewed size is explicit. Existing Update keeps its partition.
Pyxis does not shrink or move a foreign filesystem to make room.

Trusted init delegates separate append and partition-open rights. The pool DISK
is partition-bound; the ESP is reached only through the delegated native FAT32
RW view. Coexist receives neither writable whole-disk OPEN nor the reviewed
Classic erase/ESP-rebuild grants. Releasing a partition-bound handle ends its
claim; reads, like writes, need a live claim.

After typed consent, the executor appends the GPT entry first: only the chosen
slot and required CRC/header fields change, and both copies are verified. It
then opens the admitted partition and builds the native pool. A failure stops
with phase and identity evidence; no automatic rollback, retry or repartitioning
is implied. Files outside the owned Pyxis publication remain foreign.

Boot payloads use immutable `EFI/pyxis/<revision>/<attempt>/` directories. Current
owned publication and firmware registration are described in
[package upgrades](package-upgrades.md) and [system updates](system-updates.md).
They supersede the original manual-entry activation path. Fedora/Windows boot
configuration is not edited to install the owned entry. After verified
activation, unreferenced attempts are cleaned only with proven ownership and
payload hashes; ambiguous nonempty attempts refuse cleanup. The active attempt
and pinned rescue remain. [Recovery limits](https://git.internal/PyxisOS/pyxis-os/issues/1421)
remain separate from authority to publish a new attempt.

## Qualification and limits

Disposable QEMU checks include partition/GPT preservation, the native FAT32 ESP
path, Classic Install/Update compatibility and the copied Fedora fixture. The
[GRUB → Limine reference](grub-limine-chainload.md) and its
[native record](../development/experiments/grub-limine-native.md) document the
ThinkPad's separately approved manual integration: Install/Update, normal/rescue
Pyxis boots and Fedora returns. This does not prove Windows boot preservation,
other firmware, interrupted-power recovery or the later owned-entry workflow.

Target-specific native writes need their own approval and fresh disk/ESP/free-gap
evidence; never reuse another machine's offsets or diagnostic copies. Remaining
[NVMe hardware checks](https://git.internal/PyxisOS/pyxis-os/issues/1422),
[owned publication native qualification](https://git.internal/PyxisOS/pyxis-os/issues/1509)
and [inspection/recovery limits](https://git.internal/PyxisOS/pyxis-os/issues/1416)
are tracked separately. Secure Boot remains outside this profile. No supported
foreign-data filesystem mount, suspend/hibernate coordination, firmware-menu
migration or stable partition number after foreign table edits is promised.
