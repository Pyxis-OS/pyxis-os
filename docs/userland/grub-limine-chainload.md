# Fedora GRUB and Pyxis Limine chainloading

This is the earlier manually configured ThinkPad route. Current Coexist
Install/Update publishes Pyxis's owned Limine and UEFI boot option as described
in [package upgrades](package-upgrades.md); it does not need Fedora GRUB.

Fedora GRUB can EFI-chainload Pyxis through the pinned Limine executable on a
shared ESP. The route is accepted (chronium, 2026-10-21) and qualified on the
ThinkPad T14 Gen 1 AMD: Coexist Install, installed normal/rescue boot, same-source
Update and Fedora normal/rescue returns. See the
[native record](../development/experiments/grub-limine-native.md) and the earlier
[QEMU rehearsal](../development/experiments/grub-limine-chainload.md).

```text
existing Fedora firmware entry -> shim -> GRUB/BLS -> Pyxis Limine -> Caelum
```

## Loader and configuration

The stable GRUB target is `EFI/pyxis/BOOTX64.EFI`, initially the repository's
[pinned Limine 12.9.3](../../third_party/limine/UPSTREAM.md). A separately approved
Fedora-side bootstrap supplies it; Coexist Install/Update do not install that
executable. Updating the loader needs its own qualification.

On the qualified ThinkPad, Fedora's `41_custom` hook sources
`/boot/grub2/custom.cfg` from its ext4 `/boot`. The added entry uses the selected
ESP's verified, unique filesystem UUID (not its GPT partition GUID):

```text
menuentry 'Pyxis (Limine)' {
    insmod part_gpt
    insmod fat
    search --no-floppy --fs-uuid --set=pyxis_esp <ESP_FILESYSTEM_UUID>
    chainloader ($pyxis_esp)/EFI/pyxis/BOOTX64.EFI
}
```

Fedora's generated GRUB configuration, BLS entries and EFI files remain owned by
Fedora. Its persistent saved default, existing firmware entries and BootOrder
stay unchanged. Approved one-shot selection uses the existing Fedora firmware
entry and `grub2-reboot`; it does not create a new firmware entry. Other machines
need fresh inspection of their active GRUB hook and layout before writes.

Limine's primary is `/boot/limine/limine.conf` **on the ESP**, separate from
Fedora's ext4 `/boot`. The independent rescue fallback is ESP `/boot/limine.conf`.
Keep `EFI/pyxis/limine.conf` and `EFI/BOOT/limine.conf` absent to avoid
configuration shadowing. The fallback covers an absent primary, not a present
primary that names missing files. The bootstrap rescue pair has separate retained
kernel/archive files rather than depending on the active installed attempt.

## Install, Update and authority

[Coexist Install](installer.md) consumes only an already-unallocated reviewed
gap; it never shrinks partitions or formats the shared ESP. GPT append changes
only the selected unused slot and required CRCs. Pool access is partition-bound,
and ESP files use the delegated native FAT view. Foreign partitions, including
Fedora or Windows partitions, do not grant whole-disk write authority.

Install and Update stage immutable `EFI/pyxis/<revision>/<attempt>/` payloads
and print/save their boot entries. Activation in this route is a separately
reviewed Fedora-side replacement of only the Pyxis primary after independent
kernel/archive and entry verification. Update keeps the GRUB target and EFI
executable stable; earlier attempts and the independent rescue pair are retained.
The [owned publisher](../wip/installer-tui.md) has not qualified automatic Coexist
configuration/menu activation; manual activation is not that contract.

The accepted [Coexist install without a foreign OS direction](../wip/release-ideas.md#10-coexist-install-without-a-foreign-os)
(chronium, 2026-10-21) moves loader provisioning, ESP bootstrap configuration,
boot-entry creation and activation, including `nvme.write=1`, into Pyxis, its
installer or the shared publisher. A firmware boot entry is favoured; the route
is not decided and this direction is not implemented.

Installed normal boot in the native qualification explicitly enables
`nvme.write=1` for the selected pool. Rescue keeps writes disabled and uses
`boot.default_config=1`. The first read-only bootstrap mounts no pool: selecting
an as-yet-uncreated pool would enter local-only recovery rather than the live
Remote profile. A power grant authorizes power requests only, not disk, ESP or
firmware mutation.

## Qualification and limits

The ThinkPad round used the entire pre-existing aligned gap after the separately
completed Fedora shrink. It preserved the reviewed foreign GPT entries and
compared pre-existing ESP and Fedora `/boot` files. Fedora may create/update its
bounded regular `loader/random-seed` on the ESP and update consumed GRUB one-shot
and boot-accounting fields; these are recorded Fedora-owned changes, not blanket
exceptions for its EFI tree or persistent boot defaults.

The clean qualification archive includes a private installed Remote profile with
an explicit power grant. It is not the shipped installed default; the immutable
attempts retain that profile until a later clean-source Update replaces them.
Raw identifiers, GPT/ESP diagnostic copies and helper logs stay private.

The native power-off request ended with an unknown transport outcome, while
chronium visually confirmed the ThinkPad was off. Remote EOF alone proves
neither failure nor physical power-off; the
[helper observation limit](https://git.internal/PyxisOS/pyxis-os/issues/1322)
remains. The reported copy rate is a single UI observation, not a matched NVMe
throughput benchmark.

Vela, Secure Boot, firmware-update interactions, interrupted writes and automatic
Coexist publication are not qualified by this round. Native work on another
target still needs its own approval. Limine becoming Fedora's front door remains
[later work](../wip/nvme-coexistence.md#limine-as-fedoras-boot-manager).
The Fedora-side steps above must be removed under
[release-ideas entry 10](../wip/release-ideas.md#10-coexist-install-without-a-foreign-os);
this qualification closes the manual route, not that accepted follow-up.
Removing the custom GRUB choice does not require deleting the pool or recursively
removing `EFI/pyxis`; cleanup and recovery need their own reviewed scope.
