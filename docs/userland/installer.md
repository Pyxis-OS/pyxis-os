# Native installation

Build install media with `make -j16 image`. On that core image, **Install
Pyxis** is the first and default Limine entry. The stock entry starts the interactive planner. Trusted launchers may pass
`--plan`, `--dry-run` or `--apply` with explicit consent as described below.
It runs through trusted install init with the explicit
[disk and boot-file grants](../devices/installer-authority.md). Source files are
already in RAM; installing does not require a driver for the live medium.

Install copies the core archive and fetches every other program as the `base`
package set ([decisions 63 and 67](../wip/pkg-upgrade.md)), from the repository
the image names (`PACKAGE_REPOSITORY`, see [boot archive](../development/boot-archive.md)).
The install space starts the network with the archive's `config/network.lua`:
wired or dock Ethernet by DHCP. There is no Wi-Fi Install in 0.0.8; the Wi-Fi
join comes with the curses TUI installer ([decision 70](../wip/pkg-upgrade.md#decided)). Before any
target is chosen Install runs `pkg check base`; without network, or without
`base` for this revision, it refuses, writes nothing and returns to the action
list, where Update remains. A Replay of an Install plan fetches `base` too and
needs the network; Update or its Replay also needs network when the compatible
target package set must be fetched. After
the pool stage it runs `pkg --yes install base` into the new `packages` volume
and publishes the boot entries only once that set is active. A failure there
leaves a partial disk that does not boot; run Install again. Update prepares
the target release's complete package set before boot activation, preserving
the active set on a refusal; see [package upgrades](package-upgrades.md).

The separate [read-only FAT32 inspector](../devices/fat32-inspection.md) lists
and reads a selected GPT volume without invoking Install/Update. Strict inspector
refusal never authorizes an ESP rebuild.

First choose **Install**, **Update**, **Replay a printed plan** or **Cancel**. Only Install then asks for
**Coexist**, **Classic** or **Cancel**. Coexist preserves existing partitions,
appends a pool in a reviewed gap and publishes an owned Limine menu and UEFI
boot option; see [NVMe coexistence](../wip/nvme-coexistence.md). Classic provides
the whole-disk Install and existing ESP-rebuild recovery below. Update discovers
recognized installations read-only and infers their layout; it never asks for a
layout. The selected disk GUID and ESP/pool entries are rechecked by the child,
which receives the existing inferred layout's grants. Changed or ambiguous
targets refuse rather than selecting another installation. Discovery shows refusal
reasons, including a committed journal that needs one installed boot for recovery.
Back or a failed discovery returns to the action list. Replay accepts one framed
plan and acquires only the independently reviewed layout authority it needs.

Choices are highlighted numbered lists: Up/Down or the displayed number, then
Enter. Enter alone accepts the highlighted default; invalid choices re-prompt.
Prefilled value fields start selected: typing replaces the value, while navigation
allows editing it. Typed words remain for destructive confirmations (`wipe`,
`append`, `update`, `complete` and the explicit scratch publisher’s `confirm`); a mismatch re-prompts and Ctrl+C cancels. Native input loss
or I/O failure is reported, rather than retried as a user mistake.

Normal builds enable owned ESP publication for Classic and Coexist. The older
ThinkPad manual chainload route was [natively qualified](../development/experiments/grub-limine-native.md),
but the owned Coexist route has only disposable OVMF qualification. It leaves
foreign bootloader files and configuration untouched. Other native write scopes
still need separate approval.

Install/Update refuses on battery below 40% or unreadable reported battery state.
AC permits any charge, and a machine with no battery may proceed. The same gate
is rechecked before the first write; there is no bypass.

Coexistence requires 512-byte logical sectors; a 4096-byte-sector disk refuses
at selection and in the plan record, while Classic supports both sizes.
Coexistence Review requires a supported clean FAT32 ESP. FAT12/FAT16 and
unknown capacity refuse; supported small FAT32 ESPs are assessed by checked free
clusters against the fresh attempt's payloads, record and directory allowance.
Insufficient space refuses before GPT append or pool writes. Previous attempts
give no credit; proven unreferenced Pyxis attempts are swept only after confirmed
publication. The observation is
revalidated before effects, not reserved against all later exhaustion.

Choose **Install**, **Classic**, then a consent path
([decision 76](../wip/pkg-upgrade.md#decided)):
- **Blank or marked disks**, the default, admits a blank disk (no partition
  table), or an npfs disk whose every live volume has a regular `SAFE_TO_WIPE`.
- **Any disk (may erase data)** also admits a foreign or unrecognized GPT, GPT
  copies that disagree, and a damaged, empty or partially marked pool.

The default path refuses those cases and says to choose **Any disk**. A
readable pool with no markers at all is refused on both paths. Both paths end in
the same typed consent word. The
installer waits for sealed boot discovery, lists retained VirtIO and USB disk
candidates and explains eligibility. Partial USB coverage can hide additional
disks; it does not prevent listing the observed candidates. Lost registry records
or incomplete VirtIO bookkeeping refuse inventory. It selects a sole eligible
disk or asks for its displayed number when several qualify, then shows size,
GUID and escaped labels of the volumes that will be destroyed.
A zeroed first sector with neither GPT header present is listed as blank;
this describes partition metadata, not a scan of every disk byte.

Normal installation requires a validated protective GPT, at least one recognized
npfs pool, a nonempty set of live volumes in every such pool and a regular root
`SAFE_TO_WIPE` in every live volume. Consent inspection reads raw metadata with
validated COMMITTED journal images overlaid in memory; it never persists replay.
Any individually readable nonempty pool with no regular markers vetoes the
entire disk in both modes. A marked pool cannot override another final pool.
Mounted/retained pools, raw claims and unsupported, failed, read-only or
flush-incapable devices are excluded. USB targets must have known write-protection-clear media and the delivered
[flush/write-through qualification](../devices/usb-storage.md); reads alone do
not qualify a target. Unsupported GPT features/capacity and
inspection I/O/allocation failures also fail closed.

The journal prompt starts with ceil(pool bytes / 128), rounded up to MiB,
with an 8 MiB default minimum and 1 GiB cap. A 256 GB disk defaults to 1 GiB.
Type to replace the highlighted value, navigate to edit it, or press Enter; explicit choices may be 1–1024 MiB when metadata
and bootstrap files fit. The final layout is displayed before requiring the
exact word `wipe`. Cancellation before Apply makes no target writes. Consent is
inspected again through fresh read-only inspection; the single-use erase grant
also revalidates the exact reviewed GPT/geometry before write admission.

Installation rebuilds the whole selected disk: a fresh GPT, a 512 MiB FAT32 ESP
starting at 1 MiB, then an npfs pool extending to the aligned end before backup
GPT metadata. The pool has a `system` volume with an empty regular root
`SAFE_TO_WIPE` marker and a regular `.pyxis-classic` file containing exactly
`PYXIS-CLASSIC 1` plus LF (16 ASCII bytes). Classic Install synchronizes that
file and the system root; Coexist never creates it. The pool also has an empty
`home` volume and empty `packages` and `http-work` volumes. The installer formats the pool and
writes the GPT and initializes the ESP through its reviewed erase writer,
releases it, opens the explicitly bound pool and releases that raw claim before
creating `home`, `packages` and `http-work`, seeding `system://config/packages.lua`
from the boot archive when it is missing, and writing the initial configuration,
as [Update's pool stage](system-updates.md#pool-stage) does. Only then does it
stage and verify a fresh immutable boot attempt through native FAT files at
`EFI/pyxis/REVISION/ATTEMPT/`, containing `kernel.elf`, the core archive as
built as `boot.arc`, and `revision.txt`. Fresh Classic installation places Limine at
`EFI/BOOT/BOOTX64.EFI` and establishes the owned normal/rescue menu at
`boot/limine/limine.conf`, with a three-second timeout and the new disk GUID.
Live-media installer and FAT32 tool entries are never imported. The normal
entry uses `init=boot://boot-init.pxe mount.disk=<GUID> mount.pool=<N>`; rescue adds
`boot.default_config=1`. A separate verified, small rescue pair under
`EFI/pyxis/recovery/` and static `/boot/limine.conf` are provisioned before
activation. The fallback covers an absent primary configuration, not a
corrupt-but-present one. A new install also copies
`boot.lua`, `session.lua`, `network.lua` and `init-installed` from
`boot://share/installer/system/` to `system://config/`; each file is written
only when missing, and Update never writes them; Update only creates a missing
`system://config/` and seeds a missing `packages.lua`. Update refuses a Classic
install whose ESP has no boot record, or one written before task 2b, with "back
up home and reinstall" (decision 28 of [package upgrades](../wip/pkg-upgrade.md)). Boot init then reads that
[installed configuration](init.md#installed-configuration): by default the
`pyxis` space, whose `init-installed` receives the pool's system and home
volumes read-write as `system://` and `home://`, starts in `home://` and starts
the ordinary local session. `tmp://` stays RAM-backed.

Success requires the program copies to match their sources, then native ESP
sync, canonical traversal, held-object identity and full size/hash verification,
explicit view retirement and read-only pool/marker verification. Only then does the program report `installed`. Remove the
live medium and boot from the target. Reinstall requires another live-media
boot because verification retains the pool until reboot. Delete
`system://SAFE_TO_WIPE` and sync that directory to mark the installation final.

The ordinary action menu also offers **Update**. It lists eligible installations and
installed/live revisions, then requires the exact word `update`. Update preserves
the GPT and existing volumes, rechecks eligibility under exclusive raw access,
prepares the pool, then stages a fresh immutable
boot attempt and publishes reviewed configuration and owned boot entries through
native FAT without formatting a healthy ESP.
It verifies publication, retires the view, reopens `system` read-only and
removes program copies left by installs from before the core archive before
reporting `updated`. Healthy installer-layout
GPT and a compatible empty-journal pool also permit rebuilding a damaged or
missing ESP through an explicitly reviewed, one-use ESP-only rebuild grant;
readable foreign disk bindings and raw I/O/allocation failures
refuse. A selected committed journal must first be recovered by booting the
installed system. No wipe marker is needed. See
[system updates](system-updates.md) for admission, recovery and qualification.

## Classic origin marker

Classic Update requires the exact regular root `system://.pyxis-classic` file,
containing `PYXIS-CLASSIC 1` plus LF (16 bytes), alongside its existing GPT geometry
and pool identity checks. Read-only raw discovery and revalidation check the
marker without mounting or replay. A matching Classic geometry with a missing,
wrong-type or wrong-content marker refuses; it never falls through to Coexist.
`SAFE_TO_WIPE` is separate erase consent and cannot substitute for this marker.
There is no legacy-marker adoption or recovery option.

For the owner's existing blue stick only, the owner may create the marker from
its **normal installed Pyxis shell**, whose `system://` root is read-write:

```sh
echo 'PYXIS-CLASSIC 1' > system://.pyxis-classic && sync system://.pyxis-classic system://
```

`echo` adds the required LF; `sync` requests file synchronization followed by the
root directory. This is the owner's explicit one-off action, not an automatic
migration or installer operation.

## Plans

Preparation is read-only: temporary native RO ESP views are retired after
inspection; no RW mount, persistent journal replay, formatting,
cleanup or writes. Normal Review shows the target, journal/policy choices,
the revision and archive size, and destruction/recovery limits on one screen at
ordinary console sizes. `-v` / `--verbose`, `--plan` and the Print plan action
add partition extents and volume details.
Its Apply/Print/Dry-run choice edits no disk. Only the exact word
`wipe` or `update` authorizes the prepared plan; exclusive revalidation precedes
the first write and changed inputs refuse without automatic replanning.

`-v` / `--verbose` also prints detailed phase records locally. `--plan` prints
and exits; `--dry-run` reviews and exits. `--apply` reads one
`pyxis-install-plan 1` frame from stdin, ending with `end` plus LF, rebuilds its
read-only inspection and displays Review. `--apply --confirm=wipe` or
`--apply --confirm=update` provides separate non-interactive consent and skips
restart prompting. Human output/progress uses stderr; stdout carries the data-only
plan. The stock menu also offers replay. A caller may save printed bytes with
its own grants; the installer obtains no writable root to save them. Plans are
not commands and carry no authority. Only trusted install-mode launches have
the [required grants](../devices/installer-authority.md).

Replay requires a uniquely observed validated existing GPT GUID, matching geometry
and consumed state, never an enumeration number. Duplicate identities refuse;
this binds a logical disk, not authenticated physical identity. Blank/damaged
unidentified targets support in-process printing/dry-run and interactive apply;
their serialized plans cannot select a target for apply. Recorded Install IDs
and journal choices are never regenerated during replay. Hashes bind the choices,
all immutable source assets and consumed raw target reads. Missing, malformed or
stale input refuses. Standard input is framed; following bytes cannot amend or
execute that plan.

## Verification

Planning uses raw codecs because a read-only mount would retain the pool until
reboot and prevent later write admission; raw inspection bounds decoded records
(including free slots) at 65,536 per directory. This is consumed-path
validation, not whole-pool fsck. Staging space is conservatively checked
without credit for cleanup; it is not a promise against all allocation limits.
Boot payloads use fresh immutable attempts even for same-revision updates.

[QEMU record](../development/experiments/installer-plan/README.md) separates
baseline costs, measurements, inspected behavior and qualification limits.

## Boot publication

This integrated publication path is enabled for Classic and Coexist. Coexist
also provisions the pinned `EFI/pyxis/BOOTX64.EFI` loader and registers a
structured `Pyxis OS` firmware option after the record and menu verify.
Review offers moving it to the front of BootOrder, defaulting to preserving
foreign order. Unrecorded Coexist installations use Update to acquire this
owned publication; an existing ambiguous menu refuses rather than being adopted.

Review shows the proposed boot attempt, recovery
and capacity. The ESP holds no configuration: Install writes the initial
`system://config/` files once, and boot reads them there. The installer keeps
one file in `EFI/pyxis/config/`, `config.record`, which records the current boot
attempt with its payload hashes and the owned menu's recovery
copy. A record still marked prepared is an interrupted Update, below. A
malformed record, or one written before task 2b of
[package upgrades](../wip/pkg-upgrade.md), is refused, and that machine
reinstalls (decision 28).

Staging and program verification precede the final bounded publication
interval: CHECK immediately after BEGIN, frozen-input and payload revalidation,
the prepared then committed boot record, then the owned menu switch last:
normal and rescue boot the new attempt. Ambiguous ownership leaves the menu
unchanged and reports
**staged, boot activation pending**. Explicit completion is reached through
**Update**: select the target marked pending, review the recorded operation,
select completion, then type the exact word `complete`. Only exact recognized
states qualify. This reuses the existing attempt and record; it creates no new
Update. There is no automatic retry, rollback or repair. Malformed records and
ambiguous states still require manual disposition. The same explicit
completion is reachable for existing committed pending Coexist records, under
the same full proof checks and layout-confined authority. It creates no new
attempt.

### Interrupted Update

An Update stopped between its prepared and committed record leaves
`EFI/pyxis/config/config.record` with `state prepared`. The menu has not
changed, so its currently selected entries still boot, and Update refuses with "an
earlier Update stopped before its boot record committed". The committed record
for that attempt was written and verified before the prepared one, as
`EFI/pyxis/config/.pyxis-config-ATTEMPT-config.record.committed`, where ATTEMPT
is the record's `transaction`. From another system with the ESP mounted
writable, check that this file is present and differs from `config.record` only
in its `state committed` line, then rename it to `config.record`, replacing the
prepared one. Update then offers the target with **completion Review
required**; complete it as above, which publishes the staged menu. The
publisher always writes that copy before the prepared record, so a prepared
record without it was changed by something else and needs manual disposition.

An interrupted Update can also leave FAT marked dirty, which refuses writable
admission even when the pending record is recognized. Completion then remains
unavailable until external filesystem repair. Pyxis does not validate and clear
recoverable dirty state itself yet; that needs a separate owned-writer recovery
follow-up. See the [dirty-FAT limit](https://git.internal/PyxisOS/pyxis-os/issues/1420).

Installed boot reads only the committed `config.record` from the ESP, to hash
it with the loaded kernel and archive into the upgrade evidence. Its
configuration, spaces and session/network bytes come from the pool's
`system://config/`, checked by [`config validate`](configuration-editor.md); session/network bytes reach startup as private READ-only
RAM FILE snapshots. Ordinary children receive no live ESP handles or
administrative writer authority.

Publication retains the committed current attempt and the independent static
rescue pair. After confirmed activation, it removes unreferenced attempts only
when canonical names, ownership manifests, directory contents and remaining
payload hashes prove ownership. Ambiguous nonempty attempts refuse cleanup;
empty unmarked directories are left alone.
The pinned recovery pair and fallback are reverified and never replaced by
normal publication. Capacity includes retained recovery and fresh staging,
without credit for cleanup. Classic Update preserves firmware entries;
Coexist Update verifies its owned firmware option. Both change only confirmed
Pyxis-owned menu spans. The
[publication reference](../../userspace/installer/PUBLICATION.md) describes
ordering and failure limits.

## Shared mounted ESP payload stager

The reusable [payload helper](../../userspace/installer/ESP-PAYLOADS.md) stages
kernel, filtered archive and revision bytes, plus optional printed entry text,
in a fresh `EFI/pyxis/<revision>/<attempt>/` directory. It checks reviewed hashes,
synchronizes and reopens the canonical names to compare retained object identity
and content. `ownership.record` describes that exact attempt; paths and hashes
alone confer no authority. Partial attempts are retained on error, without
retry, overwrite or deletion. The caller holds the result through its final
verification and closes it before retiring the mount.

The caller supplies explicitly granted native root/control and bound identity
facts, owns Review/admission, CHECK and interval lifetime, activation and later
cleanup. The helper has no raw-disk, configuration, Limine, firmware or active
boot selection operation and requires no REMOVE grant. Its optional `entry.txt`
is saved evidence, not installed automatically. Classic Install/Update and
coexistence use this helper for their immutable attempts. The integrated publisher supplies configuration and boot-entry
activation above it; the helper itself never selects an active attempt.

Traversal requires ENUMERATE as well as LOOKUP: reused `EFI`, `pyxis`, revision
and attempt parents must have the requested canonical name under ASCII FAT case
rules. Short-name aliases of other canonical names refuse. This is checked when
opening staging parents and again during every final verification.

Cancel is available before confirming Apply. The current synchronous executor
does not poll cancellation input during payload hashing, copying or sync; progress
is not a cancellation control. Interrupting confirmed work can leave an
unreferenced partial attempt and never promises rollback of confirmed writes.

[Scratch qualification](../development/experiments/esp-payload-stager/README.md)
records QEMU staging/verification, capacity refusal and retained failure evidence.
It does not qualify configuration activation or publication-interval headroom.

## Progress

Install and Update show one bounded live line for long work: phase, a bar and
completed/total files or bytes when known, otherwise an activity indicator.
Major inspection, copy, verification, EFI publication and cleanup phases leave
one finished line. Short checks stay silent unless they fail. Selected disk
identity is shown again before journal/legacy-inventory questions and in Review.

Copy and program verification count completed regular files and payload bytes,
including bundle descendants. Verification bytes count successful comparisons
once; directories are checked separately. Totals come from the existing archive
pass, without another scan. Removal counts deleted files and directories; raw
stages count successful disk-command bytes, including sector/cluster padding.
Measured byte rates use the read-only clock. Counters describe observed work,
not durability or whole-operation success; a phase ending does not mean success.

Program verification explicitly says **filesystem view; may be cached**. Pool
sync precedes it, but ordinary npfs reads can return retained copied pages. It
is not guaranteed device read-back. ESP verification syncs and reopens native
FAT files, checks held-object identity and hashes complete contents. It does not
prove cache bypass, physical-media behaviour or power-loss safety.

Shared phase/counter records also send detailed boundaries and coalesced updates
to Caelum's log through the trusted install launch's WRITE-only
`installer_diagnostics` console grant. Ordinary programs do not receive it.
`-v` prints these records locally; normal output remains quiet. The standalone
configuration editor keeps its local detailed records and has no diagnostic grant.

Console rows reserve the final column, adapt to current width and redraw at most
four times per second. Minor work becomes visible after one second. Non-console
output leaves final major/long-operation lines, without carriage-return animation.
Clock checks are coalesced after 64 KiB or 32 items. A blocked syscall can show
only a static activity marker. Prompts/errors close the current row. Clock or
output failure disables the corresponding observer without changing confirmed
I/O, consent, checks or publication order; failure to print Review still refuses
apply. The future curses TUI can consume the same records.
[QEMU qualification and earlier costs](../development/experiments/installer-progress/README.md)
record the runtime evidence and limits.

## Restart

After the final `installed` or `updated` message the installer asks to restart,
reminding the user to remove the medium first:

```text
Remove the install medium before the machine restarts.
Press Enter to restart, or type stay to remain here:
```

Only an empty line restarts. Any other text, Ctrl+C or a failed read leaves the
installer with status 0, as before the offer. Failures and cancellations never
offer a restart. Restart is the kernel's [power restart](../kernel/acpi.md#power-off-and-restart):
it holds user tasks, flushes and checkpoints every mounted native pool, including
the one the installer retains, then resets. If the call returns, the installer
prints its status, says the installation or update is complete and to restart by
hand, and exits 0. Without the medium removed, the restart boots the live image
again.

`installer-init` gives the installer its `power` resource narrowed to the RESTART
right, so the installer cannot power off. A boot without `power` offers nothing.

Installation writes allocated metadata and boot-file storage, without secure
erasure of free space. A failed installation may leave a partial disk; the failure message
directs you to run the installer again and choose **Any disk (may erase data)**. Boot the
live image again first; the same consent vetoes still apply. There is no automatic
retry, rollback or repair. V1 uses existing writable disk drivers and supports logical
sector sizes 512 and 4096, matching kernel GPT discovery/rescan. It adds no general FAT driver or
general firmware-variable updater. The owned Coexist workflow has a typed,
boot-option-scoped UEFI grant. See the
[validation record](../development/experiments/native-filesystem-task4.3/README.md)
and [remaining limits](https://git.internal/PyxisOS/pyxis-os/issues/1416).
Merged-main [QEMU end-to-end qualification](../development/experiments/native-filesystem-task5/README.md)
includes USB-backed live-media loading and CPU entropy. The separate
[C.3 USB-target record](../development/experiments/usb-installer-c3/README.md)
documents installer raw-authority integration checks. The first
[native USB installation](../devices/usb-installation.md#validation) installed
onto a USB stick on the ThinkPad from PXE live media. The stick then booted
alone, kept a synced file across a synced power-off and passed one Update round
trip; see the
[owner-reported record](../targets/t14-gen1-amd/usb-bringup.md#2026-10-05-first-native-installation-c4).
The [program reference](../../userspace/installer/README.md) describes its SDK
boundary; the [format and host tools](../../fs/docs/npfs-host-tools.md) remain
owned by pyxis-fs.

### Legacy Coexist Update

Update recognizes the identified 0.0.9 manual Coexist menu and the owner's
0.0.8 bootstrap rescue. Review explicitly adopts only the GUID-bound Pyxis
normal/rescue entries after checking their immutable attempt manifests and all
payload hashes. Other primary-menu bytes, including an existing previous entry,
remain unchanged; retained menus protect the attempts they still reference.

As the accepted legacy-only exception, Update replaces the proven Pyxis rescue
fallback with the canonical `EFI/pyxis/recovery` pair and ownership manifest.
The new pair and staged fallback are synchronized and verified first. A checked,
bounded rename replaces `boot/limine.conf`; its original bytes remain in
`EFI/pyxis/recovery/legacy-fallback.conf`, and the old pair is retained. Foreign
fallbacks, modified legacy bytes and partial migration artifacts refuse instead
of being overwritten. Existing new-style recovery is never replaced. Fedora
files and the selected firmware-order policy remain unchanged.
