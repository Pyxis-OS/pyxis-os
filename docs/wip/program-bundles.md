# Program bundles

Status: the owner merged [#621](https://git.internal/PyxisOS/pyxis-os/pulls/621)
and separately authorized task 1. Unpacked development bundles are implemented;
review remains pending. The [qualification record](../development/experiments/program-bundles-task1/README.md)
reports manual QEMU/GDB checks and matched plain-launch costs. ZIP and the
accepted larger capture ceiling require separate tasks. Signing, consent UI and a Clang port
are outside this task.

## Unpacked development lookup

A `.pxb` is a native directory containing `manifest.json` and `app/`. Its
manifest selects native executable entries and read-only resource directories
inside `app/`. Recognition validates the layout and metadata; the suffix alone
confers no authority. Ordinary plain programs continue to use their existing
launch path and receive no implicit `app://`.

Shell and Lua launch lookup read the explicit `PYXIS_BUNDLE_CATALOG` environment
variable. Its value is a native FILE URI containing UTF-8 JSON data:

```json
{"format":1,"bundles":["boot://bundles/sample.pxb"]}
```

The catalog has exactly these fields, accepts at most eight explicit bundle
URIs, and does not enumerate an installation directory. Bare commands retain
the ordinary plain `bin://NAME.pxe` fast path. After that file is missing,
bundle lookup may select a registered command before ordinary rescue lookup.
Explicit logical `bin://` aliases use the same catalog. An explicit `.pxb`
path selects its default entry without requiring a catalog; a trailing directory
slash is accepted. Selected command names remain available as `argv[0]`.
There is no filesystem directory adapter, executable copy or symlink behind
these logical aliases.

Each catalog lookup opens and validates the complete view before selecting a
program. Reject duplicate bundle IDs, command names, malformed manifests,
unusable registered defaults/commands/resource directories, and collisions with
plain `NAME.pxe` files through the retained caller `bin://` root. Collision
inspection needs only directory LOOKUP and opens zero-right FILE handles.
A broken registered source rejects the lookup; `NOT_FOUND` from catalog lookup
means only that a valid view has no requested command. No singleton cache or
partial registration survives an error.

The resolver retains the selected revision, image, app directory and resource
directories until its owning program view closes. It does not reopen the selected
bundle URI after choosing it. The default and every command entry must be native
FILE objects whose prefix matches `P1F_MAGIC`; scripts are rejected. Remaining
P1F validation and executable capture belong to the kernel.

## Manifest and stack

The schema requires `format: 1`, `id`, `entry`, `commands`, `resource_dirs` and
`grants`; only `stack_bytes` is optional. Empty command/resource maps and an empty
grant array are allowed. Duplicate JSON keys, unknown fields, invalid types,
invalid UTF-8 and embedded NUL strings reject the manifest. Metadata contains
no arguments, executable code or arbitrary handle numbers.

```json
{
  "format": 1,
  "id": "org.pyxis.sample",
  "entry": "bin/sample.pxe",
  "commands": {"sample": "bin/sample.pxe", "sample-alias": "bin/sample.pxe"},
  "stack_bytes": 8388608,
  "resource_dirs": {"sample-data": "data"},
  "grants": [
    {"name": "memory", "resource": "memory", "rights": ["manage"], "required": true},
    {"name": "clock", "resource": "clock", "rights": ["read"], "required": false}
  ]
}
```

| Field | Implemented meaning |
| --- | --- |
| `id` | Stable registration identifier; a self-asserted ID does not authenticate an application or grant trust. |
| `entry` | Default native P1F path relative to `app/`. |
| `commands` | Flat command-name to relative native-entry map; several aliases may select the same entry. |
| `stack_bytes` | Optional integer, page-aligned, 1–8 MiB inclusive. Omission leaves the API parameter zero, which selects the kernel's 1 MiB default. Invalid requests reject without clamping. |
| `resource_dirs` | Additional startup root-name to relative directory map, with read-only grants. Names must not collide with `app`, other manifest roots/grants or existing startup bindings. |
| `grants` | Named ordinary resource requests with recognized rights and explicit required/optional status. |

Entry and resource paths remain inside `app/`: reject absolute paths, schemes,
empty components, `.` and `..`. Names reject `/` and `:`; path/name limits below
are implementation budgets. Each bundle entry uses the same stack request.
The kernel validates it for single and batch loads before image capture, eagerly
backs the requested high stack, and places one unmapped guard page directly
below it. Plain programs and boot init omit the parameter and retain 1 MiB.
There is no automatic stack growth or per-command stack setting.

## App views, grants and lifetime

Each bundle program receives its own `app://` rooted at the selected `app/`
directory, replacing an inherited app binding. App and named resource directories
receive exactly LOOKUP, ENUMERATE and READ_FILES, with no mutation rights or
parent-navigation escape from those views. Source, output and writable user data
remain outside the bundle and use explicitly delegated ordinary capabilities.

The launcher preserves explicit streams, cwd, environment, namespace and other
directory roots. Named ordinary resources come from the manifest's requests;
unused source resource grants are omitted. Caller-selected authority supplies
requests, and a shell supplies its delegated child launcher rather than its own
supervision launcher. The kernel still prevents increasing the supplier's rights
or transport authority.

Recognized request classes and rights are:

| Resource | Rights |
| --- | --- |
| `memory` | `manage` |
| `clock` | `read`, `sleep` |
| `launcher` | `launch` |
| `random`, `system_info` | `read` |
| `echo` | `send` |
| `tcp` | `connect`, `listen` |
| `udp` | `open` |
| `display` | `draw` |
| `audio` | `create` (native playback authority) |
| `screen_capture` | `capture` |
| `input` | `read` (console authority) |
| `output` | `write` (console authority) |
| `profile` | `memory`, `host` |

Programs using libc allocation request `memory/manage` under its documented
`memory` startup name; declaring an alias does not rewrite libc service lookups.

Unknown classes/rights and system-only mount/setup, space-factory or raw-device
requests reject even when marked optional. Under the accepted temporary policy,
deliver every requested grant available within the launcher's selected delegated
authority, including optional requests. An unavailable optional request is absent;
an unsatisfied required request fails the whole admission. Preparation failures
unwind unpublished storage and preserve source handles. The program enumerates
its actual startup resources/roots and queries actual handle rights; a manifest
request never synthesizes a successful grant. This policy is
[temporary technical debt](../technical-debt.md#temporary-bundle-grant-policy).

Publish a complete revision before activating its catalog registration. Do not
mutate or delete a published development tree. Retained native handles keep their
backing alive but do not freeze a writable tree or establish a snapshot; normal
caller roots may include writable aliases. Publisher discipline supplies
revision immutability in this slice. There is no revision garbage collector or
archive adapter. App/resource handles delegated to a child retain backing after
the launching process closes its own view. The manifest is launch policy metadata,
not a universal restriction on independently delegated FILE/launcher capabilities.

## Implemented budgets and qualification

| Input or preparation | Budget |
| --- | --- |
| Manifest / catalog JSON | 64 KiB each |
| Catalog bundles | 8 |
| Commands per bundle | 32 |
| Named resource directories per bundle | 8, plus `app` |
| Grant requests per bundle | 16 |
| JSON nesting | 16 |
| Name/path component | 255 bytes |
| Relative entry/resource path | 1024 bytes |
| Final userspace launch roots | 16 total |
| Launch capture / child startup | 64 KiB each, independently enforced by the kernel |

All final capture/startup validation occurs before child publication. The parser
allocates a bounded node array from the JSON byte count and releases each
manifest parse tree after retaining its decoded strings. Code inspection gives
about 5 MiB of overlapping catalog/manifest parse-tree heap at maximum JSON sizes;
this is an allocation bound, not a measured peak. No whole-bundle capture or
capture-limit increase is implemented. Existing installed HOST/NPFS executable
capture remains 16 MiB; the 256 MiB mapped-image span remains independently
bounded.

Ordinary kernel/SDK/ports/userland builds and interactive four-CPU QEMU passed
for a two-command sample with a larger stack, read-only roots and actual grants.
GDB inspected stack/guard permissions, normal retirement and unpublished batch
rollback. Matched one-/four-CPU plain launches show no observed regression;
see the [qualification record](../development/experiments/program-bundles-task1/README.md)
for exact inputs, samples and limits. No physical-hardware result is claimed.

## Future consent policy

The owner has recorded this direction, without authorizing its implementation:

- Required grants are approved or denied together at first launch, with the
  answer remembered for the user/application; denial means no launch.
- Optional grants are requested individually when their feature is first used,
  approved or denied and remembered; programs handle absence at runtime.
- A trusted file picker grants the selected file, rather than broad document
  authority.
- Updates adding a required grant, including more rights or scope, ask again.
  Reusing a manifest ID or pathname does not authenticate the update's identity.
- Revocation applies at the next launch unless a capability explicitly supports
  withdrawal while running, including delegated copies.

Startup describes the immutable initial granted set. Later picker/optional
delivery requires an explicit broker result a program can inspect. Signing,
identity authentication, consent UI and runtime revocation remain undesigned.

## Future ZIP profile

The accepted `.pxa` form uses restricted ZIP with the same manifest and `app/`
layout. A native archive-backed directory/file adapter or explicit provider
integration must establish backing lifetime before implementation. A userspace
provider alone cannot currently supply startup DIRECTORY or launchable FILE
objects. Keep decompression in userspace using the existing
[zlib port](../development/ports.md#zlib-development-library), preserving BSP
allocation/VM-mutation ownership.

Require stored manifest, executable entries and seek-heavy/large resources.
Permit method-8 deflate only for small resources, decoded once into bounded
backing before exposure. Draft budgets to qualify: 16,384 entries, 4 MiB
index/metadata, 512 MiB archive and total expanded contents; 1 MiB per deflated
entry, 8 MiB aggregate decoded backing and expansion ratio at most 100:1.
Budget exhaustion is an explicit error. Deflate needs an independent ZIP CRC
check; see [PKWARE's specification](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT)
and the [zlib manual](https://zlib.net/manual.html).

Require UTF-8 names with no code-page fallback. Validate local and central
records consistently, with overflow-safe offsets and all ranges inside the file.
Reject duplicate names; file/directory or inferred-parent collisions;
absolute/drive paths, NULs, backslashes, `.` and `..`; encryption; unknown
methods/flags; overlapping ranges; split archives; and symlink/special-file
attributes. Explicit directory records may match inferred parents; duplicate
actual records or conflicting types reject. Ignore ordinary Unix owner,
permission and executable attributes: they confer no authority. The first
profile rejects data descriptors and ZIP64. Packaging must name this subset;
generic ZIP tool defaults are not guaranteed to comply.

Check stored manifest CRC before interpreting policy and executable CRC during
capture. Before exposing a stored resource, check CRC by a bounded streaming
scan and retain that result only for the immutable revision. Deflated files must
finish exactly within declared input/output sizes and pass CRC before exposure.
CRC detects corruption, not provenance. Archive and decoded backing must survive
the launching process until dependent handles close. The first profile has no
random-access deflate index or signature layout.

## Future larger-image admission

The owner has authorized the shared-ceiling task and accepted these contracts:

- **128 MiB per selected executable**, uniformly for HOST, NPFS and RAM capture.
  This is one admission policy, not a global concurrent-capture budget. Concurrent
  launches may consume more in aggregate; physical-memory exhaustion still rejects.
- Capture backing uses **reclaimable, BSP-owned whole pages**, released after
  loading or on every failure, including partial allocation. Large capture buffers
  must not remain resident as reusable kernel heap pools.
- Over-ceiling capture returns `CALL_LIMIT`; allocation failure returns
  `CALL_NO_MEMORY`; existing I/O errors remain unchanged. Failed batches publish
  no children and discard earlier prepared children and provisional observers.
  Boot archive bytes remain uncopied, eager segment loading remains, and the
  256 MiB mapped span is independent.

Implementation and qualification remain pending. Capture matched plain-launch
and session-backing baselines on the exact main revision before code changes;
then measure near-ceiling peak memory alongside other processes and inspect
over-ceiling and mid-capture allocation failure cleanup. Add no new kernel log
lines. ZIP and Clang remain outside this task.

The accepted next direction captures only the selected executable, retains
existing eager segment loading, and shares a 128 MiB serialized-image ceiling
across installed HOST/NPFS and RAM capture. It must replace the current installed
16 MiB ceiling and bound RAM copies together in a separately authorized task.
Existing immutable boot archive bytes need no new copy. Outer archive size,
compression and serialized bytes do not define mapped span; its 256 MiB ceiling
remains independent. Peak memory and failure rollback require qualification:
capture, eager image backing, requested stack, startup/page tables and other
processes coexist, and batches retain earlier prepared children.

A future Clang-shaped bundle would keep Clang, LLD, resource headers and a SDK
sysroot in one revision, request an 8 MiB stack and receive group-bound linker
launch authority plus explicit source/cwd/streams/output grants. The
[Linux proxy sizes](hosted-clang.md#resources-and-native-limits), about 86 MiB
Clang and 55 MiB LLD, motivate selected-entry capture; they are not native
qualification. This bundle work does not resolve the compiler port's remaining
runtime or metadata blockers.

## Task status

- [x] Task 1 implementation: unpacked development manifests/catalog, command
  selection, per-program app/resource views, stack requests and actual grant
  preparation. Qualified in QEMU; owner review is pending.
- [ ] Separate task: native ZIP adapter/backing lifetime and restricted archive
  profile, with matched peak/launch/cleanup qualification.
- [ ] Separate task: shared 128 MiB captured-image ceiling and failure rollback
  qualification.

Later tasks require explicit owner authorization. The native Kilo/TCC workflow
remains usable throughout.
