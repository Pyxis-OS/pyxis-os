# TUI installer/updater and configuration delta editor

Status: **direction and all three configuration choices accepted by the owner,
2026-10-11.** Tasks 1–2 and task 3's shared FAT32 dependency are delivered.
Task 4 is merged and scratch-qualified
([#786](https://git.internal/PyxisOS/pyxis-os/pulls/786),
[userland #247](https://git.internal/PyxisOS/pyxis-userland/pulls/247)). Task 5 is
assigned to Codex alpha for proposal only, 2026-10-13; its decisions below are
unaccepted. Implementation and later tasks need separate owner go-aheads.
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
  **Revised 2026-10-14 (owner): progress means a progress bar, not a log** — see
  [progress display](#progress-display--accepted-2026-10-14).
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

## Progress display — accepted 2026-10-14

Task 1's records print one line per phase, so an ordinary Update scrolls dozens
of `Inspecting … bytes` lines, an inventory scan scrolls the disk selection out
of view, and the review lists every program's old and new sizes. The owner
wanted a progress bar for long work and detail only on request.
Accepted, not implemented; it applies to today's line-mode installer and
updater (one executor) and does not wait for the curses TUI:

- **One live line per long operation, redrawn in place:** phase name, a bar and
  completed/total files or bytes, plus a rate where known, for example
  `Copying programs [#########-----] 412/980 files 38 MB/s`. Without a known
  total it shows the phase and an activity indicator, never an invented
  percentage.
- **Short checks print nothing unless they fail.** The major steps (inspect,
  copy, verify, switch the ESP, clean up) each leave one finished line.
- **Detailed phase records move to the kernel log** (`log`) instead of the
  screen, so diagnosis keeps the same detail. The curses TUI later draws its
  progress page from the same records.
- **Verbose output is opt-in.** Per-phase records, the per-program old/new
  list and similar detail print only with a verbose flag (`-v`) or the existing
  **print plan** action, never in the normal flow.
- **The review is a summary that fits on one screen:** the target disk, counts
  of updated, new and removed programs with total sizes, what is kept and what
  is replaced, the recovery limits and the action prompt.
- **Nothing scrolls a choice out of view.** A question asked during selection,
  such as the read-only inventory scan of a revision without a manifest, runs
  under the same quiet progress line, and the selection is shown again before
  the next prompt.

### Quiet progress implementation scope — assigned 2026-10-14

Codex alpha implements this revision in today's line-mode installer/Update,
without changing planning, consent, storage I/O, verification or installed
activation gates. Keep the shared progress records; replace their screen sink
with one in-place bar/activity line, hide short checks, retain finished major
steps, add `-v`, summarize Review, and re-show the selected target before any
post-scan question. Printed plan bytes remain data-only on stdout; human output
remains on stderr. QEMU qualification uses disposable Install/Update targets.

**One authority decision pending:** the existing `log` resource is read-only
([LOG_RIGHT_READ](../../include/abi/log.h)); it cannot accept phase records.
Trusted `init-install` already holds Caelum's console WRITE grant, whose writes
enter the kernel log ring and serial output
([console_write](../../kernel/object/console.c)).

**Recommended default:** explicitly delegate that existing console with WRITE
alone to the trusted installer as `installer_diagnostics`. Send detailed phase
boundaries and coalesced counters there; never expose the grant to ordinary
programs. Local progress and errors still use the install space's own output.
Diagnostic failure disables that observer without changing confirmed I/O or
results; Review-output failure still prevents apply. This adds no kernel API or
LOG write right. The alternative is a separately designed kernel log writer,
which would expand this presentation task's public authority surface.

No implementation until this delegation choice is accepted.

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
[configuration checker](#configuration-checker); machine keys use the
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

## Configuration checker

The shared static checker was accepted within task 4 (2026-10-13) and is
implemented for scratch validation. The ordinary `config validate` bundle shares
the startup schemas and reports merged provenance and available CPU checks
([reference](../userland/configuration-editor.md)). Applying a change to a live
system stays deferred; in the [Asterism](spaces.md#asterism) direction the checker
is the validation half of **Polaris**, which later also applies configuration
changes to the Continuum supervisor. The checker reads the
[boot configuration](../userland/init.md#boot-configuration) without blocking
anything, for use while editing `system://config/boot.lua`, and reports problems
such as more than one network owner, invalid volumes, a `start` outside a space's
roots and CPUs that are not present. It shares parsing and validation with boot
init so the two cannot disagree, separates static checks from checks against this
machine's CPUs and volumes (saying "not checked" where it lacks the authority to
look), and shows the merged result: replaced entries and which spaces would start.

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
5. [ ] **Owned ESP configuration publication — task 5 proposal assigned to Codex alpha,
   2026-10-13; no implementation authorization.** Depends on
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
implemented. The editor consumes the delivered trusted mount workflow/ESP views
and shares their binding, exclusion and lifetime.
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

The baseline predates literal admission, shared schemas, ownership/catalog deltas
and complete-set selection. Those now live in [libconfig](../../userspace/libconfig/)
and the [editor](../userland/configuration-editor.md); existing Update still
regenerates the ESP until task 5.

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

## Task 5 proposal — pending owner decisions, 2026-10-13

Proposal only: #786 and userland #247 are merged. No task-5 implementation or
installed activation is authorized. The [current baseline](../development/experiments/installer-publication/README.md)
records the existing Install/Update paths before changes; native timing remains
an owner-present measurement, not inferred from QEMU.

The owner's requirement is binding, verbatim:

> "the installer will still work by default, and update by default without having to configure with this"

Install and Update require neither prior `config` use nor a separate editing
step. With no owner edits, normal Update completes with the selected revision's
defaults. Review shows required deltas with their safe default pre-selected;
accepting Review is the ordinary path. Missing catalog/record means unknown
history and archive defaults, never "configure first". Unknown history does not
certify overwriting owner edits or unowned boot-manager entries. A damaged FAT
filesystem or unavailable writer is a storage refusal, not a demand to configure.

### Buildable scope and activation gates

After acceptance, task 5 can build the shared installer/editor plan integration,
read-only snapshots and migration review, frozen boot/session/network handoff,
boot-compatible completion records, and incremental owned-file execution on
**disposable scratch images**. It can qualify default/no-history planning,
unsupported Lua, stale/cancel refusal, recovery-state selection and small-volume
functional publication before epsilon's cost fix. These checks must not be used
as proof that the real installer ESP is ready.

Installed activation and the shipped replacement for destructive Update are
blocked on [Configuration publication lease cost](../technical-debt.md#configuration-publication-lease-cost).
Epsilon investigates after the USB read-speed work. Requalify the complete path
on the installer's actual 512 MiB mirrored-FAT profile, representative larger and
occupied ESPs, and the owner's native target. Include cold admission, staging,
exclusive revalidation, prepared/swap/commit and older-recovery cleanup. All must
fit existing operation bounds and the non-extending 30-second interval with
measured headroom. Do not lengthen deadlines, renew the interval in a loop,
remove validation or silently retry to make the gate pass.

[NVMe coexistence task 3](nvme-coexistence.md#3-installer-coexistence-mode) is a
second real gate for internal-disk/coexistence installation: partition-first
write authority and the narrow GPT append operation are not implemented today.
A FAT mount alone does not supply them. Classic VirtIO/qualified USB scratch
integration can proceed independently; the internal ThinkPad path and its
Fedora preservation qualification wait for that authority. Native writes still
require the owner's separate presence/approval and explicit NVMe write option.

Before release, offer Daybreak an adversarial pass over publication order,
compatibility checks, stale-plan refusal and every recovery state below. Review
is additional evidence, not a substitute for scratch or native qualification.

### One plan and the native lifetime boundary

Extend the existing installer plan with the config editor's choices and consumed
bytes/hashes, source catalog, selected GPT facts, legacy-import provenance,
boot-file ownership and expected published state. No second current-plan slot,
service, shell wrapper or live Continuum operation. The ordinary installer
Review/confirmation covers programs, migration and configuration together.
Printed replay carries those data, no new authority; any changed input needs a
new plan/Review rather than automatic amendments.

Use native FAT32 RO views for preparation. Freeze every consumed FILE and its
identity before releasing references. Close files/directories/cwd/delegated
roots, explicitly retire the drained planning instance, then request the existing
classic whole-disk write admission. Closing a control does not retire it; BUSY
retirement stops before writes and never revokes another owner. Use the shared
FAT reader under the initial raw claim to revalidate the same consumed bytes
before **any program write**; no new ad-hoc FAT parser. Checking only later at
ESP publication would weaken the accepted stale-plan refusal guarantee.

Preserve offline/live-image Update, npfs admission, program sync/verification,
manifest publication and current/previous program retention. After releasing the
initial claim and staging programs, the classic released writable DISK can admit
its selected ESP through existing native FAT32 RW admission. Replace the ESP raw
claim/formatter with scoped parents, bound control and the task-4 publisher.
Repeat GPT/geometry/domain and byte checks under the final ESP interval. No
simultaneous raw ESP claim/mount, RO-to-RW upgrade, or implicit global editor
factory is introduced. Partition-first coexistence keeps its separately accepted
admission/revalidation rules; it is not permission for an internal whole-disk claim.

### Proposed owner decisions — at most three

These are defaults to discuss, not accepted requirements.

1. **Default reconciliation and personal override migration. Recommended:**
   with no complete usable ESP history, build a complete selected set from the
   revision's archive defaults, rendering only volumes/spaces into ESP `boot.lua`
   (hostname/input and protected system bootstrap stay archive-owned). If the
   existing pool `system://config/boot.lua` is literal and validates under the shared merge/schema, offer its named
   volume/space entries as the pre-selected migration, preserving owner values
   and showing the exact source/hash in Update Review. A complete compatible
   ESP set takes precedence; conflicting pool edits are displayed, never merged
   silently. Required migrations/default additions are visible and pre-selected;
   existing valid owner values stay Keep unless an explicit migration requires
   another choice. Declining a genuinely required change keeps the plan
   inapplicable; optional edits may stay unchanged.

   Unsupported handwritten Lua is never executed or rewritten. Show printed
   changes and a separately visible choice to activate archive defaults while
   retaining the original override unchanged as inert recovery; that fallback
   requires the final Review consent. Do not silently erase or deactivate an
   override. Unrecorded nonempty ESP files remain owner edits: Keep/Adopt or
   replacement is reviewed explicitly, never inferred from missing history. Where
   those files cannot safely be managed, Update may select complete archive defaults
   and leave them untouched with printed changes. It reports archive selection,
   not a committed ESP set. Review states the active source and the retained
   inactive bytes separately; Keep never silently activates or deactivates a
   source. That source change receives the same final Review consent. Missing
   history alone never demands prior `config` use.

   Keep the original pool override at its existing path, byte-identical, as
   **inactive recovery** after successful migration; record its source hash and
   disposition in the reviewed publication. No automatic deletion or reuse as a
   boot fallback. Once activated, ESP `boot.lua` is the sole editable boot source,
   with whole-archive fallback/rescue on failure. The accepted decision 2 remains
   **explicitly revisitable**: the owner may later choose ESP versus pool after
   living with task 5; this proposal does not reopen that decision.

   Alternative: refuse unsupported legacy import until the owner manually
   rewrites it. That is safer for retaining its active semantics, but would add
   a manual prerequisite for those edited installations. The recommended visible,
   confirmed archive fallback avoids an implicit "configure first" requirement.

2. **Boot-payload layout and legacy ownership admission. Recommended:** fresh
   Install retains explicit whole-target consent and may format only that target; after formatting
   it establishes Pyxis ownership and a complete default configuration in the
   same reviewed operation. Normal and coexistence Update **never format the ESP**.
   Stage kernel/archive/revision payloads at fresh revision-scoped paths under
   `EFI/pyxis`, with a fresh attempt identity inside the revision so same-revision
   updates never overwrite the last boot pair. Colliding staging paths refuse.
   Retain the current and one known previous boot payload pair.
   Unknown previous ownership skips cleanup. Remove older payloads only after
   verified activation and exact owned-manifest/hash checks, never by prefix.
   Editable configs remain at stable `EFI/pyxis/config/` paths, independent of
   `bin/REVISION`; program rollback does not roll back configuration. Plan capacity
   includes new staging, retained boot/config recovery and program reserve. Boot
   payloads use bounded native FILE copies/hashes; the config publisher's 1 MiB
   input bound is not widened to fit an archive.

   Update preserves the installed boot manager and firmware entries. Change only
   confirmed Pyxis-owned Limine spans, with previous-file recovery. A missing
   catalog may offer explicit, pre-selected adoption of an exactly recognized
   legacy Pyxis entry bound to this disk and the verified Pyxis boot paths;
   display that adoption in Review. Names alone prove nothing. If ownership or
   the existing Limine layout is ambiguous, preserve it and print/save a new
   Pyxis entry for manual adoption; report **staged, boot activation pending**,
   not an ordinary completed Update. The no-owner-edits legacy/default case must
   be recognized and complete through the normal Review path.

   Unsupported/damaged FAT refuses preservation Update and directs the owner to
   explicit recovery/host repair; it must not become permission to rebuild a
   possibly shared ESP. A destructive fallback inside Update is excluded by the
   accepted preservation direction; destructive Install remains a distinct action.
   The layout alternative is to keep canonical boot payload paths with explicit
   previous-file swaps, accepting a larger partial-boot-pair window. Recommended
   revision-scoped payloads keep the last boot pair reachable until activation.

3. **Activation, ordering and recovery. Recommended:** extend the existing
   complete-set record with the selected installed kernel/program revision and
   boot payload/catalog evidence and active source (ESP or archive), retaining
   one authority for readiness rather than an independent competing active
   pointer. Standalone editing preserves the boot binding; changing archive to
   ESP requires a newly validated complete publication. Stage/sync/read-verify
   new boot payloads and programs first. For ESP selection, prepare/publish the
   complete canonical config/catalog and commit/read-verify its record. For
   reviewed archive selection, publish only its boot/archive-bound readiness
   record: leave unowned Lua bytes untouched, certify no ESP set/default
   provenance, and make the loader explicitly select the complete archive triple.
   An existing prepared/malformed recovery record still requires manual
   disposition; it is not equivalent to absent history and is never repaired by
   blind retry. Then switch only the owned normal/rescue Limine entry last and
   sync/read-verify it. A committed readiness record alone is **not completed
   Update**: success additionally requires the reported source mode and matching
   owned boot-entry publication, with no cross-file atomicity claim.

   Fresh boot payloads retain immutable ownership/hash evidence, not another
   active pointer. Recheck their identities/content at the final publication
   boundary as well as consumed config bytes; include that cost in lease
   qualification. Retained previous files are recovery evidence, not selected
   defaults or permission to silently restore a source.

   On installed normal boot, the trusted RO boot-provenance issuer supplies the
   early ESP read, before editable config can affect selection. Archive mode
   explicitly selects the archive; ESP mode requires a complete
   matching record/triple/catalog and compatibility with the actually loaded
   kernel revision and archive declarations. Package the single generated catalog
   with trusted archive defaults for this early check, before `bin` binding; its
   bytes also supply the editor bundle, not another mutable configuration store.
   Freeze the **whole** selected set
   before effects; pass selected session/network as read-only owned snapshots
   through declared resources. Do not delegate live ESP handles or administrative
   writer authority to ordinary children. Drain/retire the early boot-reader
   instance after freezing; missing/ambiguous provenance, unsupported input or
   any mismatch selects the entire archive set. Rescue always uses archive
   defaults, never a previous set or the pool override. No firmware-variable write.

   Alternatives are in-place overwrites of the currently booted kernel/archive,
   or independent best-effort activation of each Lua file. The recommended
   staged boot paths and whole-set compatibility check avoid consuming a partly
   updated boot pair or mixing configurations while retaining manual recovery.

### Recovery states to qualify before activation

| Last confirmed point | Result and recovery |
| --- | --- |
| Plan/cancel/stale initial inputs | No target mutation; no automatic replanning. |
| New program or boot payload stage incomplete | Old boot entry remains. Report retained partial revision; existing same/unknown-revision program-stage limits still apply. |
| Prepared record or partial canonical config swap | Whole-archive fallback, retained previous files and prepared artifacts; stop without rollback/retry. |
| Committed readiness, old or partly switched boot entry | Old kernel rejects incompatible selection and uses its archive; new compatible kernel may select the complete set. Retain old boot pair and previous configuration for explicit recovery. |
| New entry confirmed, cleanup incomplete | Report committed update plus retained recovery/cleanup failure; never delete by prefix or claim cleanup succeeded. |
| Unknown write/flush completion | Stop writes, report the exact reached phase/uncertainty, retain artifacts; no automatic retry, repair, reboot offer or rollback. |

A program rollback keeps configuration separate: review that revision's catalog,
restore/reconcile compatible previous files explicitly, then publish a compatible
record and owned boot entry. Do not auto-restore settings merely by selecting an
old program directory. Retaining files is not a promise that arbitrary old/new
kernel/archive/config combinations boot.

### Native qualification — owner-present ThinkPad

1. Before candidate writes, take an **owner-approved spare USB** current-main
   Install/Update baseline: matched image, devices and journal; record preparation,
   confirmed apply, program stage, ESP stage, verification and total elapsed time.
   Keep raw logs and exact kernel/initrd hashes. Never measure today's destructive
   Update on the Fedora coexistence ESP. Current internal NVMe policy cannot do
   that Update anyway. An unavailable native baseline stays marked pending.
2. Luna stages the approved candidate after the FAT cost gate and scratch
   tests. Internal/coexistence targets additionally require coexistence task 3.
   The owner selects the exact target, approves writes and remains present.
   On the spare target, fresh Install then normal Update require no `config`
   invocation; with no owner edits the safe pre-selected revision defaults complete.
   Repeat with a real pool override, owner ESP edits, missing catalog/record,
   rescue boot and the printed-change case; compare recovered source bytes and
   selected provenance. Record actual full-profile lease headroom, not only totals.
3. Internal Fedora coexistence testing waits for coexistence task 3 and the
   owner's separate native write go. The owner backs up GPT/ESP and verifies host
   FAT health, the Fedora boot fallback and recovery medium first. Inventory every
   foreign file/path/metadata, the Fedora/plugin Limine block and foreign occupied
   extents. Rehearse the exact layout/tree on disposable QEMU media before the
   native install and Update. Compare all foreign file bytes and config spans
   byte-for-byte afterwards; check existing foreign chains/directory records and
   non-owned disk extents. Shared FAT/allocation/dirty metadata may change only
   as required for owned additions/publication; do not claim whole-ESP hash equality.
   Preserve `EFI/fedora`, the existing boot manager and firmware variables; boot
   Fedora and Pyxis after clean power/restart. Any unexplained foreign change fails
   qualification and blocks activation.
4. Exercise bounded failure, prepared/mismatching record and manual recovery on
   scratch copies first; no induced power loss or uncertain-write experiment on
   the owner's internal ESP. Stop/recover rather than extend the lease if the
   native path cannot complete. Record the owner-visible outcome and limits before
   proposing installed-default activation or removing the current Update path.
