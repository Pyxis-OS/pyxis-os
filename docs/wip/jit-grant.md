# JIT grant: first kernel delivery

Assigned to Codex alpha (chronium, 2026-10-27), after merged BSP-removal task 3
(#1500), before tasks 4–5. This is the kernel/SDK/launcher task from
[Quake 3 and llvmpipe](quake3-llvmpipe.md#decided-by-chronium-round-1-2026-10-23),
not a decision to enable llvmpipe or JIT by default. The three choices below
remain proposed; no implementation before chronium decides them.

## Already accepted

A separate `jit` resource has one `manage` right. Both `jit = true` on the space
and an explicit bundle declaration are required; the space gate defaults off.
Holding ordinary `memory/manage` never authorizes executable allocation or
permission changes. Regions belong to the receiving process, not the service
handle, and a transferred grant manages only that receiver's regions. W^X is
mandatory; writable/executable aliases and executable files remain outside this
first delivery. The public operations use native messages and libpyxis wrappers,
without a libc mmap/mprotect facade.

## Sequencing assessment

The impact of moving JIT forward is small. The merged machinery already supplies
caller-CPU kernel-root continuations, one drained process transaction, root
incarnation/generation and residency accounting, any-CPU acknowledged shared
invalidation, and an internally qualified quiesced permission-transition path.
JIT will be another caller-CPU client of that transaction from its first delivery,
not another BSP service. Private roots already leave/reload CR3 with PCID/global
pages disabled; the first JIT path keeps that proof rather than adding a
redundant all-CPU broadcast to every private transition.

The work pulled forward from task 5 is the production range-permission primitive
and JIT's focused authority/lifetime/permission qualification. Tasks4–5 pause
while this lands, then resume. They can change preparation/reclamation placement
and migrate the remaining clients without replacing the JIT resource or ABI.
The overall BSP-removal milestone is not declared complete.

What JIT will still lack until those tasks finish: off-BSP process/image/task
preparation and final reclamation; completed migration/audit of DISPLAY and
thread-context VM clients; removal of the remaining eager destruction and
scheduler/object IF=0 tails. Device owners stay fixed deliberately. JIT itself
must nevertheless prove its complete W^X and retirement contract now. Siblings
remain stopped for each mutation; concurrent live-code patching is not supplied.

## Decision 1 — region operations and read-only data

**Recommended default:** `ALLOCATE`, `PROTECT`, `RELEASE` on process-owned,
non-reused identities. ALLOCATE takes a nonzero byte count, rounds to pages and
returns identity/address/rounded size with eager zeroed RW/NX backing. An optional
placement hint is advisory only, may be ignored, and never promises proximity or
replaces an existing mapping. RELEASE consumes the whole identity; no partial
release. Closing handles leaves regions for process teardown.

PROTECT takes identity plus a nonzero page-aligned contained offset/size, with
three named targets: **writable** (RW/NX), **read-only** (R/NX), **executable**
(R/X). Mixed subranges within one allocation are allowed. No target can express
RWX. Only this service's allocations qualify: image pages, MEMORY allocations,
stack/TLS identities and stale/foreign JIT identities refuse as BAD_REQUEST.
Idempotent requests succeed after validation. No new operation queries a region
by a stale raw address.

The additional read-only target is a concrete consumer need: pinned LLVM
[`SectionMemoryManager::finalizeMemory`](https://git.internal/PyxisOS/pyxis-llvm/src/commit/819c2b770271807f4436dcf76e435c0727c4a1ad/llvm/lib/ExecutionEngine/SectionMemoryManager.cpp)
protects code R/X and its separate constant-data group R/NX. Its sub-blocks also
require contained-range protection. Its optional `NearBlock` does not guarantee
placement. Supporting the actual states avoids either making constants executable
or silently leaving a requested read-only mapping writable.

Alternative: retain only the two initially described states and refuse LLVM's
read-only data request; the grant could be demonstrated but would not satisfy
this first consumer's normal finalization path. No silent downgrade is proposed.

## Decision 2 — initial process budgets

**Recommended default:** initial kernel tunables of **256 MiB total live JIT
backing** and **4096 live JIT regions per process**, across all three states.
Charges are reserved before allocation effects and released only after actual
reclamation, not handle close or cleanup queue acceptance. Ordinary MEMORY does
not spend this allowance. Existing physical/metadata exhaustion still applies;
no eviction, borrowed quota or global reclaim policy is added.

A quota failure is LIMIT; physical/metadata/non-reused-identity exhaustion is
NO_MEMORY; failed allocation publishes no region/identity. The existing memory
profile distinguishes JIT bytes/state and operation counts under its current
profile authority. Initial values are tuning, not ABI or a claim about llvmpipe's
measured shader high-water mark; measure that before enabling the real consumer.

Alternative: rely only on general memory exhaustion. That provides no separate
bound on code-generation storage/records under the newly declared authority.

## Decision 3 — transition failure and caller code lifetime

**Recommended default:** keep the existing whole-process root/borrower drainage
and absolute 1 s pre-effect quiescence budget. Before the first mapping effect,
validate the complete identity/range/target and prepare any metadata and fresh
address-wait incarnations required by a writable transition. BUSY or allocation
failure leaves old mappings, bytes, states and ownership unchanged. Commit in
bounded steps under retained process exclusion; no fallible invalidation wait or
allocation follows the first PTE effect. Reply/re-entry and completion follow
actual commit/unwind, including a native stop request.

The kernel proves permission withdrawal and instruction visibility, but the
caller owns its higher-level code lifetime: prevent concurrent writes during
publication and stop logical users before making their code writable or releasing
it. Kernel drainage is temporary exclusion, not a code-entry lease. It does not
scan saved instruction pointers or invent a deferred-code executor. A caller
that resumes into released/NX code, or removes its returning code/stack, meets
the ordinary user-fault path rather than a promise that PROTECT makes that safe.

Existing address-wait semantics remain: removing write access withdraws affected
wait registrations before PTE changes; restoring write access gives fresh
incarnations. Stale waiters cannot attach to rewritten/reused pages. JIT identities
are not eligible as the guarded MEMORY identities required for native stack/TLS
pins. There is no new authority to change those pinned regions.

Alternative: track active generated-code entrants/return addresses in the kernel
and defer transitions until those leases end. Current consumers provide no such
registration protocol; that would add a separate execution/lifetime mechanism.

## Mechanism and proof obligations

Generalize the current caller-CPU transaction entry/dispatch to admit a typed JIT
request, preserving its retained request, task, cleanup attribution and private
root ownership. Do not route through the old BSP-only DISPLAY/thread forwarding
entry. Reuse process-owned allocation/identity accounting and final teardown;
keep JIT and ordinary MEMORY ownership checks distinct.

The range helper must prevalidate/preallocate before effects, unlike a caller
loop over today's single-page `vm_protect`, which can exhaust a fresh wait token
on a later page. Each step retains the same process token; no allocation,
IPI wait or consumer callback occurs inside a short page-table guard. Both
permission directions and release preserve root exclusion until completion.

Instruction visibility is an architectural proof in addition to TLB eviction.
Intel's [SDM, self-/cross-modifying code](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf)
requires publication synchronization and serialization before another CPU executes
modified instructions. Its [MOV-to-control-register reference](https://cdrdv2-public.intel.com/671110/325383-sdm-vol-2abcd.pdf)
classifies MOV CR3 as serializing. Caelum's `arch_space_activate` unconditionally
writes CR3 on private re-entry after the committed generation is admitted. The
implementation review must establish that every JIT return/sibling entry crosses
that boundary; SYSRET alone is not the proof. Check the equivalent AMD behavior
and keep architecture maintenance behind the native PROTECT completion contract.
No assumption that a local INVLPG flushes another CPU's instruction stream.

LLVM's platform adapter follows in its own focused change. Its pending section
blocks are address/size sub-blocks, not copies of the original owning MemoryBlock;
the adapter must retain the original identity/service through sub-block calls and
must not turn a stale address into authority over a new allocation. Preserve
ordinary non-JIT LLVM RW allocation without requiring the new grant. Settle any
actual adapter lifetime choice when that task reaches it. Dual-view regions,
executable file mappings, sparse JIT regions and strict-near placement are deferred.

## Delivery and qualification

1. Kernel/ABI/SDK foundation: separate object/right and process ownership,
   caller-CPU transaction client, bounded all-or-nothing range permissions,
   budgets/profile accounting and teardown. No ordinary MEMORY execute right.
2. Userland declarations/config checker, space provisioning and libpyxis wrappers;
   dependency PR before the parent pin. No shipped space enables JIT implicitly.
3. Manual native consumer and QEMU/GDB on one/four CPUs: missing/right-attenuated
   authority, cross-process/stale IDs, mixed subranges, R/NX data, both W/X
   directions, no RWX aliases, real noncalling sibling execution, address waits,
   release/reuse, budget/physical refusal, stop/unwind and joined retirement.
   Ordinary boot, existing memory/Go/display checks remain controls. Capture the
   baseline before implementation and record matched transition/closure costs;
   nested QEMU and native results stay separate. No new benchmark framework.
4. LLVM adapter and a real MCJIT-generated function using code plus read-only data;
   then the separate llvmpipe task. Native/PXE qualification goes through the
   orchestrator; no native action is authorized by this design PR.

## You should know

This adds executable-memory authority, gated twice and off by default. A declared
grant does not grant live-code patch safety. Transitions may return unchanged
BUSY while another mutation/borrower prevents quiescence, or LIMIT/NO_MEMORY
before effects. The proposed quota and caller lifetime rules require chronium's
decision. BSP preparation/reclamation and sibling-stop costs remain visible
until the later work changes them; no new grant will hide those limits.
