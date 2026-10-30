# Program bundles: ZIP archives and command index

Implemented behavior (unpacked `.pxb` manifests, per-program `app://`, grants,
stack requests, the 128 MiB selected-image ceiling and bin placement) is in the
[program bundles reference](../userland/program-bundles.md). This page keeps the
parked ZIP work, the future command index and the consent direction.

Status (2026-10-11): the owner parked ZIP tasks 2b/2c; no current consumer needs
single-file archives and unpacked `.pxb` works. Signing, consent UI and Clang
remain outside this slice. The owner accepted the three ZIP defaults on
2026-10-09 and assigned task 2a, the standalone host packer/checker, which is
merged ([#706](https://git.internal/PyxisOS/pyxis-os/pulls/706)) and stays.

Task 2b is implemented and qualified on retained draft branches:
[Pyxis #717](https://git.internal/PyxisOS/pyxis-os/pulls/717)
(`bundles/zip-native-task2b`, `1d30c4cb`),
[userland #213](https://git.internal/PyxisOS/pyxis-userland/pulls/213)
(`bundles/zip-native-task2b`, `15dac1d8`) and
[ports #98](https://git.internal/PyxisOS/pyxis-ports/pulls/98)
(`bundles/zip-native-docs`, `42fce86`). These are not main's behavior. Task 2c has
not started.

Before any merge, fix #717's authority/resource review finding with **both**
owner-agreed requirements (2026-10-11):

- Explicit authority to prepare views: a MEMORY grant or dedicated view right,
  so possession of a READ FILE alone cannot allocate kernel pages.
- Bounded global view-backing admission and a per-process outstanding-view limit,
  returning `CALL_LIMIT` before allocation. Account for failure rollback and
  backing retained by dependent handles, including after the launcher exits.

The grant form and limit values must be settled when resuming, and the chosen
accounting limits recorded with the implementation. Revisit when package
distribution or the hosted Clang build loop needs single-file archives; resuming
either task requires a new owner go.

## Future command index

Not assigned. Generate manifest command registrations at revision install/update
time. Conflicts fail the update and a bundle never shadows a plain `.pxe`; no
launch-time scan. Until then secondary commands use explicit paths or the
development catalog.

**After this task, the owner can:** use every installed bundle's declared command
names without manually maintaining a development catalog.

## Future consent policy

Recorded owner direction, without authorizing implementation:

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
identity authentication, consent UI and runtime revocation remain undesigned. The
interim delivery policy is [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1480).

## ZIP profile

The accepted `.pxa` form is restricted ZIP with the same manifest and `app/`
layout. A userspace provider alone cannot supply startup DIRECTORY or launchable
FILE objects, so the adapter below defines frozen native views plus userspace
resource preparation. Decompression stays in userspace using the
[zlib port](../development/ports.md#zlib-development-library), preserving BSP
allocation/VM-mutation ownership.

Require stored manifest, executable entries and seek-heavy/large resources.
Permit method-8 deflate only for small resources, decoded once into bounded
backing before exposure. The packer enforces only the ZIP32 format's bounds
(65,535 entries, 32-bit sizes and offsets); native budgets are chosen with the
reader when it exists. Draft budgets to qualify there: 1 MiB per deflated entry,
8 MiB aggregate decoded backing and expansion ratio at most 100:1. Exhaustion is an explicit error (profile/manifest
errors BAD_REQUEST, budgets LIMIT, allocation NO_MEMORY, CRC/read failures IO;
backend errors keep their status). Deflate needs an independent ZIP CRC check; see
[PKWARE's specification](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT)
and the [zlib manual](https://zlib.net/manual.html).

Names are UTF-8 with no code-page fallback. Local and central records must agree,
with overflow-safe offsets and all ranges inside the file. Reject duplicate names;
file/directory or inferred-parent collisions; absolute/drive paths, NULs,
backslashes, `.` and `..`; encryption; unknown methods/flags; overlapping ranges;
split archives; symlink/special-file attributes; data descriptors and ZIP64.
Explicit directory records may match inferred parents. Ordinary Unix owner,
permission and executable attributes are ignored: they confer no authority.
Generic ZIP tool defaults are not guaranteed to comply.

Check the stored manifest CRC before interpreting policy and the executable CRC
during capture. Before exposing a stored resource, check CRC by a bounded
streaming scan and retain that result only for the immutable revision. Deflated
files must finish exactly within declared input/output sizes and pass CRC before
exposure. CRC detects corruption, not provenance. Archive and decoded backing
survive the launching process until dependent handles close. The first profile
has no random-access deflate index or signature layout.

## ZIP adapter proposal

Source state at main `a617bcd7` (2026-10-11): the
[launcher](../../kernel/object/launcher.c) requires a native `OBJECT_FILE` and
[startup roots](../../kernel/user/startup.c) require `OBJECT_DIRECTORY`;
[provider FILE exports](../interfaces/file-providers.md#file-dispatch-and-lifetime)
cannot execute directly and fail after provider exit; kernel-internal
[snapshot creation](../../include/kernel/object/file.h) is not a userspace
interface. The bundle library can consume a ZIP adapter's equivalent view. No
`.pxa` runtime or performance has been measured.

### Three owner decisions, accepted 2026-10-09

The owner amended preparation authority/admission on 2026-10-11 as recorded above.

1. **Private native DIRECTORY/FILE views over a copied, immutable revision.** A
   narrow preparation interface requires a READ-authorized native source FILE and
   explicit preparation authority, copies its bytes into unpublished backing on
   BSP, and supplies a read-only snapshot FILE for inspection. It needs and grants
   no mount, namespace-publication or system authority. Frozen backing cannot be
   written through surviving source aliases. Parser, builder and launcher exit
   must not invalidate committed app/resource handles: each directory/file retains
   the backing and metadata it needs until its last reference closes, with no
   ownership cycles (backing does not retain its entry objects). No shared revision
   cache initially; see the [revision-cache follow-up](#revision-cache-follow-up).

   Opening pins an acquired byte sequence, not a pathname; replacement affects
   later opens, and held source handles prevent redirecting an in-progress copy.
   HOST/NPFS in-place mutation during acquisition still supplies no atomic
   snapshot, so publishers must publish complete archives by replacement. The
   frozen copy is validated and never reopened by URI. Whole-archive backing costs
   more RAM than held offset reads.

2. **Userspace prepares every small deflated resource before committing the
   view.** A library uses bounded reads of the frozen snapshot, the existing JSON
   manifest parser and zlib raw inflate plus independent CRC checks: no second
   complete archive buffer, no kernel decompressor. Decoded bytes are copied into
   owned revision backing through the preparation interface; a mutable RAM FILE or
   user pointer is not sealing. The kernel independently checks copied names, tree
   structure, extents, arithmetic, budgets and requested rights; ZIP interpretation
   stays in userspace. Publish only after preparation succeeds; failure closes
   provisional builder/snapshot/decoded handles, returns owned backing when the
   last reference closes, and leaves no final app view or child.

   All stored files, including executable paths readable through `app/`, pass
   their bounded CRC scan before publication; decoded resources finish exactly at
   declared sizes and pass CRC before import, which checks CRC again over the
   owned copy. Stored executable views carry entry offset, length and expected CRC:
   BSP copies only the selected entry through the existing 128 MiB capture
   allocation, checks its CRC over the captured bytes, then runs the eager loader;
   failure discards capture and prepared children. This is eager preparation with
   offset reads afterward, not extraction to a writable tree or per-read provider
   RPC. Lazy decoding is deferred.

3. **An in-repo host packer/checker** under `tools/` using the existing host
   Python dependency, with explicit profile checks rather than generic `zip`
   defaults. Start with all entries stored; later add opt-in method-8 compression
   for eligible small resources. Emit UTF-8 names with the language-encoding flag,
   consistent records, explicit CRC/sizes and neither descriptors nor ZIP64;
   reject source symlinks/special files, invalid paths, manifest/entry mismatches
   and exceeded budgets; write a seekable temporary output, check it, then publish
   by replacement. No compiler container or external dependency; a native ZIP
   writer is deferred. Third-party producers may work if they pass the same checks.
   Whole-file signing stays possible later without designing it here.

### Bin placement

Accepted later lookup order (2026-10-10): `bin://NAME.pxe`, `bin://NAME.pxb`, then
`bin://NAME.pxa`, then the development catalog and rescue. An explicit
`bin://NAME.pxa` selects its default. Shipped revisions reject `.pxb` and `.pxa`
forms sharing a basename; a manually assembled development view selects `.pxb`
first, and a broken selected candidate never falls through. Plain `.pxe`
precedence remains. No other archives are opened or parsed and no directory scan
is added. Explicit archives and catalog secondary commands use the same frozen
view.

Install/Update today copies complete `.pxb` trees and does not select root `.pxa`
files. Task 2b must add archives as ordinary copied/verified program units,
excluded from the rescue archive, preserving offline admission, rollback/cleanup
and publication ordering. All app assets, notices, provenance and metadata travel
inside either form.

### Bounds, ownership and qualification

Draft limits above are qualification inputs, per revision, not an aggregate budget
or physical-memory guarantee. Parser staging uses the bounded ZIP metadata and
existing JSON budgets, not an archive-sized allocation; decode one resource at a
time with bounded zlib allocations. Account for workspace, copied metadata and
decoded-import overlap, page rounding, indexes, and heap pools retained after free.

Archive/decoded backing creation, import and retirement stay BSP-owned. An indexed
physical-page representation can reuse ordinary FILE reads' per-CPU scratch
discipline without publishing reusable kernel-VM aliases to APs; general kernel
mappings remain BSP-owned. A caller may use its ordinary private MEMORY allocation
for userspace parsing/inflate under the [SMP contract](../kernel/smp.md) and
[VM activity/lease protocol](../kernel/memory.md#execution); stage inputs with
short mapping leases, and no user pointer survives deferred request work or an I/O
wait.

Peak includes source backing/cache, the frozen archive, metadata/parser/decoder
staging, decoded data, selected-entry capture, eager child image/stack/startup and
other live processes. The catalog retains up to eight prepared views while
checking registrations; include that overlap and unwind earlier views on failure.
Capture is 128 MiB per selected executable, not per archive; no whole-archive
allocation uses or enlarges that budget. Boot's uncopied executable path is
unchanged.

Estimated repeated-open cost (not measured): the
[Linux proxy sizes](../development/hosted-tools.md#qualified-capacity) sum to about
169 MiB (Clang 85.94, LLD 54.59, headers 8.2, sysroot 20), so 170 MiB is the
planning archive size. With no cache every independent open copies it even when
selecting only Clang; a 100-source build would copy about 16.6 GiB. Whole-view CRC
scans, selected Clang capture (about 86 MiB) and eager image loading add work. The
concern is accumulated launch time in `make -j`; parallel launches are not assumed
to hide BSP-owned preparation cost. The only related measurement, the public-FILE
[native read record](../development/experiments/native-filesystem-task4.2/README.md)
(about 147 ms per MiB in an older nested VM), does not measure snapshot import.

Before runtime slices 2b/2c, capture exact-main plain-launch/session costs and
matching `.pxb` launches with the same manifest, P1Fs, resources and stack. Repeat
one-/four-CPU workloads, cold/repeated `.pxa` launches, peak memory and
final-handle cleanup. In QEMU/GDB inspect native read-only roots, disjoint
image/stack/guard, resource seek reads after launcher exit, and an old revision
held across archive replacement. Include a near-128 MiB selected executable beside
an unused entry so the archive exceeds 128 MiB; verify only the selected entry is
captured. Qualify malformed ZIP/CRC, budget rejection, real allocation failure
during acquisition/import, cancelled preparation and failed batches: no
publication, exact owned-page rollback and a successful subsequent launch.
Separate capture data from source caches, retained page tables and heap pools.

### Revision-cache follow-up

**Revisit at the hosted Clang build loop**, using repeated launches of one
unchanged bundle against a matched `.pxb` build. A later identity-keyed cache can
share the frozen archive, validated index/CRC results and decoded backing while
the native source FILE and its content revision are unchanged. Eviction drops only
the cache's references; held handles keep their revision. Each launch still checks
caller authority and delivers its own grants; selected-entry capture and eager
image loading remain per launch.

Hits require reliable source identity and unchanged-revision evidence. A URI,
manifest ID, recycled numeric handle, size/mtime pair or ZIP CRC is not enough.
Native [FILE metadata](../../include/abi/file.h) reports boot-scoped identity where
the backend can prove it, comparable while an original reference is held; bind
aliases and comparisons after full close stay outside that contract, and identity
plus mtime does not freeze writable bytes. If immutability or change detection
cannot be proved, miss and copy rather than return stale bytes. Unchanged-revision
evidence and bounded retention/reclamation need their own task; this is a deferred
follow-up, not an implemented cache.

### ZIP tasks and owner assignment

- **2a: compliant host packaging, implemented (#706).** The standalone
  [host packer/checker](../development/pxa-packing.md) handles the stored-only
  manifest plus `app/` layout; independent Python ZIP and UnZip readers qualified
  records, CRCs, UTF-8 names and rejected inputs. Its host metadata accounting does
  not establish the native retained-index budget; task 2b must enforce and qualify
  that representation. No native adapter, installation or launch change.
  **After this task, the owner can:** package a `.pxb` as a checked stored `.pxa`.
- **2b: stored-only native launch and lifetime, implemented/qualified on draft
  branches; parked.** Frozen revision preparation, bounded native entry/tree
  import and `.pxa` recognition in the bin/explicit/catalog lookup, passing the
  native view to the existing manifest, per-program app/resource, stack and
  actual-grant path. No new global `app://` mount. Qualify a two-command bundle,
  held resource FILEs after launcher exit, replacement, near-ceiling capture and
  failure cleanup against its `.pxb` twin. Reject method 8 until the next slice.
  **After this task, the owner can:** install and run stored `.pxa` development
  bundles through the same commands and `app://` layout as `.pxb`.
- **2c: bounded small-resource deflate, unimplemented; parked.** Link the existing
  zlib port, add eager decode/import and the packer's opt-in compression. Qualify
  raw inflate, exact consumption, bad CRC/truncation, ratios/decoded budgets,
  peak/launch costs and all provisional cleanup; PNG compression does not qualify
  ZIP decoding.
  **After this task, the owner can:** ship `.pxa` bundles with compressed small
  resources while executables and large/seek-heavy resources stay stored.

Main has no native ZIP launch. New test/benchmark infrastructure, signing, consent
UI, ZIP64 and the Clang port are outside these tasks.

A future Clang-shaped bundle would keep Clang, LLD, resource headers and an SDK
sysroot in one revision, request an 8 MiB stack and receive group-bound linker
launch authority plus explicit source/cwd/streams/output grants. It does not
resolve the compiler port's runtime or metadata blockers.

## Task status

- [x] Task 1: unpacked development bundles (#625), shared 128 MiB capture ceiling
  (#629) and bundles in `bin://` (#691); see the
  [reference](../userland/program-bundles.md).
- [x] Task 2a: stored-only host packer/checker (#706).
- [ ] Task 2b merge: draft #717/#213/#98; parked 2026-10-11. Both authority and
  admission fixes are required before merge.
- [ ] Task 2c: bounded small-resource deflate; unimplemented and parked
  2026-10-11. Reassignment required.
- [ ] Future command index; separate owner assignment.

Later tasks require explicit owner authorization.
