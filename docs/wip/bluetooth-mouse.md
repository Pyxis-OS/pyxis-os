# MX Master 3S on Pyxis

Status: **tasks 1–3 complete; tasks 4–8 unassigned.** Tasks 4–8 need explicit owner
assignment; publishing, reviewing or merging this document does not authorize
implementation. The owner has seen the
[investigation results](../development/bluetooth-investigation.md) and assigned
this proposal; the investigation is now the [AX200 reference](../devices/ax200-bluetooth.md).

The goal is one bonded MX Master 3S providing ordinary relative motion, primary
buttons and vertical wheel to the system pointer on the ThinkPad's AX200. The
scan established its name, HID service and appearance. The
[owner-reported SMP evidence](../development/bluetooth-investigation.md#owner-reported-smp-evidence)
(Fedora `btmon`, from a comment on merged #542) establishes NoInputNoOutput, AuthReq
`0x09` (Bonding, No MITM, SC), key size 16, no initiator key distribution and
responder EncKey+IdKey; Fedora completed SC Just Works with AES-CCM encryption.
This is not a Pyxis pairing measurement. Pyxis GATT layout and input behavior remain
unmeasured. BR/EDR, SCO/audio, keyboards, arbitrary peripherals, vendor gestures,
DPI configuration and general Bluetooth APIs stay outside this milestone. Extra
buttons, horizontal/high-resolution wheel and any vendor report needed for basic
input require inspection and a scope decision, rather than successful fake support.

## Accepted decisions

Accepted by the owner through the orchestrator on **2026-10-08** (decisions 1–6,
incorporating the [review of #542](https://git.internal/PyxisOS/pyxis-os/pulls/542))
and in three task 1 rounds (decisions 7–14, incorporating the
[review of #548](https://git.internal/PyxisOS/pyxis-os/pulls/548)):

1. **Stack boundary:** kernel USB/xHCI, Intel initialization and HCI ownership;
   one trusted userspace service for GAP policy, L2CAP, SMP, ATT/GATT and HID.
2. **Pairing security:** LE Secure Connections with a 128-bit key; unauthenticated
   Just Works only during explicitly authorized enrollment, with no silent legacy
   fallback. Missing SC support stops for an owner decision, not permanent
   rejection of the mouse. Just Works does not authenticate the peer against an
   active attacker.
3. **Closure:** cold AX200 initialization, durable bond reuse and native pointer
   use are required; warm-host development can come first. Cold upload is the
   riskiest task (bootloader bulk transport, possibly USB re-enumeration) and
   closure needs native owner evidence.
4. **Running firmware policy:** pinned, mirrored cold SFI/DDC assets and full
   license/provenance in the boot archive; reuse only explicitly qualified
   compatible warm builds, without resetting solely to enforce the packaged pin.
   Unknown builds return to the owner. Exact assets follow cold identification;
   the host file names do not select a Pyxis pin.
5. **Bond and control authority:** one private system-wide npfs root for trusted
   startup and the sole Bluetooth service, with separate trusted-local enrollment
   and forget grants withheld from ordinary applications. Bonds are system-wide.
6. **Protection at rest:** capability isolation initially, acknowledging that
   disk/mount/backup access exposes keys. Wrapping is deferred to the
   [credentials direction](credentials-and-biometrics.md); no disk-encryption claim.
7. **Producer authority and submission:** a dedicated trusted startup grant for
   one process-owned Bluetooth input source, with kernel-issued epochs and
   consecutive source sequences. Submit copied normalized reports; the kernel
   owns positions/routing. Graphics-consumer grants provide no producer authority.
8. **PS/2 coexistence and source loss:** separate physical snapshots, aggregated
   by OR; conditional loss rule in [input-source contract](#input-source-contract).
   On the ThinkPad, PS/2 remains live: Bluetooth sleep does not hide its cursor.
9. **Scan and reconnect policy:** explicit enrollment uses the qualified 30-second
   legacy active scan, 100 ms interval/window, duplicate filtering and bounded
   candidate selection. Keep the public scanner address initially, unrecorded;
   later privacy-address changes need a separate owner decision. Permit bounded
   automatic reconnect only to the saved bond after ordinary radio loss, with IRK
   resolution and encrypted bond reuse. Pairing retries require a new user action;
   authentication failure stops for owner action. Initial reconnect configuration:
   three attempts per loss episode, delays of 1/2/4 seconds, each with a 30-second
   overall attempt deadline and a separate cleanup deadline; exhaustion requires
   explicit user retry. These values need qualification and remain settings, not
   ABI guarantees. Actual LE connection parameters follow measurement.
10. **HID report scope:** measured relative X/Y, left/right/middle and ordinary
    vertical wheel. Mandatory vendor setup or layout/unit changes return to the
    owner before input injection. Back/forward buttons and the thumb wheel are
    possible later additions, not assigned; other controls are unsupported.
11. **USB HID sharing:** private transport and normalized producer semantics,
    with class/report policies separate. Preserve existing endpoint admission;
    defer a general HID parser until a concrete second consumer needs one. This
    assigns no USB HID implementation, new speeds/hubs or public raw-USB API.
12. **Controller/service lifetime:** exclusive process ownership, explicit
    restart and re-grant only after confirmed cleanup (see
    [Measurement and implementation gates](#measurement-and-implementation-gates)).
    Retain existing terminal-failure/DMA limits; no automatic recovery, DMA
    release or endpoint replacement is authorized.
13. **Durable bond success and failure:** report successful usable enrollment and
    activate input only after SC encryption and durable local storage of the
    LTK/IRK/private identity. Report durable forgetting only after local removal
    and synchronization. On persistence failure, stop input and reconnect,
    disconnect and report durability/peer-bond uncertainty; never claim rollback,
    peer-key deletion or old-block erasure. A missing private persistent root makes
    bonding unavailable. Usable RAM-only bonding is not part of this milestone.
14. **Warm-version qualification evidence:** a recorded profile/build enters the
    production compatibility list only after HCI features/credits and transport,
    SC enrollment/encryption, durable reconnect/RPA resolution and measured
    HID/pointer input pass on that profile, with revision/configuration and owner
    native evidence recorded. Explicitly assigned staged development may use a
    recorded provisional profile/build to produce this evidence; it does not imply
    production readiness, pin equivalence or closure. Unknown or mismatched builds
    outside that assignment return to the owner. A version reply or scan alone
    does not qualify production compatibility.

Accepted 2026-10-09 after the native cold batch: Bluetooth readiness requires
complete enumeration of the xHCI controller hosting the AX200 and exactly one
identified AX200 candidate. Unsupported controllers elsewhere remain in the
global USB inventory without blocking that controller. Incomplete devices,
descendants, budgets/deadlines or hardware failure on the hosting controller
remain terminal. Candidate identity is counted before transport admission;
multiple known AX200s remain ambiguous even if a transport cannot bind.

Accepted 2026-10-08 (task 2 continuity): task 2 fails closed with explicit
input-loss/unavailability if the controller reuses a previously disconnected
connection handle. Independent event and ACL reception cannot yet establish a
retirement boundary that proves delayed old bytes belong to the old connection;
a new generation alone is insufficient. Valid old-link ACL data for a known
retired handle is discarded without erasing the disconnect notification or
disabling storage. The connection/reconnect tasks must establish and qualify a
safe reuse boundary before bonded reconnect is ready. This limit does not relax
required durable reconnect or native closure. See the
[revisit condition](https://git.internal/PyxisOS/pyxis-os/issues/1475).

New evidence that requires a policy change returns to the owner.

## Accepted ownership and service lifetime

These lifetime contracts are accepted for future implementation; the service is not
implemented by the existing probes.

| Owner | Responsibility |
| --- | --- |
| Kernel USB/xHCI worker | Checked descriptors, endpoint admission, rings/DMA, physical completion identity, copied receive queues and controller quarantine. All mutation stays on the owning BSP worker. |
| Kernel Bluetooth/HCI adapter | AX200 selection and initialization, complete HCI command/event/ACL framing, command credits, controller ACL packet credits and bounded copied packet delivery. It alone changes that bookkeeping. |
| Trusted userspace Bluetooth service | Enrollment/reconnect policy, connection procedures, L2CAP fragmentation/reassembly and LE signaling, SMP, ATT/GATT discovery, bond storage and HID report interpretation. It consumes HCI results rather than maintaining a second credit authority. |
| Kernel system pointer | Physical position, button state, surface routing, cursor, activation and pointer lock. The Bluetooth service submits normalized input as an authorized source. |

The controller grant is exclusive and process-owned, with copied HCI
packets/results and explicit capacity, deadline, sequence/loss and terminal-state
semantics. It grants controller control to the trusted service, not raw USB,
physical addresses or DMA mappings. Kernel initialization finishes before handoff;
service commands cannot race its firmware transaction. Numeric protocol/right
declarations belong to review of later assigned implementation; no placeholder
ABI is proposed here.

Commands and ACL data need separate, bounded queues and the controller's real
completion/credit events. A command timeout is not permission to associate a late
same-opcode reply with a new request. Idle interrupt or bulk reception must not
quarantine the controller; the synchronous bulk-IN timeout policy is insufficient
for runtime ACL reception and Intel bootloader events, so owned asynchronous bulk
reception is extended for these uses while storage's request ownership and failure
behavior stay separate.

Service exit, connection loss, queue discontinuity or terminal transport failure
stops input and clears that source's held buttons, cancels protocol work and
reports that source unavailable; no silent dropped-button state or implicit
re-pairing. Teardown cannot recycle outstanding DMA. Start with the existing
terminal transport limits and explicit reboot recovery where ownership remains
uncertain. Mouse radio disconnect/sleep is distinct from USB controller removal
and should not quarantine USB storage. Reconnect to the stored bond uses bounded
backoff; authentication failure stops for owner action rather than deleting the
bond or enrolling whoever advertises the same name. Tag pending work and input
with a connection generation so a reused HCI connection handle cannot deliver a
previous connection's notifications as fresh input; losing that continuity makes
the source unavailable.

Trusted startup starts one system-wide controller service and supplies controller,
clock, randomness, private storage and input-source grants directly to it; it
does not give each space a radio owner. Its placement and bootstrap handoff are
implementation details within that singleton/grant policy. A read-only status
export can use the [process environment](../interfaces/environment.md);
enrollment/forget management must be granted separately to a trusted local control
tool, never ordinary shell children by incidental environment inheritance. Existing
startup has no supervisor; service restart is explicit. A retained client must not
silently bind to a replacement provider. Reacquiring the controller requires
confirmed cleanup or an explicit unavailable/reboot result, not recycled ownership.

## Input-source contract

Owner-accepted (decisions 7 and 8); alpha's relayed no-conflict agreement with pointer
tasks 1+2 at
[#545 revision `a2ff720`](https://git.internal/PyxisOS/pyxis-os/src/commit/a2ff720487667e4afa63dbc2b9e0b0fff2a81c13)
(2026-10-08) is a completed dependency contract, not a Bluetooth producer.
Per-source physical snapshots, OR aggregation and the conditional loss rule are
implemented for PS/2 and USB boot mice by the coordinator in the
[pointer reference](../interfaces/pointer.md#input-source-coordination); the
Bluetooth producer grant, epochs, sequencing and adapter are not. No numeric ABI
declarations exist. Terminal/mux wheel (pointer task 3) is independent of Bluetooth;
Bluetooth must not create a separate cursor, SDL position or focus path.

**Authority/lifetime.** Trusted startup supplies a dedicated producer grant only to
the Bluetooth service. Acquisition is exclusive and process-owned; handle copies
or closure do not transfer/release ownership. Owner exit or explicit release ends
the source. The service initially owns an inactive source and marks it live only
after the encrypted link, decoded input map and durable stored bond are ready
(decision 13). There is one Bluetooth mouse/source initially, alongside PS/2.

**Submission.** Each complete normalized report carries a kernel-issued source
epoch, a consecutive source-local sequence, signed 32-bit `dx`, `dy`, `wheel`, and
a full post-report left/right/middle held-button mask. `+dx` is right, `+dy` down,
`+wheel` toward the user. No acceleration: one relative count moves one unlocked
pixel. Locked input retains relative counts and the parked position. Ordinary
integer wheel steps require conversion from the measured report map; unsupported
high-resolution or horizontal units are not silently treated as steps. No physical
position, surface identity, destination or output event flags come from the
service; the kernel computes them under the existing consumer ABI.

**Execution and results.** Capture and validate the complete scalar record before
BSP work, retaining no user pointer. Route through the existing BSP request path;
source state, position and routing mutate on BSP/IF=0. A synchronous success means
the record was applied, not that a consumer read it or a frame was displayed. No
silently dropping producer FIFO. Reject invalid fields without applying motion or
buttons. An admitted call completes with a known application result under the
[BSP request lifetime](../kernel/bsp-service-requests.md), one outstanding request
per task; no producer timeout or independent cancellation. Account for the prior
submission before resetting for transport/HID continuity loss, so a late old
submission cannot reintroduce a cleared button.

**Continuity.** Starting/reconnecting input uses a fresh kernel epoch. Old-epoch
submissions are rejected without disturbing a new connection. A current-epoch
sequence gap ends and resets that epoch; duplicates never apply twice. Source
sequence is distinct from HCI receive sequence and Bluetooth connection
generation. A lost HID transition requires source reset even if submitted
sequence numbers remained consecutive. Initial/reconnected held buttons cannot
synthesize a fresh press or satisfy user activation: release and a new press are
required.

**Aggregation/loss.** PS/2 and Bluetooth retain separate physical snapshots;
aggregate button state is their OR, so one source cannot release another's hold
and the same button has a single press until all holders release. Motion/wheel
follows BSP application order through the common router. Disconnect,
discontinuity, explicit reset or owner exit clears the affected source,
invalidates its queued old-epoch work and retains position/other snapshots.
Whether the lost source held buttons is determined from its physical snapshot
immediately before clearing, including overlapping holds also present on PS/2 and
held-but-suppressed buttons; it is not a test for the last aggregate holder or
consumer-accepted ownership. Reset accepted consumer input, cancel drag and clear
pending activation only when that lost snapshot had held buttons. Revoke lock only
when the lost source held buttons or no live source remains. Surviving snapshots
stay intact, but after a triggered reset their held buttons need release/new
press. With a zero lost snapshot and another live source, preserve accepted input,
queued events, drag, activation, lock and cursor. An idle period without reports
is not source loss. Reset is not a click or permission to relock.

No-live-source hiding applies to machines without PS/2. On the ThinkPad, PS/2
stays live when Bluetooth disconnects. With no live source, input is unavailable
and the effective cursor is hidden, while consumer cursor preferences and geometry
keep their session ownership.

## Proposed firmware integration

The [task 4 provenance](../development/experiments/bluetooth-task4/README.md#deferred-cold-upload-and-asset-provenance)
and the [firmware readiness record](../development/experiments/bluetooth-firmware-readiness/README.md)
cover the implemented pinned, mirrored and licensed AX200 SFI/DDC assets, bounded
secure upload and real boot-event retirement (task 3). What remains:

- Production firmware compatibility list: recorded qualified versions are kept
  separately from the packaged file pin and follow decision 14. The investigation's
  warm acceptance does not qualify a build for production.
- Cold facts for any new profile: read cold version/boot parameters before
  choosing the AX200 profile; do not choose files from the running build alone.
  A new pin proposes the exact
  [linux-firmware upstream](https://gitlab.com/kernel-firmware/linux-firmware)
  commit, aliases, SFI/DDC targets, sizes/hashes, WHENCE entry and
  `LICENCE.ibt_firmware` to the owner, who supplies the mirror/cache entry. The
  build uses only that mirror and rejects missing or mismatched assets, with no
  upstream fallback.
- Firmware is separate from kernel source licensing; Intel's
  [published license](https://kernel.googlesource.com/pub/scm/linux/kernel/git/iwlwifi/linux-firmware/+/4590121e2ecc02935ec26674e63837c46b422310/LICENCE.ibt_firmware)
  permits unmodified binary redistribution with notice/disclaimer; the selected
  pin's WHENCE and complete license must be checked and distributed.
- If startup ever requires USB re-enumeration, design its retirement/reattachment
  boundary first; today's active-removal quarantine is not a recovery path. The
  accepted [cold-upload debt](https://git.internal/PyxisOS/pyxis-os/issues/1501)
  stays open until cold initialization is measured.

## Pairing gates and proposed bond storage

Discover a candidate with the accepted bounded active scan; surface parsed names,
HID service/appearance and run-local labels only. Scanning is an explicit control
action, not a perpetual background enrollment loop. An authorized local user
selects a candidate while physically putting the mouse in pairing mode. Ambiguous
candidates stop for selection; a matching name/service does not establish identity.
The owner's [Fedora account](../development/bluetooth-investigation.md#owner-reported-linux-pairing)
supports an explicit scan-and-select flow (`bluetoothctl` found and paired the
mouse when KDE's scan did not; the first attempt failed and the second worked).
Enrollment failure must permit another explicitly authorized user attempt after
the previous attempt is accounted for, with no automatic pairing retry loop.
Uncertain transport ownership can still require reboot, and missing SC remains an
owner-decision gate; retry does not bypass either boundary.

The legacy-only stop is not indicated for this mouse, but Pyxis checks the actual
feature exchange and completed encryption in tasks 4/5. Task 4 first exchanges SMP
Pairing Request/Response and aborts with Pairing Failed before key generation,
then disconnects without completing pairing or storing a bond. Record only IO
capability, AuthReq (including the SC bit) and maximum key size, with addresses
excluded before recording. Check SC, key size and association prerequisites;
advertised features are a gate before task 5's crypto work, not successful Pyxis
pairing or permission to use legacy SMP. The accepted policy permits Just Works
only with explicit enrollment consent. Missing SC support or a required
unsupported association method stops for an owner decision before crypto
implementation. Verify the negotiated key size and encryption success before
accepting any HID input. Reject legacy downgrade, debug keys and malformed
exchanges. The
[SMP key-distribution rules](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/security-manager-specification.html)
ignore EncKey for LE SC. IRK distribution establishes the resolution requirement;
the owner's account does not measure address rotation or Pyxis resolving it.

Prefer the existing pinned [Mbed TLS/TF-PSA-Crypto port](../../ports/mbedtls/README.md)
for P-256 and AES-CMAC primitives, subject to checking its exported configuration
and required operations. Keep SMP transcript/state logic in the service. Do not
hand-roll cryptography or change TLS configuration silently; any recipe change
belongs in a focused ports PR before updating the parent pin, following
[repository ownership](../development/sdk-and-repositories.md). Use the explicit
[random grant](../devices/randomness.md) through a checked crypto entropy adapter
to the [kernel ChaCha20 generator](../devices/random-generator.md). Initial/required
seed failure aborts pairing; timestamps and predictable fallback keys are not
acceptable.

Bond ownership uses a dedicated writable npfs root granted only to trusted startup
and the Bluetooth service. Ordinary applications receive neither that root nor
raw keys; a hidden subdirectory of shared `home://` or `tmp://` is not isolation.
The service alone writes keys and private peer identity (including identity/type
needed for bond lookup/resolution). The bond must hold the SC-derived LTK and
peer IRK; filenames, status and diagnostics use an opaque local bond label. These
private records never enter source, reports, PRs, raw packet dumps or debugger
captures; no address, key, raw packet or identity dump belongs in documentation
or captures. The storage authority is capability isolation, not an invented UID
or permission bit; there is no encryption at rest (decision 6), and a key beside
the encrypted bond is not protection. Live boots without a private persistent
root report bonding unavailable rather than claim RAM storage is persistent.

Write a bounded bond record to a temporary file, atomically replace its entry and
explicitly synchronize through [native filesystem operations](../interfaces/filesystem-mutations.md);
closing a file alone is insufficient. Per decision 13, report usable enrollment
and activate input only after encryption and durable storage. Reboot must load
the saved security properties and authenticate possession of the bond without
falling back to new pairing. Authorized forgetting stops reconnect, disconnects,
removes/syncs the record and reports durability; it cannot promise erasure of old
disk blocks, backups or the mouse's retained key, and succeeds only after removal
plus sync. Record encoding is an ordinary implementation choice.

## Proposed HID and pointer path

On an encrypted bonded link, use bounded L2CAP/ATT transactions to discover the
HID service, read the Report Map (including long values), identify input Report
characteristics through Report Reference descriptors and enable notifications
with their Client Characteristic Configuration descriptors. Select Report
Protocol where supported. The
[HID over GATT profile](https://www.bluetooth.com/specifications/specs/hid-over-gatt-profile/)
is the protocol reference; boot-mouse offsets or a presumed three-byte report
are not a substitute for the mouse's actual map. First record an address-free
map/layout assessment; unsupported layouts or required vendor initialization
return for a scope decision before pointer injection.

Scope is decision 10. Preserve releases and wheel units, with bounded arithmetic
and explicit sequence/loss handling. Reject absolute or unsupported reports
rather than interpreting them as relative motion. Initially rediscover GATT
after reconnect; caching/service-change policy can follow only if needed.

Input goes through the [system pointer](../interfaces/pointer.md) and the
[input-source contract](#input-source-contract): the same kernel position, routing,
cursor and lock path as PS/2, and consumer APIs do not confer input-injection
authority. Preserve the pointer reference's tab-press consumption, drag anchoring,
hovered-surface wheel delivery and non-activating warp rules.

## Proposed sharing with USB HID

Reuse private xHCI endpoint configuration, receive ownership, completion copying,
progress and terminal-loss reporting. Bluetooth ACL and USB HID report delivery
remain separate consumers. Do not combine storage, Bluetooth and HID state or
introduce a public raw-USB interface to enable reuse.

The current one-stream, root/full-speed, boot-present profile is enough for the
observed AX200, not a general USB HID claim. Broaden each admission only for a
concrete later HID device, with its own task/qualification. Do not add endpoint
recovery or cancellation speculatively; retained DMA and controller-wide
quarantine remain visible limits unless explicitly replaced. A common bounded
HID report decoder may be extracted when both consumers have demonstrated needs;
sharing report normalization and the pointer input contract is accepted alongside
private transport reuse, with class/report policies separate. A general parser
still waits for a concrete second need.

## Proposed tasks and completion evidence

Every task starts only after its necessary decisions and explicit owner
assignment. Use focused kernel/userspace/ports PRs, publish dependency commits
first and state merge order. Probe branches remain historical evidence.

- [x] **1. Contracts and dependencies.** Complete 2026-10-08
  ([#548](https://git.internal/PyxisOS/pyxis-os/pulls/548)): decisions 7–14 and the
  input-source contract above; pointer tasks 1+2 merged in #545.
- [x] **2. Runtime HCI transport.** Complete 2026-10-08
  ([#552](https://git.internal/PyxisOS/pyxis-os/pulls/552)): checked AX200 binder,
  exclusive controller grant, event/asynchronous ACL reception, credits and
  process-exit/loss behavior; see the [HCI reference](../devices/bluetooth-hci.md)
  and [runtime transport record](../development/experiments/bluetooth-runtime-hci/README.md).
  Real ACL traffic and storage coexistence are qualified with task 4's connection
  consumer. Do not block the xHCI worker for a 30-second userspace scan as the
  investigation probe did.
- [x] **3. Firmware readiness.** Complete 2026-10-09
  ([#564](https://git.internal/PyxisOS/pyxis-os/pulls/564)): pinned/mirrored/licensed
  AX200 SFI/DDC, bounded cold upload and real boot-event retirement; owner-reported
  native cold upload/DDC READY and warm skip passed. See the
  [evidence and limits](../development/experiments/bluetooth-firmware-readiness/README.md#native-confirmation-2026-10-09).
  Production compatibility still requires later encrypted bond/HID/input evidence.
- [ ] **4. LE connection and discovery.** Bounded authorized scan/selection,
  connection/disconnection, ACL/L2CAP framing and signaling, and minimal public
  ATT/GATT discovery. At the start, measure SMP Pairing Request/Response fields
  and abort before key generation: missing SC or inadequate key size stops for
  an owner decision before task 5. This warm-path gate can precede cold
  qualification when explicitly assigned; cold remains required for closure.
  Protected attributes, the secured HID map and all input wait for the required
  encryption and bond state; encryption-required replies are a boundary, not a
  failure to bypass.
- [ ] **5. Secure Connections enrollment.** Proceed only after task 4's feature
  gate supports the accepted security policy, or an explicit owner decision
  revises it. Implement the observed association with reviewed crypto/entropy
  integration, enforce decision 2 and measure encrypted operation. Report
  security properties accurately.
- [ ] **6. Durable bonds and reconnect.** Private npfs grants, checked record
  replacement/sync, explicit management authority, reboot bond reuse, mouse
  sleep/reconnect and durable forget. No silent re-enrollment on key mismatch.
- [ ] **7. HID over GATT and pointer.** Decode the measured Report Map, subscribe
  to required input reports and submit normalized source events through the
  accepted kernel pointer contract. Verify motion, press/release, wheel, local
  terminal/mux routing and lock/device-loss behavior; preserve PS/2 input.
- [ ] **8. Qualification and closure.** Matched before/after QEMU workloads for
  idle/active input and concurrent storage, with repetitions and variation;
  ordinary interactive boots/debugger inspection, no new test infrastructure.
  Owner-run native cold start, durable reconnect and everyday pointer use close
  the accepted hardware goal. Document remaining limits/debt, retire this WIP
  to subsystem references and update links after the owner accepts completion.

## Measurement and implementation gates

Exact firmware pin/mirror for any new profile, the actual compatibility list and
later runtime parameters (ordinary implementation choices such as record encoding
included) follow measurement under the accepted policies; none authorizes
implementation without a new assignment.

Under decision 12, confirmed cleanup means all outstanding commands, ACL credits
and pending waits are accounted for, old connection epochs invalidated, and that
service's input source ended under decision 8 before re-grant. Exit or handle
closure alone does not prove this. Ordinary service exit does not itself mean USB
removal or authorize quarantining unrelated storage; if cleanup cannot be
confirmed, leave the Bluetooth service unavailable rather than claim readiness.
Existing real USB-failure quarantine remains in force. Re-grant cannot imply
recycling posted DMA, stream replacement, USB recovery or reattachment. Bond
authentication failure remains a separate stop for owner action, not permission to
recover the controller or silently enroll a new peer.

| Question | Evidence now | Later measurement/coordination gate |
| --- | --- | --- |
| Mouse SC/association/key size | Owner-reported SC Just Works, NoInputNoOutput, size 16 | Task 4 checks Pyxis Pairing Request/Response; task 5 verifies SC completion/encryption, with no legacy fallback. |
| LTK/IRK and private identity | Owner reports Identity Information and Identity Address Information; peer supplies IdKey | Task 5 derives LTK/captures IRK privately; task 6 qualifies bond reuse and RPA resolution without recording identities. |
| HCI/controller capabilities | Task 2 transport and warm Reset/version/legacy scan worked | Task 4 checks actual supported commands, LE features, ACL lengths/credits and asynchronous progress with real ACL traffic; source review is not ACL qualification. |
| Cold firmware/profile | Task 3 native cold upload/DDC and warm skip | Production compatibility list per decision 14. |
| Discovery/reconnect | One active scan identified the mouse | Task 4 qualifies connection/cancel/disable and identity selection; task 6 qualifies sleep/reconnect, key mismatch and bounded backoff. |
| HID map and vendor requirements | Advertising HID UUID/appearance only; basic scope accepted | After required encryption, read long Report Map, Report References, protocol mode and required characteristics; mandatory scope changes return to the owner before subscriptions/injection. |
| Wheel units/report limits | No mouse report received by Pyxis | Task 7 derives report IDs/widths/signs/buttons/wheel conversion from the measured map; unsupported mandatory vendor behavior returns to the owner. |
| Producer ABI and PS/2 coexistence | Coordinator aggregation/loss implemented for PS/2 and USB boot mice; Bluetooth producer unimplemented | Implement the accepted source adapter and producer grant; task 7 qualifies held-button loss, stale epochs, locked/ordinary input and PS/2 coexistence. |
| Terminal/mux consumption | #545 ordinary graphics and lock merged; terminal/mux wheel implemented by pointer task 3 | Qualify routing through the existing destinations without inventing a Bluetooth terminal path. |
