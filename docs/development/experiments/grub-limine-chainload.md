# Fedora GRUB → Limine rehearsal — 2026-10-21

Task 1 of the [accepted chainload plan](../../wip/grub-limine-chainload.md).
Disposable QEMU images only: no ThinkPad/vela connection, native write,
partition shrink, PXE staging or firmware-variable change on a laptop.
No production code, dependency pin or bootloader binary changes.

## Inputs and configuration

- Pyxis `c35788ecd3d555591350c53b4654bc34acdb5604`, the merged plan's main.
  Userland `cf731eff`, fs `f3f22b6e`, ports `053d5714`, lwIP `a1aadb91`.
- Clean `make -j8 image` in builder
  `pyxis-llvm23.1.3-3d69e83`, with Kconfiglib 14.1.0 available to its Python.
  No personal game-data overrides. Build log and artifact hashes are retained.
- Chronium-supplied Fedora Cloud Base Generic 44-1.7 FAT32 derivative,
  24 GiB sparse disk with a 600 MiB ESP at GPT entry 4, unchanged Fedora EFI
  files and free space after it. This is not either laptop's layout. Its
  pristine raw SHA-256 is
  `b463c5359b79640907971459f91912b3e32c89dde4b968483a37ff1cd622afde`,
  independently rechecked after the runs. Originals were never written.
- QEMU 10.2.2, q35, four vCPUs, 2 GiB, standard VGA, no NIC, fresh copies of
  chronium's Fedora `edk2-ovmf-20260812-8.fc44` CODE/VARS pair. Install/Update
  use nested KVM/`host`, an NVMe target and SCSI installer CD. EFI chainload,
  fallback, refusal and Fedora-return checks use KVM/`host` with VirtIO block.
  Additional TCG/`max` NVMe controls are distinguished below. These are
  functional observations, not performance comparisons.

The fixture has Btrfs subvolumes for both root and `/boot`, unlike vela's
recorded separate ext4 `/boot`. Its generated GRUB configuration calls `blscfg`
and sources `/boot/grub2/custom.cfg` through `41_custom`; no regeneration was
needed. Its ESP Fedora configuration redirects into that GRUB configuration.
The sole BLS entry names the existing Fedora kernel/initramfs. No persistent
Fedora rescue BLS entry exists in this cloud fixture.

Pinned Limine is the unchanged repository EFI binary, SHA-256
`94308b4e9ee1783a65cfd60570240a80595baf125c5b18e729d3dd977194ad51`.
The built kernel is 7,403,160 bytes, SHA-256
`9b9c95583b5b5ea69baa262368d388682ce88ecd2daa7e266487e2993a430b11`.
Both installer attempts produce the same 31,130,068-byte filtered archive,
SHA-256 `30e1480ccb4800a7b634a22716f7da15b315e7817a6e7f7f72b13292659f3068`.
Same-revision Update intentionally has identical payload content and a new attempt.

## Rehearsal and results

| Check | Observed result |
| --- | --- |
| Unchanged Fedora control | Serial login through both TCG/NVMe and KVM/VirtIO; ACPI powerdown completes |
| Coexist Install | Trusted selector chooses Coexist, disk 1, ESP 4, 2 GiB pool, 16 MiB journal; Apply completes, installer exits 0; menu unchanged |
| Bootstrap | Copy pinned Limine to `EFI/pyxis/BOOTX64.EFI`; add the accepted GRUB custom entry using the selected ESP filesystem UUID; `grub2-script-check` passes |
| Primary configuration | Printed entry placed at ESP `/boot/limine/limine.conf`; GRUB reports selecting Pyxis, then Limine loads the staged kernel/archive and Pyxis reaches the installed shell |
| Independent fallback | Separate copied kernel/archive pair under a test-owned ESP directory; `/boot/limine.conf` names only its rescue entry; removing the primary boots that pair with `boot.default_config=1` |
| Missing EFI executable | GRUB reports the absent `EFI/pyxis/BOOTX64.EFI`; Caelum does not start |
| Present primary, missing kernel | Limine reports failure to open the selected executable and returns control to its menu on a key; it does not use the existing rescue fallback |
| Coexist Update | Trusted discovery selects the existing Coexist pool; Apply completes and installer exits 0; existing menu, custom entry and EFI loader remain unchanged |
| Explicit manual activation | Replace only the ESP primary with Update's printed entry; GRUB's target stays fixed; Limine loads the new attempt and reaches the installed shell |
| Menu-choice rollback | On another updated copy, remove `custom.cfg` and restore the original test-modified `grubenv`; Fedora's unchanged BLS entry reaches serial login, then cleanly powers down |
| Fedora rescue target | A disposable custom probe uses the original BLS kernel, initramfs and options plus `systemd.unit=rescue.target`; reaches Rescue Mode; cloud root is locked, so no maintenance shell is claimed |

