# TUI installer/updater and configuration delta editor

Status: **direction and all three configuration choices accepted by the owner,
2026-10-11.** Task 1 is delivered. Task 2 is assigned, with its three
implementation choices accepted; later tasks still need separate owner go-aheads.
Builds on the [installer](../userland/installer.md),
[updates](../userland/system-updates.md), [system layout](../userland/system-layout.md)
and [program bundles](../userland/program-bundles.md).

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
- **Mounted read-write FAT32 remains required**, delivered independently by
  [shared FAT32 volumes](fat32-volumes.md) after its engine. The accepted native
  volume/directory backend is not deferred to a future scheme provider or owned
  by this consumer; the move completes no task.
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

Task 2 delivers the accepted per-revision inventory and Programs report. Its
format, legacy comparison and replay rules were accepted by the owner on
2026-10-11, as defaulted below. Copy
verification remains a filesystem-view check; unsigned inventory hashes neither
authenticate a revision nor prove that installed payloads were not later edited.

## Task 2 choices — accepted 2026-10-11

All three defaults accepted by the owner; implementation is authorized.

Inspection: current installer selects whole `.pxe`/`.pxb` roots, with 32 directory
levels, 65,536 entries and 255-byte component names. It has no revision inventory
or serialized plan. Update inspection uses raw read-only codecs and requires an
empty journal. Opening even a read-only npfs volume retains the pool until reboot
and prevents subsequent write admission; planning must extend the raw decoder,
not open a target volume. Disk IDs identify devices only for the current boot;
the [disk ABI](../../include/abi/disk.h) supplies no persistent hardware identity.

1. **Manifest and legacy comparison. Default: reserved
   `.pyxis-revision-manifest` in `bin/REVISION`, canonical bounded text.** A format
   tag precedes bytewise-sorted relative-path records: directories, including empty
   ones, and regular files with decimal byte size and SHA-256. Hex-encode path
   bytes so whitespace/control bytes cannot become delimiters; refuse duplicate
   paths, invalid components, unsupported kinds or inconsistent ancestors. The
   manifest excludes itself and includes the source revision; its installed file
   counts toward the tree's entry limit. Cap each manifest and serialized plan at 16 MiB, in addition to existing traversal limits;
   unusually large inventories that fit the old tree bounds may now refuse.
   Publish the manifest after copied-tree verification, sync and verify its bytes
   before the ESP switch. Destination payload/structure checks remain mandatory
   filesystem-view checks that may use cached pages, not fresh device read-back.
   Programs are compared as whole roots, including bundle contents/empty
   directories, with old/new payload sizes; Install lists all selected roots as
   new. A valid old manifest describes the installed revision, not a live audit
   of later payload edits. Missing/invalid manifests on a known previous revision
   require explicit consent to a one-time raw read/hash inventory with progress;
   declining refuses that comparison. Unknown previous revisions report unknown,
   never unchanged, and retain the existing rebuild/cleanup limits.
   **Alternative:** a length-prefixed binary format and refusal of legacy
   comparison until an inventory exists. Text is inspectable; explicit legacy
   scanning keeps current installations updateable without silently trusting them.
2. **Replay and target identity. Default: strict data-only text over standard
   input/output, using the same in-memory plan as interactive Review.** `--plan`
   constructs/reports/prints without applying; `--apply` reads a plan, displays its
   Review and requires `wipe` or `update`. Non-interactive apply supplies that
   explicit consent separately; `--dry-run` never acquires write access. No plan
   command executes embedded instructions, creates a saved file or obtains new
   authority; a caller can retain the printed bytes with its own grants.
   Serialized apply requires one uniquely observed validated existing GPT disk
   GUID, geometry and expected inspection state. Never resolve by inventory
   number alone; duplicate GUIDs refuse. This binds a logical disk, not an
   unforgeable physical identity. Blank/damaged targets without usable
   identity support printed/dry-run plans but apply only through the held target
   in the same interactive invocation. New Install GUIDs/journal choices are
   recorded once, not regenerated on replay. **Alternative:** add a persistent
   hardware-identity contract first, or require an explicit target rebinding and
   a new Review. The default permits portable replay where identity is available
   without expanding the disk ABI or guessing which blank stick was reviewed.
