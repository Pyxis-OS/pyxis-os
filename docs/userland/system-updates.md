# System updates through the installer

Boot the newer live image, select **Install Pyxis** in Limine, then choose
**Update** in the [native installer](installer.md). Update has two stages. It
first prepares the pool's volumes and configuration, then stages immutable boot
payloads, the kernel and the core archive as built, and publishes reviewed configuration and owned
boot entries. Normal builds enable this publication and installed configuration
loading for Classic and Coexist. Coexist also verifies its owned Limine loader
and UEFI boot option after publication. No prior `config` use is required for an update without owner edits. It preserves
the GPT, partition identities and bounds, and every existing volume, including
`system://` and `home://`
data. It uses the
same trusted [reviewed disk and boot-source authority](../devices/installer-authority.md)
as install.

## Eligibility and confirmation

The trusted selector discovers recognized classic and coexistence installations
read-only, offers a numbered list and infers the existing layout without asking
for one. Back or discovery failure returns to the action list. Refused targets
retain their reason, including the instruction to boot once for committed-journal
recovery. Pending publication is shown explicitly in target discovery. The child rechecks the chosen
disk GUID and ESP/pool entries before planning and preserves that layout's
existing grants and recovery behavior. Its Review shows installed and live
kernel revisions. Committed readiness and matching owned menu evidence identify an
activated immutable attempt. Legacy installations use the optional `boot/revision`;
a missing, empty, structurally damaged or nonprintable record displays `unknown`.
A revision string alone does not authorize an update. Retained VirtIO
and USB candidates use the same selection and typed-consent rules. USB write
claims require known write-protection-clear media and the delivered
[flush/write-through qualification](../devices/usb-storage.md), with no latched
write failure or mounted/claimed device.
Inventory waits for sealed discovery; partial USB coverage may conceal other
eligible disks, while lost registry records or incomplete VirtIO bookkeeping
refuse inventory.

