# 0.0.10 package upgrades

Status: **tasks 1–3 and owned Coexist publication merged; signing floor in progress
(2026-10-26).**
This milestone changes the delivered [updater](pkg-upgrade.md). The signing-floor
implementation and qualification remain open; this plan does not claim a completed release.

## Accepted contract

1. **One active release and package set.** Remove the `Pyxis OS (previous)`
   boot entry, per-boot package-set bindings, retained predecessor sets and
   `pkg rollback`. `pkg install ID@VERSION` can reinstall an older package;
   it does not roll the operating system back. Keep the independent static
   rescue pair and the publisher's copies needed to diagnose or complete an
   interrupted publication. *(chronium, 2026-10-26)*
   The committed current record has no cleanup binding. At the next
   `pkg upgrade` or installer Update publication, the publisher removes
   unreferenced Pyxis-created payload attempts under `EFI/pyxis/`, after
   proving their canonical names, ownership records and payload bytes.
   It excludes the committed current attempt and the fixed static rescue
   pair. It never sweeps Fedora, Limine or `EFI/BOOT`; uncertain ownership
   refuses rather than guessing. This is publication-time work, not a boot
   scan or a manual cleanup procedure. *(chronium, 2026-10-26)*
2. **Whole-set resolution.** Resolve every installed package for the target
   release from its recorded repository origin. Existing entries without an
   origin need a reviewed source choice; a matching ID alone does not prove
   origin. Verify the complete proposed set, including dependencies and
   command/SDK projections, before staging. Missing compatible builds appear
   as explicit proposed removals. The user must approve each removal; `--yes`
   cannot approve one. An unavailable dependency or refusal leaves the active
   set and boot selection unchanged. *(chronium, 2026-10-26)*
3. **Stage, reboot, then switch.** `pkg upgrade` durably stages the entire
   compatible package set and the new release before activating the new boot
   payload. It ends with `Upgrade staged; type reboot to finish`, without
   receiving power authority or initiating a reboot. The running system continues
   to use the old `packages/current`. The verified first boot of the new
   release switches `packages/current` before package views or collectors
   start. Missing or ambiguous transition evidence enters core rescue before
   exposing a mixed release/set. Package mutations refuse while the
   transition is pending. There is no live upgrade or physical transaction
   spanning the packages volume and ESP. *(chronium, 2026-10-26)*
4. **0.0.9 migration.** Move from 0.0.9 with the 0.0.10 installer's Update
   path, which must prepare a compatible complete set before boot activation.
   Do not build a 0.0.9-compatible bridge `pkg`. Release notes must say that
   0.0.9 `pkg upgrade` cannot perform this transition. *(chronium,
   2026-10-26)*
5. **Owned Coexist boot.** A reviewed Coexist Install/Update publishes Pyxis's
   pinned Limine at `EFI/pyxis/BOOTX64.EFI`, normal and static rescue entries,
   and a Classic-style committed activation record. It registers one `Pyxis OS`
   UEFI boot option for that stable file after verified publication. The option
   is appended to `BootOrder` without changing the relative order of existing
   options. Installer review asks whether to move Pyxis first; the default is
   no. No Fedora GRUB chainload, manual entry, per-attempt selector, or foreign
   bootloader configuration write is part of the path. Ambiguous ownership or
   firmware registration refuses rather than guessing or blindly retrying.
   An existing unrecorded Coexist installation needs the installer's Update to
   gain the owned loader, option and record. Boot warns loudly when evidence is
   absent or invalid; `pkg` returns a specific refusal rather than obtaining
   ESP write authority. Conflicting evidence enters rescue.
   *(chronium, 2026-10-26)*
   Firmware variable access requires a typed UEFI grant. Any holder may request
   the grant's bounded, structured `Boot####`/`BootOrder` writes; the installer
   and other trusted tools receive it through normal grant configuration, and
   ordinary programs do not. Runtime firmware calls are serialized under a
   lock from any CPU, without a CPU 0 ownership requirement. A failed or
   uncertain registration is reported as incomplete and is not automatically
   retried. *(chronium, 2026-10-26)*
