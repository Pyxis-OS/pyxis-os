# Program bundles: first slice

Status: owner-assigned proposal only, 2026-10-09. No bundle implementation,
signing or consent UI is authorized. This develops the
[2026-10-07 application-bundle direction](vfs.md#application-bundles) and
[hosted Clang's admission needs](hosted-clang.md#resources-and-native-limits).
It follows the pending [capacity revision #617](https://git.internal/PyxisOS/pyxis-os/pulls/617):
high guarded stacks, a 256 MiB image span and a **1 MiB plain-program stack**.
The owner chose manifests as the route to larger stacks after the fixed 8 MiB
default's measured cost. Bundles need no new P1F field or compiler-container
rebuild; their load parameters do need coordinated kernel ABI, SDK and libc work.

## Inspected starting point

[`app://` is reserved and unbound](../userland/system-layout.md#roots).
`bin://` currently exposes ordinary executable files from the running revision;
bare commands search it, then `boot://`. The
[native launch request](../../include/abi/launcher.h) takes an already-open native
FILE, explicit attenuated grants, roots, streams and startup context. It has no
manifest parser, stack request or bundle policy. HOST and NPFS capture at most
16 MiB of the whole serialized executable before loading. The loader needs
contiguous stable bytes, so a mounted archive does not by itself remove capture.

Startup already enumerates actual named resources/roots; handle information
reports their rights and transport. Its snapshot is immutable, not a requested-
grant checklist or runtime grant broker. Launch capture and child startup each
have a 64 KiB budget; libc's launch profile permits 16 roots. Startup roots
require native DIRECTORY objects and image launch requires native FILE objects:
an exported FILE-protocol endpoint is not currently an executable input.
The existing [zlib port](../development/ports.md#zlib-development-library)
supplies inflate and CRC32, but no ZIP parser or mounted native directory view.

These are inspected interfaces. Storage, manifest and budgets below are proposed;
the Linux Clang/LLD sizes are existing proxy measurements, not native qualification.

## Bundle unit and per-program view

Evaluate the owner's ZIP idea as one installable application unit, with this
same logical layout in either form:

```text
manifest.json
app/
  bin/clang.pxe
  bin/lld.pxe
  lib/clang/23/include/...
  sysroot/...
```

The owner's proposed minor names are **`.pxa`** (Pyxis archive) for ZIP and
**`.pxb`** (Pyxis bundle) for an unpacked directory. Recognition validates the
layout/format; an extension alone is not admission or authority. The unpacked
form preserves Kilo/TCC development without requiring a native ZIP writer.
Default: accept it through explicitly configured development lookup, with the
same manifest and grant rules; ordinary plain executables continue to work.

The bundle-aware launcher grants the child a read-only directory rooted at
`app/`, bound as **that program's `app://`**. It receives LOOKUP, ENUMERATE and
READ_FILES, without mutation rights or a parent-navigation escape. Each program
sees its own files; there is no shared `app://` listing of installed applications.
Plain programs without bundles receive no implicit app root. Named resource
directories are additional read-only views inside that same revision. Writable
user data, source and output remain outside it and require ordinary grants.

An installation/lookup catalog maps each exposed `bin://` command to a bundle
revision and relative entry. The resolver carries that association to the
bundle-aware launcher, rather than discarding it after opening a raw executable.
Several command names may select one entry (Clang's argv[0] aliases), or different
entries such as LLD. There is no executable copy or symlink requirement. Default:
reject command collisions in an active lookup view; explicit replacement changes
the complete registration, not one entry at a time. Keep existing plain `bin://`
and rescue `boot://` commands alongside registered bundle commands.

Publish a complete revision before activating its manifest/command registration.
Never mutate a published revision in place. Launch pins one revision for the
manifest, executable and all later app/resource lookups; updates switch new
launches while existing handles retain old backing. A read-only child grant
alone does not provide that immutability against the updater. Archive backing
and decoded resource ownership must survive the launching process and remain
until dependent handles close. Unpacked development revisions must follow the
same publication rule; editing a live tree is not a supported snapshot.

## Manifest and load parameters

Default: bounded UTF-8 **JSON data**, not executable Lua. The proposed schema
has `format: 1`; reject duplicate keys, unknown fields, invalid types and malformed
bundles rather than fall back to plain launch. Example values illustrate the
Clang-shaped unit, not an implemented port:

```json
{
  "format": 1,
  "id": "org.pyxis.clang",
  "entry": "bin/clang.pxe",
  "commands": {
    "clang": "bin/clang.pxe",
    "clang++": "bin/clang.pxe",
    "ld.lld": "bin/lld.pxe"
  },
  "stack_bytes": 8388608,
  "resource_dirs": {
    "clang-resource": "lib/clang/23",
    "sysroot": "sysroot"
  },
  "grants": [
    { "name": "launcher", "resource": "launcher", "rights": ["launch"], "required": true }
  ]
}
```

| Field | Proposed first-slice meaning |
| --- | --- |
| `id` | Stable application identifier retained across updates; registration controls its association, not a self-asserted string granting trust. Consent identity authentication remains later work. |
| `entry` | Default native P1F entry, relative to `app/`. |
| `commands` | Flat command-name to relative native-entry mapping; argv[0] preserves the selected command. No arguments or executable code in metadata. |
| `stack_bytes` | Omitted means 1 MiB. Explicit integer, page-aligned, 1–8 MiB inclusive; invalid/over-cap requests reject, never silently clamp. One request applies to this bundle's entries; per-command sizing is deferred. |
| `resource_dirs` | Startup root-name to relative directory mapping, all read-only. Names must not collide with app or existing startup bindings. |
| `grants` | Named requests for recognized ordinary service classes, scoped rights and required/optional status. Types/scopes come from a system-owned catalog, never arbitrary handle numbers. |

Entries/resource paths stay within the app view: no absolute paths, schemes,
empty components, `.` or `..`. Requests and startup must fit their existing
budgets. The kernel bounds the initial-stack parameter for single and batch
loads, places its guard directly below the requested eager backing, and returns
the actual top. Plain launch and boot init omit it and keep 1 MiB. No automatic
growth, public threads or TLS follows. All validation/preparation precedes child
publication, with complete unpublished cleanup on failure. A manifest is launch
policy metadata, not a universal restriction on independently delegated native
FILE/launcher capabilities.

## Grant delivery now, consent later

**Owner policy now (2026-10-09): deliver every requested grant** that is a valid
ordinary resource available within the bundle launcher's delegated authority.
This policy lives in the bundle-aware userspace launcher/system policy layer;
the kernel still prevents gaining rights or transport the supplier does not hold.
Metadata cannot obtain system-only mount/setup, space-factory or raw-device
authority. Such requests reject as invalid. Existing space ceilings and group-
bound launch delegation remain in force; a compiler gets LAUNCH, not authority
to create privileged spaces or administer mounts.

If a required request cannot be supplied, launch none of the program: admission
is all-or-nothing for that required set. An unavailable optional request is absent;
available optional requests are delivered at launch under this temporary policy.
From day one the program enumerates **what it actually received** through startup
resources/roots and queries actual handle rights. Never synthesize a successful
grant or infer receipt from the manifest. Normal streams, cwd and input/output
grants remain explicit launch context; the manifest does not confer access to
arbitrary documents. “Grant everything available” is
[temporary technical debt](../technical-debt.md#temporary-bundle-grant-policy),
not a permanent security contract.

**Owner's later direction, recorded but not implemented:**

- Required grants are requested at first launch and approved or denied as a
  whole; denial means no launch. The answer is remembered for the user/application.
- Optional grants are requested **in context when their feature is first used**,
  individually approved/denied and remembered. They are not all requested at
  first launch; programs handle absence at runtime.
- A trusted file picker's selection is itself the grant to that selected file,
  instead of broad document-access authority.
- An update adding a required grant asks again, including increased scope/rights.
  This depends on stable application identity across revisions; merely reusing
  a manifest ID or installation pathname does not establish that identity.
- Revocation applies at the next launch unless a grant explicitly supports
  withdrawal while running, including its delegated copies.

Startup describes the initial granted set. Later optional/picker delivery needs
an explicit broker result that the program can inspect; it cannot rewrite an
immutable startup snapshot. This note records that future need without defining
a broker API, signing scheme, consent UI or runtime-revocation mechanism.

## ZIP profile and resource budgets

ZIP distinguishes stored data, compression methods and size/offset metadata;
the Pyxis subset is a deliberate packaging restriction, not general ZIP support.
See [PKWARE's format specification](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT).
Stored entries permit backing-file offset reads. Deflate requires decoding from
the stream state unless a separate index exists; no such index is proposed.
Raw inflate also needs an independent ZIP CRC check
([zlib manual](https://zlib.net/manual.html)).

Default: require **stored** manifest, executable entries and seek-heavy/large
resources. Permit method-8 deflate only for small resources, decoded once into
bounded backing before exposure. Draft implementation budgets, to qualify before
shipping: 64 KiB manifest, 16,384 entries, 4 MiB index/metadata, 512 MiB archive
and total declared expanded contents; at most 1 MiB output per deflated entry,
8 MiB aggregate live decoded backing and expansion ratio at most 100:1.
Budget exhaustion is an explicit error, not truncated output or unlimited cache.
Absolute output/allocation caps remain necessary even with a ratio check.

Require UTF-8 names (ASCII is valid), with no code-page fallback. Validate local
and central records consistently, with overflow-safe offsets and
all data/metadata inside the file. Reject duplicate names, file/directory or
inferred-parent collisions, absolute/drive paths, NULs, backslashes, `.`/`..`,
encryption, unknown methods/flags, overlapping ranges, split archives and
symlink/special-file types expressed by attributes. Ignore ordinary Unix
ownership/permission/execute attributes: they confer no authority. Explicit
directory records may match inferred parents; conflicting types or duplicate
actual records reject. Treat entries only as regular files or directories. The first subset rejects
data descriptors and ZIP64: the stated sizes/counts need neither; add ZIP64 only
if a later admitted size requires it. Packaging instructions must name this
subset; a generic ZIP tool's defaults are not guaranteed to match.

Check the stored manifest's CRC before interpreting policy, and executable CRC
during capture before loading. Check stored resource CRC
by a bounded streaming scan before its first exposure, retaining the result only
for that immutable revision; no full-entry allocation is needed. Deflated files
must complete exactly within the declared input/output sizes and pass CRC before
exposure. CRC detects corruption, not provenance or authority. A single archive
keeps room for later whole-file signing; no signature layout is designed here.

The view needs native archive-backed directory/file support or explicit provider
integration. A userspace ZIP parser/provider alone cannot currently supply a
startup DIRECTORY or launchable FILE. Keep decompression in userspace using the
existing zlib port; design the native adapter/backing lifetime before coding,
preserving BSP allocation/VM-mutation ownership. An alternative is validated
unpacking to an immutable installed directory: simpler native interfaces, but
duplicate storage and extraction cost, and it is not a direct mounted ZIP view.

## Installed executables and the Clang-shaped bundle

A stored archive entry is random-access data, not an executable memory mapping.
The smaller first loader change is to capture **only the selected executable**
from that view, not the whole archive, then use existing eager segment loading.
Default proposal: captured executables share a **128 MiB** serialized-image
ceiling, replacing the installed HOST/NPFS limit and covering new bundle entries
and RAM copies too. This also permits larger ordinary installed programs;
plain programs still keep the 1 MiB stack. Use one kernel capture policy across
the relevant workers, not a caller-supplied claim that a file is a bundle.
Unpacked bundle entries are ordinary files, and today's kernel sees no trusted
manifest/registration association. Retaining 16 MiB for plain files while allowing
128 MiB only for bundles would therefore need separate verified admission
authority; filenames or JSON alone cannot enforce it. Existing immutable boot
archive bytes need no new copy. Neither compression nor outer ZIP size defines mapped span.
The 256 MiB rounded image-span ceiling remains independently enforced.

These are provisional admission budgets, not native Clang size measurements or
guarantees of fitting RAM. A load can retain 128 MiB capture plus up to 256 MiB
eager image backing, requested stack/startup/page tables and other processes;
batches also retain earlier prepared children. Admission must fail cleanly under
pressure, with bounded allocations and full rollback. Removing whole-executable
capture would require a separate reader/segment-loading contract and its failure
ordering; offset reads alone do not implement it. Retaining the 16 MiB cap would
leave the Clang-sized executable blocked despite its mounted bundle.

The first Clang-shaped bundle contains separately stored native Clang and LLD
entries, declares an 8 MiB stack, and supplies read-only roots for roughly 8 MiB
of Clang resource headers and the roughly 20 MiB source SDK sysroot. The
[existing Linux proxies](hosted-clang.md#resources-and-native-limits) are about
86 MiB Clang and 55 MiB LLD: each motivates larger selected-image capture, not
capture of their combined archive. Clang receives group-bound launch authority
for the linker and command lookup, plus explicitly supplied source/cwd/streams
and output-directory rights outside the bundle. A self-contained sysroot is the
default; sharing a separately versioned SDK root can follow with its own lifetime
contract. Resource layout and native path adaptation still need the compiler
port; installing this bundle does not resolve Clang's runtime/metadata blockers.

## Three owner decisions

1. **Storage and view:** default restricted ZIP `.pxa` with stored executable/large
   entries and bounded small deflate, plus equivalent unpacked `.pxb` development
   bundles. Each child gets its own `app://`. Alternative: unpack ZIP at install
   into native immutable directories, reducing adapter work at storage/copy cost.
   Extension spelling is the owner's minor choice, not an ABI requirement.
2. **Manifest and grant contract:** default the JSON fields above, 1 MiB omitted
   stack and an 8 MiB eager maximum, actual startup grants from day one, and the
   owner's temporary deliver-all-available policy. Record the later consent/picker/
   identity/revocation direction. A larger stack maximum needs new memory evidence;
   executable manifests add unnecessary policy execution to this slice.
3. **Large-image admission:** default selected-entry capture with a proposed
   shared 128 MiB captured-image ceiling, replacing installed 16 MiB and bounding
   RAM copies too; mapped span remains 256 MiB. Qualify peak memory/rollback before implementation completion.
   Alternative: reader-based segment loading, reducing peak copies at a larger
   loader/interface change. Bundle-only larger capture needs verified admission
   authority; keeping all capture at 16 MiB defers Clang admission.

After acceptance, the first bounded task should define the manifest/command
resolution and startup app view using an unpacked development bundle, including
the bounded stack request and actual grants. ZIP adapter and larger capture then
need explicit implementation tasks and matched peak/launch/cleanup qualification.
The native Kilo/TCC workflow remains usable throughout. This proposal assigns
neither those tasks nor a Clang port and stops for owner review.