A classic target has healthy matching GPT copies with the exact installer
layout: the 512 MiB ESP at 1 MiB and an npfs partition extending to the aligned
end before backup GPT metadata. Its pool must pass the live format code's
writable-mount metadata admission, have a live `system` volume and an empty
selected journal. A valid npfs header/control copy may survive a damaged peer,
following kernel admission. The system volume must also contain the exact regular
root file `.pyxis-classic`: `PYXIS-CLASSIC 1` plus LF, 16 ASCII bytes. Classic
Install creates and synchronizes the marker and root; Coexist never writes it.
Read-only raw discovery and revalidation require it alongside geometry and pool
identity. Missing, wrong-type or wrong-content markers refuse when Classic
geometry matched, without falling through to Coexist. There is no legacy-marker
adoption or recovery option. The owner’s existing blue-stick one-off is documented
under [Classic origin marker](installer.md#classic-origin-marker). This is metadata
admission, not whole-pool fsck.

Coexistence targets instead use their healthy GPT's explicitly selected existing
ESP and native pool, supported native ESP admission and installed-payload
ownership evidence. Update publishes a fresh immutable attempt through the
owned record and menu, verifies the pinned Pyxis Limine loader and its UEFI boot
option, and preserves foreign bootloader files and configuration. Existing
unrecorded Coexist installations need one installer Update to gain that record,
loader and option. They never gain Classic's erase or ESP-rebuild grants.
Installed `pkg upgrade` has separate narrow authority and a
[verified reboot-time package-set switch](package-upgrades.md); live-media Update
remains available for migration and recovery.

A selected committed journal is refused: boot the installed system once to
recover it, then update. Eligibility inspection never mounts, replays or
writes the pool; only the confirmed pool stage mounts it. Finalized and
unfinalized installations both qualify; Update needs no `SAFE_TO_WIPE` marker
and has no Read the room mode.

ESP inspection distinguishes preservation, explicitly reviewed rebuild and
refusal. Normal Update preserves a supported clean native FAT32 ESP, its boot
manager and unowned bytes. Only entries a committed boot record describes are
replaced; names alone prove no ownership. Missing/ambiguous menu ownership
leaves it unchanged and reports staged, boot activation pending. A prepared
boot record is an [interrupted Update](installer.md#interrupted-update); a
malformed record or unresolved publication artifacts require explicit
disposition. Ordinary Update does not silently repair or retry them.

Missing or structurally damaged FAT contents may be rebuildable using healthy
installer-layout GPT and a compatible empty-journal pool as the recovery anchor.
Readable configuration must not carry a foreign or invalid disk binding. Raw
I/O/allocation failures and unsupported extents fail closed. A valid FAT12/FAT16
ESP or unsupported FAT32 feature refuses preservation Update. Rebuild requires
its own reviewed, one-use ESP-only grant; it is never a fallback for a failed
preservation attempt and is unavailable to coexistence. Existing volumes,
including any `system://config/boot.lua`, are retained. The pool stage adds
`home`, `packages` and `http-work` when an older installation lacks them.
Installed packages stay; newer builds of them come through `pkg`.

Review the [plan](installer.md#plans),
then type the exact word `update` to confirm. Cancellation/dry-run makes no writes. The
installer repeats read-only eligibility and consumed-input checks, then opens
the exact reviewed pool. Any explicit recovery also consumes its separately
reviewed GPT/geometry-bound ESP grant. Changed bounds, GUIDs or consumed state
refuse without automatic replanning. It also plans the new boot tree and
checks that the sources fit before mutation.

## Pool stage

The installer opens the exact reviewed pool partition, releases its raw claim
and mounts the pool through that partition-bound handle. It creates an empty
`home` volume when the pool has none, without opening it, and stops there if
that fails. It likewise creates the `packages` and `http-work` volumes when
missing and seeds `system://config/packages.lua` from the boot archive only when
that file is absent; an existing file is never replaced. It writes no programs:
the core archive is copied to the ESP as built, and every other program is a
package ([decision 18](../wip/pkg-upgrade.md)).

This is an offline target update: pool-partition admission refuses a retained
npfs instance on the selected extent. Held development directories are not
snapshots; do not modify or delete published development trees while programs
use them.

Until the ESP holds the new kernel, the disk boots its old kernel and archive.
An install from before the core archive may still hold legacy `bin/REVISION`
program copies; Update removes only copies whose ownership it can prove.

## Installs from before the core archive

Update never changes `system://config/` (decision 15), and an install from
before the core archive has an `init-installed` that starts session without a
services script and no `init-services`. After updating, its normal entry shows
session's usage instead of a shell. Reinstall, or repair from the
**Pyxis OS (rescue)** entry, which boots the install's original recovery pair:

```text
echo '#!boot://init-shell.pxe' > system://config/init-services
echo 'service start --root json-http=json+http --optional-read-only-root https=https --optional-read-only-root json-https=json+https http boot://httpfs.pxb' >> system://config/init-services
echo 'session boot://session.pxe' >> system://config/init-services
echo '#!boot://init-shell.pxe' > system://config/init-installed
echo 'service start text boot://textfs.pxe' >> system://config/init-installed
echo 'session boot://session.pxe --configure-network --start-services system://config/init-services' >> system://config/init-installed
sync system://config
```

These are the files Install writes, from `share/installer/system/` in the core
archive; an edited `init-installed` keeps its other lines and changes only its
session line. Then `pkg install base` installs the programs, which are packages
now. chronium chose no compatibility path (2026-10-23).

## Replacement and verification

The boot record, menu activation and independent recovery sequence below is
shared by Classic and Coexist. Coexist adds a pinned loader and structured
firmware registration after verified publication.

Review freezes the target identity, the boot record and menu bytes and the
recovery evidence. An initial exclusive checked interval revalidates consumed
inputs before any program writes. Changed inputs refuse without automatic
replanning. Update never touches the configuration: it lives in
`system://config/` on the pool, and edits there apply at the next boot without
an Update. A boot record written before task 2b of
[package upgrades](../wip/pkg-upgrade.md) refuses Update; back up `home` and
reinstall (decision 28). See [boot publication](installer.md#boot-publication).

The installer opens the reviewed ESP as a native RW FAT view, leaving the pool
mounted. A healthy filesystem is preserved. Each update, including a same-revision
update, stages fresh `kernel.elf`, `boot.arc` and `revision.txt` files under
`EFI/pyxis/REVISION/ATTEMPT/`. Collisions refuse. The kernel comes from the live
boot's original immutable FILE; the archive is filtered without the moved program
roots and bundle descendants. Component-boundary filtering leaves unrelated names
intact. Fresh Install or explicitly reviewed ESP rebuilding initializes the
bootstrap tree, including `EFI/BOOT/BOOTX64.EFI`; normal Update preserves the
installed boot manager.

The shared payload helper synchronizes and reopens canonical names, checks held
object identity and hashes complete contents. Existing names must match under
ASCII FAT case rules; an 8.3 alias of another canonical name refuses. Before
activation, the publisher provisions or fully reverifies the independent small
rescue pair under `EFI/pyxis/recovery/` and the static `/boot/limine.conf` fallback.
Normal publication never replaces or cleans them. Higher-priority shadowing
configurations and foreign fallback bytes refuse. The fallback covers an absent
primary `/boot/limine/limine.conf`, not a corrupt-but-present one.

After staging, BEGIN followed immediately by CHECK starts the non-extending
30-second final publication interval. Revalidation includes the consumed record
and menu and fresh payload identity/content. The prepared then committed boot
record follows. Only then may confirmed Pyxis-owned
menu spans switch, with synchronization and readback: normal and rescue to the
new attempt. A legacy `Pyxis OS (previous)` entry is removed only when its
binding or the complete prior menu proves ownership. A committed record alone does not report `updated`: menu activation,
verification and explicit view retirement must also succeed. This is completion
detection, without cross-file atomicity or power-loss durability guarantees.

Installed startup reads no configuration from the ESP: the normal entry reads
the pool's [`system://config/`](init.md#installed-configuration), which Update
never changes. Boot init only
hashes the committed record with the loaded kernel and archive into the upgrade
evidence.

Only the committed current boot attempt remains, in addition to pinned rescue.
After verified activation, unreferenced attempts are cleaned only with canonical
names, ownership manifests, complete directory checks and matching payload hashes.
Ambiguous nonempty attempts refuse cleanup; empty unmarked directories remain.
Capacity accounts for
fresh staging, retained payloads, configuration recovery and allocation overhead
before writes; old attempts give no capacity credit.

After verified activation and view retirement, `system` is reopened read-only.
The installer removes `bin/REVISION` program copies left by installs from before
the core archive when their ownership is proven; unknown ownership skips
cleanup. The emptied `bin` volume stays
([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1426)). Cleanup is best effort
and warns on failure. Only completed publication reports `updated` and
[offers a restart](installer.md#restart). Remove the live medium before restarting.
The retained pool requires another live-media boot before another Update. Opening
`system` establishes root access, without auditing ordinary user files or replacing
whole-pool fsck.

## Recovery and qualification

A failure in the pool stage leaves the installed boot entries unchanged. A failure during
ESP publication retains partial attempts and recovery evidence at the reported
phase. Choose **Update**, select the pending target, inspect the completion
Review, then select completion and type `complete`. This accepts only exact known
record, payload, menu and artifact states, reusing the attempt and committed
readiness/retention generation. It neither creates a new attempt nor starts a new
Update. Prepared/malformed or ambiguous states still require manual disposition.
Existing committed pending Coexist records use the same explicit completion
path and proof checks with their own confined authority.

An interrupted Update can leave ESP FAT marked dirty. Pyxis then refuses writable
admission, leaving Update completion unavailable until external filesystem repair,
even for a recognized pending record. Validating and clearing recoverable dirty
state under the owned writer with proven recovery is a separate unimplemented
follow-up; see the [dirty-FAT limit](https://git.internal/PyxisOS/pyxis-os/issues/1420).
A reviewed ESP rebuild failure can leave an incomplete ESP.
Inspect the reported phase before booting live media and reviewing another Update.
The GPT and pool remain the recovery anchor, and the same eligibility checks
apply on the next run. There is no automatic retry, rollback, pool repair or
power-loss durability guarantee. Classic Update leaves firmware variables
unchanged; Coexist registers or verifies its Pyxis-owned UEFI boot option with
bounded structured writes and readback.

The [recognition record](../development/experiments/system-updates-task1/README.md)
covers the initial action menu, revision recording and candidate/refusal cases.
The [ESP-update qualification record](../development/experiments/system-updates-task2/README.md)
records the finalized installation update, target-only reboot, preserved data,
host structural checking and recovery of an interrupted ESP replacement.
These records qualify the original VirtIO target path. Installer USB raw-disk
authority is now implemented, using the same Update workflow; its integration
checks are in the [C.3 USB-target
record](../development/experiments/usb-installer-c3/README.md). The first
[native USB installation](../devices/usb-installation.md#validation) on the
ThinkPad passed one physical Update round trip, preserving a synced file. Power
loss and uncertain I/O remain [deferred](https://git.internal/PyxisOS/pyxis-os/issues/1416).
This interface is the live-media installer Update. The separate installed
[`pkg upgrade`](package-upgrades.md#upgrading-the-release) stages a signed
release and complete package set. npfs format migration remains outside both.

The [publication record](../development/experiments/installer-publication/README.md)
separates integrated disposable-QEMU evidence from the owner-reported 2026-10-20
native gate on vela. That gate covers clean Install, installed loading,
same-revision Classic Update and rescue through the prior UI; foreign-file
preservation was not checked on the freshly wiped native medium. The action-list,
Replay and reachable pending-completion corrections have separate QEMU records;
the old native pass does not qualify that UI or Coexist activation. Owned Coexist
Install and Update have disposable OVMF checks, not a native firmware-variable
qualification.
QEMU cut results do not qualify native power cuts, torn sectors or devices ignoring
FLUSH.
