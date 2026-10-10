# Bundles in bin://

Accepted plan, 2026-10-10; not implemented. The owner accepted both lookup
decisions below and authorized task 1 to start after #686 merges.
This sits alongside the accepted, unimplemented
[ZIP proposal #639](https://git.internal/PyxisOS/pyxis-os/pulls/639);
it does not authorize its packaging or adapter tasks.

## Accepted direction

- Bundles live flat beside plain programs: `bin://nvim.pxb` and
  `bin://lua5.1.pxb`, next to programs such as `bin://grep.pxe`.
  Installed bundles belong to the same `bin/REVISION` as those programs.
- For now, bare `NAME` checks `bin://NAME.pxe`, then `bin://NAME.pxb` and
  selects that bundle's default entry. This reads no installation directory
  listing and parses no other bundle's manifest. The filename selects the
  candidate; neither its manifest ID nor its `commands` map must equal `NAME`.
- Other declared commands remain accessible through explicit paths, the
  development catalog or its logical aliases. There is no automatic registration
  of every manifest command at launch time.
- Notices, port notes and packaged source/provenance text travel inside each
  bundle. Bundle-only `share/neovim/` and `share/lua51/` trees disappear.
- Later, generate a manifest-command index during revision install/update.
  Conflicting command registrations fail that update; a bundle cannot shadow a
  plain `.pxe`. This is future work, not a launch-time scan.

## Inspected starting point

Inspected main `a6f2a1ad`, userland `54f65d04` and ports `64422ecf`.
These findings are code inspection; this proposal has no runtime measurements.

The [system layout](../userland/system-layout.md) already binds the entire boot
archive as live `bin://`, or the selected pool revision directory on installed
boots. The archive exposes native directories, including empty directories.
No new kernel directory backend is needed for unpacked bundles.

Shell `shell/directory.c` and the Lua 5.5 port's `lua/pyxis.c` independently
implement plain bin lookup, catalog lookup and rescue fallback. Both use
libpyxis's existing bundle preparation for app/resource roots, stacks and grants.
[Catalog lookup](program-bundles.md#unpacked-development-lookup) currently
validates up to eight explicitly listed bundles; it never scans a directory.
Lua 5.1/luv is a separate interpreter bundle, not another copy of this Lua 5.5
`pyxis.run` binding.

Ports stage the two bundles under `share/neovim/nvim.pxb` and
`share/lua51/lua5.1.pxb`; licenses and provenance are outside them. The installer
selects only top-level regular `.pxe` files, filters exact archive names and
copies/verifies files. Its revision cleanup also assumes files. Moving bundles
requires complete-tree handling, not just suffix recognition in the shell.

## Accepted lookup decisions (2026-10-10, not implemented)

1. **Direct bin candidates precede the optional development catalog,
   and only an absent candidate permits fallback.** Bare lookup becomes
   `.pxe` → `.pxb` → configured catalog → `boot://NAME.pxe` rescue fallback.
   A denied/wrong-type plain candidate, malformed or incomplete selected bundle,
   missing declared entry/resource, failed grant admission or budget exhaustion
   reports its error; it never silently launches a lower-priority program.
   A selected bundle's missing member is a broken bundle, not an absent candidate.
   A missing bin binding behaves as absence, as today. Retain the selected native
   view through preparation; never reopen its pathname to choose the image or
   resource roots. Lookup adds no authority.

   Keep `PYXIS_BUNDLE_CATALOG` optional and explicit, with its existing eight-entry
   limit and whole-registration validation. A plain or same-name bundle hit
   does not consult it, so an unrelated broken catalog cannot break that launch.
   Secondary commands and development bundles outside bin still use it; there is
   no default catalog containing all installed bundles. This preserves the
   development facility but makes the installed filename authoritative.

   Explicit `.pxb` URIs still select the default entry. Existing logical
   `bin://NAME` aliases retain literal FILE lookup first; after its absence use
   the same `.pxe`/`.pxb` candidates and catalog, without rescue-root fallback.
   Explicit `.pxe` and other ordinary FILE paths retain ordinary launch behavior.
   In particular, launching `.../app/bin/other.pxe` directly does not apply a
   bundle's stack/app/grant policy; use its catalog command for that policy.
   Task 1 adds no URI fragment or entry-selector syntax. Share ordered candidate
   selection in libpyxis so shell and Lua do not develop separate precedence.

2. **Future `.pxa` is the next same-name candidate after `.pxb`, and
   shipped revisions reject two bundle forms with the same basename.** Once
   ZIP launch is implemented, bare lookup is `.pxe` → `.pxb` → `.pxa` → catalog
   → rescue. An explicit `bin://NAME.pxa` selects its default. No other archives
   are opened or parsed. A manually assembled development view with both forms
   selects `.pxb`; a broken `.pxb` does not fall through to `.pxa`. Revision
   packaging/install rejects that ambiguity rather than hiding one shipped form.
   Plain `.pxe` precedence remains, including when it shares a bundle basename.
   The later command index rejects declared-command conflicts with plain programs.

   The ZIP adapter acquires the selected archive's frozen native revision view
   under #639's accepted contract. Manifest, app/resource roots, stack and actual
   grants then use the existing bundle path. Only the selected stored executable
   uses the [128 MiB capture path](program-bundles.md#selected-image-admission);
   whole-archive frozen backing has its separate budget. These costs and lifetime
   rules are unchanged by bin placement. Task 1 implements neither ZIP recognition
   nor the adapter; bare `.pxa` lookup waits for that separately assigned task.

## Packaging, install and lifetime

Stage `bin/nvim.pxb` and `bin/lua5.1.pxb` in the ports output, which image
assembly maps to the archive root. Keep `manifest.json` and `app/` layouts;
Neovim's `app/share/nvim/runtime` and `nvim_runtime://` remain the resource paths.
Use `app/metadata/` for port notes and `source.txt` provenance, with a `licenses/`
subdirectory for application and linked-dependency notices. Existing source
text is provenance, not a source-code distribution; preserve its meaning.
Copy required dependency notices into each distributed bundle, while retaining
notices for independently distributed SDK libraries. Do not add startup roots
or broaden grants just to expose metadata.

Install/update selects root `.pxb` directories as complete program units alongside
non-rescue `.pxe` files. Copy with bounded file buffers and traversal, preserve
empty directories, then verify names, kinds, structure, sizes and bytes, including
missing/extra entries. Filter the bundle root and descendants from the installed
rescue archive using component boundaries (`nvim.pxb/`, not a string prefix).
An installed system then loads these bundles from its kernel's bin revision; the
live archive has exactly the equivalent root-level bundle paths. Old
`boot://share/...` paths are removed; docs/catalog fixtures must follow the move.

Keep the [update commit rule](../userland/system-updates.md#program-stage):
copy all selected programs → pool sync → verify → publish/flush/verify the ESP.
A failure before the ESP switch leaves a different previous revision bootable.
Reruns retain the existing clear/rewrite behavior and its same-revision / unknown
revision interruption limitation; this task does not promise atomic same-revision
replacement. Old-revision cleanup is recursive, preserves current and previous
(or skips cleanup when previous is unknown), and remains best effort after success.

This is the existing **offline target update** boundary:
[READ_WRITE admission](../devices/installer-authority.md#inventory-and-raw-access) refuses any
retained npfs pool on that disk. It prevents installed applications with app/bin
handles from coexisting with an admitted target update. No new revision lease or
GC service is needed here. A held directory by itself is not a snapshot and would
lose lookups if its descendants were removed. Keep the prohibition on modifying
or deleting published development trees while programs can use them; future
online/program-only updates must settle revision lifetime separately.

## Assigned first task after #686 merges

- [ ] **Task 1: unpacked bin lookup and relocation of the two existing bundles.**
  Implement shared ordered candidate selection for shell and Lua; move both
  bundles and their metadata/notices; add recursive installer copy, verification,
  archive filtering and cleanup under the existing offline boundary. Preserve
  plain-program admission, manifest limits, stack requests and actual grants.
  Update current usage references only when the behavior lands. Userland and
  ports PRs come first, rebased onto current main, with published heads pinned by the Pyxis integration
  PR; no compiler-container rebuild or new mirror is required.

  Capture exact-main plain/bundle launch and session-backing baseline before code.
  Repeat matched one-/four-CPU QEMU runs, including Neovim startup/runtime use
  from the current archive and its installed npfs-backed equivalent; plain hits
  must not parse manifests or enumerate bin. Check bare `nvim`, `lua5.1`, plain-first precedence, no-catalog
  launch, a catalog secondary command, explicit aliases and malformed/denied
  candidates. Inspect app/resource read-only grants and cleanup. Install and boot
  an npfs target, read bundled runtime/notices, repeat same-revision Update and
  two successive revisions, verify recursive old-revision cleanup and mounted
  target refusal. Check interrupted unpublished program copy and a successful
  rerun. No new klog or benchmark/test infrastructure.

  **After this task, the owner can:** run `nvim file.c` and `lua5.1 script.lua`
  without a catalog on both live media and an installed revision, with their
  runtime and notices installed beside the corresponding plain programs.

- **Future command index, separately assigned:** build and verify a revision-wide
  manifest command map during packaging/install/update, reject conflicts before
  the ESP switch, and resolve it without per-launch catalog-wide parsing. Live
  images generate the equivalent index at archive assembly. Settle its format,
  alias rules, trust/validation and lifetime before code.
  **After this task, the owner can:** use secondary manifest commands by bare
  name without maintaining a development catalog.
