# `pkg upgrade`

Status: **direction and the [decisions](#decisions) accepted (owner,
2026-10-20; chronium, 2026-10-21); in progress, see [tasks](#tasks).** The rest
of this document remains a proposal, refined as each task starts.
The direction, the single shared publisher, the trust rules and the schedule are
owner-accepted in the
[upgrader section](packages.md#upgrader-pkg-upgrade-accepted-direction-owner-2026-10-20)
of the packages plan and in
[0.1.0 entry 2](release-ideas.md#2-pkg-upgrade-update-the-running-installed-system-in-place).

## Dependencies

The installer's publisher (userland
[#253](https://git.internal/PyxisOS/pyxis-userland/pulls/253), pyxis-os
[#797](https://git.internal/PyxisOS/pyxis-os/pulls/797)) is merged. This
proposal uses only the parts of its contract in `installer/PUBLICATION.md` and
`installer/ESP-PAYLOADS.md` listed below.

| Publisher contract used | Source in #253 |
| --- | --- |
| Payloads staged into a fresh `EFI/pyxis/REVISION/ATTEMPT/` (`kernel.elf`, `boot.arc`, `revision.txt`, `ownership.record`) from reviewed sources and SHA-256 values, then reopened and compared by retained object identity and full hash | payload stager |
| Capacity admission with no credit for later cleanup; staging parents opened by canonical FAT name | payload stager |
| One non-extending 30-second interval: CHECK, recheck of frozen inputs and fresh payload identity, boot-bound readiness record (prepared, then committed), owned Limine spans switched last | publication |
| Current and one known previous attempt retained, plus the pinned `EFI/pyxis/recovery/` pair; older attempts removed only on confirmed activation and exact owned-manifest evidence | publication |
| Failure, deadline or unknown completion stops with artifacts retained; **staged, boot activation pending** is a distinct state with an explicit completion Review | publication |
| Loaded-byte evidence from the trusted early reader: the kernel and archive actually loaded must match the committed readiness before the ESP selection is used | installed configuration selection |

Three things the documented contract does not yet provide, and `pkg upgrade`
needs:

1. **A reusable library.** The publisher lived inside the installer program and
   planned from a selected disk. `pkg upgrade` needs the stage, interval and
   retention parts as a library with a different front end: sources are package
   files, the target is the booted ESP, and there is no program stage. Task T1
   extracts it so that Install, Update and `pkg upgrade` keep one publisher.
2. **A previous-entry menu item.** The published menu has exactly two owned
   entries, normal and rescue. The previous attempt's files are kept, but no
   entry boots them. See [decision 1](#decisions).
3. **Program packages instead of `bin/REVISION`.** An upgrade that replaces the
   kernel and archive but leaves programs in `bin/REVISION` cannot switch the
   programs at the same reboot. `pkg upgrade` therefore lands after
   [stage 2](packages.md#staged-plan) has retired `bin/REVISION`.

## The release package

A release is an ordinary package of kind `release`, named `pyxis-system`. It is
fetched, verified and extracted into the store exactly like any other package
([verification order](../interfaces/packages.md#verification-order)).
[Format 2](../interfaces/packages.md), introduced by stage 2 with the kind
`program`, gains the kind `release`.

```lua
return {
  format = 2,
  id = "pyxis-system",
  version = "0.0.8-1",
  kind = "release",
  depends = {
    { id = "base-tools", version = "0.0.8-1", sha256 = "<archive sha256>" },
  },
  files = 2,
  bytes = <total of both files>,
  inventory_sha256 = "<sha256 of package.files>",
  release = {
    revision = "<kernel revision string>",
    kernel = "boot/caelum.elf",
    archive = "boot/initrd.cpio",
  },
}
```

- **Tree:** `tree/boot/caelum.elf` and `tree/boot/initrd.cpio`. The manifest names
  them; nothing is found by scanning. `revision.txt` is written by `pkg` from
  `release.revision`, not shipped.
- **Package set:** the release's `depends` is the release-tied set, resolved and
  conflict-checked by the existing resolver. For a `release` package each
  dependency also carries the SHA-256 of its archive, which must equal the index
  entry of the repository that supplies it. One signature over the release
  manifest then covers the kernel, the archive and every pinned program package
  through `inventory_sha256` and these hashes.
- **Program packages** carry `abi`, the
  [package ABI identifier](packages.md#the-abi-identifier) shared with stage 2
  (accepted, chronium, 2026-10-21). `pkg` refuses to put a program package into
  a set whose release does not provide that ABI, as the packages plan requires
  for the game packages. Set records keep `release`.
- **Archive source:** today a CI-built cpio shipped as a file. With
  [0.1.0 entry 5](release-ideas.md#5-the-image-is-assembled-from-packages) the
  archive is generated from the package set by the one deterministic generator
  and checked against an `archive_sha256` in the release manifest. The publisher
  takes a kernel source, an archive source and their SHA-256 values, and an
  unlinked private RAM FILE already serves as a source, so this step changes only
  where the archive bytes come from; the rest of this document does not change.
- **Checks before staging:** the same ELF and archive checks the installer applies
  to its sources, including the rescue-archive identity check. Staged payloads
  stay within the stager's 1 GiB bound.
- **Choosing a version:** versions are opaque. `pkg upgrade` takes the last
  `pyxis-system` version in the chosen repository's index, or an explicit
  `pkg upgrade pyxis-system@VERSION`. If its `revision` and hashes equal the
  booted release, it reports that the system is current and writes nothing.
- **Recovery pair:** the release never carries or replaces the recovery pair or
  rescue archive ([entry 5, decision 2](release-ideas.md#5-the-image-is-assembled-from-packages)).
  `pkg upgrade` reverifies the pair and refuses if it is absent or fails, with
  the reason. Installs made before the pair existed need one Update from live
  media first.

## The booted ESP

Upgrade targets "the install that booted", never a chosen disk. The proof is the
agreement of three independent facts, each already produced for installed boot,
and any gap refuses with the missing fact named:

1. **Provenance.** The kernel's boot-provenance issuer names exactly one healthy
   GPT ESP by normalized Limine disk and partition UUIDs and media kind
   ([FAT32 mounts](../devices/fat32-mounts.md#trusted-browsing-profiles)). Live
   optical or PXE boots have none, so `pkg upgrade` refuses on live media.
2. **The running pool.** The `mount.disk=` disk GUID of the booted pool equals
   the ESP's disk GUID, so an ESP of another disk is never written even if one is
   attached.
3. **Loaded bytes.** The committed readiness record on that ESP names the kernel
   and archive attempt that the trusted early reader measured at boot. Init
   already freezes this evidence for configuration selection; it keeps a
   read-only snapshot for the space that holds the upgrade grant. A rescue boot,
   missing, prepared, malformed or mismatching evidence refuses. UUIDs name a
   logical target, not physical hardware.

The upgrade then runs the publisher's own pending-state check: an unresolved
prepared or malformed record refuses, and a committed record whose menu switch
never completed is offered for completion, not overwritten.

## The ESP-write grant

Today no shipped profile writes the running system's ESP. `boot-esp` is a
read-only view fixed at admission, and a later editor never upgrades a read-only
instance ([init](../userland/init.md#fat32-volumes-and-selected-grants)).
Proposed, as a kernel and ABI change:

- A new issuer right on the boot-provenance issuer, `UPGRADE_BOOT_ESP`, held by
  trusted init and delegated to a space only through a space gate
  `package_upgrade = true`, the same shape as `package_management`. Only the `pkg`
  bundle declares the resulting `boot_esp_upgrade` grant, beside its existing
  `package_management` grants, and ordinary bundles
  receive it only through a declared grant. As with `package_management`, the
  holder is trusted: the executable name does not constrain a delegated grant.
- `OBSERVE` returns the bound identity facts above, or refuses.
- `ADMIT_RW` takes the facts `pkg` reviewed. It succeeds only if they still match
  the provenance, the partition is the GPT ESP and no other instance of it is
  live, which means init's read-only boot reader has been drained and retired.
  It returns one native FAT32 root, confined like `DISKS_OPEN_OWNED_ESP` to
  `EFI/pyxis`, `boot/limine` and `boot/limine.conf` ([decision 5](#decisions)),
  and its bound control: root
  `LOOKUP|ENUMERATE|READ_FILES|CREATE|WRITE_FILES|REMOVE` and control
  `OPEN_ROOT|OBSERVE|WRITE` with the publisher's interval operations. `REMOVE`
  exists only for retention of older owned attempts.
- No raw disk, GPT write, claim, format or other partition is reachable. Closing
  and retiring the view ends the grant, and it cannot be reclaimed in the same
  boot after a failed phase without a fresh `ADMIT_RW`.
- This is the same pattern as the installer's reviewed `REVIEW_ESP` and
  `BEGIN_ESP_REBUILD`, minus its raw writer. `pkg` receives no `DISKS` or
  `MOUNT_READ_WRITE`.
- The typed confirmation word of the installer's grants is not checked by the
  kernel here, because the accepted convention is `pkg`'s own console `[y/N]`
  review.

## Confirmation flow

`pkg upgrade` follows the install conventions: archive sizes before fetches,
fetch, whole-archive verification and extraction per package, then a review, then
a `[y/N]` prompt where only `y` or `Y` accepts. There is no non-interactive flag.

```text
Upgrade pyxis-system 0.0.8-1 (revision 1a2b3c4) from horse/public
  kernel and boot archive staged under EFI/pyxis/1a2b3c4/ on the booted ESP
  package set N+1: base-tools 0.0.8-1 (was 0.0.7-1), doom 0.0.8-1 (was 0.0.7-1) ...
  kept: fonts 1.0.0-1 (not release-tied)
  trust: horse/public is unsigned (trusted by configuration)
  menu: this entry becomes "previous"; rescue and recovery are unchanged
Proceed? [y/N]
```

- Declining, an empty answer or Ctrl+C leaves the ESP, `current` and the set
  records untouched. Prepared store trees stay inactive, as for any install.
- The review is the only prompt. A rejected trust verdict, a missing recovery
  pair or a failed identity check refuses before it.
- After the publisher confirms the activation the last line is `upgrade staged:
  reboot to run revision 1a2b3c4`. `pkg` never reports a blanket success and never
  restarts the machine. If the publisher reports **staged, boot activation
  pending**, `pkg` prints that and how to complete it, and exits nonzero.
- Release-tied packages without a build for the new release (optional programs
  such as a game package) are listed in the review as removals, or upgraded to
  the same-ID build for the new release from the repository recorded with the
  installed package. A package whose repository has no such build is a removal the
  user must accept; declining cancels the upgrade. See
  [decision 3](#decisions).

## Staging the package set for the same reboot

The set must not change under running programs, and a new-release program must
not run on the old kernel. So the upgrade writes an inert set and the new boot
activates it.

Writes, in order:

1. Fetch, verify and extract the release and the release-tied packages into the
   store. Nothing is visible yet.
2. Write the immutable set record `sets/N.lua` and its `N.commands` projection.
   The record names `release = "REVISION"` and its predecessor, and the existing
   current set stays active.
3. Stage and verify the kernel and archive on the ESP through the publisher.
4. Write and sync the marker `next`:
   `{ set = N, release = "REVISION", attempt = "<32 hex>", kernel_sha256 = ..., archive_sha256 = ... }`.
5. Run the publisher interval: readiness record, then the menu spans last.

At boot, trusted init, which already collects garbage on the volume before
providers start, selects the active set from the booted release:

- If `next` exists and the booted revision, and the loaded kernel and archive
  hashes from the early reader, equal it, init promotes: it writes `current.new`,
  renames it over `current`, syncs, and removes `next`. The previous set stays as
  predecessor.
- Else it uses `current` if its set's release equals the booted revision. If not,
  it walks the predecessor link of the retained sets for one that does. This is
  how the **previous entry** boots the previous programs. Selection by walking
  writes nothing.
- If no retained set matches the booted revision, init starts only the archive's
  rescue programs and prints which release it expected. It never starts programs
  built for another release.
- A stale `next` that matches no boot is ignored and replaced by the next upgrade.
  Boot collection also retains the set and store trees that `next` names.

While the booted release differs from the `current` set's release (a rollback
boot), `pkg` mutations refuse with an explanation. `pkg rollback` stays
available: it publishes the effective predecessor as the new `current` and
thereby abandons the newer release deliberately.

Running programs never see their package set change, because the switch happens
only at boot. No running-program lifetime contract is needed.

## Rollback

Booting the previous entry rolls back the kernel, archive and package set at once
through the selection above. It needs the previous entry in the menu
([decision 1](#decisions)). Making that rollback permanent is `pkg rollback`
from the previous boot. A later upgrade makes the booted release the new
predecessor, and the publisher's retention removes the attempt before it, after
the new entry is confirmed and its ownership evidence matches. The store trees
older than the retained two sets are collected by boot maintenance, as today.

## Trust

`verify_release_signature()` exists from the first version:

```c
enum release_trust { RELEASE_VERIFIED, RELEASE_REFUSED, RELEASE_NOT_VERIFIED };
enum release_trust verify_release_signature(const struct release_evidence *release,
    const struct repository_trust *repository);
```

`release_evidence` holds the verified `package.lua` bytes and their SHA-256, the
repository identity and the signature bytes if the repository supplied any. There
is no verifier yet, so it returns `RELEASE_NOT_VERIFIED`. The signature format,
where the repository stores it and key distribution belong to the later verifier,
which gets its own proposal. Only the covered bytes are fixed here: the release
manifest, which binds everything else.

Per-repository trust is configuration. The repository list in
`system://config/packages.lua` changes from URL strings to tables
`{ url = "...", trust = "unsigned" }`, where `trust` is absent for a repository that
must be verified. `pkg repository add URL --unsigned` records the mark and plain
`add` does not. The mark affects `pkg upgrade` only in this proposal; ordinary
package installs keep integrity checks only until the signing work in the
[public mirror prerequisites](packages.md#later-ideas).

| Verdict | Repository marked unsigned | Repository not marked |
| --- | --- | --- |
| VERIFIED | proceeds; review prints the signer | proceeds; review prints the signer |
| NOT_VERIFIED | proceeds; review prints `upgrade: REPO is unsigned (trusted by configuration)` | refuses before the review |
| REFUSED | refuses | refuses |

REFUSED is never overridden by configuration: a repository marked unsigned that
presents a signature that fails is rejected. The verdict is computed after the
release package is fully verified and before the review. The publication boundary
rechecks that the manifest hash equals the evaluated one. Signed releases remain
required before any non-LAN repository is used.

## Failures and interruption

The publisher's rules apply unchanged: artifacts are retained, nothing is retried,
repaired or rolled back automatically, and no cross-file atomicity or power-loss
durability is claimed.

| Point reached | State left | Recovery |
| --- | --- | --- |
| Before the review, or declined | Prepared store trees inactive; nothing on the ESP | Rerun; `pkg unlock` for an abandoned lock |
| Fetch, verification or extraction failure | Active set and ESP unchanged | Rerun |
| Set record written, ESP stage incomplete | Inert set record; a partial attempt retained and named | The old entry is unchanged. A fresh attempt follows; the partial one has no ownership credit and stays until removed with exact evidence by hand |
| Staged and verified, `next` written, interval not completed | Old entry still the only active one; `next` ignored by a different boot | Rerun |
| Interval cut, readiness committed, menu old or partly switched | **staged, boot activation pending** | `pkg upgrade` offers completion with the same attempt, after a fresh review |
| New entry confirmed, cleanup incomplete | Upgrade committed; older attempts retained | Reported; removed by exact owned-manifest evidence at the next upgrade |
| New kernel boots, promotion fails (volume error) | `current` is still the old release | Init starts only rescue programs and reports; boot the previous entry or `pkg rollback` |
| Power loss anywhere | As for the row reached | The pinned recovery pair and rescue entry are the manual recovery |

## QEMU qualification

Ordinary builds and interactive QEMU boots, with no new test or automation. The
configuration (CPU count, devices, accelerator, disk images) is recorded with each
result, and nested-VM figures are distinguished from the owner's host.

1. An installed image on a virtio disk with a loopback repository served from
   the host holds a release and its package set. Upgrade, reboot into the new
   entry, check the promoted set, then boot the previous entry and check kernel,
   archive and programs together, then `pkg rollback`.
2. Refusals: live media, a rescue boot, an ESP of another attached disk, a
   mismatching readiness record, a missing recovery pair, a declined review and a
   non-unsigned repository with NOT_VERIFIED. The unsigned repository prints its
   visible line. REFUSED has no reachable verifier, so it stays unqualified until
   one exists.
3. Failures: a corrupted archive, a refused capacity on a small ESP, and a seeded
   foreign file under the ESP compared byte for byte afterwards.
4. Cuts: the QEMU process stopped by its recorded PID at each row of the failure
   table that can be reached, then the next boot and rerun observed.
5. Phase costs for fetch, verification, extraction, staging and publication, and
   the installer's Update on the same image as a matched baseline, repeated, with
   the variation recorded. Stage sizes use the real archive.

Native qualification repeats the sequence on a spare USB target first, with the
owner present, and never on the Fedora coexistence ESP without the owner's
separate go. The installer's Update tab retires only after this passes.

## Tasks

Each task is one focused PR with QEMU checks. T1 to T3 do not depend on program
packages; T4 starts after stage 2's format task, T5 after program packages.

- [x] **T1. Shared publisher library.** `installer/boot_publisher.h` and
  `libpublisher.a`: prepare, admission recheck and publication from borrowed views,
  reviewed target facts and sources. The installer links it; behaviour unchanged.
- [ ] **T2. Previous menu entry** ([decision 1](#decisions)). The planner writes
  `Pyxis OS (previous)` for `previous_boot` once its `ownership.record` verifies;
  the record gains the binding that pending completion needs ([decision 6](#decisions)).
  boot-init accepts loaded bytes matching `previous_boot` ([decision 4](#decisions)).
- [ ] **T3. ESP-write authority** ([decisions 2 and 5](#decisions)). The kernel
  right, `OBSERVE` with the pool's `mount.disk=` GUID, `ADMIT_RW`, the
  `package_upgrade` gate and boot-init's read-only evidence snapshot (loaded kernel
  and archive sizes and SHA-256, committed record bytes). Security review by Daybreak.
- [ ] **T4. `pkg upgrade` ESP switch.** The `release` kind, the booted-ESP
  refusals, `verify_release_signature()` and the trust table, the `[y/N]` review,
  and publication through T1 under T3's grant.
- [ ] **T5. Set staging and promotion.** `next`, init promotion, rollback-boot
  selection and `pkg rollback`.

## Decisions

Accepted by the owner, 2026-10-20, all as proposed:

1. **Previous entry in the menu.** The shared publisher adds a third owned entry,
   `Pyxis OS (previous)`, for the retained previous attempt, written and removed
   with the same ownership rules as normal and rescue, for Install, Update and
   `pkg upgrade`. It is what makes "boot the previous entry" a rollback.
2. **ESP-write authority.** A new one-use `UPGRADE_BOOT_ESP` issuer right on the
   boot-provenance issuer, bound to the reviewed identity facts, with no raw disk
   reach. Rejected: delegating `DISKS` and `MOUNT_READ_WRITE` to `pkg`, which gives
   a package manager the whole-disk inventory.
3. **Release-tied packages the release does not list.** Each is upgraded to the
   same-ID build for the new release from the repository recorded at install. A
   package with no such build is listed as a removal that needs explicit
   acceptance, and declining cancels the upgrade.

Accepted by chronium, 2026-10-21:

4. **Configuration on a previous-entry boot.** boot-init uses the committed ESP
   set when the loaded kernel and archive match `previous_boot` and the previous
   archive's trusted catalog equals the selected catalog. Otherwise it uses the
   archive defaults and prints why. Rejected: archive defaults always, which loses
   session and network settings on rollback.
5. **Scope of the RW grant.** `ADMIT_RW` returns the kernel's existing owned-ESP
   confinement, as `DISKS_OPEN_OWNED_ESP` does: `EFI/pyxis`, `boot/limine` and
   `boot/limine.conf`. `pkg` never reaches `EFI/BOOT` or other loaders' files.
   Rejected: a whole-ESP root.
6. **The readiness record may evolve.** `config.record` is not a frozen
   cross-release format; T2 adds the binding pending completion needs. A boot-init
   that cannot read a newer record, such as an older release booted from the
   previous entry, falls back to the archive defaults and prints why. Rejected:
   freezing the format and recognizing the outgoing previous span from that
   attempt's `ownership.record`.
