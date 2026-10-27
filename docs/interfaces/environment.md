# Process environment

Each process owns one table shared by its sibling tasks. Each exact name contains
an immutable capability grant (`ENVIRONMENT_CAP`) or immutable text
(`ENVIRONMENT_TEXT`). Standard streams, directories, native services, provider
clients and string variables use this same table. The
[ABI](../../include/abi/environment.h) defines the operations and layouts.

The native `PROTOCOL_ENVIRONMENT` handle is caller-bound. READ authorizes lookup,
information and snapshot capture; WRITE authorizes mutation. Copying or delegating
that handle applies its attenuated rights to the recipient's own table. It carries
no parent-table pointer or authority to enumerate another process's private values.
Launch supplies a separately owned child table; it never shares the parent's table.

## Names, values and admission

Names contain 1–63 ASCII letters, digits, `_`, `+`, `-` or `.`. They are exact and
case-sensitive, without URI normalization, protected names or a separate directory
and service name space. Wire name arrays contain the terminating NUL and zero
padding. Text may be empty, contains no embedded NUL and carries no authority.
Its byte length excludes the terminating NUL.

A capability cell owns one logical grant, preserving its wrapper, resource rights
and transport authority. SET requests exact subsets of the supplied handle's masks
through ordinary capability transfer admission. LOOKUP returns those stored masks
unchanged; callers can attenuate their returned copy separately. Metadata attests
interface and authority, not availability. See [forwarders](forwarders.md) for CUT,
slot retargeting and controller lifetime.

Current kernel admission policy allows 1,024 entries, 1 MiB aggregate name/text
bytes and 16 mutations per request. The byte charge includes each name's NUL and,
for TEXT, its final NUL; CAP values add only their name bytes. These limits are
[tunable implementation policy](../../include/kernel/object/environment.h), not ABI
constants. Policy exhaustion reports `CALL_LIMIT`. Environment text uses an explicit
caller buffer and has no endpoint payload limit of 4 KiB. Launch retains its own
64 KiB capture and startup admission budgets.

An immutable [command-directory list](command-search.md) is an ordinary CAP
value, exported under `commands` for ordinary child inheritance. Its constructor
uses WRITE on this caller-bound environment to create an owned value; it does not
mutate the table or publish a name. SET performs that separate atomic publication.

## Operations

| Operation | Required right | Request and result |
| --- | --- | --- |
| `ENVIRONMENT_LOOKUP` | READ | Exact name and caller text buffer; fixed tagged `environment_entry_reply` |
| `ENVIRONMENT_LOOKUP_FILE` | READ | Same exact lookup; CAP unchanged, TEXT as an owned sealed native READ FILE |
| `ENVIRONMENT_SET` | WRITE | Address/count of `environment_set_entry`; atomic mutation of the complete array |
| `ENVIRONMENT_UNSET` | WRITE | Address/count of `environment_unset_entry`; atomic conditional removal |
| `ENVIRONMENT_INFO` | READ | No payload; `environment_info_reply` with generation, count and byte charge |
| `ENVIRONMENT_SNAPSHOT` | READ | Capture flags; owned read-only handle and captured information |

Mutation arrays are nonempty and contain unique names. SET replaces an existing
value or inserts an absent name. Per-entry
`ENVIRONMENT_SET_NO_REPLACE` preserves an existing value and succeeds without
changing it. `ENVIRONMENT_SET_REQUIRE_ABSENT` instead returns
`CALL_ALREADY_EXISTS` if that name exists, leaving the entire batch unchanged.
These flags are mutually exclusive. Duplicate names and invalid values reject the
request before publication; capacity and generation/identity exhaustion are
preflighted before any entry changes. Successful no-ops leave generation unchanged.

UNSET with identity zero removes whichever publication currently has that name.
A nonzero identity removes only that publication. An absent name or mismatched
identity is a successful no-op. Identities are never reused within a table;
replacement creates a new identity. A batch checks each supplied identity
independently and publishes its removals together. Each changing SET or UNSET
increments the table generation once.

LOOKUP returns name, kind, flags, publication identity and captured generation.
A CAP reply installs one owned local handle; closing it does not unset the name.
A TEXT reply copies the complete value plus NUL to the explicit caller buffer.
Insufficient capacity returns `CALL_BUFFER_TOO_SMALL` **with the fixed entry reply**,
including `text_length`; required capacity is `text_length + 1`. It publishes no
text byte or handle. This is a narrow exception to the usual zero reply length on
failure. A live-table size retry can observe a newer complete value. Complete
output ranges are validated together under a short memory lease, and a failed CAP
reply closes only its unreported returned handle generation.

`ENVIRONMENT_LOOKUP_FILE` uses the same lookup request with zero text address and
capacity. It captures one immutable cell before leaving the table guard. CAP
entries retain their original authority and lineage; TEXT entries return a native
READ-only FILE containing exactly the counted bytes, excluding NUL. Empty text
is an empty FILE. Allocation and copying run on the BSP outside the guard, with
the captured cell retained across waits; no second lookup can change the value.
The reply keeps its CAP/TEXT tag and publication metadata, with an owned handle
in either case. Admission/allocation failure publishes no handle.

## Snapshots and capture ownership

