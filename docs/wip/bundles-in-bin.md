# Bundles in bin://

Task 1 implements the owner's accepted 2026-10-10 direction from
[#686](https://git.internal/PyxisOS/pyxis-os/pulls/686). Qualification is in progress.
Current layout and update contracts are in [system layout](../userland/system-layout.md)
and [system updates](../userland/system-updates.md). This page retains the remaining
ZIP/index work; it authorizes neither.

## Implemented unpacked placement and lookup

Bundles live flat beside plain programs: `bin://nvim.pxb` and `bin://lua5.1.pxb`,
next to programs such as `bin://grep.pxe`. Installed bundles belong to the running
kernel's `bin/REVISION`; live bin is the archive root. The bundle includes port
notes, provenance and application/linked-dependency notices in `app/metadata/`.
The old bundle-only `share/neovim/` and `share/lua51/` trees are removed;
independently distributed SDK/library notices remain.

Shell and Lua use libpyxis `program_open`. Bare `NAME` selects
`.pxe` → `.pxb` default → optional development catalog → rescue `boot://NAME.pxe`.
Only an absent candidate permits fallback. Denied or wrong-type candidates,
malformed/incomplete bundles, missing members and failed admission report their
error. A direct bin hit reads no catalog or directory listing. The filename
selects the candidate independently of the manifest ID and command names.
Retain the selected native view through preparation; do not reopen its pathname.
Lookup adds no authority or new kernel mechanism.

Explicit `.pxb` URIs select the default. Logical `bin://NAME` tries a literal FILE
first, then the same bin candidates/catalog, with no rescue fallback. Ordinary
FILE paths retain ordinary launch behavior: launching an inner `app/bin/other.pxe`
directly does not apply bundle stack/app/grant policy. Secondary commands use
explicit paths or the [development catalog](program-bundles.md#unpacked-development-lookup).
The catalog remains optional, bounded to eight explicit bundles, with complete
registration validation; it is not a default installed-command index.

The installer selects complete root `.pxb` trees alongside non-rescue `.pxe`
files, preserves empty directories, and verifies names, kinds, structure, sizes
and bytes without extra entries. Recursive traversal and file buffers are bounded.
Filtering removes the root and descendants at path-component boundaries, so an
unrelated `nvim.pxb-extra` stays in the rescue archive. Copy → pool sync → verify
precedes the ESP switch. Current and previous revisions survive recursive,
best-effort cleanup; unknown previous revision skips cleanup. Existing
same-revision/unknown-revision clear-and-rewrite interruption limits remain.

READ_WRITE target admission refuses any retained npfs pool on that disk. This
existing offline-update boundary excludes running applications holding app/bin
handles from an admitted target update. A held directory alone is not a snapshot;
keep published development trees immutable while in use. Online/program-only
updates need a separate revision lifetime contract.

## Accepted future ZIP lookup (2026-10-10, not implemented)

Future `.pxa` follows `.pxb`: `.pxe` → `.pxb` → `.pxa` → catalog → rescue. An
explicit `bin://NAME.pxa` selects its default. Shipped revisions reject both
bundle forms sharing a basename. A manually assembled development view selects
`.pxb` first; a broken `.pxb` never falls through to `.pxa`. Plain `.pxe` precedence
remains even with a same-basename bundle. No other archives are opened or parsed.

The accepted, unimplemented [ZIP proposal #639](https://git.internal/PyxisOS/pyxis-os/pulls/639)
acquires the selected archive's frozen native revision view. Manifest,
app/resource roots, stack and actual grants then use the existing bundle path.
Only the selected stored executable uses the
[128 MiB capture path](program-bundles.md#selected-image-admission); whole-archive
frozen backing has its separate budget. Bin placement changes neither cost nor
lifetime. ZIP recognition, packaging and adapter tasks need separate owner go.

## Tasks

- [ ] **Task 1: unpacked bin lookup and relocation of the two existing bundles.**
  Shared shell/Lua lookup, both bundle moves and bundled notices, recursive
  installer copy/verification/filtering/cleanup. Preserve manifest/grant/stack
  admission. Userland and ports PRs precede the Pyxis integration; no new mirror
  or compiler-container rebuild. Capture exact-main baseline before code and
  matched one-/four-CPU launch, Neovim startup and session-backing runs; qualify
  live and installed npfs roots, errors, updates, recursive cleanup and interrupted
  program-copy recovery. Results will be linked here when qualification finishes.

  **After this task, the owner can:** run `nvim file.c` and `lua5.1 script.lua`
  without a catalog on live media and installed revisions, with runtime and
  notices beside the corresponding plain programs.

- [ ] **Future command index, separate owner assignment.** Generate manifest
  command registrations at revision install/update time. Conflicts fail the
  update and a bundle never shadows a plain `.pxe`; no launch-time scan.

  **After this task, the owner can:** use every installed bundle's declared
  command names without manually maintaining a development catalog.
