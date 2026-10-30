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

Shell, Lua/native jobs and libuv share the owned libpyxis `program_open`
selection. Bare names walk the inherited [command list](../interfaces/command-search.md)
only. Each conventional entry exposes `NAME.pxe` or a complete `NAME.pxb`;
a conflict refuses. A package entry exposes only its declared command index.
Only genuine name absence permits continuing; denied, malformed or incomplete
selected candidates fail visibly. The fixed bin/catalog/boot/home search and
development catalog API are removed. A development command directory must be an
explicit list entry; no writable directory or cwd is searched by default.

Paths containing `/` or `:` remain explicit. `bin://NAME` opens that exact path;
it supplies no suffix or catalog fallback. Use `pkg://ID/NAME.pxe` or
`pkg://ID/NAME.pxb` for a package's program, or on an installed system
`bin://NAME`, where the command view's entries are command names. An explicit `.pxb` path selects its default;
a package export can select a declared bundle command. The selected command
remains `argv[0]`. Opening an inner `app/bin/other.pxe` explicitly applies no
bundle policy. `which NAME` reports the same resolver's entry and package/set
identity without launching.

The resolver retains the selected revision, image, app directory and resource
directories until its owning program view closes. The default and every command
entry must be native FILE objects whose prefix matches `P1F_MAGIC`; scripts are
rejected. Remaining P1F validation and executable capture belong to the kernel.
A script's shebang may name a bundle as its interpreter; it then launches as
that bundle ([script launch](script-launch.md#bundle-interpreters)).

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

An exported [`path` capability](../interfaces/command-search.md) is preserved
by ordinary bundle launch without a per-program grant declaration. It carries
only its held read-only command directories, and does not supply a launcher.
Explicit leaf job profiles may withhold it.

The launcher preserves explicit streams and cwd, selected exported environment entries
and declared directory grants. `pipe/create` is an ordinary manifest request available to
foreground bundles when their supplying shell holds the pipe service; plain
foreground programs still receive no pipe-creation grant. Exact held-shell scripts
retain their launching shell's source inventory, including available PIPE CREATE,
under the [script rules](shell.md#script-runtime-grants). `clipboard_local` and
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

A TCP `listen` request additionally needs the supplying space's explicit
`tcp_listen = true` gate, or an already held declared LISTEN grant. Trusted
launchers keep the gated source private; raw children and undeclaring bundles do
not inherit it. A declared LISTEN binding is itself private and can supply another
explicit bundle declaration. Neither the space flag nor the manifest widens the
held factory scope. This grants any native permitted nonzero port on its scoped
configured interfaces; Caddy's configuration is not a port authority boundary.

TCP, UDP and echo factories retain their immutable interface scopes through
COPY and launch; a manifest cannot widen them. A TCP/UDP/echo grant may declare
`"interface": "wlan0"` (or another canonical configured `ethN`/`wlanN`/`lo` role).
Preparation resolves its retained identity using the launcher's supplied native
registry READ authority, then derives a one-interface factory from the actual
held packet scope and declared rights. It never mints policy or falls back to a
wider scope. Missing/out-of-scope required selections refuse before launch;
optional ones are omitted. The temporary factory is owned through launch
preparation and closed afterward. The child receives registry READ only if it
separately declares it. A one-interface factory already pins off-link traffic to
that interface's own gateway; no default-route change is made. `dns_udp` is a separately declared
resolver transport derived inside the launcher's held UDP scope. Its selected
`DNS_SERVER` text travels with that path; unavailable resolver authority supplies
neither an implicit UDP grant nor a fallback server. Network policy, writable
interface facets and DHCP broadcast factories stay in trusted setup handoffs.

| Resource | Rights |
| --- | --- |
| `memory` | `manage`, `create_shared` |
| `jit` | `manage` (explicit generated-code authority) |
| `threads` | `manage` |
| `keyboard`, `pointer` | `input` |
| `job_work` | `create`, `remove`, `read-files`, `write-files` |
| `job_provisioner` | `create`, `remove`, `read-files`, `write-files`, `lookup`, `enumerate` |
| `clock` | `read`, `sleep` |
| `launcher` | `launch` |
| `random`, `system_info` | `read` |
| `process_view` | `list` (every space's processes) |
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

Shared-region creation separately declares `memory/create_shared`. A program
using allocation and creation requests both on its normal runtime binding:

```json
{"name":"memory","resource":"memory","rights":["manage","create_shared"],"required":true}
```

Trusted boot-init/session/mux/Remote handoffs retain the held constructor
privately; no per-space switch enables it. Stock shell, Lua and Lua 5.1 bundles
keep their required MANAGE-only `memory` and explicitly declare a separate
optional launcher source:

```json
{"name":"memory_create_shared","resource":"memory","rights":["manage","create_shared"],"required":false}
```

Creation requests select that held source when present, otherwise a held
canonical MEMORY grant. Preparation requires native MEMORY with no transport
and attenuates one source to the declared rights; it never combines grants or
mints missing authority. A creation-bearing result stays private and can supply
another explicit declaration. Exact held native interpreter continuations
preserve their captured source inventory; interpreter bundles retain the source
through the optional declaration above. Ordinary native children and existing
MANAGE-only declarations gain no creation right. Explicit native grant lists
can still delegate held creation or region authority without changing a default
profile. Missing optional authority is omitted; missing required authority
refuses preparation. See
[shared memory](../interfaces/shared-memory.md) for SDK helpers and the opt-in
`shared-min` example and qualification record.

Generated-code consumers separately declare `jit/manage`. Trusted launchers have
a private source only when their space opts into `jit = true`; the live image's
Development, Multiplexer and Remote spaces do, and installed defaults do not. Exact held-shell scripts retain an already-held JIT source and offer it to
declaring children in every command position. A shell bundle still receives only
its declared grants, including optional JIT. Ordinary native children do not
inherit JIT through environment aliases. Explicit native grants/transfers remain
possible and apply to the receiving process. See [JIT memory](../interfaces/jit.md) for ownership,
permission transitions, quotas and caller code-lifetime obligations.

`procs --all` and other system-wide process listings declare
`process_view/list` (chronium, 2026-10-29). Boot init passes a private view to
every space's trusted startup, session, multiplexer and Remote handoffs, so under
the temporary delivery policy below every bundle that declares it receives it.
Like JIT, it never reaches ordinary children through environment aliases, and
exact held-shell scripts offer a held copy to declaring children. Listing a
program's own space needs no grant ([process listing](../interfaces/process-listing.md)).

Unknown classes/rights, system-only setup, space-factory and raw-device requests
reject even when optional. Selected external native grants use the bounded
source contract below; they confer no implicit admission authority. Under the accepted temporary policy, every
requested grant available within the launcher's selected delegated authority is
delivered, including optional ones. An unavailable optional request is absent; an
unsatisfied required request fails the whole admission. Preparation failures
unwind unpublished storage and preserve source handles. A manifest request never
synthesizes a grant: the program enumerates its actual startup resources and
queries actual handle rights. See
[temporary technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1480).

A complete development revision is published before its search entry is
made available, and a published development tree must not be mutated or deleted.
Retained native handles keep backing alive but do not freeze a writable tree or
form a snapshot; publisher discipline supplies revision immutability (see
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1481)).
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

A bundle may explicitly declare `job_provisioner` with CREATE, REMOVE,
READ_FILES, WRITE_FILES, LOOKUP and ENUMERATE rights to delegate provisioning.
The shell declares this role optionally. **Accepted, owner, 2026-10-20:** any
bundle may declare it under the current grant-all policy. Each declaring foreground launch
receives a new RAM tree when authority is available. Exact held-shell launches
use an owned, waited group; arbitrary declaring bundles without an owned scope
retain the tree until pane/session cleanup or reboot.
Revisit consent and narrowing when grants stop being grant-all.

Client `job_work` leaves remain available to session launches. Ordinary bundle
`job_provisioner` delegation remains foreground-only; exact held-shell scripts
retain their available provisioning source in foreground, pipeline, background
and capture positions, using a fresh confined subtree under the existing
enclosing owner. Interpreter selection uses the retained native FILE, without
reopening mutable interpreter names. Preparation uses the separately held ancestor
and never copies an existing client context from ordinary inventory. If both
roles are declared, client work and provisioning use distinct trees. Neither role
creates absent authority or becomes an exported root. A client leaf cannot
become a provisioner, and a retention account cannot become a factory.

Ownership is prepared before directory creation and client admission. A retained
WAIT observer allows the creator to clean a leaf only after confirmed retirement;
group-owned subtrees require confirmed group cleanup. Transient bundle release
does not destroy a live context. Unknown admission or creator failure retains
the named tree privately in RAM until reboot, without retry or invented cleanup.
A delegated provisioning subtree is retained after successful or unknown
admission even when the interpreter process exits: successors and services may
still use it. Only an enclosing owner with confirmed group retirement may
remove it, as shell-owned capture/output groups do after their wait; otherwise
it remains privately linked until pane/session cleanup or reboot. Preparation or
definite admission failure removes the unused tree. This leaves existing client
leaf retirement observation unchanged. A capture handoff does not extend private
work storage lifetime: when its owning group retires, cleanup clears/unlinks the
tree even if a recipient retained a directory capability. Do not use captured
work directories as persistent results; existing client leaves have the same limit.
See the accepted [caller-failure limit](https://git.internal/PyxisOS/pyxis-os/issues/1329).
Grant-all-when-available remains the current manifest policy; future consent is
still in the [bundle direction](../wip/program-bundles.md#future-consent-policy).

## Budgets

| Input or preparation | Budget |
| --- | --- |
| Manifest JSON | 64 KiB |
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
manifest preparation heap at maximum JSON size (an allocation
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

## Placement

A bundle is a complete package tree entry beside plain programs, such as
`pkg://nvim/nvim.pxb` and `pkg://lua5.1/lua5.1.pxb`, exported as commands. A
`DISTRIBUTION=full` archive also keeps them at its root. Each bundle carries
port notes, provenance and application/linked-dependency notices in
`app/metadata/` ([layout](system-layout.md#programs)). Installed Nvim runtime startup is substantially slower than from the
archive ([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1429)).

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

Bundles may explicitly declare `service`/`create` from their trusted launcher's
held endpoint constructor. The common resolver narrows actual rights and installs
the role privately, including aliases; ordinary children never inherit it
implicitly. Missing required authority refuses. Lua and libuv bundle adapters
follow this rule; an explicit resource selection still bounds available grants.
Under grant-all this is the general IPC constructor, not only completion creation;
see [the consent revisit point](https://git.internal/PyxisOS/pyxis-os/issues/1499).

Provider launches can additionally declare `publication`/`call` from their held
private setup caller. Publication requests zero resource rights and CALL
transport. Neither declaration manufactures authority.

## Selected external grants

The common resolver accepts `directory`, `mount`, mount-only `disks` and
read-only `file` classes with an explicit `source` key. Trusted configuration or shell mount ownership
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

A `file` request admits only `read` and receives one native FILE, such as boot
init's boot evidence for the `pkg` bundle in a `package_upgrade` space
([`pkg upgrade`](../wip/pkg-upgrade.md)).

A manifest's top-level `"packages": ["ID", ...]` gives the bundle a read-only
`pkg://` holding only those packages, selected at each launch through its
`source.packages` source and the current package set; a listed package that
cannot be resolved fails with its ID as the missing resource. `"sdk": true` adds
a read-only `sdk://` of the same set; `"sdk": "optional"` launches without it
when it is unavailable. A bundle launched by a bundle, which holds no packages
source, receives its launcher's own `sdk://` instead
([decision 59](../wip/sdk-view.md)). A bundle never inherits its launcher's
`pkg://`, nor an undeclared `sdk://`, and no grant or resource directory may be
named or aliased `pkg` or `sdk`. See
[reaching package files](../interfaces/packages.md#reaching-package-files).

DIRECTORY rights are the native lookup/enumerate/read-files/write-files/create/
remove/filesystem-info names. MOUNT rights are open-root/observe/write/retire/
interval. DISKS supports enumerate/mount-read-only/mount-read-write, never raw
open. Authority comes only from selected actual capabilities; current
[deliver-all-available debt](https://git.internal/PyxisOS/pyxis-os/issues/1480)
remains. The native parser in `lib/bundle.c` remains authoritative; the host packer
validates the same declaration fields without claiming runtime availability.
