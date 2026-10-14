# Capability forwarders

Status: accepted contract for capability-model task 1, **not implemented**.
The remaining recovery-authority choice is proposed below; this document does
not declare an existing ABI. See the
[task scope](../wip/capability-model-implementation.md#task-1-scope-delta-2026-10-14).

A forwarder owns a target grant. Holders use its kernel-attested type and
attenuated authority; a separate controller carries CUT and, for service
slots, RETARGET. Retargeting accepts the same type and cannot widen existing
holder authority. Retargeting a service slot to nothing returns NOT_FOUND on
later use. CUT is terminal and returns DENIED instead.

Ordinary COPY, launch and IPC preserve an existing revocation lineage.
Explicit revocable copies add derivation tracking; plain grants require none.
Capabilities opened through a revocable copy inherit its lineage, so opening
a file does not escape a directory cut. Independent reply attachments retain
their own lineage. Bytes already read are not revocable.

## Controller lifetime (accepted 2026-10-14)

- **Revocable copy:** releasing its last logical controller grant cuts the
  copy and its descendants automatically, including process teardown.
  In-flight storage references do not prolong controller authority.
- **Service slot:** releasing its last controller keeps its last target for
  holders, following the owner's recovery-over-shutdown principle. The
  intended replacement manager still requires explicit management authority;
  ordinary holders gain none from manager loss.

## Operation and storage lifetime

CUT denies new operations and fails queued operations. Already-delivered
provider work may complete, subject to ordinary deadline, caller-stop and
provider/receiver lifetime rules. The delivery boundary is RECEIVE under the
endpoint lock. Existing WITHDRAW cancels received work, so its replacement
must change that behaviour rather than reuse it unchanged. Export IDs and
RETIRE/ACK remain provider-storage lifetime machinery, not a second public
revocation operation.

Native queued operations need a before-effects boundary and readiness waits
must observe cut. Creation and returned-child derivation must preflight
allocation before effects. BSP owns allocation and destruction; guards never
span allocation, callbacks or waits. Admitted operations own storage separately
from logical grant authority. No shared-memory unmapping is introduced.

## Recovery authority (proposed, awaiting owner)

Once every CUT/RETARGET grant has been released, none survives to authorize a
new controller. Keeping the target alive does not resolve that authority gap.

Default proposal: slot creation gives its creator a separate recovery
capability, delegable to a supervisor, which issues a fresh controller only
for that slot. Recovery ownership is not active CUT/RETARGET ownership. It
cannot revive CUT, widen the slot ceiling or give ordinary holders management.
Revocable copies have no recovery capability. Alternative: defer takeover and
record that losing every management grant leaves a usable but permanently
unmanaged slot in task 1.
