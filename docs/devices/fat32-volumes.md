# FAT32 volumes

Pyxis can inspect supported GPT FAT32 media, create files through the confined
raw writer, and mount native read-only or read-write FILE/directory views. The
shared freestanding engine lives in pyxis-fs; Caelum owns mounted state, native
capabilities, physical-extent exclusion and BSP-worker I/O. The kernel parser's
[accepted exposure](https://git.internal/PyxisOS/pyxis-os/issues/1417) remains.
The [native mount reference](fat32-mounts.md) defines operations, ownership,
publication intervals, admission budgets and failure/clean-power ordering.

## Supported media and validation

The profile requires healthy GPT with both copies validated, matching 512- or
4096-byte device/FAT sectors, power-of-two clusters of 1–128 sectors, and one FAT
or two mirrored FATs.
Legacy/hybrid MBR, bare FAT, FAT12/16, exFAT and active-FAT/nonmirrored layouts
refuse. Volume bounds, FAT chains, long names, directory tails, labels and FSInfo
are checked within finite memory, depth, work and I/O budgets; exhaustion returns
an error without weakening validation. Native admission and explicit CHECK
validate the whole volume; mounted mutations validate touched structures and
maintain the retained allocation map. Raw inspection of one file does not prove
unrelated metadata sound.

Enumeration preserves valid UTF-8 names. Lookup accepts ASCII long names and
short aliases, case-insensitively; mutation uses the supported ASCII profile and
refuses unsupported namespace collisions. Native identity belongs to the live
mounted object, not its reusable directory slot or first cluster. Held FILE grants
keep authorized access to the old object/chain after unlink/replacement; growth
zero-fills. Metadata reports
only genuinely valid fields; DOS-local times are not fabricated Unix mtimes.
The [reader](../../fs/docs/fat32-read.md), [raw writer](../../fs/docs/fat32-write.md)
and [mounted engine](../../fs/docs/fat32-mounted.md) own the format contracts.

The [logical-sector qualification](../development/experiments/fat32-4kn.md)
records independent host checks and native RO/RW mounts on disposable 512/4096-byte
VirtIO media; it does not establish physical-device qualification.

## Engines and native interfaces

[Read-only inspection](fat32-inspection.md) lists/reads explicitly selected,
quiescent media through INFO | READ authority. The host inspector and native boot
profile never repair or write. The installer's separate lenient ESP recovery
reader retains its foreign-file safeguards; a strict refusal never authorizes
rebuilding the ESP.

[Raw scratch writing](fat32-scratch-writing.md) performs incremental creation,
append, extension and staged replacement under an explicit partition claim.
Session-only creation receipts authorize updates to files that writer created;
restart loses those permissions, and names never imply ownership. Success follows
the final flush, dirty flags are preserved and an uncertain mutation stops the
writer. RELEASE ends its claim even on failure. This is a raw development profile,
separate from mounted editing; no whole-tree formatter or repair is supplied.

Native mounts return a root/control pair atomically through `DISK_OPEN_FAT32`
or the mount-only `DISKS_OPEN_FAT32` front door. Trusted init's factory is
separate from raw access; operator delegation is explicitly configured, absent
by default. Fixed-mode RO instances cannot upgrade. Root/data rights and control
OPEN_ROOT/OBSERVE/RETIRE/WRITE/INTERVAL rights are separately attenuated.

RW mounts serve ordinary native writes, resize, create/remove, file rename and
sync. Mutations complete ordered durability barriers before success; FAT supplies
no journal, crash-atomic rename or rollback promise. Uncertain mutation/flush
freezes the instance with diagnostics and retained exclusion. RETIRE is
BUSY-until-drained without revocation; final-control close does not retire.
Clean power seals mutated volumes with handles and intervals held, clears only
owned dirty marks when detached chains permit, then seals npfs and shuts storage
down. Unresolved mutation failure blocks clean power; held files alone do not.
See [mutations](fat32-mounts.md#native-mutations-and-publication-intervals),
[exclusion/retirement](fat32-mounts.md#exclusion-and-retirement) and
[clean power](fat32-mounts.md#failure-and-clean-power) for exact rules.

The mounted adapter retains a validated read-chain proof only within an
unchanged exclusive epoch. Strict confirmed EOF appends may extend the accepted
append proof; other mutations, CHECK and errors invalidate it. Retained chain
storage grows geometrically within the mount budget. Mutation preparation indexes
phase/sector records before effects and batches only adjacent sectors in the
same phase, within the device's private transfer bound (at most 64 KiB here).
Every effectful phase keeps its durability barrier; empty phases do not flush.

The bounded native [FILE transfer](../interfaces/file-providers.md#file-dispatch-and-lifetime)
path supports at most 1 MiB between explicit RAM/initrd and FAT FILE grants.
It preallocates stable private backing and uses device-sized direct read/write
runs. Ordinary FILE/DISK message limits remain 4 KiB; no raw-media or mount
permission is added. Physical-media, power-loss and consumer activation
qualification remain separate gates.

## Configured views and everyday workflow

[Boot configuration](../userland/init.md#fat32-volumes-and-selected-grants) admits
an exact selected disk GUID/GPT entry with ambiguity refused, or a provenance-bound
RO `boot-esp` view. Identifiers belong only in owner-local configuration; repository
examples use placeholders. Optical/PXE boots do not guess a boot ESP.

Ordinary FAT roots are RO. Separately selected data/control sources remain outside
ambient roots. The [common bundle resolver](../userland/program-bundles.md#selected-external-grants)
supplies explicitly declared editor grants and reports actual received authority.
A manifest requests authority; it cannot authorize admission. Trusted local
session/mux successors preserve selected sources; remote-service startup omits them.
No shipped configuration enables FAT management or RW ESP access.

The [shell](../userland/shell.md#fat32-mount-management) owns bounded bindings:
`mount --fat32 --list`, explicit disk/partition/RO-or-RW mount, `sync --mount`,
and `release`. RW mounting publishes an RO root and separate selected write/control
sources. Release validates the controller/domain, moves a mounted cwd to home
with a report and unbinds its own data. BUSY/failure retains the controller for an
explicit retry after other owners drain. Missing or unusable home refuses before
unbinding. `fat32ctl.pxb` is a thin declared-grant
list/state/sync/retire utility; it cannot alter its parent's bindings.

**Boot ESP writes are deliberately permitted by an authorized RW factory**
(owner-accepted default, 2026-10-13), supporting the accepted ESP `boot.lua` and
[configuration-editor direction](../wip/installer-tui.md#accepted-configuration-contract--2026-10-11).
The boot-origin issuer/view stays RO. Fixed-mode drainage, explicit editor grants,
extent exclusion and backend write qualification still apply. FAT support does
not deliver the editor's ownership/adoption policy, coexistence installation or
ordinary Update preservation; those remain separate consumer work.

## Qualification and remaining limits

The [read-only](../development/experiments/fat32-read-only.md),
[directory-validation](../development/experiments/fat32-directory-validation.md),
[raw-write](../development/experiments/fat32-raw-writes.md),
[native-mount](../development/experiments/fat32-native-mount.md),
[mounted-write](../development/experiments/fat32-mounted-writes.md) and
[trusted-workflow](../development/experiments/fat32-trusted-workflow.md) records
retain exact revisions, control costs, disposable-image checks and QEMU/GDB
results. They are nested-VM evidence. The workflow timing comparison remains
inconclusive; it establishes no performance-neutrality claim.

There is no MBR/bare-volume discovery, new SD driver, hotplug, external-writer
snapshot, general filesystem repair or reconstruction, or filesystem/partition
resizing. The one mounted repair is the bounded FAT2 mirror repair that a writable
mount's first effective mutation performs ([mounts](fat32-mounts.md#native-mutations-and-publication-intervals)). FAT directory moves and wider
Unicode mutation remain unsupported. Physical scratch-USB and owner-present
native ESP qualification, unplug/torn-sector/power-loss behaviour remain pending;
[deferred work](https://git.internal/PyxisOS/pyxis-os/issues/1418)
records their consequences and revisit points. The read-only ESP check below is
scheduled by the owner; milestone closure neither performs nor authorizes it.

## Owner-present native ESP run sheet

No native ESP step runs without explicit owner approval **and presence**. The
pending owner-scheduled run is RO only:

1. Record merged kernel/dependency revisions and the intended boot profile.
   Confirm the actual GPT ESP boot provenance and selected disk/partition;
   optical/PXE must not guess a target. Keep other writers quiescent. Do not
   stage an image or change firmware variables as part of this check.
2. In owner-local boot configuration, opt in to `esp = {kind = "boot-esp"}`,
   the space's `esp = "read-only"` root, and a selected `fat32_control` MOUNT
   source with `access = "read-only"`. Keep `home://` on another authorized
   volume. Run `fat32ctl state`, `ls esp://` and read/hash a known file chosen
   by the owner. Verify RO mode/extent and actual controller rights (OBSERVE |
   RETIRE); `fat32ctl sync` must report denied. No formatter,
   repair, dirty-bit clear or configuration-editor save is permitted.
3. Close readers and run `release --control fat32_control esp://`. If cwd is
   on ESP it reports moving to `home://`. BUSY retains the controller; close
   other owners' views, then retry `release esp://`. Validation/I/O failure or
   BUSY causes no forced retirement or fallback. Disable the
   opt-in profile afterwards unless the owner chooses to retain it.

Real ESP writing needs separate explicit owner approval/presence and the consumer's
ownership/adoption policy; this run sheet does not authorize it. Power-off with
open files remains usable under the accepted seal contract.