6. **Package updates and origins.** `pkg update` selects every installed
   package; `pkg update ID...` selects those IDs and required dependency changes,
   then validates the whole proposed set. An unchanged set reports `already up
   to date` without publishing a new set. The recorded origin binds the
   repository name, URL and signed/unsigned policy. A legacy entry offered by
   exactly one configured source adopts it. The whole-plan review shows one
   summary line, `N packages adopted from NAME (only source)`, per source;
   individual lines remain for actual package changes or ambiguous origins.
   Multiple possible sources need an explicit console choice, batched by
   source; `--yes` cannot choose an ambiguous
   origin. Proposed removals need individual approval. *(chronium, 2026-10-26)*
7. **Reviewed signed-index floor.** Freeze the latest published index for each
   stock signed repository from chronium's controlled signing tree. Compare
   `current` and exact index bytes with the published copy and verify the
   signature and signer under the pinned Software Signing CA before recording
   the snapshot in reviewed source. A network response alone never creates or
   advances the floor. The version-1 archive table is bounded per repository
   by name, canonical URL and signed policy, and holds the serial, signed UTC
   time and exact index SHA-256. A signed index must pass both this embedded
   floor and the packages volume's accepted-index record. Stock signed roots
   without an embedded floor refuse first contact. Ordinary development images
   carry the last checked-in snapshot without fetching; explicitly configured
   unsigned local roots still work. User-added signed roots not in the stock
   snapshot retain the current commit-time first-contact rule until a separate
   reviewed-floor import is designed. *(chronium, 2026-10-26)*

The pending transition is proof of the next selection, not a second active
selection or rollback binding. An old set may occupy space until the first
verified new boot resolves the transition, but is not retained as a fallback
afterward. A failed new boot can require rescue, installer Update or reinstall.

## Work

- [x] **Task 1 — retire rollback interfaces:** remove the previous boot role,
  boot-record binding and old payload retention; remove `pkg rollback`, the
  `.previous` set sidecar and predecessor-based collection. Keep publication
  recovery copies and the static rescue pair. Include the bounded,
  ownership-proven publication-time orphan sweep above. Update callers and
  reference docs together. Assigned to Daybreak.
- [x] Record origins and resolve a complete target set, including explicit
  review of legacy origins and removals.
- [x] Add durable pending transition evidence and the verified first-boot
  `packages/current` switch before installed consumers start. Qualify
  interruption boundaries and core-rescue refusal.
- [x] Integrate the set planner with `pkg upgrade` and 0.0.10 installer Update;
  qualify the actual 0.0.9 migration and the post-stage instruction.
- [x] Add kernel UEFI runtime support and the typed, boot-option-scoped grant;
  qualify bounded firmware variable operations in OVMF.
- [x] Publish the owned Coexist Limine/config/record, register its firmware
  option after verification, and add boot/`pkg` diagnostics. Qualify Install,
  unrecorded Update and representative refusal boundaries on disposable OVMF
  images. Native firmware and power-cut qualification remain open.
- [ ] Settle the signing-floor source and qualify first repository contact.

## Task 3 qualification

At pyxis-os `a482367f` with pyxis-userland `68b8688b`, the full core image
built and 4-vCPU QEMU ran on disposable 3 GiB disks. A released 0.0.9 image
installed 38 packages; the migration fixture reduced its active set to one
`shell` package and used a local repository built from the exact task-3 image.
Classic Update displayed `1 package adopted from public (only source)`, staged
set 3 while `current` remained set 2, and verified the new ESP attempt. The
installed disk-only boot logged `verified package set 3 for this boot` and
started the installed space; afterward `current` and `.boot-selection` named
set 3, and `.upgrade-pending` was gone. An earlier refused Update had changed
configuration without publishing boot selection, so this run also checked a
retry from that no-publication state. The test covers one legacy-origin
package at the target revision, not a 38-package target download.