3. **Staleness and execution. Default: refuse changed inputs before the first
   write, with one executor and no automatic replanning.** Record hashes of all
   source payloads/assets, the resulting inventory/boot configuration, selected
   layout and existing admission/consent state, prior manifest and inspected ESP
   state. Reconstruct and compare the relevant inspection under exclusive raw
   ownership; include npfs header/control/catalog and metadata used for the
   report. A synthesized legacy inventory also requires rereading its payload
   hashes there. Read-only planning has no snapshot guarantee, so inconsistent
   scans refuse. A valid old manifest does not trigger a full old-payload audit;
   its hash cannot detect subsequent edits to uninspected file contents. Check
   available space conservatively before mutation; limits/allocation failure
   refuse rather than make a fit promise. Preserve existing claim/release,
   programs-before-ESP, sync/verification, partial-failure and retention contracts;
   dry-run/cancel never mount, persist replay, format or clean up.
   **Alternative:** always rehash all old installed payloads at planning and
   apply. The default avoids that recurring I/O while stating exactly what the
   Programs report proves; legacy comparison pays the scan cost explicitly.

Before implementation, capture current Install/Update costs on fresh matching
targets. Repeat matched runs after implementation, separating planning/hashing,
copying, verification and sync. Interactive QEMU checks cover install/update,
legacy inventory, manifest reports, print/replay, stale-plan refusal and
cancel/dry-run with unchanged target bytes. No new harness, FAT32/config editor
or stronger device-read-back contract is part of task 2.

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

Progress coverage can land independently before curses or coexistence. No new
kernel configuration model is proposed.

1. [x] **Complete progress coverage — delivered 2026-10-11.**
   Shared observational phase/counter records cover source/target inspection,
   copying, filesystem-view verification, raw EFI read-back, sync and cleanup.
   [Reference](../userland/installer.md#progress) and
   [matched QEMU costs](../development/experiments/installer-progress/README.md).
   Program verification may use cached npfs pages; stronger device read-back
   needs a separate owner decision. No TUI or verification behavior change.
   **After this task the owner can:** see the active phase throughout an
   ordinary install/update, including verification.
2. [ ] **Shared planner/executor and revision inventory — assigned 2026-10-11.**
   Apply the [accepted choices](#task-2-choices--accepted-2026-10-11) and
   extract current install/update behavior behind read-only plan construction, print/replay and
   one confirmation boundary; write/verify revision manifests and the Programs
   report. Retain all existing authority and interruption limits.
   **After this task the owner can:** review and replay an explicit install/update
   plan and see new/updated/removed/unchanged program sizes before any writes.
3. **Shared dependency, former mounted FAT32 task (moved, not completed).**
   Consume [FAT32 volumes](fat32-volumes.md) (native read-write files, trusted
   bootstrap ESP binding, scoped rights, sync/release, raw-writer exclusion); no
   duplicate engine/backend task here. The editor keeps its accepted config
   ownership/confirmation and ordinary-Update preservation obligations.
   **After that shared milestone the owner can:** read/edit/sync authorized FAT32
   files independently of the TUI/configuration editor.
4. [ ] **Shared configuration validation and delta editor.** Under the accepted
   contract, deliver ESP configuration loading, declaration/ownership parsing,
   per-change planning, the checker and standalone editing. Share machine-setting
   validation and coordinate its writer; no live Continuum application. Qualify
   the new loading/editor on scratch ESPs until task 5 replaces destructive Update.
   **After this task the owner can:** reconcile Lua configuration against revision
   requirements on a scratch ESP, preserve unowned content and validate before saving.
5. [ ] **Owned ESP configuration publication.** Depends on
   the [shared FAT32 engine/backend](fat32-volumes.md) and
   [coexistence task 3](nvme-coexistence.md#tasks), exclusive ESP write admission
   and the shared checker/editor. Integrate
   the accepted coexistence plan, conf swap/recovery and printed-entry fallback without
   firmware-variable edits or changes to Fedora's block. Replace ordinary Update's
   ESP reformat as described under plan and apply; enable installed ESP config
   loading only with this path.
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
