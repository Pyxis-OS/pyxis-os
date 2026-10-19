# Program bundles

A program bundle packages an executable with its own resources, a stack request
and named grant requests. Only the unpacked form, a `.pxb` directory, is
implemented on main. Single-file `.pxa` archives are a
[parked direction](../wip/program-bundles.md); `tools/pxa.py` packs and checks
them on the host only ([packing](../development/pxa-packing.md)). Introduced in
[#621](https://git.internal/PyxisOS/pyxis-os/pulls/621) (design),
[#625](https://git.internal/PyxisOS/pyxis-os/pulls/625) (unpacked bundles),
[#629](https://git.internal/PyxisOS/pyxis-os/pulls/629) (capture ceiling) and
[#691](https://git.internal/PyxisOS/pyxis-os/pulls/691) (bin placement).

## Unpacked development lookup

A `.pxb` is a native directory containing `manifest.json` and `app/`. Its
manifest selects native executable entries and read-only resource directories
inside `app/`. Recognition validates the layout and metadata; the suffix alone
confers no authority. Ordinary plain programs keep their existing launch path and
receive no implicit `app://`.

Shell and Lua share libpyxis `program_open`: bare `NAME` tries
`bin://NAME.pxe`, then the default of `bin://NAME.pxb`, then an optional
development catalog, then `boot://NAME.pxe`, then `home://bin/NAME.pxe` and
`home://bin/NAME.pxb`. Only candidate absence permits
fallback; denied, wrong-type, malformed or incomplete selected candidates and
failed admission fail visibly. A direct bin hit enumerates no directory and parses
no unrelated manifest or catalog. The filename selects a bundle independently of
its ID and command names. Lookup adds no authority; it retains the selected native
view through preparation and never reopens its pathname.

The explicit `PYXIS_BUNDLE_CATALOG` environment variable names a native FILE URI
containing UTF-8 JSON data:

```json
{"format":1,"bundles":["boot://bundles/sample.pxb"]}
```

The catalog has exactly these fields, accepts at most eight explicit bundle
URIs, and does not enumerate an installation directory. It is a development
facility for secondary commands and bundles outside bin; no default catalog lists
installed bundles. Logical `bin://NAME` tries a literal FILE first, then the same
`.pxe`/`.pxb` candidates and catalog, without rescue fallback. An explicit `.pxb`
path selects its default entry without a catalog; a trailing slash is accepted.
Selected command names remain available as `argv[0]`. No filesystem directory
adapter, executable copy or symlink sits behind these logical aliases. Launching
an inner `app/bin/other.pxe` by its ordinary path applies no bundle policy.

Each catalog lookup opens and validates the complete view before selecting a
program. It rejects duplicate bundle IDs and command names, malformed manifests,
unusable registered defaults/commands/resource directories, and collisions with
plain `NAME.pxe` files through the caller's retained `bin://` root (directory
LOOKUP and zero-right FILE handles only). A broken registered source rejects the
lookup; `NOT_FOUND` means only that a valid view has no requested command. No
singleton cache or partial registration survives an error.

The resolver retains the selected revision, image, app directory and resource
directories until its owning program view closes. The default and every command
entry must be native FILE objects whose prefix matches `P1F_MAGIC`; scripts are
rejected. Remaining P1F validation and executable capture belong to the kernel.

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

| Field | Meaning |
| --- | --- |
| `id` | Stable registration identifier; a self-asserted ID does not authenticate an application or grant trust. |
| `entry` | Default native P1F path relative to `app/`. |
| `commands` | Flat command-name to relative native-entry map; several aliases may select the same entry. |
| `stack_bytes` | Optional integer, page-aligned, 1-8 MiB inclusive. Omission leaves the API parameter zero, which selects the kernel's 1 MiB default. Invalid requests reject without clamping. |
| `resource_dirs` | Additional startup root-name to relative directory map, with read-only grants. Names must not collide with `app` or other manifest roots/grants. Lookup uses the caller's path context; final child preparation rejects collisions with explicitly selected child roots/resources, so a bundle may privately name roots also present in its parent. |
| `grants` | Named ordinary resource requests with recognized rights and explicit required/optional status. |

Entry and resource paths remain inside `app/`: absolute paths, schemes, empty
components, `.` and `..` are rejected. Names reject `/` and `:`. Each bundle
entry uses the same stack request. The kernel validates it for single and batch
loads before image capture, eagerly backs the requested high stack, and places
one unmapped guard page directly below it. Plain programs and boot init omit the
parameter and keep 1 MiB. There is no automatic stack growth or per-command stack
setting; see [program loading](../kernel/program-loading.md).

## App views, grants and lifetime

Each bundle program receives its own `app://` rooted at the selected `app/`
directory, replacing an inherited app binding. App and named resource directories
receive exactly LOOKUP, ENUMERATE and READ_FILES, with no mutation rights or
parent-navigation escape. Source, output and writable user data stay outside the
bundle and use explicitly delegated ordinary capabilities.

The launcher preserves explicit streams and cwd, selected exported environment entries
and declared directory grants. `pipe/create` is an ordinary manifest request available to
foreground bundles when their supplying shell holds the pipe service; plain
foreground programs still receive no pipe-creation grant, except a script run by
`boot://init-shell.pxe`, which needs it for [pipelines](shell.md#script-mode). `clipboard_local` and
`clipboard_shared` (owner decision, 2026-10-13) follow the plain-program rule: the
shell supplies them only on foreground and session launches, and only the stores it
holds, so a game declaring them optionally still runs without them and its
[Copy/Paste](../interfaces/clipboard.md#graphics-and-sdl2) then refuses. Caller-selected
authority supplies the requests, and a shell supplies its delegated child launcher
rather than its own supervision launcher. The kernel still prevents increasing
the supplier's rights or transport authority. Unused source resource grants are
omitted. `net_registry` follows the same rule: the shell offers the handle it
holds, attenuated to the declared READ, only to a bundle that declares it, as
[`net`](net.md) does.

TCP, UDP and echo factories retain their immutable interface scopes through
COPY and launch; a manifest cannot widen them. `dns_udp` is a separately declared
resolver transport derived inside the launcher's held UDP scope. Its selected
`DNS_SERVER` text travels with that path; unavailable resolver authority supplies
neither an implicit UDP grant nor a fallback server. Network policy, writable
interface facets and DHCP broadcast factories stay in trusted setup handoffs.

| Resource | Rights |
| --- | --- |
| `memory` | `manage` |
| `threads` | `manage` |
| `keyboard`, `pointer` | `input` |
| `job_work` | `create`, `remove`, `read-files`, `write-files` |
| `clock` | `read`, `sleep` |
| `launcher` | `launch` |
| `random`, `system_info` | `read` |
| `echo` | `send` |
| `tcp` | `connect`, `listen` |
| `pipe` | `create` |
| `net_registry` | `read` |
| `udp`, `dns_udp` | `open` |
| `display` | `draw` |
| `audio` | `create` (native playback authority) |
| `screen_capture` | `capture` |
| `clipboard_local`, `clipboard_shared` | `publish`, `paste` |
| `input` | `read` (console authority) |
| `output` | `write` (console authority) |
| `profile` | `memory`, `host` |

Programs using libc allocation request `memory/manage` under its documented
`memory` startup name; declaring an alias does not rewrite libc service lookups.

Unknown classes/rights, system-only setup, space-factory and raw-device requests
reject even when optional. Selected external native grants use the bounded
source contract below; they confer no implicit admission authority. Under the accepted temporary policy, every
requested grant available within the launcher's selected delegated authority is
delivered, including optional ones. An unavailable optional request is absent; an
unsatisfied required request fails the whole admission. Preparation failures
unwind unpublished storage and preserve source handles. A manifest request never
synthesizes a grant: the program enumerates its actual startup resources and
queries actual handle rights. See
[temporary technical debt](../technical-debt.md#temporary-bundle-grant-policy).

A complete development revision is published before its catalog registration is
activated, and a published development tree must not be mutated or deleted.
Retained native handles keep backing alive but do not freeze a writable tree or
form a snapshot; publisher discipline supplies revision immutability (see
[technical debt](../technical-debt.md#published-development-bundle-revisions)).
App/resource handles delegated to a child keep backing after the launching
process closes its own view. The manifest is launch policy, not a restriction on
independently delegated FILE/launcher capabilities.

## Explicit provider grants

Accepted by the owner on 2026-10-19: a bundle may request a read-only provider
from the launcher's held exported context:

```json
{"name":"http","resource":"provider","source":"http",
 "rights":["open-read"],"required":false}
```

Only `open-read` is admitted. The source must be an exported PROVIDER with
actual READ and CALL authority; declaration cannot create or enlarge it.
Required unavailable requests fail admission; optional ones are absent.
A bundle declaring any provider grants receives only its declared providers,
with no ambient provider roots or aliases, even if every request is optional
and absent. A bundle declaring none retains the existing context behavior.
This rule is enforced by launch preparation, not by discarding a copy inside
the application. The `pkg` client declares HTTP and HTTPS this way.

## Private job contexts

The space's `jobs = true` gate supplies a private RAM provisioning tree to the
trusted startup path, independently of `launch`. A declaring client also needs
selected native clock READ, PIPE CREATE and delegated LAUNCH. The manifest's
`job_work` request causes a fresh leaf to be created for that launch; it receives
only its requested CREATE/REMOVE/READ_FILES/WRITE_FILES rights. Lua's requests
are optional, so pure Lua works without job authority and its job API refuses.
An unavailable required request refuses client admission.

`bundle_launch_prepare_jobs` receives the provisioner separately from ordinary
resource inventory. `bundle_launch_prepare` never forwards a caller's existing
client leaf. Ordinary children, captured stages and app views receive neither
the provisioning ancestor nor implicit context aliases. Existing trusted init
and session-successor handoffs retain provisioning authority, while mux and
remote startup create private subtrees for their retained session groups.
The ancestor is outside exported environment entries and cwd. No basename, path or
environment string authenticates a context.

Ownership is prepared before directory creation and client admission. A retained
WAIT observer allows the creator to clean a leaf only after confirmed retirement;
group-owned subtrees require confirmed group cleanup. Transient bundle release
does not destroy a live context. Unknown admission or creator failure retains
the named tree privately in RAM until reboot, without retry or invented cleanup.
See the accepted [caller-failure limit](../technical-debt.md#private-job-context-cleanup-after-caller-failure).
Grant-all-when-available remains the current manifest policy; future consent is
still in the [bundle direction](../wip/program-bundles.md#future-consent-policy).

## Budgets

| Input or preparation | Budget |
| --- | --- |
| Manifest / catalog JSON | 64 KiB each |
| Catalog bundles | 8 |
| Commands per bundle | 32 |
| Named resource directories per bundle | 8, plus `app` |
| Grant requests per bundle | 20 |
| JSON nesting | 16 |
| Name/path component | 255 bytes |
| Relative entry/resource path | 1024 bytes |
| Final userspace launch roots | 16 total |
| Launch capture / child startup | 64 KiB each, independently enforced by the kernel |

Final capture/startup validation occurs before child publication. The parser
allocates a bounded node array from the JSON byte count and releases each parse
tree after retaining decoded strings; code inspection gives about 5 MiB of
overlapping catalog/manifest parse-tree heap at maximum JSON sizes (an allocation
bound, not a measured peak). Capture copies only the selected executable, never
the whole bundle.

## Selected-image admission

The development manifest budget includes one extra request for a separately
scoped resolver path (`dns_udp`), preserving ordinary UDP delegation. This is
an implementation budget, not an archive or kernel launch-format limit.

Accepted 2026-10-09 and implemented in #629:

- **128 MiB per selected executable**, uniformly for HOST, NPFS and RAM capture
  (serialized image). This is one admission policy, not a global concurrent
  budget; concurrent launches may consume more in aggregate and physical-memory
  exhaustion still rejects. The 256 MiB mapped-image span is independent.
- Capture backing uses reclaimable, BSP-owned whole pages, released after loading
  or on every failure, including partial allocation; it does not remain resident
  as reusable kernel heap pools.
- Over-ceiling capture returns `CALL_LIMIT`, allocation failure `CALL_NO_MEMORY`,
  and I/O errors keep their status. A failed batch publishes no children and
  discards earlier prepared children and provisional observers. Boot archive
  bytes stay uncopied and eager segment loading remains.

The BSP allocates, reads, loads and releases capture pages. APs transfer the
ownership descriptor through request metadata without touching its buffer, so
reusing the kernel mapping needs no remote TLB retirement. RAM capture holds the
source FILE operation busy until the BSP finishes copying. Capture, eager image
backing, stack, startup/page tables and other processes coexist; batches also
retain earlier prepared children. Outer archive size does not define either
admission limit. Details are in [program loading](../kernel/program-loading.md).

## Placement in bin

Bundles live flat beside plain programs: `bin://nvim.pxb` and `bin://lua5.1.pxb`
next to `bin://grep.pxe`. Live `bin://` is the archive root; an installed
`bin://` is the running kernel's `bin/REVISION`. Each bundle carries port notes,
provenance and application/linked-dependency notices in `app/metadata/`
([layout](system-layout.md#programs)).

The installer copies complete root `.pxb` trees alongside non-rescue `.pxe`
files, preserves empty directories, and verifies names, kinds, structure, sizes
and bytes with no extra entries. Traversal and file buffers are bounded.
Filtering removes a root and its descendants at path-component boundaries, so an
unrelated `nvim.pxb-extra` stays in the rescue archive. Copy, pool sync and verify
precede the ESP switch; current and previous revisions survive recursive,
best-effort cleanup, and an unknown previous revision skips cleanup. The
same-revision and unknown-revision clear-and-rewrite interruption limits are in
[system updates](system-updates.md).

READ_WRITE target admission refuses any retained npfs pool on that disk, which
excludes running applications holding app/bin handles from an admitted target
update. Online or program-only updates need a separate revision lifetime
contract. Installed Nvim runtime startup is substantially slower than from the
archive ([technical debt](../technical-debt.md#installed-bundle-runtime-reads)).

## Qualification

Ordinary builds and interactive four-CPU QEMU passed for a two-command sample with
a larger stack, read-only roots and actual grants; GDB inspected stack/guard
permissions, retirement and unpublished batch rollback. Matched one-/four-CPU
plain launches show no observed regression: see the
[task 1 record](../development/experiments/program-bundles-task1/README.md), the
[capture record](../development/experiments/program-bundles-capture/README.md)
(near-ceiling images on all three backends, real allocation-exhaustion rollback)
and the [bin placement record](../development/experiments/bundles-in-bin/README.md)
(live and installed roots, updates, recursive cleanup). No physical-hardware
result is claimed.

Provider bundles can declare `service`/`create` and `publication`/`call`.
The trusted launcher supplies only its held endpoint-service and private setup
caller. Publication requests zero resource rights and CALL transport; the common
resolver queries and narrows the real grant. Neither class creates authority.

## Selected external grants

The common resolver accepts `directory`, `mount` and mount-only `disks` classes
with an explicit `source` key. Trusted configuration or shell mount ownership
makes a named source available; a manifest cannot create it. The launcher queries
its actual protocol/rights before preparing exact attenuation. Ordinary programs
receive no `source.*` inventory automatically. Trusted local successors preserve
that inventory under the shared eight-source budget.

A DIRECTORY request may additionally specify a distinct `root` alias. LOOKUP is
required for a URI root; alias collisions with inherited roots, app/resource
roots and declared resource names reject preparation. Cwd and its actual rights
remain unchanged. Reserved `source.*` resource names cannot be produced by an
ordinary manifest, and sources stay out of cwd/roots unless explicitly declared.

```json
{"name":"fat32_data","resource":"directory","source":"fat32_write","root":"edit",
 "rights":["lookup","enumerate","read-files","create","write-files","remove"],"required":false}
```

Neovim ships this optional declaration. Without the selected source, ordinary
startup is unchanged; with it, `edit://` is writable while ambient FAT roots stay
RO. The program enumerates/query its actual received set from startup. Required
missing grants fail before child publication; unavailable optional grants are
absent. A selected root may be inherited by permitted descendants; this is no
additional confinement beyond directory authority.

A DIRECTORY request may name `"package": "ID"` instead of `source`. The launcher
resolves it at each launch through its `source.packages` source and the current
package set, granting only lookup, enumerate and read-files; a required package
that cannot be resolved fails with its ID as the missing resource. See
[reaching package files](../interfaces/packages.md#reaching-package-files).

DIRECTORY rights are the native lookup/enumerate/read-files/write-files/create/
remove/filesystem-info names. MOUNT rights are open-root/observe/write/retire/
interval. DISKS supports enumerate/mount-read-only/mount-read-write, never raw
open. Authority comes only from selected actual capabilities; current
[deliver-all-available debt](../technical-debt.md#temporary-bundle-grant-policy)
remains. The native parser in `lib/bundle.c` remains authoritative; the host packer
validates the same declaration fields without claiming runtime availability.