A second disposable disk had a foreign boot file and Linux partition. Coexist
Install added a Pyxis pool, but selecting Coexist Update returned to the
installer menu with the explicit refusal. The whole-disk SHA-256 was identical
before and after the refused Update, and the foreign boot file was unchanged.
On a scratch clone of the installed Classic disk, corrupting the retained
boot-selection proof made boot-init enter core rescue before starting the
installed space. These checks establish the refusal and negative proof path;
they do not simulate power loss at every publication write.

At the task-3 revision, the in-system `pkg upgrade` staging path and its final instruction built at
this revision but were not run against a signed release repository: the
signing-floor source is still an open decision. The 0.0.10 release notes must tell 0.0.9 users to use installer
Update, because the 0.0.9 `pkg upgrade` client cannot stage this transition.

## Owned Coexist qualification

At pyxis-os `223556b2` with pyxis-userland `b6fbf16`, a 4-vCPU KVM/OVMF
QEMU run on a disposable 4 GiB GPT disk installed beside a 512 MiB FAT32 ESP
containing `EFI/fedora/untouched.txt`. Review offered the BootOrder choice
(default: retain foreign order); Apply created pool partition 2, published
`EFI/pyxis/BOOTX64.EFI`, the normal/rescue menu and committed record, then
read back its `Pyxis OS` firmware option. A second run updated that installation,
staged a complete two-package set from a local synthetic repository and verified
the record, menu and firmware entry. Disk-only OVMF boot loaded that option and
Limine; boot-init logged `verified package set 2 for this boot` before starting
the installed space. Both extracted ESPs passed `fsck.fat -n`, and the Fedora
file remained unchanged.

For the unrecorded-Update route, a disposable clone had the owned loader,
record and menu removed while retaining the pool and payloads. A prior
publication recovery copy had to be removed as well: without its record that
copy is ambiguous, and Review correctly refused before writing. The resulting
unrecorded clone accepted Update, staged set 3 and verified the newly published
record, menu and firmware entry; `fsck.fat -n` passed and the Fedora file was
unchanged. A separate clone with a changed `boot/limine/limine.conf` was refused
at discovery; its whole-disk SHA-256 was identical before and after. This is a
synthetic migration fixture, not a released 0.0.9 image.
OVMF does not qualify native UEFI variable behavior, firmware that ignores
flushes, or every interrupted write boundary. The move-first BootOrder choice
was inspected in code but was not exercised in QEMU.

## Open decisions for later rounds

- The reviewed floor proves the published state at freeze time, not that no
  later index exists. A release must stop if the controlled tree, published
  copy or signature disagree. A signed release tag and signed ISO bind the
  checked-in table to the delivered core archive; CI needs no signing key.
- **Reboot initiation:** keep a separate design open; this task gives `pkg` no
  power grant and no mediated restart action.

## Follow-ups after 0.0.10

- Extend the typed `uefi` grant with bounded, structured operations to list
  boot entries, read and update `BootOrder`, and set `BootNext`. Add a small
  boot-entry tool so an authorized Remote session can request a one-time boot
  of an existing entry, such as Fedora on vela. Scope, readback and failure
  behavior need their own authority decision; this milestone does not implement
  the operations or grant them to ordinary programs. *(chronium idea,
  2026-10-26)*

## You should know

`packages/current` is a live view: changing it during the old boot would show
new-ABI packages to old-release processes. Task 3 therefore switches it only
after verified first-boot selection. ESP and npfs
cannot commit atomically; the gate must refuse uncertain evidence rather than
show a mixed system. Staging can temporarily occupy space for both sets. The
standalone rescue pair is a recovery tool, not a previous-version selection.
An interrupted payload cleanup may leave an empty unmarked attempt directory;
it contains no boot bytes and the next publication leaves the ambiguous empty
name untouched. A nonempty attempt without valid ownership proof refuses the
cleanup without guessing. The sweep runs after confirmed activation, so its
failure is reported as incomplete cleanup of an already selected release.
