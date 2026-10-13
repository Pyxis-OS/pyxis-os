# TUI installer/updater and configuration delta editor

Status: **direction and all three configuration choices accepted by the owner,
2026-10-11.** Tasks 1–2 and task 3's shared FAT32 dependency are delivered.
Task 4 is implemented and scratch-qualified for review
([#786](https://git.internal/PyxisOS/pyxis-os/pulls/786),
[userland #247](https://git.internal/PyxisOS/pyxis-userland/pulls/247)); later tasks
still need separate owner go-aheads.
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
- **Mounted read-write FAT32 is delivered independently** by
  [shared FAT32 volumes](../devices/fat32-volumes.md), including trusted selection
  and declared editor grants. The native backend is not a scheme provider or
  owned by this consumer; configuration/editor integration remains here.
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

## Task 2 choices — accepted 2026-10-11

The owner accepted the text SHA-256 manifest with explicit legacy scanning,
standard-stream replay bound to a unique validated GPT logical identity, and
stale-plan refusal under exclusive revalidation. The implementation lives in the
[installer reference](../userland/installer.md#plans-and-programs-report) and
[revision inventory](../userland/installer.md#revision-inventory).

Manifests include sorted paths, kinds, empty directories, sizes and hashes;
metadata is bounded at 16 MiB alongside traversal limits. Programs are whole
selected roots with old/new payload sizes. Missing/invalid manifests need
explicit raw read/hash consent; unknown history stays unknown. A valid manifest
is historical inventory, without a recurring full old-payload audit.

Printed plans convey no authority. Replay requires unique validated existing
GPT identity and matching geometry/state, rather than an enumeration number.
Blank/unidentified targets remain interactive-only for apply. Install IDs and
journal choices are recorded once. Preparation/cancel/dry-run never mount,
persist replay or write. One executor rechecks consumed sources, target metadata,
ESP and prior inventory before writing, refuses stale plans without automatic
replanning and conservatively checks capacity without promising allocation.

Programs remain synced/verified before the ESP switch; verification may use
cached filesystem pages. Existing interruption, retention and whole-ESP limits
remain. This task provides no FAT32 mount/config editor or stronger device
read-back contract. [Matched costs and qualification](../development/experiments/installer-plan/README.md)
record the QEMU evidence and its limits.

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
2. [x] **Shared planner/executor and revision inventory — delivered 2026-10-11.**
   Read-only plan construction, print/replay and one confirmed executor implement
   the [accepted choices](#task-2-choices--accepted-2026-10-11). Revision manifests
   and whole-program sizes are published/verified before the ESP switch; existing
   authority and interruption limits remain. [Reference](../userland/installer.md#plans-and-programs-report)
   and [QEMU qualification/costs](../development/experiments/installer-plan/README.md).
   **After this task the owner can:** review and replay an explicit install/update
   plan and see new/updated/removed/unchanged program sizes before any writes.
3. **Shared dependency delivered: mounted FAT32.**
   Consume [FAT32 volumes](../devices/fat32-volumes.md) (native read-write files, trusted
   bootstrap ESP binding, scoped rights, sync/release, raw-writer exclusion); no
   duplicate engine/backend task here. The editor keeps its accepted config
   ownership/confirmation and ordinary-Update preservation obligations.
   **With this delivered dependency the owner can:** read/edit/sync authorized FAT32
   files independently of the TUI/configuration editor.
4. [x] **Shared configuration validation and delta editor — delivered by Codex alpha,
   2026-10-13; scratch publication/replay qualified.** Under the accepted
   contract, deliver ESP configuration loading, declaration/ownership parsing,
   per-change planning, the checker and standalone editing. Share machine-setting
   validation and coordinate its writer; no live Continuum application. Qualify
   the new loading/editor on scratch ESPs until task 5 replaces destructive Update.
   **After this task the owner can:** reconcile Lua configuration against revision
   requirements on a scratch ESP, preserve unowned content and validate before saving.
5. [ ] **Owned ESP configuration publication.** Depends on
   the [shared FAT32 engine/backend](../devices/fat32-volumes.md) and
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

## Task 4 baseline and decisions — 2026-10-13

Task 4 only; the functional baseline preceded implementation. Native FAT32 read/write mounts and the raw writer are
implemented. Epsilon owns the task-5 mount workflow/ESP views; this consumer must
share its binding, exclusion and lifetime rather than create a second mount
workflow. Grant names/provisioning have been requested through the orchestrator.
Only scratch ESP loading/editing is qualified here; ordinary Update and installed
ESP activation stay unchanged until installer task 5.

### Functional baseline

Current main `2f2109fc`, userland `9f781e94`, fs `414be0ac`, ports `f93becb9`.
Used the successful current-main CI image (Forgejo run ID 2172), without rebuilding
or measuring timing while the owner's LLVM build saturates horse. Kernel SHA-256
`550306b410a9211ebaa2cb739d5d7e8af26f55807576aab1754b6cea8c6f5850`;
initrd `87ffe577483ddd5fba107ea0ea296ecf711a9d3a90446cebce05c89a48e43441`.

Manual QEMU 10.2.2, Q35/nested KVM, host CPU, four CPUs, 512 MiB, matching fresh
OVMF, modern VirtIO block/RNG, PS/2, software GOP 1280x800, no NIC/USB. A disposable
160 MiB GPT disk held a 128 MiB ESP with boot/session/network Lua, Limine text and
synthetic foreign bytes. The existing trusted read-only scratch browser selected
disk 1/entry 1 and read all five files through native FAT32. Reported no failure;
QEMU showed zero scratch writes. Before/after whole-disk SHA-256 was identical
(`8546e815826917b25bbe329acfcde4786502e730c009bda848733fa8438e0d46`), and the
scratch FAT image passed `fsck.fat -n`. All QEMU jobs stopped. No owner media used.

Source inspection: [libconfig](../../userspace/libconfig/config.c) runs restricted
Lua and loses source spans; it permits computation and has no execution budget.
Boot schema/merge are in [boot-init](../../userspace/boot-init/config.c), session
schema in [session config](../../userspace/session/config.c), and network decoding
is mixed with [runtime networking](../../userspace/session/network.c). There is
no declaration catalog, delta/checker/editor or complete-set selection record.
Existing Update still regenerates the ESP. Native mounting/reads above are runtime
results; these missing facilities and validation boundaries are source findings.

### Task 4 round one — accepted 2026-10-13

1. **Input and validation accepted: declarative admission for the new ESP path.**
   Parse literal `return { ... }` data: tables, explicit keys/array entries,
   strings, numbers, booleans and comments, with duplicate-key/structure checks;
   no calls, loops, functions, metatables or migration execution. Recognize
   ownership markers as real comments, never marker-looking string bytes.
   Unsupported handwritten Lua stays untouched and receives printed changes;
   automatic validation/edit/apply refuses to certify it. Keep the existing
   trusted archive evaluator for recovery/legacy inputs, but extract shared typed
   schema/merge validation so boot, checker and editor use the same constraints.
   Separate source validity from unavailable CPU/volume/zoneinfo checks, with
   source/entry/reason diagnostics and merged-entry provenance. Include static
   network address/subnet/gateway constraints without applying networking.
   Reuse the existing machine hostname/scroll validators and paths; no separate
   schema/store, machine-setting writer, Polaris or Continuum implementation.

2. **Boot meaning and selection accepted, revisitable: preserve the current override model.**
   ESP `boot.lua` replaces the current pool override input: volumes/spaces only,
   by-name whole-entry merge with archive installed defaults; no editable `system`
   bootstrap binding or competing hostname/input store. Display and confirm the
   existing pool override's migration into that file. Session/network files are
   complete inputs with their existing defaults, not per-file fallback chains.
   Validate the whole Lua triple before effects and freeze the selected bytes;
   later sessions receive explicitly declared READ snapshots, not independently
   reopened ESP paths. Missing/corrupt/unsupported/incomplete selection uses the
   complete archive-default triple, with rescue always selecting it. Task 4 builds
   and scratch-qualifies the loader; installed activation waits for task 5.
   **Owner may revisit:** where the editable boot configuration lives (ESP
   versus the npfs pool), after living with task 5's installed activation. The
   present accepted contract migrates the owner's personal pool override to the
   ESP and makes ESP editing the configuration path. See the
   [revisitable location limit](../technical-debt.md#editable-boot-configuration-location).

3. **Declarations and deltas accepted: a revision-owned data catalog.**
   Ship one literal data catalog beside installer source assets, with stable ID,
   file/schema target, required/optional disposition, default and optional explicit
   migration per entry. Targets are complete volume/space/settings entries or
   complete Limine entries. Marker syntax is `-- pyxis:begin ID` / `-- pyxis:end ID`
   for Lua and `# pyxis:begin ID` / `# pyxis:end ID` for Limine; entry separators
   belong inside the span. Missing/duplicate/overlapping/ambiguous markers offer
   printed changes; unmarked adoption requires an explicit displayed choice.
   Preserve outside bytes exactly. Migrations are declared entry/field moves or
   explicit typed replacements reviewed individually, not executable scripts.
   Plans record each choice, expected consumed bytes/hashes and source catalog;
   declined required changes make the plan inapplicable. Optional changes may
   be declined; changed owner values are never replaced just because a default
   changed. Old defaults require a supplied/preserved old catalog: without one,
   provenance is unknown and existing values are conservatively owner edits.
   Do not invent old defaults from the current archive or programs manifest.

### Task 4 round two — accepted 2026-10-13

All three defaults are accepted. These settle the command, authority/lifetime
and recovery boundaries; round one is not reopened.

1. **Standalone surface accepted: normal `config` bundle invocation.**
   `config validate` checks the supplied selection and reports static errors,
   merged-entry provenance and unavailable machine-dependent checks.
   `config plan` prints a data-only delta plan; `config edit` uses the existing
   plain libterm review/prompt flow for each change and final Apply/Print/Cancel.
   `config apply` consumes one framed plan on stdin, revalidates it and requires
   final confirmation; `--confirm=config` supplies that consent non-interactively.
   Human diagnostics/progress use stderr; printed plan bytes use stdout. All
   surfaces share the existing installer planner/executor family; no current-plan
   global slot, separate service, curses frontend or live Continuum application.
   The declaring bundle requests read-only source/catalog views and optional
   administrative writable config parents/bound control. Without writer authority
   validation/planning still work; write modes refuse. Trusted administrative or
   scratch startup provides grants where allowed. No call-site wrapper/prefix,
   raw-disk/mount-acquisition grant or implicit forwarding to ordinary children.
   Machine `config get/set/list` and its npfs writer remain their separately
   assigned task, using the same machine validators rather than competing stores.

2. **Target/replay accepted: stable, issuer-bound ESP evidence.**
   Reuse the accepted plan identity model: normalized GPT disk/ESP UUIDs,
   sector geometry/partition extent and expected consumed bytes/hashes. Epsilon's
   trusted selected-view issuer must supply read-only facts tied to its root and
   bound FAT control; the editor checks current root/control domain/extent too.
   UUIDs name a logical target, not authenticated physical hardware. No raw disk
   access is given to the editor. MOUNT_OBSERVE supplies retained GPT identity and
   device geometry with explicit validity; ordinals and names alone do not fill that gap.
   Missing/ambiguous evidence permits read-only validation/printed diagnostics,
   but refuses publication/replay. A printed plan grants no authority: apply
   needs the independently supplied matching writable view/control and refuses
   stale source, target or file bytes without automatic replanning.

3. **Publication/provenance accepted: complete-set record and one previous set.**
   Retain the current catalog beside the committed configuration, with its exact
   bytes/hash and source revision; later deltas can distinguish old defaults from
   edits. Missing/corrupt history remains unknown. Keep one complete previous Lua
   set/catalog and recovery copies of any changed Limine file. Stage new files,
   sync and read-verify before switching, using the existing native FAT32 writer.
   Under a bounded exclusive interval, recheck expected current bytes, flush a
   prepared record before the first canonical-name swap, publish only approved
   changes with retained recovery files, then flush a committed record naming the
   exact selected triple/catalog hashes. This is completion detection, not a FAT
   journal or cross-file atomicity. The loader requires a matching complete record;
   absent, prepared, malformed or mismatching state selects the complete archive
   defaults. Previous Limine recovery remains manual.
   Failure/deadline/uncertainty stops writes, retains artifacts and prints the
   reached phase/recovery names; no blind rollback, retry or resume. Cleanup of an
   older previous set occurs only after committed publication, and only for
   verified owned artifacts named in that record, never by filename prefix.
   Scratch-only qualification until task 5 replaces destructive Update; no installed
   loader activation or personal-pool deletion in task 4.

### Ordinary Install/Update remains the default path

Owner requirement accepted 2026-10-13: "the installer will still work by default,
and update by default, without having to configure with this".

Install and Update require **no prior use of `config` and no manual configuration
step**. Task 4 changes nothing in installed Install/Update: its new loader/editor
is scratch-only. When task 5 integrates configuration publication, normal Update
with no owner edits completes with the revision's defaults. Required changes
are shown in its existing review, with the safe default pre-selected; accepting
the review is the ordinary path, never a prerequisite `config` invocation.

A missing/absent catalog or completion record does not block Update. The loader
uses the complete archive-default triple; unsupported or unknown-provenance
edits receive printed changes while their bytes remain protected. Task 5 may
use that archive/printed-change path without asserting that an unrecorded value
was an old default or silently adopting unowned spans. Lack of provenance still
prevents the standalone editor from certifying an unsafe overwrite; it must
never turn into "configure first" for ordinary Install/Update. The committed
record gates selected ESP overrides, not the ability to install/update programs.
This is consistent with whole-triple archive fallback and preserves rescue.

### Bound ESP evidence — delivered by #790 / userland #249

The native FAT32 issuer exposes immutable selected-target facts
through the already bound MOUNT control's OBSERVE reply, requiring its existing
OBSERVE right. This keeps facts tied to the issuer's actual mounted instance,
not a caller-authored sidecar or a raw-DISK grant. It retains domain, device,
GPT-entry ordinal, first-block, block-count and lifecycle flags alongside:

| Fact | Source at mount admission |
| --- | --- |
| `disk_guid[16]`, raw GPT bytes | Validated `fat32_volume.disk_guid`, cross-checked against the kernel's healthy GPT snapshot and selected disk GUID. |
| `partition_guid[16]`, raw GPT bytes | Validated `fat32_volume.partition_guid`, cross-checked against the selected GPT entry and boot-origin UUID where applicable. |
| `partition_type[16]`, raw GPT bytes | Validated `fat32_volume.partition_type`; the config publisher verifies ESP type, rather than trusting a root name. |
| `partition_attributes`, 64 bits | Validated `fat32_volume.partition_attributes`, already matched to the selected GPT entry. |
| `device_block_size`, bytes | Admitted `block_info.block_size` / `fat32_volume.source.sector_bytes`. |
| `device_block_count` | Admitted `block_info.block_count` / `fat32_volume.source.sectors`; distinct from the existing selected partition's block count. |
| `MOUNT_STATE_IDENTITY_VALID` flag | Explicit validity in the OBSERVE contract/flags: zero, unavailable or unsupported facts must not be fabricated; clients refuse publication/replay without complete facts. |

The UUID byte order is the existing on-disk GPT convention; the client uses the
shared GUID formatting rules for normalized plan text. Names, paths, configured
selectors and ordinal numbers never substitute for these fields. Kernel
`kernel/fs/fat32.c` already cross-checks the admitted library volume against the
healthy snapshot before publication; OBSERVE can copy the retained immutable
volume/source data, not re-read raw media on each query. Preserve retired/failed
state reporting, and never certify a tombstone as an active publication target.
Metadata from a held directory must match the control's live domain; stable
UUIDs remain logical target identity, not physical-device authentication.

The public ABI and libpyxis helper validate the reply together. The client owns
consumed file/catalog hashes and revision checks; those are not issuer facts.
There is no new factory, selector, grant class or writer right.

### Task 4 delivery — 2026-10-13

[userland #247](https://git.internal/PyxisOS/pyxis-userland/pulls/247) and
[parent #786](https://git.internal/PyxisOS/pyxis-os/pulls/786) deliver shared typed
validation/literal admission, catalog/marker deltas, declared scoped sources,
frozen selection, replay and native FAT32 complete-set publication. They consume
the merged bound identity ABI/helper (#790 / userland #249).
The [reference](../userland/configuration-editor.md) describes the contract;
the [scratch record](../../userspace/config-editor/qualification/README.md)
distinguishes builds, source inspection, QEMU/GDB and remaining native limits.

Confirmed edit/adoption, committed loading, replay, stale target/source/catalog
refusal, required decline, explicit migrations, foreign-byte preservation and
previous-set cleanup are scratch-qualified. A larger scratch volume exhausted
the native publication lease and stopped with retained recovery; this cost is
[debt](../technical-debt.md#configuration-publication-lease-cost), not permission
to extend the lease or activate installed publication.

No native media was touched. Installed startup, personal pool overrides and
ordinary Install/Update remain unchanged; task 5 owns their activation. Normal
Install/Update requires no prior `config` invocation or manual setup.
