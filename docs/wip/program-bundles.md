# Program bundles

Status: the owner merged the design in
[#621](https://git.internal/PyxisOS/pyxis-os/pulls/621) and unpacked development
bundles in [#625](https://git.internal/PyxisOS/pyxis-os/pulls/625), shared capture
in [#629](https://git.internal/PyxisOS/pyxis-os/pulls/629), and bin placement/tree
installation in [#691](https://git.internal/PyxisOS/pyxis-os/pulls/691). The owner
accepted all three ZIP defaults on 2026-10-09. Task 2a, the host packer/checker,
is owner-assigned 2026-10-11; it is not implemented. Tasks 2b and 2c still need
separate owner go-aheads. Signing, consent UI and Clang remain outside this slice.

## Unpacked development lookup

A `.pxb` is a native directory containing `manifest.json` and `app/`. Its
manifest selects native executable entries and read-only resource directories
inside `app/`. Recognition validates the layout and metadata; the suffix alone
confers no authority. Ordinary plain programs continue to use their existing
launch path and receive no implicit `app://`.

Shell and Lua share libpyxis `program_open`: bare `NAME` tries
`bin://NAME.pxe`, then the default of `bin://NAME.pxb`, then an optional
development catalog, then `boot://NAME.pxe`. Only candidate absence permits
fallback; denied, malformed or incomplete selected candidates fail visibly.
A direct bin hit enumerates no directory and parses no unrelated manifest or
catalog. The filename selects a bundle independently of its ID/command names.
See [bin placement and installation](../userland/system-layout.md).

The explicit `PYXIS_BUNDLE_CATALOG` environment variable names a native FILE URI
containing UTF-8 JSON data:

```json
{"format":1,"bundles":["boot://bundles/sample.pxb"]}
```

The catalog has exactly these fields, accepts at most eight explicit bundle
URIs, and does not enumerate an installation directory. It remains a development
facility for secondary commands and bundles outside bin; there is no default
catalog listing installed bundles. Logical `bin://NAME` tries a literal FILE
first, then the same `.pxe`/`.pxb` candidates and catalog, without rescue fallback. An explicit `.pxb`
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
directory roots. `pipe/create` is an ordinary manifest request available to foreground bundles
when their supplying shell holds the pipe service. Plain foreground programs
still receive no pipe-creation grant. Named ordinary resources come from the manifest's requests;
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
| `pipe` | `create` |
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

Publish a complete development revision before activating its catalog registration. Do not
mutate or delete a published development tree. Retained native handles keep their
backing alive but do not freeze a writable tree or establish a snapshot; normal
caller roots may include writable aliases. Publisher discipline supplies
revision immutability in this slice. Development trees have no revision garbage collector or archive adapter.
Installed bundles instead follow the [offline update boundary](../userland/system-updates.md#program-stage),
with recursive current/previous revision retention; online updates remain future work. App/resource handles delegated to a child retain backing after
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
this is an allocation bound, not a measured peak. Capture copies only the
selected executable, with a shared 128 MiB serialized-image ceiling for HOST,
NPFS and RAM; it never copies the whole bundle. The 256 MiB mapped-image span
remains independently bounded.

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
layout. The [accepted adapter direction](#zip-adapter-proposal) below defines
frozen native views and userspace resource preparation; neither is implemented.
A userspace provider alone cannot supply startup DIRECTORY or launchable FILE
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

## ZIP adapter proposal

Source refreshed at main `a617bcd7` on 2026-10-11: the
[launcher](../../kernel/object/launcher.c) requires a native `OBJECT_FILE`, and
[startup roots](../../kernel/user/startup.c) require `OBJECT_DIRECTORY`.
[Provider FILE exports](../interfaces/file-providers.md#file-dispatch-and-lifetime)
cannot execute directly and fail after provider exit. Kernel-internal
[snapshot creation](../../include/kernel/object/file.h) is not a userspace
publication interface. The current bundle library opens a native revision
directory through bin candidates, explicit paths or the development catalog;
it can consume a ZIP adapter's equivalent view. No `.pxa` runtime or
performance has been measured; #629 qualifies selected-image capture only.

### Three owner decisions, accepted 2026-10-09, not implemented

1. **Private native DIRECTORY/FILE views over a copied, immutable revision.** A narrow native
   preparation interface accepts a READ-authorized native source FILE, copies
   its bytes into unpublished backing on BSP, and supplies a read-only snapshot
   FILE for userspace inspection. It needs no mount, namespace-publication or
   system authority and grants none. Frozen backing cannot be written through
   surviving source aliases. Parser, builder and launcher exit must not invalidate
   committed app/resource handles: each directory/file retains the backing and
   metadata it needs until its last reference closes. Avoid ownership cycles;
   backing does not retain its entry objects. No shared revision cache initially;
   see the [hosted-build revisit point](#revision-cache-follow-up).

   Opening pins that acquired byte sequence, not a pathname. Replacement affects
   later opens; held source handles prevent redirecting an in-progress copy.
   HOST/NPFS in-place mutation during acquisition still supplies no atomic source
   snapshot, so publishers must publish complete archives by replacement and
   avoid modifying an open source. Validate the frozen copy, never reopen the URI
   to parse, decode or load. Whole-archive backing costs more RAM than held offset
   reads; current sources cannot enforce immutability against WRITE aliases.

2. **Userspace prepares every small deflated resource before committing the view.** A library uses
   bounded reads of the frozen snapshot, the existing JSON manifest parser and
   zlib raw inflate plus independent ZIP CRC checks. No complete second archive
   buffer or kernel decompressor. Private decoded bytes are copied into owned
   revision backing through the native preparation interface; retaining a mutable
   RAM FILE or a user pointer is not sealing. Kernel code independently checks
   copied names, tree structure, extents, arithmetic, budgets and requested rights;
   ZIP interpretation stays in userspace. Publish only after preparation succeeds.
   Failure closes provisional builder/snapshot/decoded handles and returns their
   owned backing when the last reference closes, with no final app view or child.

   All stored files, including executable paths readable through `app/`, pass
   their bounded CRC scan before view publication; decoded
   resources finish exactly at declared input/output sizes and pass CRC before
   import. Import checks CRC again over the owned decoded copy, so a changed
   staging source cannot publish different bytes. Stored executable views carry
   their entry offset, length and expected CRC: BSP copies only the selected
   entry through the existing 128 MiB capture
   allocation, checks its CRC over those captured bytes, then runs the eager
   loader. CRC failure discards capture and any prepared children. This is eager
   resource preparation with offset reads afterward, not extraction to a writable
   tree or a per-read provider RPC. It adds cold-launch work but avoids runtime
   decoder/provider failure and bounds retained decoded data. Lazy decoding is
   deferred.

3. **An in-repo Pyxis host packer/checker.**
   Use the existing host Python dependency, under `tools/`, with explicit profile
   checks rather than relying on generic `zip` defaults. Start with all entries
   stored; later add opt-in method-8 compression only for eligible small resources.
   Emit UTF-8 names with the ZIP language-encoding flag, consistent local/central
   records, explicit CRC/sizes, ordinary file/directory attributes, and neither
   descriptors nor ZIP64. Reject source symlinks/special files, invalid paths,
   manifest/entry mismatches and exceeded budgets. Write a seekable temporary
   output, check it, then publish the complete `.pxa` by replacement. This adds no
   compiler container or external dependency; a native ZIP writer remains deferred.
   Third-party producers may work if their result passes the same profile checks.
   Keep whole-file signing possible later without designing it here.

### Bin placement and installation

The accepted later lookup order is `bin://NAME.pxe`, `bin://NAME.pxb`, then
`bin://NAME.pxa`, followed by the development catalog and rescue. A broken
selected candidate fails without fallback. Shipped revisions reject `.pxb` and
`.pxa` forms sharing a basename; no directory scan or unrelated manifest parse
is added. Explicit archives and catalog secondary commands use the same frozen
view. The generated manifest-command index remains separate future work.

Live bin:// is the boot archive; installed bin:// is `bin/REVISION` on the
pool volume. Today Install/Update copies complete `.pxb` trees with empty
directories and verifies them after pool sync, before ESP publication, retaining
current/previous revisions. It does **not** yet select root `.pxa` files. Task 2b
must add archives as ordinary copied/verified program units and exclude them
from the rescue archive, preserving offline admission, rollback/cleanup and
publication ordering. All app assets, notices, provenance and metadata travel
inside either form. Task 2a only produces/checks archive files; it changes none
of these launch or installation paths.

### Bounds, ownership and qualification

Keep the existing draft limits as qualification inputs: 512 MiB each for archive
bytes and total expanded contents, 16,384 ZIP records, 4 MiB retained revision
index/metadata including inferred directories, 1 MiB per deflated resource,
8 MiB aggregate decoded data and 100:1 expansion. Parser staging uses the bounded
ZIP metadata and existing JSON budgets, not an archive-sized allocation; decode
one resource at a time with bounded zlib allocations. Account for workspace,
copied metadata and decoded-import overlap,
page rounding, indexes, and userspace heap pools retained after free. These are
per-revision bounds, not an aggregate budget or a physical-memory guarantee.
Profile/manifest errors return BAD_REQUEST, budgets LIMIT, allocation exhaustion
NO_MEMORY and CRC/read failures IO; existing backend errors keep their status.

Archive/decoded backing creation, import and retirement stay BSP-owned. An
indexed physical-page representation can reuse ordinary FILE reads' per-CPU
scratch discipline without publishing reusable kernel-VM aliases to APs. General
kernel mappings remain BSP-owned; a caller may still use its ordinary private
MEMORY allocation for userspace parsing/inflate under the existing
[SMP contract](../kernel/smp.md) and [VM activity/lease protocol](../kernel/memory.md#execution).
Stage inputs with short mapping leases; no user pointer survives deferred
request work or an I/O wait.

Peak includes source backing/cache, the frozen archive, metadata/parser/decoder
staging, decoded data, selected-entry capture, eager child image/stack/startup,
and other live processes. The existing catalog retains up to eight prepared
bundle views while checking all registrations; include that overlap and unwind
earlier views on failure. Capture is 128 MiB **per selected executable**, not per
archive or all command entries combined; mapped span stays independently 256 MiB.
No whole-archive allocation uses or enlarges the executable-capture budget.
Boot's existing uncopied executable path remains unchanged.

**Estimated repeated-open cost, not measured `.pxa` timing.** The
[Linux proxy sizes](hosted-clang.md#resources-and-native-limits) sum to about
169 MiB: Clang 85.94, LLD 54.59, headers 8.2 and sysroot 20 MiB, before ZIP
metadata. Use 170 MiB as a planning archive size. With no cache, each independent
bundle open copies those 170 MiB, even when selecting only Clang. Copy time is
`170 MiB / effective snapshot throughput`; throughput must include allocation,
zeroing, source reads and writes into frozen backing. Illustrative rates give:

| Source | Assumed effective snapshot rate | Estimated acquisition per open |
| --- | --- | --- |
| NPFS | 10 / 100 MiB/s | 17 / 1.7 s |
| RAM | 0.5 / 2 GiB/s | 0.33 / 0.083 s |

These rates are sensitivity inputs, not Pyxis measurements or promised ranges.
The public-FILE [native read record](../development/experiments/native-filesystem-task4.2/README.md)
measured about 147 ms per 1 MiB in an older nested VM; extrapolating that path
would give about 25 s, but it does not measure the proposed BSP snapshot import.
NPFS cache/device behavior and RAM scratch/page-copy overhead need matched
measurement. A 100-source build would copy about 16.6 GiB of archive bytes through
100 opens; catalog neighbors add further copies. Whole-view CRC scans, selected
Clang capture (another roughly 86 MiB) and eager image loading add work beyond
acquisition. RAM avoids source-device I/O but still repeats the archive copy.
The concern is accumulated launch time in `make -j`, not only resident memory;
there is no assumption that parallel launches hide BSP-owned preparation costs.

Before runtime slices 2b/2c, capture exact-main plain-launch/session costs
and matching `.pxb` launches using the same manifest, P1Fs, resource bytes and
stack. Repeat one-/four-CPU workloads, cold/repeated `.pxa` launches, peak memory
and final-handle cleanup. In QEMU/GDB inspect native read-only roots, disjoint
image/stack/guard, resource seek reads after launcher exit, and an old revision
held across archive replacement. Include a near-128 MiB selected executable
beside an unused entry so the archive exceeds 128 MiB; verify only the selected
entry is captured. Qualify malformed ZIP/CRC, budget rejection, real allocation
failure during acquisition/import, cancelled preparation and failed batches:
no publication, exact owned-page rollback and a successful subsequent launch.
Separate capture data from source caches, retained page tables and heap pools.

### Revision-cache follow-up

**Revisit at the hosted Clang build loop**, using repeated compiler launches of
one unchanged bundle and a matched `.pxb` build. A later identity-keyed cache
can share the same frozen archive, validated index/CRC results and decoded
backing when the native source FILE and its content revision are unchanged.
This fits the immutable-view and last-dependent-handle lifetime contracts:
cache eviction drops only the cache's references, and held app/resource handles
keep their original revision. Each launch still checks caller authority and
delivers its actual grants; cached bytes never cache another launch's grants.
Selected-entry capture and eager image loading remain per launch.

Hits require reliable source identity and unchanged-revision evidence. A URI,
manifest application ID, recycled numeric handle, size/mtime pair or ZIP CRC is
not sufficient. Native [FILE metadata](../../include/abi/file.h) now reports
boot-scoped identity where the backend can prove it, comparable while an original
reference is held; bind aliases
and comparisons after full close remain outside that contract. Identity plus
mtime alone does not freeze writable source bytes. Unchanged-revision evidence
and bounded cache retention/reclamation still need their own task.
If immutability or change detection cannot be proved for a backend, miss and
copy rather than return stale bytes. This is a named deferred follow-up, not an
implemented cache or a fourth owner decision for tasks 2a/2b/2c.

### ZIP tasks and owner assignment

- **2a: compliant host packaging — owner-assigned 2026-10-11.** Add the stored-only
  packer/checker for the accepted manifest plus `app/` layout. Manually compare
  local/central records and CRCs with an independent ZIP reader, including UTF-8
  names and rejected source/path cases. No native adapter or launch change yet.
  **After this task, the owner can:** package a `.pxb` as a checked stored `.pxa`
  for the following native-launch slice.
- **2b: stored-only native launch and lifetime — separate owner go required.**
  Add frozen revision preparation,
  bounded native entry/tree import and `.pxa` recognition in existing explicit
  bin/explicit/catalog lookup; pass the resulting native view to the existing manifest,
  per-program app/resource, stack and actual-grant path. No new global `app://`
  mount. Qualify a two-command bundle, held resource FILEs after launcher exit,
  replacement, near-ceiling capture and failure cleanup against its `.pxb` twin.
  Reject method 8 explicitly until the next slice.
  **After this task, the owner can:** install and run stored `.pxa` development
  bundles through the same commands and `app://` layout as `.pxb`.
- **2c: bounded small-resource deflate — separate owner go required.**
  Link the existing userspace zlib port,
  add eager decode/import and the packer's opt-in compression. Qualify raw inflate,
  exact consumption, bad CRC/truncation, ratios/decoded budgets, peak/launch costs
  and all provisional cleanup; PNG compression does not qualify ZIP decoding.
  **After this task, the owner can:** ship `.pxa` bundles with compressed small
  resources while executables and large/seek-heavy resources remain stored.

Task 2a is authorized as a standalone host tool, with independent-reader
qualification and no image-build integration or performance measurements.
Tasks 2b/2c remain unassigned. No native ZIP launch, new test/benchmark
infrastructure, signing, consent UI, ZIP64 or Clang port is authorized by 2a.

## Selected-image admission

The owner accepted these contracts on 2026-10-09; they are implemented:

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

The BSP allocates, reads, loads and releases capture pages. APs transfer the
ownership descriptor through request metadata without accessing its buffer;
reusing the kernel mapping therefore requires no remote TLB retirement. Shared
kernel page-table ancestors retain their ordinary VM lifetime, separately from
reclaimable capture data. RAM capture holds the source FILE operation busy until
the BSP finishes copying, releasing that loan on success and failure.

Capture, eager image backing, requested stack, startup/page tables and other
processes coexist; batches also retain earlier prepared children. The
[qualification record](../development/experiments/program-bundles-capture/README.md)
reports matched plain-launch and session-backing measurements, near-ceiling
native images on all three backends, and real allocation-exhaustion rollback.
Outer archive size and compression do not define either serialized-image or
mapped-span admission. ZIP and Clang remain unimplemented.

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
  preparation. Qualified in QEMU and merged in #625.
- [ ] Task 2a: compliant stored-only host packer/checker; owner-assigned
  2026-10-11, with independent-reader and rejected-input qualification.
- [ ] Task 2b: stored-only native ZIP views/launch/install and lifetime; separate
  owner go required.
- [ ] Task 2c: bounded small-resource deflate; separate owner go required.
- [x] Shared 128 MiB captured-image ceiling and failure rollback qualification.
  Implemented and qualified in QEMU; merged in #629.

Later tasks require explicit owner authorization. The native Kilo/TCC workflow
remains usable throughout.
