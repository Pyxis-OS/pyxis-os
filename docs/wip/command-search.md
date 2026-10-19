# Command search list

Status: **decisions 1–3 accepted by the owner, 2026-10-19; implementation assigned.**
Task 1 is assigned; each following accepted task starts after its predecessor merges. Packages slice 1 task 4 (#988/#991, ports #131,
userland #304) is parked until this prerequisite is delivered; no per-program
`package_catalog` grant is added.

Install a tool, then run its ordinary name from a shell, editor or build tool.
Descendants use the same search contract with delegated capabilities. Finding
code grants no launcher, network, writable-directory or administrative rights.

The [environment](../interfaces/environment.md) currently has scalar CAP/TEXT
cells, not capability lists. Shared
[program lookup](../userland/program-bundles.md#unpacked-development-lookup)
has a fixed bin/catalog/boot/home sequence. Packages switch immutable numbered
sets through `current`; their [command index](program-bundles.md#future-command-index)
is unimplemented. [Execution groups](../interfaces/execution-groups.md) support
contained subgroups, but broader [nested launch](nested-execution-groups.md)
remains a separate native-build gate. This proposal extends the existing
[one-environment model](capabilities-and-providers.md#one-environment-decided).

## Decision 1 — held list and ordinary inheritance (accepted, owner, 2026-10-19)

**Accepted:** one exported environment entry, `commands`, holds an
immutable native list capability whose members are ordered command-directory
capabilities. Member labels and descriptions are diagnostic text, never paths to
reopen or authority. The list retains each directory with its supplied rights;
a read-only directory copy suffices for command search. Introduce the small list
container rather than parallel text/count/numbered environment cells.

Publish replacement atomically; capture one owned list through lookup and child
preparation. Retained entries survive parent exit/replacement; revocation and
resource failure still apply. Caller-visible bounded capacities refuse on
exhaustion rather than truncate.

Ordinary launch adapters pass the exported list by default, including through
bundle, Lua, libuv and libc launch paths. The generic kernel launcher still
installs only supplied grants. A parent can remove/attenuate entries, replace
them using directories it holds, or withhold the list. Dropping entries preserves
relative order; changing priority is an explicit replacement. With the task 3
resolver, no child recovers omitted entries from global state: missing and
explicitly empty lists mean no bare-name search, with no implicit bin, archive,
catalog, home or cwd fallback. Explicit held paths retain their own authority.
Private job/leaf profiles may omit search.

Task 1 delivers list transport only. Until task 3 replaces the resolver, existing
bare-name lookup still uses independently held boot/bin roots and other existing
search candidates. Narrowing or withholding the list alone does not restrict
that lookup.

## Decision 2 — live package commands and one resolution (accepted, owner, 2026-10-19)

**Accepted:** generalize the planned per-set command index behind a
read-only native command-directory view, as one list entry. It is scoped to one
packages volume. Its private backing authority is not exposed: children receive
command targets and their bundle resources, not a packages-volume catalog or a
package-management grant. No per-program catalog grant or wrapper is needed.

The publisher prepares and verifies an immutable command index with each set
before switching `current`. The index maps exported names (including declared
aliases such as `ld.lld`) to exact targets in that set. Duplicate exports inside
one set refuse publication; ordinary duplicate names across search entries are
resolved by list order. An absent `current` is the valid empty set. Bootstrap an
existing nonempty set's missing index through an explicit management operation;
a missing/damaged active index is an error, never an empty view. This brings only
the index needed for toolchain discovery forward, not the program-package upgrader.

A lookup captures each backing volume's current set once, including misses,
and reuses it for repeated entries over that volume. Independent volumes have
independent set identities. Image, selected bundle command, stack, app/resources
and provenance use that captured set. A switch during preparation cannot mix
versions. Later lookups, including in an already running editor, see the new set;
running children and retained editor resource views keep the old selection.
Retain boot-only collection; never collect a target while selected holders remain.

The view issues native read-only target directory/FILE handles. It must support
capturing a fixed per-set directory view for one resolution without returning its
private store root. Keep package policy/index generation in userland; the native
view consumes a bounded, validated command projection, not arbitrary package Lua
or executable configuration. It carries command names, package ID/version and
confined tree-relative targets, never host paths or escapes from the held volume.
Implementation defines its wire layout and capture operation; this proposal
adds no placeholder ABI.
A resident service that launches on another process's behalf is unnecessary.

One libpyxis resolver walks the list and returns an owned selection: image,
complete bundle policy/resources where applicable, selected command, and entry
label/ordinal plus package ID/version/set when applicable. Every name-based
launcher uses it: shell, Lua/native jobs, Neovim/libuv and build-tool libc adapters,
including Clang locating LLD. Bundle requests are satisfied solely from the
caller's held/delegated authority; unresolved required grants refuse. The command
view grants indexed code and its bundle resources, not arbitrary package-ID
grants. Other package data needs separately selected authority; reuse a captured
set for those grants only with its originating volume. No inner executable
shortcut may discard the manifest or its stack and resource contract.
Neovim inspects the retained selection it actually launches, rather than resolving
`clangd` a second time for header views.

Libc's PATH-searching launch adapters use the same capability list.
A textual `PATH` cannot create directory authority. In the first slice, absent
PATH uses the held list; a PATH-aware libc launch can also accept its exact
diagnostic label projection. A different/unsupported textual PATH refuses at
launch instead of silently ignoring the requested search. Ordinary `setenv`
and native TEXT publication retain their storage semantics; neither silently
changes the capability list. General PATH-string translation to an explicitly
narrowed list of already-held directories is deferred. Likewise,
sharing lookup does not make spawn-and-wait an implementation of `execvp`:
process replacement must be implemented truthfully or refused. Ports use libc
and supported native spawn adapters, with no POSIX-shaped kernel or port-local
capability plumbing.

## Decision 3 — defaults, shadowing and containment (accepted, owner, 2026-10-19)

**Accepted:** trusted startup supplies read-only system commands,
then the live package-command view. System commands include installed bin and
archive/rescue command roots as explicitly ordered entries. `bin://` remains a
held directory for explicit paths; it is neither a global alias for the list nor
an implicit higher-priority fallback. The new resolver replaces the fixed
bare-name sequence and development-catalog search; a development command source
is an explicitly appended entry. The per-set index sits behind the package entry,
not beside it as a competing search mechanism.

No writable directory or cwd is searched by default. User/project command
entries are explicitly appended as read-only search views; the caller may still
hold separate write authority over their backing. An explicit list replacement
can intentionally put one first; this is the parent's local policy, not a new
privilege. First match wins across entries. Continue only on genuine name
absence; a selected malformed, inaccessible, revoked or incomplete target, or an
unreadable/damaged entry, fails visibly instead of falling through to another
command. Within a conventional directory, conflicting plain and bundle exports
of the same logical name refuse rather than choose by suffix accident.

A shared provenance query and shell `which NAME` resolve with the same rules
without executing. They report the selected entry, target and captured package
identity, or the concrete failure. A later independent launch may see a newer
set; only the retained selection used by that launch proves what it ran.

Search is independent of supervision. Withheld LAUNCH still prevents execution;
search entries do not convey it. Descendants use only an explicitly delegated
own-group/subgroup launcher; parent stop and cleanup cover the subtree. Preserve
explicit leaf profiles. Native make/CMake/Ninja chains wait for the ordinary
nested-launch/delegation policy and its qualification; this proposal neither
settles the broader default-grouping questions nor authorizes an outside-group
broker, executor or hidden wrapper. Direct editor/tool launches can qualify the
search foundation before the native-build gate opens.

## Delivery sequence

| Task | Scope and what the owner can do afterwards |
| --- | --- |
| 1 (assigned) | Immutable list capability, environment capture and explicit inherited/narrowed launch staging. The owner can pass an ordered held search list to a direct child and observe that omitted entries are absent from the inherited list. Bare-name lookup remains unchanged until task 3. |
| 2 | Per-set command projection, publication with the existing current switch, live read-only view and retained selections. A running client can discover a newly installed tool without restart or receiving the store root. |
| 3 | Common lookup/provenance, system defaults and libc/Lua/libuv/editor adapters; replace fixed search and catalog shortcuts together. Shell and editor resolve the same tool, with honest unsupported port operations. Native build-chain qualification remains gated on nested execution delivery. |
| 4 | Resume packages task 4, adapt the parked alias checkpoint to the common view, and complete its consumer checks. No per-program package_catalog grant is carried forward. |

Qualification will use ordinary builds and interactive QEMU: inherited, empty
and narrowed lists; ordered duplicate resolution and explicit project entries;
selected-target refusal without fallback; package install/switch/rollback while
a client remains running; old held resources after a switch; Neovim server/header
identity; no descendant-launch authority gained by searching. Qualify contained
compiler/linker and build-tool chains only when their nested-launch policy is
accepted and implemented. No native/PXE action or new test framework is proposed.

The three decisions above are accepted. Stop only for a new behavior, authority
or lifetime decision; packages task 4 resumes after the prerequisite lands.