The bootstrap primary and fallback were prepared explicitly from Linux on
unmounted disposable images. They were not installed by the Coexist publisher.
The rescue pair has separate files; it does not depend on the current attempt's
kernel/archive names. Normal and rescue Pyxis shells power off successfully.
GDB confirms disk/GPT boot origin, normal versus rescue command lines and zero
retained FAT mount instances/workspace after each installer completes.

### Preservation

Before/after file inventories compare sizes and SHA-256 over every regular file
in Fedora's `/boot` subvolume and the ESP, with symlink targets recorded too.
Comparison occurs around each Apply, before later manual bootstrap or Linux boots:

- Install preserves all **370** existing file records and adds only its five
  immutable-attempt files. Fedora's EFI tree, fallback loader, generated GRUB
  configuration and BLS entry are unchanged.
- Update preserves all **381** baseline file records, including the custom
  entry, pinned EFI loader, both menus, independent rescue pair and first attempt.
  It adds only its five new attempt files. This does not claim atomic replacement
  of an existing same-revision program tree in the npfs pool.
- Update preserves the protective MBR, both GPT headers and both entry arrays
  byte-for-byte. Independent `sgdisk --verify` and `fsck.fat -n -v` succeed.
  GPT's pre-existing unaligned root-end advisory remains unchanged.

This proves the compared files and GPT regions, not every unused ESP byte,
sector-tear durability or physical-device firmware behaviour. Linux's later
normal/rescue boots can write their own root state; those changes are outside
these pre/post-Apply comparisons.

## Lookup observations and limits

With the exact pinned EFI binary and this GRUB handoff, deliberately added
`EFI/pyxis/limine.conf` and `EFI/BOOT/limine.conf` files pointing at distinct
missing kernels were **not selected**: the valid ESP primary still booted.
This differs from treating either path as an unconditional higher-priority
location. The [tagged loader source](https://github.com/Limine-Bootloader/Limine/blob/v12.9.3/common/lib/config.c)
derives its first candidate from the EFI loaded-image path, with a fallback
when that path cannot be recovered. These runs do not establish which
loaded-image path the binary used or explain the difference. They do establish
that the intended primary/fallback layout works for this handoff. Keep both
EFI config locations absent as required by the accepted plan and current
publisher guard; do not weaken that guard. Native read-only rehearsal must verify
lookup through the selected laptop's actual shim/GRUB before native integration.

The fixture's Btrfs-backed `grubenv` cannot be saved by GRUB (`sparse file not
allowed` also appears in the unchanged control). Host-prepared `next_entry`
selected test entries, but GRUB could not clear it. It was explicitly restored
for the rollback check. This is not a working native one-shot-selection claim;
use the existing menu reveal/selection for the eventual native check. The
ThinkPad's current filesystem and GRUB/BLS layout remain unobserved.

The missing rescue BLS entry is a fixture limit: reaching Fedora's rescue target
through a temporary custom probe is not a test of a distribution-generated
rescue kernel entry. Preserve and exercise the native machine's actual rescue
entry, if present, in its separately reviewed native step. No locked account was
unlocked and no permanent Fedora kernel entry was added.

The unchanged NVMe control stalls in OVMF under nested KVM/`host`; the same
fixture boots Fedora under TCG/`max`, and KVM/VirtIO boots normally. An additional
TCG/NVMe chainload reaches Caelum and archive recovery with NVMe writes disabled;
it does not qualify an installed RW NVMe session. Install/Update themselves
complete through the SCSI-booted installer with QEMU-only `nvme.write=1`.
No Limine or firmware fix is claimed, and native controller checks stay pending.

One VirtIO control failed to launch because another owned guest held its GDB
port; a second launch on a distinct port is the passing control. One rollback
setup attempted an unsupported host mount option and produced no valid saved
environment; it was discarded and repeated with a checked 1,024-byte original.
Neither void attempt is counted as evidence. All owned guests/debuggers and loop
mounts were stopped or released after qualification.

## Evidence and next gate

Raw commands, build log, QMP/serial/GDB captures, screenshots, file manifests,
GPT comparisons and SHA-256 records are retained privately under
`/shared/present/grub-limine-rehearsal/`. Helpers and disposable images are
qualification artifacts, not shipped installer machinery. Neither laptop was
contacted and no native run sheet was executed.

Task 1 is complete for review. Native integration and ThinkPad space preparation
remain separate authorizations: fresh read-only inventory first, then a reviewed
backup/recovery and filesystem-specific shrink plan; after an approved shrink,
verify Fedora boot before any Pyxis write. This result authorizes none of those
steps and does not qualify automatic Coexist publication.
