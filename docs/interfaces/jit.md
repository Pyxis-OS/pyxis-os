# JIT memory

JIT is separate from ordinary MEMORY. A `jit/manage` capability authorizes native
generated-code memory in the calling process; ordinary MEMORY remains RW/NX.
The ABI is [jit.h](../../include/abi/jit.h); libpyxis exposes the corresponding
`jit_allocate`, `jit_protect` and `jit_release` functions.

## Authority and ownership

The live image's Development, Multiplexer and Remote spaces enable JIT; its
Read-only space and installed defaults do not. A space explicitly sets `jit = true`
to provision a private JIT source for its trusted launchers. Bundle launch supplies it only
for an explicit `jit` grant with `manage` rights. Trusted session, mux and Remote
handoffs preserve that source. Exact held-shell script re-entry also retains it
in foreground, pipeline, background and capture positions, so a declaring child
can receive the same JIT authority as when launched directly from that shell.
The shell bundle declares JIT optionally and remains bounded by its manifest.
Standalone Lua 5.5/5.1 requests JIT optionally and retains its source through
exact held interpreter continuation. Child bundles still declare `jit/manage`;
embeddings and captured leaf jobs gain no inventory. Ordinary native children
and libuv outside that CLI context keep their explicit declaration rules;
an environment alias does not make JIT an ordinary inherited grant. See
[script runtime grants](../userland/shell.md#script-runtime-grants).

This is a source-provisioning gate, not an additional runtime space ceiling:
explicit native grant lists and capability transfer can convey already-held
authority. Every call checks MANAGE and acts on the receiver's own process.
Transfer never gives access to the sender's allocations. Closing a handle does
not release backing; process exit reclaims it after joined retirement.

## Operations

| Operation | Input | Result |
| --- | --- | --- |
| ALLOCATE | Nonzero byte size, advisory placement hint | Zeroed RW/NX pages and a non-reused identity, address and rounded size |
| PROTECT | Identity, nonzero page-aligned contained offset/size, named state | Whole requested interval becomes RW/NX, R/NX or R/X |
| RELEASE | Identity | Whole allocation reclaimed; identity becomes stale |

The initial allocator ignores the optional hint; it promises neither proximity
nor fixed placement. There is no RWX state, writable alias, executable FILE
mapping, sparse JIT region or raw-address permission operation. A stale, foreign,
ordinary MEMORY, image or guarded stack/TLS identity refuses with BAD_REQUEST.
Knowing an address or identity does not confer another protocol's ownership.

Initial per-process tuning limits are 256 MiB live backing and 4096 live regions,
across all states. LIMIT means quota exhaustion; NO_MEMORY means backing,
metadata or non-reused-identity exhaustion. Charges return after actual unwind
or reclamation. Failed allocation publishes no region. No eviction or borrowed
quota is added.

## Publication and caller lifetime

Each operation runs as a caller-CPU kernel continuation under the existing
private VM transaction. Roots and mapping borrowers drain before effects;
sibling user threads are held. The pre-effect admission deadline is one second.
BUSY leaves old mappings and bytes unchanged. PROTECT validates the entire
interval and reserves every fresh address-wait token before its first effect.
Its commit performs no fallible allocation or acknowledgement wait. Preparation
and commit each advance in bounded steps with the same owner retained.

Removing write access withdraws enrolled address waiters. Restoring it creates
fresh incarnations. Allocation rollback and JIT RELEASE likewise finish bounded
steps before returning. A native stop cannot abandon an admitted mutation or
publish process completion before its storage is actually reclaimed.

Private root departure precedes drainage; post-commit dispatch unconditionally
reloads CR3 before resuming the caller or a held sibling. The serializing MOV CR3
and guarded publication provide instruction visibility as well as translation
withdrawal. SYSRET or local INVLPG alone is not the proof. See the
[source and qualification record](../development/experiments/jit-grant/README.md).

Callers still own logical code lifetime: finish code users before making code
writable or releasing it, and prevent concurrent writers while publishing it.
Temporary kernel exclusion is not a code-entry lease. Resuming into released or
NX code, writing RX pages, or unmapping returning code/stack meets the ordinary
user-fault path. This interface does not make arbitrary live patching safe.

## Profiling and limits

The existing PROFILE MEMORY BEGIN/SNAPSHOT/END interval includes separate JIT
allocation/protection/release counters and process-wide live byte/region/state
gauges. Operation counters belong to the calling native thread; gauges describe
its whole process even with collection disabled. Both share ACTIVE/SATURATED.
Elapsed service/total durations include scheduling and are not CPU-time claims;
disabled collection adds no operation timing reads. Ordinary MEMORY totals keep
their existing scope.

The development snapshot grows from 136 to 368 bytes in place. Older profiling
clients must be rebuilt; the release ABI list is cleared for this deliberate
break. No compatibility operation or reader is added.

Region lookup is linear within the 4096-region tuning limit. Wait withdrawal
scans the existing global buckets outside the page-step bound. Launch preparation,
process reclamation and DISPLAY mapping work share the
[VM ownership and retirement paths](../kernel/vm-ownership.md); JIT does not
change fixed device/control owners. No native
hardware qualification or enabled LLVM consumer is implied by the manual
generated-code example.
