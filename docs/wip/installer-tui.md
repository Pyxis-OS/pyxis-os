# TUI installer/updater and configuration delta editor

Status: **direction and all three configuration choices accepted by the owner,
2026-10-11.** Tasks 1–2 and task 3's shared FAT32 dependency are delivered.
Task 4 is merged and scratch-qualified
([#786](https://git.internal/PyxisOS/pyxis-os/pulls/786),
[userland #247](https://git.internal/PyxisOS/pyxis-userland/pulls/247)). Task 5 is
assigned to Codex alpha: all three task-5 defaults accepted **2026-10-13**.
Implementation is authorized for the buildable scratch scope only; installed
activation and replacement of shipped Update remain blocked on the gates below.
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
The current line-mode executor accepts Cancel before Review confirmation only;
confirmed Apply, including synchronous payload staging and verification, does
not poll cancellation input and can retain partial staged artifacts if interrupted.

## Progress display — accepted 2026-10-14

Implemented in the line-mode installer and updater (one executor), independently
of the later curses TUI: one bounded live bar/activity line for long work,
silent short checks, finished major steps, opt-in `-v`/Print details, a normal
Review summary and selected target re-shown before later questions. Printed
plans remain data-only on stdout; human output remains on stderr. Planning,
consent, storage, verification and installed-activation gates are unchanged.
See the [implemented progress contract](../userland/installer.md#progress) and
[QEMU record](../development/experiments/installer-progress/README.md#quiet-display-revision--qemu-2026-10-14).

**Delegation accepted by the owner, 2026-10-14.** Trusted `init-install`
explicitly delegates its existing Caelum console with WRITE alone as
`installer_diagnostics`. Only the trusted installer receives it. Detailed phase
boundaries and coalesced counters enter the existing kernel log ring/serial
path, while local progress and errors use the install space's own output.
Diagnostic failure disables that observer without affecting confirmed I/O;
Review-output failure still prevents apply. No LOG write right or new kernel API
is added. A general authority-scoped userspace log writer remains
[technical debt](../technical-debt.md#userspace-kernel-log-writer).

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

1. [x] **Complete progress coverage — delivered 2026-10-11; quiet display revision
   delivered 2026-10-14.**
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
5. [ ] **Owned ESP configuration publication — task 5 assigned to Codex alpha,
   2026-10-13; buildable scratch scope authorized.** Installed activation remains
   blocked on the gates below. Depends on
   the [shared FAT32 engine/backend](../devices/fat32-volumes.md) and
   [coexistence task 3](nvme-coexistence.md#tasks), exclusive ESP write admission
   and the shared checker/editor. Integrate
   the accepted coexistence plan, conf swap/recovery and printed-entry fallback without
   firmware-variable edits or changes to Fedora's block. Replace ordinary Update's
   ESP reformat as described under plan and apply; enable installed ESP config
   loading only with this path.
   **Buildable checkpoint:** [userland #253](https://git.internal/PyxisOS/pyxis-userland/pulls/253)
   and [#797](https://git.internal/PyxisOS/pyxis-os/pulls/797) implement frozen
   reconciliation, private READ-only RAM FILE handoff, boot-bound records and
   build-gated incremental publication. Cancel/stale/unsupported-input checks
   pass. The earlier 40 MiB timeout predates the delivered FAT32 core and
   once-per-publication CHECK. The rebased caller now uses the shared ESP payload
   helper. The epoch read fix reduces the matched 40.4 MB hash from 410 s to
   about 13 s. The accepted append-proof/batching follow-up reduces a complete
   matched Apply from 789.62 s to 615.84 s. The subsequently accepted bounded
   FILE transfer, indexed preparation and device-sized runs reduce the same
   40.49 MB archive workload to 19.85 s complete Apply and 7.17 s within the
   unchanged 30-second interval; both final hashes pass, with owned entry
   activation confirmed. A final ownership/error-hook confirmation takes
   22.22 s / 8.62 s and the same device-write totals. Device writes fall from
   101.59 MB / 79,817 requests / 45,595 flushes to
   46.96 MB / 1,081 requests / 294 flushes. These are nested QEMU results,
   not native-media or power-loss qualification. The reusable core is
   [#895](https://git.internal/PyxisOS/pyxis-os/pulls/895),
   [userland #276](https://git.internal/PyxisOS/pyxis-userland/pulls/276), and
   [fs #42](https://git.internal/PyxisOS/pyxis-fs/pulls/42), after fs #41/#40.
   Representative occupied/larger ESPs, full-profile integration, recovery-pair
   cleanup and native qualification remain release gates. The publisher PRs
   stay draft; task 5 remains unchecked and Daybreak reviews before release.
   [Qualification record](../development/experiments/installer-publication/README.md#buildable-scope-checkpoint).
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
previous-set cleanup are scratch-qualified. The earlier larger-volume lease
failure is retained in the scratch record. The delivered FAT32 retained-allocation
core and once-per-publication CHECK now allow the actual 128 MiB editor
publication to complete within the unchanged lease
([qualification](../development/experiments/fat32-preflight-cost.md#actual-128-mib-editor-publication-2026-10-14)).
This does not qualify task 5's full installer profile or authorize installed
activation.

No native media was touched. Installed startup, personal pool overrides and
ordinary Install/Update remain unchanged; task 5 owns their activation. Normal
Install/Update requires no prior `config` invocation or manual setup.

## Task 5 — accepted contract, 2026-10-13

#794 is merged. The owner accepted all three defaults on **2026-10-13**
and authorized the buildable scratch scope below. Installed activation and the
replacement of shipped destructive Update are not authorized. The [current baseline](../development/experiments/installer-publication/README.md)
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
filesystem or unavailable writer is a storage refusal, not a demand to configure. Normal
Update never formats a healthy ESP. The owner's accepted 2026-10-15
[classic recovery route](nvme-coexistence.md#classic-update-recovery--accepted-by-the-owner-2026-10-15)
is separate: explicit Review and consent can issue a single-use, snapshot-bound
ESP-only rebuild grant for a selected classic installation. Coexistence never
receives it; shared Fedora or Windows ESPs are never rebuilt.

### Buildable scope and activation gates

The authorized scope builds the shared installer/editor plan integration,
read-only snapshots and migration review, frozen boot/session/network handoff,
boot-compatible completion records, and incremental owned-file execution on
**disposable scratch images**. It can qualify default/no-history planning,
unsupported Lua, stale/cancel refusal, recovery-state selection and functional
publication. These checks must not be used as proof that the real installer ESP
is ready. The [shared payload stager](../../userspace/installer/ESP-PAYLOADS.md)
remains a separate dependency: [userland #272](https://git.internal/PyxisOS/pyxis-userland/pulls/272)
and [#880](https://git.internal/PyxisOS/pyxis-os/pulls/880) precede task-5
[userland #253](https://git.internal/PyxisOS/pyxis-userland/pulls/253) and
[#797](https://git.internal/PyxisOS/pyxis-os/pulls/797).

Classic Install/Update keep their fixed boot paths until this task delivers the
committed boot binding (owner decision relayed by epsilon, 2026-10-15).
Coexistence uses immutable attempts and manual entries meanwhile; the stager's
ownership manifest is not active-binding authority.

The FAT32 core and once-per-publication CHECK are delivered. The current
publisher runs CHECK immediately after BEGIN, before interval revalidation and
publication effects. Its successful 128 MiB editor qualification does not clear
[task 5's publication lease gate](../technical-debt.md#configuration-publication-lease-cost).
Installed activation and the shipped replacement for destructive Update remain
blocked on complete-path qualification using the installer's actual 512 MiB
mirrored-FAT profile, representative larger and occupied ESPs, and the owner's
separately approved native target. Include cold admission, staging,
exclusive revalidation, prepared/swap/commit and older-recovery cleanup. All must
fit existing operation bounds and the non-extending 30-second interval with
measured headroom. Do not lengthen deadlines, renew the interval in a loop,
remove validation or silently retry to make the gate pass.

### Write-cost decisions — accepted 2026-10-15

The long clean-control run used the pre-epoch kernel and ended in status 17
(TIMED_OUT), not INPUT_LOST. That does not qualify the merged read fix. Before
fs #41, an ordinary nested control spent minutes staging before BEGIN:
the mutation path walked the growing file chain twice per bounded append and
flushed five phases per call, including empty phases. See the
[provenance and write-cost record](../development/experiments/installer-publication/README.md#slow-clean-control-follow-up--2026-10-15).
Both defaults below were accepted by the owner on 2026-10-15. The append proof,
phase batching and adapter invalidation hooks are implemented. Completed matched
40.49 MB attempts take 789.62 s before and 615.84 s after, with matching hashes;
the optimized final interval still takes 43.65 s and safely refuses activation.
This intermediate result has no 30-second margin; the subsequent bounded
transfer result below supersedes it.

1. **Retain validated file-chain proof across consecutive committed appends.**
   Accepted: under the existing exclusive mount owner, perform a full
   cold validation, then validate selected metadata and the touched tail/new
   clusters for strict EOF appends. Update the proof only after confirmed commit;
   never rewalk the untouched prefix on every append. Other mutations, CHECK,
   errors and lifecycle changes invalidate it; overwrite/shrink use full
   validation. Read-proof invalidation on every mutation remains unchanged.
   Silent damage to an untouched prefix may wait for cold validation/CHECK to be
   detected, rather than being checked on each append. No retry or success on an
   uncertain mutation. This explicitly extends the write-validation lifetime.
2. **Flush effectful phases, with bounded adjacent-sector batching.**
   Accepted: retain dirty-mark/data/FAT/directory/reclaim/FSInfo order,
   flush after every phase that writes, and retain explicit sync/seal flushes.
   Batch adjacent prepared sectors only within one phase and existing admitted
   transfer/workspace bounds; never delay work across calls. Omit empty-phase
   flushes, revising the current format contract's explicit empty-flush promise.
   Any submitted write/flush error still stops the writer with retained recovery
   evidence. No durability barrier around actual writes is removed.

Keep the existing publication deadline and both final payload checks. These
choices address staging costs; they do not by themselves clear the final
publication-interval or native-qualification gates.

### Bulk-transfer decision — accepted 2026-10-15

The owner requires seconds for the 40 MB nested publication and clear margin
inside the existing 30-second interval. fs #41 is an intermediate improvement,
not the delivered performance target. The unchanged-image
[attribution](../development/experiments/installer-publication/README.md#remaining-write-amplification--2026-10-15)
reproduces 612.43 s: 468.18 s writing the archive, with 9,926 tiny mutations,
39,666 flushes and 510,741 reads. Per-call validation and durable metadata
publication dominate. Both final verification passes also pay thousands of
small FILE read round trips. The owner accepted the bounded transfer below on
2026-10-15; implementation is authorized.

**Accepted: add a bounded native FILE-to-FILE range transfer, up to
1 MiB per operation, between FAT32 and an explicitly supplied memory-backed
FILE.**
Keep the global 4 KiB IPC payload bound. The request names source/destination
FILE capabilities, offsets and a finite byte count; source READ and destination
WRITE are both required. This is a reusable native FILE mechanism, not raw disk
access or an installer-only operation. Initially support RAM/initrd-to-FAT32 and FAT32-to-RAM; unsupported backing
combinations refuse before
effects, never pretend to copy or automatically replay uncertainty.

Capture exact source and destination grants and forwarder lineage before
acceptance. Keep both objects and worker-owned backing alive until actual job
retirement. A read-only mask alone does not freeze mutable aliases: reserve the
RAM FILE's existing operation ownership nonblocking, returning BUSY rather than
waiting while holding another reservation. No spinlock or user-buffer mapping
lease spans allocation, I/O or scheduling. Reject the same object on both sides.
Preallocate/charge the bounded scratch and result storage before effects. The
destination range is not usable as a completed transfer until confirmed;
errors/uncertainty preserve existing stop/retention and no-retry rules.

For this caller, use explicitly provisioned private RAM workspace (the scratch
installer already has `config_snapshot_parent`), with unlinked temporary FILEs.
Heap-generated filtered archive bytes first enter that RAM FILE through ordinary
writes. Read verification ranges into a RAM FILE and hash its confirmed bytes;
RAM reads may keep their existing small message form. This supplies explicit
buffer authority without a caller-address payload, new MEMORY storage object,
implicit grant, new public seal primitive or call-site wrapper.

One transferred range is one FAT mutation, not a loop of 4080-byte mutations.
Retain current per-operation validation and all effectful phase barriers; no
new geometry/directory proof lifetime or writeback policy is proposed. Pack
adjacent prepared sectors in phase/LBA order into device-bounded runs (up to
64 KiB here), with operation-sized indexed preparation and disjoint charged
scratch. Grow retained chain capacity geometrically instead of reallocating at
every cluster. These are implementation changes under the accepted durability
contract. Do not enlarge the public raw DISK message or change media authority.

The implemented range path is factored into focused kernel/filesystem/SDK
and userland dependencies before its installer consumers. Measured complete
Apply is **19.85 s**, including a **7.17 s** final interval (22.83 s margin),
with both final payload checks unchanged. A final ownership/error-hook
confirmation is 22.22 s / 8.62 s. Whole-boot logical device writes are
46,956,032 bytes in 1,081 requests and 294 flushes, down from
101,585,920 bytes / 79,817 / 45,595. Payload sizes and the write-size histogram,
exact revisions, setup and limits are in the
[qualification record](../development/experiments/installer-publication/README.md).
These establish the disposable nested control's headroom, not installed or
native activation. Subsequent three-attempt qualification found older-pair
verification still using small FAT reads (28.62 s interval); it now reuses the
stager's bounded native FILE readback, reducing that control to 12.91 s. A
1 GiB mirrored ESP with 256 MiB foreign occupancy and two-pair history passes
cleanup at 12.56 s. Current/previous hashes, exact older-pair deletion, foreign
bytes, prepared-record refusal, missing-member fallback/cancel and uncertain
older-ownership retention are recorded in the same qualification report.
Installed integration, remaining interrupted-state/adversarial evidence and
owner-approved native checks remain separate gates; task 5 stays unchecked. Keep the 30-second interval, both hashes and all validation
and effectful durability barriers. No deadline extension, background replay,
installed activation or native write is authorized by this result.

[NVMe coexistence task 3](nvme-coexistence.md#3-installer-coexistence-mode) is a
second real gate for internal-disk/coexistence installation: partition-first
write authority and narrow GPT append operation must land through that task
before this path can activate. A FAT mount alone does not supply them. Classic VirtIO/qualified USB scratch
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

### Accepted owner decisions — 2026-10-13

Accepted contracts; implementation and qualification status are recorded separately.

1. **Default reconciliation and personal override migration. Accepted:**
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

2. **Boot-payload layout and legacy ownership admission. Accepted:** fresh
   Classic keeps its fixed boot paths through native FAT files in NVMe
   coexistence task 3 (owner, 2026-10-15). This task 5 introduces classic's
   immutable attempts together with committed boot-binding evidence; task 3
   already uses attempts for coexistence and prints a manual entry only.
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
   possibly shared ESP. An automatic destructive fallback inside Update is
   excluded. The separately reviewed classic recovery grant above preserves the
   existing damaged/missing-ESP recovery route without granting it to coexistence;
   destructive whole-disk Install remains a distinct action.
   The layout alternative is to keep canonical boot payload paths with explicit
   previous-file swaps, accepting a larger partial-boot-pair window. Accepted
   revision-scoped payloads keep the last boot pair reachable until activation.

3. **Activation, ordering and recovery. Accepted:** extend the existing
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
2. Luna stages the approved candidate after the full-profile lease gate and
   scratch qualification. Internal/coexistence targets additionally require
   coexistence task 3.
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


### Publication review corrections and recovery decisions — 2026-10-16

Daybreak reviewed parent `edd20f10` / userland `435c79a` at source level;
no sector-boundary power-loss qualification was performed. Installed activation
remains disabled. Retained payload pairs alone do not guarantee a reachable
Limine configuration after a cut during its canonical rename. Do not claim
one bootable set at every interruption until the independent recovery path is
implemented and qualified.

**Readiness correction (implemented, qualification recorded in the PR).** A
committed record must match the sizes and SHA-256 digests of the kernel ELF and
archive actually loaded, as supplied by the trusted early reader, in addition
to loaded revisions and trusted catalog bytes. Same revision/catalog is not
payload identity. Missing or mismatching evidence selects the whole archive
triple. Never substitute a hash of a newly staged file, its pathname, a build
revision or an untrusted caller's claim for the loaded bytes. The scratch
preview hashes its existing kernel-issued boot FILEs; an ordinary early loader
without those grants must refuse ESP selection until trusted issuance is wired.

**Canonical cleanup correction (implemented).** The reviewed cleanup path
already used `install_esp_child_open` for EFI, pyxis, revision and attempt; its
create branch was unused. The follow-up makes cleanup traversal uniformly
canonical and read-only, with no dormant create path. Both the shared stager and
cleanup use the same canonical-name helper; FAT short aliases never certify a
parent's ownership.

### Recovery and confinement — accepted 2026-10-18

1. **Static fallback and pinned rescue pair.** Provision, synchronize, reopen and
   verify a Pyxis-owned `/boot/limine.conf` before switching the primary. It
   selects an immutable, **small non-game** kernel/archive pair under
   `EFI/pyxis/recovery`. Normal publication never replaces or cleans that config
   or pair; retain it in addition to current and previous. Never overwrite a
   foreign fallback. Audit all higher-priority Limine config candidates and
   refuse shadowing. A fresh installation is recoverable only after the first
   complete fallback provision is confirmed. Review must account for actual
   rounded allocation of **current + previous + pinned rescue**, configuration
   recovery, filesystem overhead and the fresh staging attempt in the selected
   ESP; a 512 MiB ESP is not presumed to fit two large personal archives plus an
   ordinary live archive. Insufficient space refuses before writes. Build rescue
   directly from trusted component roots and `boot/rescue.list`, excluding game
   data, personal overrides, SDK and hosted development payloads, not by using the
   current personal archive as the rescue source.

2. **Bounded QEMU cuts.** Sector-boundary power-cut qualification on disposable
   images is authorized, with temporary never-merged observation/injection.
   Include uncut, before-removal, missing-name-window and after-publication
   controls, both directory-sector orderings, actual pinned Limine boot, exact
   loaded hashes and foreign-byte checks. Preserve cut images read-only; their
   expected dirty marks do not authorize repair. No native cuts or claim about
   torn individual sectors or devices ignoring FLUSH.

3. **Battery gate.** Install and Update refuse on battery below 40%; AC permits
   any charge, and no battery permits operation. A reported battery with
   unreadable state refuses clearly. Use the same ACPI snapshot/capacity basis
   as the power widget, with no firmware queries or guessed percentages. Show
   refusal before consent, recheck before the first write, and apply it to
   classic, coexistence and the scratch publisher. No bypass option.

4. **Owned ESP view.** A separate DISKS right confines mutation to `EFI/pyxis`,
   `boot/limine` and exact `/boot/limine.conf`. Review refuses extra content in
   the writable `boot/limine` directory. Installer review-authority opens require
   the reviewed ESP partition and type. Ordinary configured FAT32 opens through
   boot init's `fat_mounts`/`DISKS_OPEN_FAT32` keep existing RO/RW rules, including
   USB partitions that are not ESPs. The future system upgrader is a separate
   package-manager/kernel/archive client and receives the owned-ESP-view right
   independently of installer review authority. No firmware-variable write.

Pinned Limine 12.9.3 searches beside its EFI executable first (or EFI/BOOT when
that location is unavailable), then `/boot/limine/limine.conf`,
`/boot/limine.conf`, `/limine/limine.conf`, `/limine.conf`. It stops at the first
found file: the fallback covers an absent primary, not a corrupt-but-present
one. It must not sit at a higher-priority location that shadows the primary.
[Search source](https://github.com/Limine-Bootloader/Limine/blob/e3aa138d383ca4fe061f05931b5a54497cd502f2/common/lib/config.c#L118-L169).
FAT rename is not power-fail atomic: it prepares deletion/new directory entries
then writes ordered sectors before the phase flush. Duplicate names or a
same-cluster overwrite do not establish a native atomic-write guarantee.

### Pending activation recovery — owner decision needed, 2026-10-18

Both blind-review findings still apply at `fd2d8cf0` / userland `475d6cb`:
legacy adoption rehashes inherited payloads twice inside the interval using
small FILE reads, and a committed readiness record can advance ahead of the
menu. The first is fixed by reusing the existing bounded native readback for
both full inherited hashes, without caching content proof or omitting either
new-payload hash. Matched runtime qualification remains separate.

The second needs a narrow recovery/lifetime decision. The frontend uses only
`record.boot` as inherited menu evidence; after a pending switch the actual menu
may still name `record.previous_boot`. Publication then retains obsolete config
artifacts, and the next publication deliberately refuses their presence under
the accepted manual-disposition rule. Advancing another canonical record would
also overwrite the only bounded references to those retained artifacts.

**Proposed default, not accepted: an explicit fresh Review action to complete
one pending activation before a new publication.** Do not create a new readiness
record or payload attempt. Reopen and validate the complete committed triple,
current payload hashes, retained previous pair and exact old-menu/recovery
bytes; establish the static fallback first. A known inherited menu matching the
recorded previous pair may be switched to the record's current pair under the
normal bounded interval. A menu already matching that current pair needs no
redundant rewrite. Only after confirmed matching menu readback may the existing
exact-hash cleanup finish; keep current/previous binding slots unchanged. A new
ordinary Update then gets a new plan and Review. This is fresh explicit consent,
not an automatic retry of the earlier uncertain mutation, an implicit rollback
or an additional active-pointer authority.

Prepared/malformed records, ambiguous or edited menus, mismatching hashes and
unrecognized artifacts retain the existing manual-disposition refusal. The
alternative is to preserve that refusal for all pending activation and print
manual recovery instructions; it does not clear the reported ordinary recovery
blocker. No code for the proposed completion action is authorized or implemented
until the owner answers.

The independent accepted pieces may be checkpointed meanwhile. Installed
activation stays disabled and task 5 remains unchecked; no native writes or
sector-cut run is implied by a source/build checkpoint.
