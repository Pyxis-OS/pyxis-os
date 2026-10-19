# Command-directory lists

Task 1 of the [command-search contract](../wip/command-search.md) implements an
immutable ordered list of directory capabilities. The live package view, common
name resolver and default search list are later tasks; existing command lookup
is unchanged in this slice.

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
