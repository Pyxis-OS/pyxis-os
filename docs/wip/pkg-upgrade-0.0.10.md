# 0.0.10 package upgrades

Status: **tasks 1–2 merged; task 3 in qualification (chronium, 2026-10-26).**
This milestone changes the delivered [updater](pkg-upgrade.md). The signing-floor
choice below remains open; this plan does not claim a completed release.

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
   payload. It ends with `Upgrade staged. Reboot now? [Y/n]`. Declining leaves
   the upgrade staged; reboot is still required. The running system continues
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
5. **Coexist provenance.** A reviewed Coexist Install/Update writes the
   activation record and prints one manual boot entry; it never edits a
   foreign boot menu. An existing unrecorded Coexist installation needs the
   installer's Update to gain that record. Boot warns loudly when the record
   is absent or invalid, and `pkg` returns a specific refusal rather than
   obtaining ESP write authority. *(chronium, 2026-10-26)*
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
- [ ] Add durable pending transition evidence and the verified first-boot
  `packages/current` switch before installed consumers start. Qualify
  interruption boundaries and core-rescue refusal.
- [ ] Integrate the set planner with `pkg upgrade` and 0.0.10 installer Update;
  qualify the actual 0.0.9 migration and the reboot prompt.
- [ ] Add Coexist activation evidence and specific boot/`pkg` diagnostics.
- [ ] Settle the signing-floor source and qualify first repository contact.

## Task 3 qualification to date

On a disposable 3 GiB QEMU disk, a Classic Install from the task-3 image
published the boot/package identity and booted into its installed shell. A
released 0.0.9 image installed 38 packages on another disposable disk; for a
small local migration fixture, its package set was explicitly reduced to
`shell` and its single repository was redirected to the matching task-3 build.
The 0.0.10 installer Update reviewed the target, staged set 3 while `current`
remained set 2, published a new ESP attempt, and reported verified activation.
The installed boot then logged `verified package set 3 for this boot`, reached
the shell, changed `current` to set 3, published a matching `.boot-selection`
and removed `.upgrade-pending`. This checks the 0.0.9 migration path with one
legacy-origin package, not a 38-package download at the target revision. An
earlier refused Update had synchronized system configuration on this disk but
left its 0.0.9 boot selection and package set unchanged; the successful run
therefore also checked a retry after that no-publication failure.

The in-system `pkg upgrade` publication, its reboot action, uncertain boot
evidence and Coexist activation record still need qualification or later-round
work before this milestone is complete. The 0.0.10 release notes must tell
0.0.9 users to use installer Update, because the 0.0.9 `pkg upgrade` client
cannot stage this transition.

## Open decisions for later rounds

- **Signing-floor snapshot authority:** choose the trusted build-time source
  and schema of the shipped signed-index floor. A network response observed
  at build time is not itself a trusted floor.

## You should know

`packages/current` is a live view: changing it during the old boot would show
new-ABI packages to old-release processes. The first-boot gate is therefore a
required later task, not part of retiring rollback interfaces. ESP and npfs
cannot commit atomically; the gate must refuse uncertain evidence rather than
show a mixed system. Staging can temporarily occupy space for both sets. The
standalone rescue pair is a recovery tool, not a previous-version selection.
An interrupted payload cleanup may leave an empty unmarked attempt directory;
it contains no boot bytes and the next publication leaves the ambiguous empty
name untouched. A nonempty attempt without valid ownership proof refuses the
cleanup without guessing. The sweep runs after confirmed activation, so its
failure is reported as incomplete cleanup of an already selected release.
