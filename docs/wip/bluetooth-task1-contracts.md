# Bluetooth task 1: contracts and dependencies

Status: **task 1 authorized, documentation and decisions only, 2026-10-08.**
Based on main `8c4368e`, including the owner's native batch
[merged #547](https://git.internal/PyxisOS/pyxis-os/pulls/547). The
[mouse milestone](bluetooth-mouse.md) retains accepted rounds one and two.
The owner accepted both task 1 rounds below on 2026-10-08, including the
source-loss adjustment. The third round remains pending. Task 1 cannot close until
decisions and pointer coordination are recorded; it authorizes no implementation.

## Accepted first task 1 round

Accepted by the owner through the orchestrator on **2026-10-08**, incorporating
Claude's [review of #548](https://git.internal/PyxisOS/pyxis-os/pulls/548):

7. **Producer authority and submission:** a dedicated trusted startup grant for
   one process-owned Bluetooth input source, with kernel-issued epochs and
   consecutive source sequences. Submit copied normalized reports; the kernel
   owns positions/routing. Graphics-consumer grants provide no producer authority.
8. **PS/2 coexistence and source loss:** separate physical snapshots, aggregated
   by OR. Clear the lost source's snapshot/buttons. Reset accepted input and
   cancel drag only when that source held buttons; revoke lock only when it held
   buttons or no live source remains. A buttonless disconnect while another
   source is live changes nothing visible. On the ThinkPad, PS/2 remains live:
   Bluetooth sleep does not hide its cursor.
9. **Scan and reconnect policy:** explicit enrollment uses the
   qualified 30-second legacy active scan, 100 ms interval/window, duplicate
   filtering and bounded candidate selection. Keep the public scanner address
   initially, unrecorded. Permit bounded automatic reconnect only to the saved
   bond after ordinary radio loss, with IRK resolution and encrypted bond reuse.
   Pairing retries require a new user action; authentication failure stops for
   owner action. Initial reconnect configuration: three attempts per loss episode,
   delays of 1/2/4 seconds, each with a 30-second overall attempt deadline and a
   separate cleanup deadline. Exhaustion requires explicit user retry. These
   values need qualification and remain settings, not ABI guarantees. Actual LE
   connection parameters remain gated.

## Accepted second task 1 round

Accepted by the owner through the orchestrator on **2026-10-08**:

10. **HID report scope:** measured relative X/Y, left/right/middle and ordinary
    vertical wheel. Mandatory vendor setup or layout/unit changes return to the
    owner before input injection. Back/forward buttons and the thumb wheel are
    possible later additions, not assigned here; other controls are unsupported.
11. **USB HID sharing:** private transport and normalized producer semantics,
    with class/report policies separate. Preserve existing endpoint admission;
    defer a general HID parser until a concrete second consumer needs one. This
    assigns no USB HID implementation, new speeds/hubs or public raw-USB API.
12. **Controller/service lifetime:** exclusive process ownership, explicit
    restart and re-grant only after confirmed cleanup, defined below. Retain
    existing terminal-failure/DMA limits; no automatic recovery, DMA release or
    endpoint replacement is authorized.

## Third task 1 decision round

Only these **two** decisions are presented now, each pending:

13. **Durable bond success and failure.** Recommended: report successful usable
    enrollment and activate input only after SC encryption and durable local
    storage of the LTK/IRK/private identity. Report durable forgetting only after
    local removal and synchronization. On persistence failure, stop input and
    reconnect, disconnect and report durability/peer-bond uncertainty; never
    claim rollback, peer-key deletion or old-block erasure. A missing private
    persistent root makes bonding unavailable. Temporary usable RAM-only bonds
    would be an explicit alternative and would not meet accepted closure.
14. **Warm-version qualification evidence.** Recommended: a recorded profile/build
    enters the production compatibility list only after HCI features/credits and
    transport, SC enrollment/encryption, durable reconnect/RPA resolution and
    measured HID/pointer input pass on that profile, with revision/configuration
    and owner native evidence recorded. Explicitly assigned staged development
    may use a recorded provisional profile/build to produce this evidence; it
    does not imply production readiness, pin equivalence or closure. Unknown or
    mismatched builds outside that assignment still return to the owner. A
    version-reply/scan-only qualification would provide weaker evidence.

## Owner-reported security evidence

The [report](../development/bluetooth-investigation.md#owner-reported-smp-evidence)
records the owner's Fedora `btmon` account from a comment on merged #542:
NoInputNoOutput, AuthReq `0x09` (Bonding, No MITM, SC), key size 16,
no initiator key distribution and responder EncKey+IdKey. Fedora completed SC
Just Works and AES-CCM encryption with size 16. This answers the device-capability
question for this mouse; it is not a Pyxis pairing measurement.

The legacy-only owner stop is not indicated for this device. Task 4 still checks
the actual Pyxis-side exchange and task 5 checks completed encryption; mismatches
remain explicit rather than relaxing the accepted security policy. Persist the
SC-derived LTK and peer IRK, with private identity/type needed for resolution.
The [SMP key-distribution rules](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/security-manager-specification.html)
ignore EncKey for LE SC; it does not require a legacy Encryption Information
exchange. IRK distribution establishes the resolution requirement; the
owner's account does not measure address rotation or Pyxis resolving it. No
address, key, raw packet or identity dump belongs in documentation or captures.

## Pointer dependency inspected

Read-only source inspection used draft
[PR #545](https://git.internal/PyxisOS/pyxis-os/pulls/545), revision
`304d1d74b2a70afc38464707e4aa58eca40efd8e`:

- [Consumer ABI](https://git.internal/PyxisOS/pyxis-os/src/commit/304d1d74b2a70afc38464707e4aa58eca40efd8e/include/abi/pointer.h):
  graphics-owner acquisition, geometry/mapping identities, ordinary positions,
  locked relative counts, state/reset/activation and cursor/warp operations.
- [Kernel router](https://git.internal/PyxisOS/pyxis-os/src/commit/304d1d74b2a70afc38464707e4aa58eca40efd8e/include/kernel/pointer.h):
  `pointer_handle_input(const struct mouse_event *)`, BSP/IF=0 ownership.
- [Current input shape](https://git.internal/PyxisOS/pyxis-os/src/commit/304d1d74b2a70afc38464707e4aa58eca40efd8e/include/kernel/mouse.h):
  signed counts, post-event held buttons and reset, with the signs below.

Claude's comment reviews earlier ordinary-input revision `19a0d66`, not all of
`304d1d7`. Preserve its tab-press consumption, drag anchoring, hovered-surface
wheel delivery and non-activating warp rules. Draft task 2 adds lock and consumer
migration; terminal/mux wheel delivery waits for pointer task 3. Bluetooth must
not claim those terminal consumers already work or create a separate cursor,
SDL position or focus path.

The current router has one anonymous `device_buttons` snapshot. Passing separate
mouse snapshots directly would release each other's held buttons. Also, cursor
visibility and consumer acquisition still check PS/2 `mouse_available()`. Future
integration needs per-source state and availability from any admitted live source,
then the same kernel router. These are required extensions to coordinate with
alpha, not a claim that #545 already implements a producer interface.

## Input-source contract for coordination

These source semantics are owner-accepted; alpha's agreement is pending.

**Authority/lifetime.** Trusted startup supplies a dedicated producer grant only
to the Bluetooth service. Acquisition is exclusive and process-owned; handle
copies or closure do not transfer/release ownership. Owner exit or explicit
release ends the source. The service initially owns an inactive source; it marks
it live only after the encrypted link and decoded input map are ready. There is
one Bluetooth mouse/source initially, alongside the existing PS/2 source.

**Submission.** Each complete normalized report carries a kernel-issued source
epoch, a consecutive source-local sequence, signed 32-bit `dx`, `dy`, `wheel`,
and a full post-report left/right/middle held-button mask. `+dx` is right,
`+dy` down, `+wheel` toward the user. No acceleration: one relative count moves
one unlocked pixel. Locked input retains relative counts and the parked position.
Ordinary integer wheel steps require conversion from the measured report map;
unsupported high-resolution or horizontal units are not silently treated as steps.
No physical position, surface identity, destination or output event flags come
from the service. The kernel computes them under the existing consumer ABI.

**Execution and results.** Capture and validate the complete scalar record before
BSP work, retaining no user pointer. Route through the existing BSP request path;
source state, position and routing mutate on BSP/IF=0. A synchronous success means
the record was applied, not that a consumer read it or a frame was displayed.
Do not add a silently dropping producer FIFO. Reject invalid fields without
applying motion or buttons. An admitted synchronous call completes with a known
application result under the existing
[BSP request lifetime](../kernel/bsp-service-requests.md), with one outstanding
request per task; no producer timeout or independent cancellation is added.
Account for the prior submission before resetting for transport/HID continuity
loss, so a late old submission cannot reintroduce a cleared button.

**Continuity.** Starting/reconnecting input uses a fresh kernel epoch. Old-epoch
submissions are rejected without disturbing a new connection. A current-epoch
sequence gap ends and resets that epoch; duplicates never apply twice. Source sequence
is distinct from HCI receive sequence and Bluetooth connection generation. A lost
HID transition requires source reset even if submitted sequence numbers remained
consecutive. Initial/reconnected held buttons cannot synthesize a fresh press or
satisfy user activation: release and a new press are required.

**Aggregation/loss.** PS/2 and Bluetooth retain separate physical snapshots.
Aggregate button state is their OR: one source cannot release another's hold,
and the same button has a single press until all holders release. Motion/wheel
from either source follows BSP application order through the common router.
Disconnect, discontinuity, explicit reset or owner exit clears the affected
source, invalidates its queued old-epoch work and retains position/other physical
snapshots. Determine whether the lost source held buttons from its physical
snapshot immediately before clearing, including overlapping holds also present
on PS/2 and held-but-suppressed buttons; this is not a test for the last aggregate
holder or consumer-accepted ownership.

Reset accepted consumer input, cancel drag and clear pending activation only
when that lost snapshot had held buttons. Revoke lock only when the lost source
held buttons or no live source remains; revocation clears its activation
permission under the existing lock rule. Surviving physical snapshots remain
intact, but after a triggered
accepted-input reset their held buttons need release/new press. With a zero lost
snapshot and another live source, preserve accepted input, queued events, drag,
activation, lock and cursor. An idle period without reports is not source loss.
Reset is not a click or permission to relock.

No-live-source hiding applies to machines without PS/2. On the ThinkPad, PS/2
stays live when Bluetooth disconnects; its cursor remains available. If no live
source exists, input is unavailable and the effective cursor is hidden, while
consumer cursor preferences and geometry retain their existing session ownership.

This records owner-accepted semantics, not numeric ABI declarations or implemented
operations. The orchestrator has sent the contract to alpha for per-source
device-loss handling in pointer task 2. Record alpha's agreement only when relayed;
none is recorded yet, and no alpha branch or ABI file was edited.

## Later decisions and gates

The third round presents durable bond success/failure and production warm-version
qualification. Exact firmware assets, mirror and later runtime parameters remain
measurement/implementation prerequisites, not additional policy questions now.

Under accepted decision 12, confirmed cleanup means all outstanding commands, ACL credits
and pending waits are accounted for, old connection epochs invalidated, and
that service's input source ended under accepted decision 8 before re-grant.
Exit or handle closure alone does not prove this. Ordinary service exit does
not itself mean USB removal or authorize quarantining unrelated storage; if
cleanup cannot be confirmed, leave the Bluetooth service unavailable rather
than claim readiness. Existing real USB-failure quarantine remains in force.
Re-grant cannot imply recycling posted DMA, stream replacement, USB recovery or
reattachment. Bond authentication failure remains a separate stop for owner
action, not permission to recover the controller or silently enroll a new peer.

Milestone round two already settles pinned cold assets and qualified warm reuse.
Decision 14 proposes the qualification evidence; exact assets remain gated. The
[task 4 provenance](../development/experiments/bluetooth-task4/README.md#deferred-cold-upload-and-asset-provenance)
provides candidates `intel/ibt-20-1-3.sfi` and `.ddc`, their alias targets and
decompressed hashes, not a selected Pyxis pin. Before task 3 relies on firmware,
capture cold version/boot parameters, verify the matching upstream commit's
aliases/targets, WHENCE and complete license, then give the owner exact source
URLs/revision/files/hashes to mirror. Record the owner-provided mirror URL and
verify its contents before build consumption. No mirror endpoint or blob is
invented here, and task 1 downloads no firmware. A cold identifier mismatch or
unqualified running version returns to the owner.

| Question | Evidence now | Later measurement/coordination gate |
| --- | --- | --- |
| Mouse SC/association/key size | Owner-reported SC Just Works, NoInputNoOutput, size 16 | Task 4 checks Pyxis Pairing Request/Response; task 5 verifies SC completion/encryption, with no legacy fallback. |
| LTK/IRK and private identity | Owner reports Identity Information and Identity Address Information; peer supplies IdKey | Task 5 derives LTK/captures IRK privately; task 6 qualifies bond reuse and RPA resolution without recording identities. |
| HCI/controller capabilities | Warm Reset/version and legacy scan worked | Task 2 checks actual supported commands, LE features, ACL lengths/credits and asynchronous progress; source review is not ACL qualification. |
| Cold firmware/profile | Fedora's file provenance only | Task 3 captures cold facts, selects/mirrors assets, observes real upload/boot/DDC and native readiness. Warm host upload does not qualify Pyxis cold startup. |
| Discovery/reconnect | One active scan identified the mouse | Task 4 qualifies connection/cancel/disable and identity selection; task 6 qualifies sleep/reconnect, key mismatch and bounded backoff. |
| HID map and vendor requirements | Advertising HID UUID/appearance only | After required encryption, read long Report Map, Report References, protocol mode and required characteristics; scope approval precedes subscriptions/injection. |
| Wheel units/report limits | No mouse report received by Pyxis | Task 7 derives report IDs/widths/signs/buttons/wheel conversion from the measured map; unsupported mandatory vendor behavior returns to the owner. |
| Producer ABI and PS/2 coexistence | #545's consumer/router design inspected | Alpha coordination agrees source lifetime/aggregation and availability; task 7 qualifies held-button loss, stale epochs, locked/ordinary input and PS/2 coexistence. |
| Terminal/mux consumption | #545 ordinary graphics and lock are draft; terminal wheel waits | Integrate only with the pointer tasks that supply each destination; qualify routing without inventing a Bluetooth terminal path. |

Task 1 remains unchecked while decisions or alpha agreement are pending. None of
these gates authorizes a probe, firmware reset, pairing attempt, build or native
check in this documentation task. No address or secret is included in its evidence.