SNAPSHOT captures exported entries by default. `ENVIRONMENT_SNAPSHOT_INCLUDE_PRIVATE`
adds the caller's own private values; `ENVIRONMENT_SNAPSHOT_TEXT_ONLY` excludes all
capability cells. These flags can be combined. The returned handle has
`PROTOCOL_ENVIRONMENT_SNAPSHOT` and READ authority. It supports LOOKUP, LOOKUP_FILE, INFO and
zero-based `ENVIRONMENT_ENUMERATE`; an index outside the captured count returns
`CALL_NOT_FOUND`. LOOKUP, LOOKUP_FILE and INFO also accept the shared `PROTOCOL_ENVIRONMENT`
request tag. Mutation operations cannot reach a captured table.

A snapshot fixes the selected cells and generation. Replacement or removal does
not alter a captured cell or a returned grant. Capturing a slot preserves its
wrapper identity and does not freeze its current target. Explicit CUT and ordinary
resource lifetime continue to apply. A snapshot that includes controllers owns
those controller grants; use TEXT_ONLY for caches that must not prolong them.

Native snapshots, including proxies retaining snapshot ancestry, cannot be targets
of explicit forwarder creation or slot RETARGET. Those operations return WRONG_TYPE
before publication, preventing a captured slot from retaining its own snapshot.
Normal snapshot COPY, lookup and automatic immutable revocation lineage still work.

The last logical snapshot owner releases its cells synchronously. Storage-only
operation references cannot keep their authority alive. An admitted LOOKUP retains
its selected cell before capability growth or copying, so snapshot CLOSE or live
UNSET after that capture cannot remove the logical grant being returned. Native
storage reclamation remains deferred to the BSP. Kernel provisional captures use
`environment_snapshot_release()`; a borrowed cell requires a logical snapshot
owner throughout its use.

Table allocation, mutation and snapshot creation run on the BSP without private-VM
mutation. Guards cover bounded pointer/counter access and cell-reference capture.
Grant callbacks, allocation, user copies, capability growth and waits run after
unlocking. Final cell release ends its logical grant synchronously outside the
guard. Process teardown destroys the table after admitted process activity drains.

The [SDK helpers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/environment.h)
provide allocation-free LOOKUP/ENUMERATE, owned capability lookup, and heap-owned
`environment_capture` records. A capture owns each returned CAP and copied TEXT;
close it once. Its find results and staged launch names/text borrow that capture,
which must remain live through synchronous launch capture. Libc caches only TEXT
with INCLUDE_PRIVATE and TEXT_ONLY, using INFO generation to refresh it. `getenv`
returns a borrowed value; mutation must be excluded through its last use.

## Publication

Service startup publishes the exported slot as `NAME` and its private controller
as `NAME.control` in one REQUIRE_ABSENT SET batch. Either collision rejects both
entries. The suffix is a userspace convention, subject to the same 63-byte name
limit; it grants no implicit management authority. A trusted supervisor can stage
an explicitly owned controller copy for takeover. Ordinary child profiles select
exported service grants and preserve their native allowlists.

Service replacement retargets the held compatible slot through its controller.
Unsetting
a name removes that publication only; independently captured grants keep their
ordinary lifetime and revocation rules. Revocable copies auto-cut on their last
controller release, while a service slot retains its last target after manager
loss. There is no separate namespace object, whole-map lookup grant or controller
registry.

Child standard streams are ordinary `stdin`, `stdout` and `stderr` entries.
Libc captures owned stream
grants from one startup snapshot and releases that snapshot before constructors
or main. Closing or replacing an original standard descriptor conditionally unsets
its recorded publication identities before releasing its facets. A later native
replacement is preserved; no HTTP observation companion is inferred.
Other captures and explicit aliases still keep their own grants alive. Launch
redirection changes child staging, leaving the parent's live publications intact.


## Shell selection and FILE append

The shell writes typed assignments into this table; a plain assignment is private,
and `export` makes child selection explicit. CAP argv operands stage the captured
publication only for the command using them. Different-source same-name automatic
bindings refuse before redirects/launch; matching masks or numeric handles are
not identity. Initial publication IDs are recorded solely to preserve startup
profile attenuation, without retaining another grant or live table. See the
[shell contract](../userland/shell.md#typed-values-and-command-sequences).

FILE stdout/stderr may have an exported TEXT companion `stdout.append` or
`stderr.append`, exactly `1`. It declares native APPEND policy; no extra FILE
right is created. The companion is read from the same stream snapshot, requires
a writable FILE passing a zero APPEND support probe, and is re-staged only with
that selected stream. Each operation appends at most 4,080 bytes atomically;
application writes or flushes spanning operations can interleave. Missing means
false; malformed values and stdin append are refused.
Launch adapters discard stale companions when replacing a stream. Descriptor
export propagates the adopted policy, including through Lua and libuv.


## Shell capture publication

A shell capture moves the output collector's owned result into one ordinary CAP
entry. Its account-backed FILE retains the originating session charge until the
last backing holder releases it. A native failed FILE remains failed even when
collection returned CALL_OK or its producer exited zero. No byte materialization
or hidden shell result table is used for an already re-readable handoff.

The primary and any private `.control` or `.status` companions publish in one
SET batch; companion REQUIRE_ABSENT preserves all old bindings on collision.
These are separate grants: primary copy/export/unset does not implicitly affect
companions, and a suffix is no authority or pairing proof. Inline result entries
exist only in explicit child staging and are never inserted into the parent.
Service startup retains its leaf process observer through publication, stopping
and waiting on failure, or releasing the observer after success. See the
[shell contract](../userland/shell.md#held-output-captures).
