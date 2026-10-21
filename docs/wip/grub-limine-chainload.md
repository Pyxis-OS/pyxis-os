# Fedora GRUB and Pyxis Limine chainloading

All three decisions are **accepted (chronium, 2026-10-21)**. This refines the
[NVMe coexistence](nvme-coexistence.md#sideloaded-limine-first) sideloading route.
Task 1, the disposable QEMU rehearsal, is assigned to epsilon and starts after
this plan merges. Task 2, native integration, requires separate authorization.
The ThinkPad goes first because its hands-free harness, passwordless sudo and
remote reboot are already available; vela remains a later target.

## Evidence and assumptions

Source inspection uses Pyxis `3f7f9240` and its userland pin `cf731eff`.
The [ThinkPad inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt)
and [notes](../targets/t14-gen1-amd/notes.md), dated 2026-10-02, record a 238.5 GiB
NVMe disk with a 600 MiB ESP, a 2 GiB partition and a 235.9 GiB partition,
with no usable gap reported. Fedora's first firmware entry targets
`EFI/fedora/shimx64.efi`; a Windows Boot Manager entry also exists, without proof
that its files remain. Preserve any Microsoft files along with Fedora's files.
The snapshot records Secure Boot disabled but not filesystem types, usage or
GRUB/BLS configuration. Vela's ext4/btrfs layout must not be assumed here.

The accepted chainload route and stable loader/config paths apply to either
machine. The ThinkPad differs in space preparation and its smaller ESP: fresh
read-only inspection must establish filesystem/encryption details, usage,
layout, boot entries, GRUB/BLS style, ESP geometry, contents and free capacity.
If the running Fedora uses another boot path, reconcile it in review before
writes rather than force the assumed route.

The committed [vela inventory](../targets/ideapad-slim5-16iph11/ideapad-inventory.txt)
records a 953.9 GiB NVMe disk, a 954 MiB vfat ESP at partition 1 mounted on
`/boot/efi`, a 1.9 GiB ext4 `/boot` at partition 2 and a 475.9 GiB btrfs
partition 3. It records Fedora firmware entries for `EFI/fedora/shimx64.efi`
and `EFI/fedora/shim.efi`. The [target notes](../targets/ideapad-slim5-16iph11/notes.md)
record Secure Boot disabled; that is dated evidence, not a fresh check.

Chronium reports unused space after partition 3 and Fedora's default GRUB/BLS
setup. The inventory does not establish exact free-gap LBAs, an available GPT
slot, ESP free bytes or its complete file tree, nor the active GRUB custom-entry
hook. No new target inventory was taken: vela is off. Those facts, FAT32 geometry
and health, Secure Boot state and the active Fedora configuration must be checked
read-only before an implementation's native write step. Rounded `lsblk` sizes
are not partition-append coordinates.

## Current installer boundary

Coexist Install appends the reviewed native-pool partition and preserves foreign
partitions; Update opens the existing pool. Both stage immutable
`EFI/pyxis/<revision>/<attempt>/` payloads and print/save a Limine entry. They do
not install a Limine EFI executable, activate that entry or edit Fedora's menu.
The payload set is kernel, archive, revision and entry text, plus its manifest:
see [plan.c](../../userspace/installer/plan.c) and
[esp_payload.c](../../userspace/installer/esp_payload.c). Classic's fixed
`EFI/BOOT/BOOTX64.EFI` installation must not be used to bootstrap a shared ESP.

Classic [publisher activation](installer-tui.md#integrated-publication--implementation-and-qualification-2026-10-18)
is accepted after its 2026-10-20 disposable native gate; that did not authorize
internal NVMe writes or qualify Coexist activation. Its configuration layout uses `/boot/limine/limine.conf` with independent `/boot/limine.conf` rescue
fallback. Coexist integrated activation remains separately gated
([plan.h](../../userspace/installer/plan.h)). These paths are on the ESP, not
Fedora's separate ext4 `/boot` partition. No Linux filesystem write authority or
firmware-variable authority follows from an owned ESP grant.

## Routes

Recommended first route:

```text
firmware -> Fedora shim -> GRUB/BLS -> Pyxis Limine -> Caelum
```

GRUB EFI-chainloads the pinned Limine executable; Limine loads Caelum through its
native boot protocol. Fedora keeps its existing BLS kernel/rescue management.
This adds a menu handoff but needs no copying of Fedora kernel versions or
kernel-update importer. GRUB supports an EFI executable as a
[chainloader target](https://www.gnu.org/software/grub/manual/grub/html_node/chainloader.html);
Fedora's [BLS design](https://fedoraproject.org/wiki/Changes/BootLoaderSpecByDefault)
keeps its generated kernel entries separate from the custom entry.

The reverse route is possible: firmware selects Limine, whose EFI-chainload
entry selects the verified Fedora shim, then GRUB/BLS selects Fedora. Limine's
[pinned configuration format](https://github.com/Limine-Bootloader/Limine/blob/v12.9.3/CONFIG.md)
provides `protocol: efi` and a target `path`. This preserves Fedora's kernel
management too, but making Limine the front door adds firmware/default-menu
policy. Keep that as a later option; do not implement direct Fedora Linux-protocol
entries or reciprocal default entries in this slice. Neither route establishes
Secure Boot support for the unsigned Pyxis loader.

## Accepted decisions (chronium, 2026-10-21)

1. **First front door — accepted: Fedora GRUB stays first.** Add an explicit Pyxis
   choice; leave Fedora's default, firmware entries and BootOrder unchanged.
   Existing firmware selection remains recovery. Limine-first remains the later
   direction in the coexistence plan, not a prerequisite for this route.

2. **GRUB entry ownership — accepted: one chronium-managed Fedora-side custom
   entry.** Verify whether the active GRUB configuration sources
   `/boot/grub2/custom.cfg`; use it if present. Otherwise use the distribution's
   `/etc/grub.d/40_custom` hook and normal generation into `/boot/grub2/grub.cfg`.
   Fedora's [unified configuration design](https://fedoraproject.org/wiki/Changes/UnifyGrubConfig)
   makes the ESP's Fedora `grub.cfg` a redirect, not the file to overwrite.
   Confirm the installed layout first. Do not invent a BLS EFI entry, edit the
   generated Fedora entries, or give the Pyxis installer Linux-root authority.

3. **Loader and configuration ownership — accepted: stable
   `EFI/pyxis/BOOTX64.EFI`, pinned initially to Pyxis's Limine 12.9.3.** A reviewed
   Fedora-side bootstrap copies and verifies the [pinned binary](../../third_party/limine/UPSTREAM.md)
   into that Pyxis-owned path; current Coexist does not do this. Reuse the
   publisher's ESP primary/fallback configuration paths. Do **not** place
   `limine.conf` beside the executable: Limine searches there first and would
   shadow publication; [recovery.c](../../userspace/installer/recovery.c) already
   refuses `EFI/pyxis/limine.conf` and `EFI/BOOT/limine.conf`. Refuse conflicting
   existing files rather than overwrite unreviewed content. Use the currently
   printed Coexist entry manually until its publisher activation is qualified.
   A later loader upgrade needs separate qualification. Per-attempt EFI loaders
   would instead require changing the GRUB target on each update.

Illustrative GRUB template only, after the selected ESP and its unique filesystem
UUID have been verified; `<ESP_FILESYSTEM_UUID>` is not a GPT partition GUID:

```text
menuentry 'Pyxis (Limine)' {
    insmod part_gpt
    insmod fat
    search --no-floppy --fs-uuid --set=pyxis_esp <ESP_FILESYSTEM_UUID>
    chainloader ($pyxis_esp)/EFI/pyxis/BOOTX64.EFI
}
```

Use GRUB's [filesystem UUID search](https://www.gnu.org/software/grub/manual/grub/grub.html#search),
not guessed `(hd0,gpt1)` numbering. Refuse ambiguous cloned identifiers in the
preflight; do not add signature bypasses. A reverse-route template would target
the verified shim using Limine's ESP partition-GUID path, with placeholders only.

## Bounded progression and validation

Task 1 is assigned after this plan merges; task 2 needs a separate go.

1. **Disposable Fedora/BLS rehearsal.** On a copy of the existing bootable
   [Fedora fixture](../development/experiments/nvme-coexistence-task3.md), verify
   the active GRUB hook, stage the pinned loader and Pyxis entry, then exercise
   Fedora normal/rescue and GRUB -> Limine -> Pyxis. Exercise a Coexist Update:
   GRUB's stable target must remain unchanged. Verify primary/fallback search,
   absence of config shadows and honest refusal for a missing loader or target.
   Compare foreign EFI files and Fedora's GRUB/BLS files before/after, accounting
   only for the explicitly added custom entry. Rehearse removing that entry.
   **After this task chronium can:** review a booted, reversible chainload route
   without touching either laptop's disk.

2. **Separately authorized native integration.** Obtain fresh read-only disk,
   gap, ESP, BLS and Secure Boot evidence; review GPT/ESP/Fedora configuration
   backups and a bootable recovery medium before consent. On the ThinkPad, any
   required shrink is its **own separately authorized, reviewed Linux-side step**:
   first review the fresh inventory and filesystem-specific feasibility, backup
   verification, exact operation and recovery plan. An agent may execute that
   approved step through passwordless sudo; sudo access itself is not approval.
   Never assume the filesystem supports shrinking. Verify Fedora boots after
   the shrink, before any Pyxis write. If safe space preparation is unavailable,
   stop; do not substitute formatting or destructive resizing.
   Honor the publisher and native-install gates. The confined Coexist executor
   consumes only already-unallocated space and never resizes anything. No shared
   ESP formatting or automatic firmware edits.
   Verify Fedora boot before and after, Pyxis Install/Update and exact preservation
   outside reviewed Pyxis additions and the custom GRUB entry. A GRUB menu hidden
   by Fedora policy may require its normal menu-reveal key; automatic visibility
   changes are outside this proposal.
   **After this task chronium can:** select Pyxis from Fedora GRUB and keep the
   original Fedora firmware boot path.

Rollback of the menu choice removes the Fedora-side custom entry and regenerates
GRUB only if that hook requires it. It does not require deleting partitions or
recursively removing `EFI/pyxis`; that directory also contains installed payloads
and publication evidence. Remove only specifically reviewed bootstrap files if
necessary. Native integration and the separate shrink step remain unassigned.
