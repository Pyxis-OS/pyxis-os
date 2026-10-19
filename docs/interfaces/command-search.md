# Command-directory lists

The [command-search contract](../wip/command-search.md) provides immutable ordered
lists of directory capabilities, live package-command directories and an explicit
owned-selection resolver. Existing launcher defaults and `program_open()` lookup
remain unchanged until task 3 migrates their adapters.

The exported `commands` entry in the [process environment](environment.md) holds
one `PROTOCOL_COMMAND_LIST` CAP with `COMMAND_LIST_RIGHT_READ`. Ordinary shell,
session/remote, bundle, Lua and libuv launch adapters preserve that exported
value from their owned environment capture. A withheld/private value is not
inherited. An exported TEXT or wrong-protocol `commands` value refuses ordinary
launch; it is not silently discarded. Explicit native job resource profiles
still receive only their selected grants.

## Construction and publication

`ENVIRONMENT_CREATE_COMMAND_LIST`, requiring WRITE on the caller-bound environment,
accepts a copied array of held directory handles, requested masks and diagnostic
labels. It returns a new owned list without changing the environment table,
generation or any name. Publishing it is a separate ordinary atomic SET of the
`commands` CAP, optionally exported. This constructor provides no directory or
launcher authority the caller does not already hold.

Each member preserves its native directory grant and revocation lineage. Requested
rights must be an exact held subset of LOOKUP, ENUMERATE and READ_FILES; transport
is zero. Writable rights, exported-provider directories and non-directory objects
refuse. Zero-right members and empty lists are valid. Labels have at most 63
bytes before NUL and zero padding; they are descriptions, not paths or protected
names. Duplicate labels or directories do not change the supplied order.

Construction is all-or-nothing. Current bounded kernel policy admits 16 entries;
INFO reports that capacity and admission exhaustion returns LIMIT. It is not a
format/ABI maximum. User addresses, masks and grants are captured before BSP
allocation; no user buffer remains borrowed across the service wait.

## Read and lifetime

| Operation | Result |
| --- | --- |
| INFO | Immutable member count and current constructor capacity. |
| GET(index) | Diagnostic label and a new independently owned directory grant with the stored masks. Out of range is NOT_FOUND. |

Metadata does not promise availability. GET may return a retained cut directory
wrapper successfully; using it still refuses. Neither GET nor copying a list
unwraps revocation or freezes a directory's contents.

The list owns its members through logical capability ownership. Captured copies,
child grants and environment cells preserve that ownership independently of the
original creator and publication. Replacing/unsetting the parent entry or closing
the original directory handles does not change an already held list. Last logical
list CLOSE releases its member grants synchronously, even if an admitted operation
still retains storage; final memory reclamation stays on the BSP. GET retains a
provisional logical owner while cloning its selected member.

Explicit REVOCABLE/SLOT creation and RETARGET refuse lists, including targets
whose forwarder/controller ancestry retains one, with WRONG_TYPE. This applies
the [grant-container restriction](forwarders.md) already used for environment
snapshots: it prevents mutable captures and member/slot reference cycles.
Ordinary COPY, launch/IPC and automatic immutable revocation derivation remain
supported. Directory members may themselves be revocable or retargetable.

## SDK and launch staging

The SDK exports [command_search.h](../../userspace/include/command_search.h),
with create/info/get/publication helpers and an owned `command_search_capture`.
Capture reads one immutable list publication and owns the returned member handles;
close it once. A missing `commands` entry returns NOT_FOUND, while an empty list
succeeds with zero members. Staging borrows that captured list until synchronous
launch capture completes; failures preserve the caller's staging counts.

Narrowing constructs a new list from already-held members and attenuated masks.
Publication never retargets an existing list. Parents may explicitly replace,
reorder or withhold their exported list; children cannot reacquire omitted members
from this interface. Native launch still installs only supplied grants, and
search lists confer no descendant-launch authority.

This narrows list membership only. Until task 3 replaces name lookup, a child
can still resolve bare names through independently held boot/bin roots and the
other existing search candidates, even when its list is empty or withheld.


## Live package-command directories

`package_commands_open()` derives a native read-only command directory from a
held npfs volume root with LOOKUP and READ_FILES. Subdirectories and other
backends refuse; this neither mounts nor discovers a volume. Returned rights
are the held subset of LOOKUP, ENUMERATE and READ_FILES. The private backing root
is retained internally and never returned. The new operations share DIRECTORY;
see [the ABI](../../include/abi/package_commands.h).

`package_commands_capture()` returns a fixed directory over one selected set.
An absent `current` captures the empty set. An unreadable/malformed selector or
missing/damaged active index is IO, never an empty result or a fallback. The
kernel recognizes only the bounded existing literal selector schema, not package
Lua or executable configuration. The filesystem worker validates and owns the
projection and its originating root; no user buffers or user-process lifetime
are borrowed. Current admission is 256 exports per projection, within the
shared 1 MiB native-wrapper budget; exhaustion returns LIMIT, never truncation.

INFO reports a kernel-issued volume domain, diagnostic pool/volume UUIDs, set,
live/fixed state, count and limit. The held-lifetime domain identifies captures;
stored UUIDs alone cannot prove origin because media can be cloned.
A caller can reuse a previous fixed capture from the same originating volume,
including an empty capture, after `current` changes. Rights are intersected and
both grants' CUT lineages apply. A fixed source refuses reuse of a different set.
Native target opens preserve those lineages and ordinary held-object lifetimes.
Mutation through a view refuses, and no view returns its private store root or
arbitrary package-ID authority.

LOOKUP returns only an indexed native READ FILE or read-only complete `.pxb`
directory, plus its command/package/version/set provenance. Ordinary directory
lookup and enumeration also expose indexed logical names; capture explicitly
before a multi-operation resolution. The kernel validates all projected strings,
sort order, duplicate absence, kinds and confined tree-relative paths. It opens
only `store/ID/VERSION/tree/PATH` using held inodes, with no URI re-resolution or
bundle-inner shortcut. Publication is responsible for validating each declared
bundle command and package inventory before the index becomes current.

## Explicit owned selection

`command_search_resolve()` in [command_selection.h](../../userspace/include/command_selection.h)
walks a supplied held list without adding any defaults. It returns one owned
selection containing the image, full bundle policy where present, selected name,
entry label/ordinal and package provenance. Each originating package volume is
captured once per resolution, even on a miss; later entries reuse that capture.
Independent volumes retain independent set identities. Only name absence permits
continuing. Conventional directories expose `NAME.pxe` or `NAME.pxb`; conflicting
exports refuse. A generated suffix exceeding a backend's component limit cannot
select a target and is treated as an absent candidate.

Keep the selection alive through launch preparation/admission and inspect that
same selection for resource views. A bundle retains its stack, manifest grants,
app and resource directories, rather than launching an inner executable directly.
When it requests other package trees, preparation requires separately held
package-source authority from the same actual mounted volume, checked by native
capture reuse rather than stored UUID equality, and uses its captured set number. The command view alone supplies no such package grant or launcher.
Closing a selection releases its image/bundle and fixed view. Switching current
does not retarget existing selections; later resolutions see the new set.
Boot-only package collection remains unchanged in scope.
