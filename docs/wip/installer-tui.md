# TUI installer/updater and configuration delta editor

Status: **direction and all three configuration choices accepted by the owner,
2026-10-11; docs only.** No delivery task is assigned; each needs a separate go.
Builds on the [installer](../userland/installer.md),
[updates](../userland/system-updates.md), [system layout](../userland/system-layout.md)
and [program bundles](program-bundles.md).

## Accepted direction — 2026-10-11

- A curses TUI using [PDCurses over libterm](application-ports.md#libraries-and-terminal-tools),
  with windows/panes and a left-side step menu. Install and Update share it.
- **Plan/apply split:** pages only inspect and edit a plan. No target writes,
  formatting, persistent replay or cleanup before final Review confirmation.
  Print and replay the same plan non-interactively; TUI, future CLI and standalone
  configuration editor share one executor.
- **Progress everywhere:** every I/O phase reports counters or a phase line,
  including inventory, reads, hashing, copy verification, sync and cleanup.
  Existing copy counters/phase lines are a starting point, not full coverage.
- **Mounted read-write FAT32 is part of this line**, after NVMe FAT32 engine
  task 2: a real volume/directory-capability backend, not only an installer
  library and not deferred to a future scheme provider.
- **Editable configuration lives on the ESP.** Selected Lua configs move out of
  the cpio archive into readable/editable FAT32 files beside the Limine editing
  workflow; archive defaults remain available for recovery. This does not depend on keeping `system://` as the configuration store.
- The configuration delta editor covers **Limine and Pyxis Lua configuration**.
  Each revision declares required/optional entries and migrations; the owner
  accepts or rejects each change. Preserve foreign/unowned bytes exactly,
  including Fedora entries, and keep the printed-entry fallback. This reverses
  [NVMe decision 2](nvme-coexistence.md#decisions) for owned entries only.
- Install writes a revision manifest **inside `bin/REVISION`**, with relative
  name, size and hash for every installed file. Updates compare manifests for
  their Programs report and reuse them for verification.
- The page/plan framework may later accommodate accounts. No account schema,
  identity model or account operations are part of this milestone.

## Plan and apply

The proposed plan records mode, source revision/manifest, selected disk and GPT
identities/extents, program changes and sizes, configuration changes and the
owner's choices. It includes expected current configuration bytes/hashes and
manifest state, so replay means the same reviewed operation, not choosing a new
target by enumeration number. Planning uses raw read-only inspection of the target
pool, without persistent replay or retained mounts that would prevent subsequent
write admission; reading configuration through FAT32 must release conflicting
views before acquiring an exclusive publication claim. Plan-time FAT mounts
are read-only and make no metadata repairs. A printed plan grants no authority;
the executor needs the existing [installer grants](../devices/installer-authority.md)
or explicitly delegated mounted FAT32 directory/file rights.

Final Review shows the complete change set, destructive extent if any, required
consent and recovery limits. Confirming authorizes that plan alone; non-interactive
apply must supply the same explicit consent. Revalidate source, target,
eligibility, available space and expected configuration under the applicable
exclusive claims before the first write. Changed inputs require a new plan;
do not silently amend a confirmed one. A dry run performs reads only.

Keep program staging/sync/verification before the ESP switch, offline admission,
same-revision interruption limits and current/previous revision retention from
the update contract. Stage configuration changes before publication, but do not
claim an atomic switch across program, Lua and Limine files. Failure after writes
reports the completed phase and recovery action; it never claims whole-install rollback. Standalone editing uses
this executor too, through delegated ESP config rights. The mounted backend and
installer partition claims must exclude simultaneous writers; applying requires
revalidation under that exclusive ownership. Normal boot does not confer a
raw-disk service on the editor. Runtime application to Continuum is later.

Ordinary Update currently reformats the ESP and regenerates its configuration.
This line must replace that behavior with owned-file publication preserving
editable Lua, retained recovery copies and unowned contents in **every Update**,
not just coexistence mode. Do not enable installed ESP configuration until that
preservation path lands. Fresh Install may format only its explicitly consented
target; any offered carry-forward configuration must be captured in the plan
before that destructive apply, not rediscovered afterwards.

## Pages

| Step | What the owner reviews or edits |
| --- | --- |
| Mode | Install or Update; install includes the accepted coexistence choice. |
| Disk/partitioning | Disk, layout and eligibility reasons. Update is read-only. Coexistence uses only already-free extents, partition-confined writes and the narrow GPT append operation; no Fedora resize or whole-disk claim. |
| Programs report | New, updated, removed and unchanged programs, file/total sizes and bundle contents. The owner confirms the report; removal refers to the new revision, not immediate deletion of the previous one. |
| Configuration | Per-entry old/new values, reasons, required/optional changes, migrations and validation results. |
| Review | All choices and writes, exact target, consent and recovery limits; the only apply boundary. |
| Progress | Phase plus completed/total files or bytes where known; phase/activity when no numeric total is available. Verification has its own counters. |
| Done/restart | Verified completion or precise failure/uncertainty; restart only after success, with the existing remove-media reminder. |

Back/navigation edits only the plan. Progress/error output remains usable without
curses as plain records for replay; UI redraw/output failure must not bypass
consent or suppress executor failures. Do not use invented percentages for a
blocking flush whose completion cannot be measured.

## Configuration delta model

Archive defaults are read-only today; installed boot already merges
[`system://config/boot.lua`](../userland/init.md#pool-override), while
[session/network Lua](../userland/session-configuration.md) comes from the archive.
The proposed ESP source replaces the selected installed-config paths together;
migration of an existing pool override is displayed and confirmed, never silently
dropped. Existing machine keys remain as implemented until their own store task
changes them. The editor uses their shared schema rather than inventing another
store. The editor shows old defaults, owner edits and new revision requirements separately. It offers
additions, changed defaults, optional removals and explicit schema migrations,
not a wholesale regenerated file. A declined required change leaves the plan
inapplicable with its reason; an optional change can be declined. Changed owner
values are preserved unless the owner approves replacement. Do not execute
arbitrary migration scripts or reinterpret unsupported Lua as a writable table.

Share parsing/schema/merge validation with boot init and the
[configuration checker](boot-configuration-checker.md); machine keys use the
[machine-settings validator](../userland/machine-settings.md) and its planned
[validating writer](machine-settings.md#later-writer-and-authority).
Separate static validity from checks requiring CPU/volume authority, reporting
unavailable checks honestly. Keep rescue's archive-default boot. The standalone
editor is the editing half of [Polaris](spaces.md#asterism), without implementing
live space changes or Continuum. Configuration write rights remain administrative;
ordinary sessions receive selected read-only config, not a writable whole ESP.

FAT32 has no journal or guaranteed atomic rename. This ESP is shared with
firmware, Limine and Fedora: preserve their files and every unowned config byte;
no whole-ESP rebuild in coexistence mode. The proposed boot init needs an early ESP read
before it can choose spaces, so bootstrap ESP identification/mount authority
must come from the trusted boot handoff, not the editable Lua it is about to
read. An ambiguous or unavailable binding cannot select a guessed partition.

ESP configuration is **not revisioned with `bin/REVISION`**. A program rollback
does not restore compatible settings. Migrations/rollback go through the delta
editor, which compares the chosen revision's declarations with the current ESP
state and preserved previous files. Old defaults in the archive provide rescue,
not automatic restoration of owner edits. Multi-file publication can be partial;
validation/fallback and a reported recovery path are mandatory.

## Revision manifest

Proposed representation: one reserved manifest file in `bin/REVISION`, sorted
relative paths, byte sizes and SHA-256 hashes, with bundle descendants included.
It does not list itself; directory structure/empty directories need separate
records for complete-tree verification. Publish it only after the revision's
copies have passed verification and sync it before the ESP switch. Bounds and
parsing must reuse the installed-tree admission limits, with no implicit budget
increase. It is a copy inventory, distinct from a bundle's launch manifest.

Compare the immutable new-source inventory to the prior revision manifest without
rereading every installed payload for the report. A missing/invalid old manifest
means unknown, not unchanged: offer an explicit one-time read/verify inventory
with progress, or refuse that comparison. Hash newly copied destination bytes
and check structure against the manifest; do not replace read-back verification
with trust in the manifest. Unsigned hashes do not authenticate a revision or
prove that installed bytes were not modified after installation.

## Accepted configuration contract — 2026-10-11

1. **Lua files and location. Installed boot/space, session and network
   configuration under `EFI/pyxis/config/` on the ESP**, exposed through a
   proposed `esp://` root. Use `boot.lua`, `session.lua` and `network.lua`; keep
   shipped live/install bootstrap and recovery defaults in `boot://config/`.
   Keep executables, init scripts, trust/zoneinfo assets and user application
   configs in their existing locations. Retain the owner's Limine config path;
   do not relocate Fedora's files. Normal consumers receive selected read-only
   views; the trusted editor receives scoped writable config rights.
2. **FAT32 publication and boot fallback. Write new, sync and read-verify,
   then swap while retaining the previous file as recovery fallback.** Recheck
   the old bytes under exclusive ownership and flush each publication boundary.
   FAT has no atomic-rename guarantee: interruption can leave the canonical name
   missing or the swap uncertain. Stop without blind retry; retain recovery
   names and printed entries. Boot init reads/validates the selected Lua set
   before applying it; missing, corrupt, unreadable or recognizably incomplete
   config uses the complete archive-default set with a reason, and rescue always
   selects that set. Detection of interrupted multi-file publication belongs in
   the FAT/editor task; valid Lua syntax alone cannot prove a complete set.
   Limine loads before boot init: recovery from its previous file is manual.
   Keep the owner's tested GRUB/firmware fallback; boot init cannot repair a
   Limine configuration that prevented it from starting.
3. **Ownership. Stable entry IDs declared by the revision, with
   explicit begin/end comment markers for managed spans in Limine and Lua.**
   Adopt existing unmarked Pyxis entries only after a displayed owner decision;
   names alone confer no ownership. Preserve every byte outside selected spans;
   Lua spans must be complete declarative schema entries checked by the shared
   parser. Required/optional/migration declarations refer to IDs, not line numbers.
   Missing, duplicate, overlapping or ambiguous markers refuse automatic editing
   and offer printed changes; unsupported handwritten Lua stays untouched.

## Dependencies and ordered delivery

All tasks need an owner go. Progress can land independently before curses or
coexistence. No new kernel configuration model is proposed.

1. [ ] **Complete progress coverage.** Audit every installer I/O phase, including
   source/target inspection and copy verification; share reporting with plain
   output and later TUI. Capture current install/update costs, then matched runs.
   **After this task the owner can:** see which phase is active throughout an
   ordinary install/update, including verification.
2. [ ] **Shared planner/executor and revision inventory.** Extract current
   install/update behavior behind read-only plan construction, print/replay and
   one confirmation boundary; write/verify revision manifests and the Programs
   report. Retain all existing authority and interruption limits.
   **After this task the owner can:** review and replay an explicit install/update
   plan and see new/updated/removed/unchanged program sizes before any writes.
3. [ ] **Mounted read-write FAT32.** After [NVMe FAT32 engine task 2](nvme-coexistence.md#2-fat32-read-and-write-for-the-esp),
   provide a native volume/directory-capability backend, explicit bootstrap ESP
   binding, normal FILE/directory reads/writes/sync, scoped rights and exclusion
   against raw partition writers. Settle backend/boot-handoff integration before
   implementation; the deferred generic scheme-provider idea is not a mounted
   FAT32 implementation. Share the engine, without whole-ESP rewriting. Qualify
   on scratch volumes first, preserving non-Pyxis files.
   **After this task the owner can:** read and edit FAT32 files through native
   paths in Pyxis, including a delegated ESP configuration directory.
4. [ ] **Shared configuration validation and delta editor.** Under the accepted
   contract, deliver ESP configuration loading, declaration/ownership parsing,
   per-change planning, the checker and standalone editing. Share machine-setting
   validation and coordinate its writer; no live Continuum application. Qualify
   the new loading/editor on scratch ESPs until task 5 replaces destructive Update.
   **After this task the owner can:** reconcile Lua configuration against revision
   requirements on a scratch ESP, preserve unowned content and validate before saving.
5. [ ] **Owned ESP configuration publication.** Depends on
   [FAT32 task 2 and coexistence task 3](nvme-coexistence.md#tasks), the mounted FAT32
   backend, exclusive ESP write admission and the shared checker/editor. Integrate
   the accepted coexistence plan, conf swap/recovery and printed-entry fallback without
   firmware-variable edits or changes to Fedora's block. Replace ordinary Update's
   ESP reformat/regeneration too, preserving editor state and recovery copies;
   enable installed ESP config loading only with this path.
   **After this task the owner can:** plan an ordinary or coexistence update and
   apply only approved configuration changes without losing existing edits.
6. [ ] **PDCurses port and installer pages.** Native libterm backend first, then
   the page/menu frontend over the same planner/executor; no separate write path.
   **After this task the owner can:** install, update and reconcile configuration
   through the full TUI, or apply the same printed plan without it.
7. [ ] **Qualification and closure.** Disposable QEMU install/update/coexistence
   runs and the owner's native batch; confirm cancellation/dry-run writes nothing,
   stale-plan refusal, manifest diffs/read-back verification, migration/foreign-byte
   preservation, recovery fallback and progress in every phase. Use manual runs
   and existing inspection tools; a new hands-free harness needs separate go.
   **After this task the owner can:** use the qualified workflow with documented
   recovery limits. Fold this WIP into the installer/config references.
