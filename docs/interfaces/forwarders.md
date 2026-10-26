# Capability forwarders

A forwarder owns a target grant. Holders use its kernel-attested type and
attenuated authority; a separate controller carries CUT and, for service
slots, RETARGET. Retargeting accepts the same protocol/representation and
cannot widen existing holder authority. Retargeting a service slot to nothing returns NOT_FOUND on
later use. CUT is terminal and returns DENIED instead.

## Creation and authority

[`forwarder.h`](../../include/abi/forwarder.h) defines the native
`FORWARDER_CREATE` syscall. It accepts a held target, REVOCABLE or SLOT, resource
and transport masks, and the COPY-style SAME_RIGHTS flag (explicit masks must
then be zero). Its request is a fixed 40 bytes; its reply buffer carries a
capacity, and the change that first grows the request adds its size
(chronium, 2026-10-22). It publishes two handles or neither: the holder and a
controller.
No ambient service grant is needed to attenuate already-held authority.
Allocation, target ownership and both destination slots are preflighted.

The controller uses `PROTOCOL_FORWARDER`: CUT requires CUT authority and no
payload; RETARGET requires RETARGET authority and a replacement handle (zero
empties the slot). Controllers carry no target authority. COPY can attenuate
management rights; zero-right controller copies (including forwarded proxies)
do not count as managers.
Native storage-only observers do not preserve close-bearing authority: GROUP
WAIT cannot keep CONTROL alive, terminal attachment observation cannot keep
HANGUP alive, and zero-right or INTERRUPT-only console views cannot keep input,
output, ARMED or PASSTHROUGH open. Native ARMED and PASSTHROUGH grants each carry
that right alone. Exported clients and pipe directions retain their existing
logical lifetime rules, including zero-right grants.
Insufficient replacement authority returns DENIED; wrong type returns
WRONG_TYPE; cycles are BAD_REQUEST. A cut slot cannot be retargeted.

`FORWARDER_MATCH` takes a held candidate and requires RETARGET authority. It is
read-only: OK proves the candidate follows this exact SLOT through copies or
immutable wrappers; WRONG_TYPE reports an unrelated holder, even if two slots
currently target the same provider. A distinct mutable slot is never traversed.
CUT reports DENIED, invalid handles BAD_HANDLE and exhausted traversal LIMIT.
No pointer, numeric object identity, target grant or extra authority is returned.
The shell uses this check on one captured holder/controller pair before starting
a replacement provider and retains that pair through startup/publication.

Explicit REVOCABLE/SLOT creation and RETARGET refuse native environment snapshots
and [command-directory lists](command-search.md) with WRONG_TYPE, including a
proxy retaining either container through forwarder targets,
controllers or lineage ancestors. A bounded admission walk checks those edges
under the forwarder guard; exhaustion returns LIMIT without publication or changing
the old target. This prevents held-grant containers and mutable slots from retaining each
other after every external handle closes. No accepted service uses these targets.
Ordinary container COPY and automatic immutable lineage derivation remain supported;
such a derived proxy cannot become an explicit forwarder target. Exported providers
are identified by their actual representation, not merely an advertised protocol.

Holder metadata is fixed at creation. **Accepted owner restriction,
2026-10-14:** RETARGET requires the same attested protocol and representation:
native to native or exported to exported. Native/exported switches return
WRONG_TYPE without changing the target, even for matching FILE protocols.
Their current invocation envelopes and transport masks differ; no FILE bridge
is provided in this slice. Resource and transport ceilings remain fixed. The
[debt entry](https://git.internal/PyxisOS/pyxis-os/issues/1278)
records the consequence and revisit point.

Ordinary COPY, launch and IPC preserve an existing revocation lineage.
Explicit revocable copies add derivation tracking; plain grants require none.
Capabilities opened through a revocable copy inherit its lineage, so opening
a file does not escape a directory cut. Independent reply attachments retain
their own lineage. Bytes already read are not revocable.

New descendant grants also own logical holder references to their ancestors,
so unsetting or replacing an environment publication cannot naturally retire its
provider while opened files remain held. These are not controller references: last-controller
auto-CUT still applies. A delivered OPEN that completes after CUT may publish
an already-denied child handle, preserving the operation's delivery outcome
without allowing the child to escape revocation. Provider OPEN/PREPARE replies
inherit the invoking lineage; HTTP COMMIT's new response reader and handoff
observation do too. Independently retaining an observation separates its
ordinary lifetime from control/response closure, without escaping source CUT.

## Controller lifetime (accepted 2026-10-14)

- **Revocable copy:** releasing its last logical controller grant cuts the
  copy and its descendants automatically, including process teardown.
  In-flight storage references do not prolong controller authority.
- **Service slot:** releasing its last controller keeps its last target for
  holders, following the owner's recovery-over-shutdown principle. The
  intended replacement manager still requires explicit management authority;
  ordinary holders gain none from manager loss.

A slot owns its current target without permanently inheriting that target's
lineage. Cutting a current provider denies use until the supervisor retargets
the slot using an independently held compatible grant; it does not cut the
slot itself. Operation resolution captures both the slot and the selected
provider, so admitted operations and opened children retain their original
provider's lineage across replacement. Explicit CUT on the slot is terminal.

## Operation and storage lifetime

CUT denies new operations and fails queued operations. Already-delivered
provider work may complete, subject to ordinary deadline, caller-stop and
provider/receiver lifetime rules. The delivery boundary is RECEIVE under the
endpoint lock. CUT replaces the public WITHDRAW operation; unlike the old
implementation, it does not cancel RECEIVED work. Export IDs and
RETIRE/ACK remain provider-storage lifetime machinery, not a second public
revocation operation.

Native queued operations need a before-effects boundary and readiness waits
must observe cut. Creation and returned-child derivation must preflight
allocation before effects. BSP owns allocation and destruction; guards never
span allocation, callbacks or waits. Admitted operations own storage separately
from logical grant authority. No shared-memory unmapping is introduced.

Ordinary COPY/launch/attachment capture preserves the wrapper. Operation
resolution instead pins a target and flattened ancestors; RETARGET cannot
redirect a captured operation. Native operations elect start immediately
before effects or output publication. CUT winning denies pending operations
and wakes interruptible/readiness waits; start winning preserves the real
result, including late output failure and uncertain mutations. Queued SEND
owns its context after sender exit. Native completions report DENIED when cut
before start. Cleanup still drains buffers, returned handles and backend
ownership. Short leases and BSP VM-mutation transactions remain unchanged.

The implementation currently bounds distinct ancestors/hops at 32 and reports
LIMIT on exhaustion; this is tunable policy, not ABI. Plain grants allocate no
lineage and take a null-context path. Final destruction is BSP-owned. CUT
detaches logical targets before callbacks; its queued receipt pins prevent
record reuse during cancellation. Caretaker storage keeps management metadata
safe without delaying natural provider RETIRE; admitted leaf references do
delay RETIRE until accepted work drains. RETIRE/ACK remains provider-private
storage lifecycle, not another revocation authority. Selecting a new target
after its natural retirement returns ENDPOINT_CLOSED; caretaker storage cannot
revive a retired export or enqueue fresh work after its RETIRE notice.

## Takeover authority (accepted 2026-10-14)

Controllers can be copied and the manager's supervisor (init or Continuum)
keeps a copy. A takeover uses that retained controller. There is no separate
recovery capability or implicit authority for ordinary holders. If every
controller copy is released, the slot stays unmanaged and keeps its last
target for holders.
