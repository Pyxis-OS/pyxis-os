# MX Master 3S on Pyxis

Status: **task 1 complete; task 2 assigned 2026-10-08, baseline preparation.**
All [task 1 decisions and alpha's agreement](bluetooth-task1-contracts.md) are
recorded as accepted. Pointer tasks 1+2 merged in #545. No task 1 policy questions
remain. Task 2 is assigned; tasks 3–8 still need explicit owner assignment.
Prepared from main `85e1b749550194d167019e62c7718907ca4db6b1`, after
[merged #536](https://git.internal/PyxisOS/pyxis-os/pulls/536). The owner has seen
the [investigation results](../development/bluetooth-investigation.md) and
assigned this proposal. Publishing, reviewing or merging it does not authorize
implementation. The owner chooses the milestone and explicitly starts its tasks.
The native batch is complete in [merged #547](https://git.internal/PyxisOS/pyxis-os/pulls/547).
Task 1 started from fresh main `8c4368e` and is now rebased onto main `abbeded`.
Its contracts/dependencies are complete. The owner explicitly assigned task 2
after approving #548. The [runtime transport record](../development/experiments/bluetooth-runtime-hci/README.md)
tracks its baseline and implementation handoff; no implementation is complete.

The goal is one bonded MX Master 3S providing ordinary relative motion, primary
buttons and vertical wheel to the system pointer on the ThinkPad's AX200. The
scan established its name, HID service and appearance. The later
[owner-reported SMP evidence](../development/bluetooth-investigation.md#owner-reported-smp-evidence)
establishes SC Just Works/16-byte encryption on Fedora; Pyxis GATT layout and
input behavior remain unmeasured. BR/EDR, SCO/audio, keyboards, arbitrary
peripherals, vendor gestures, DPI configuration and general Bluetooth APIs stay
outside this proposed milestone. Extra buttons, horizontal/high-resolution wheel
and any vendor report needed for basic input require inspection and a scope
decision, rather than successful fake support.

## Accepted first round

Accepted by the owner through the orchestrator on **2026-10-08**, incorporating
the [review of #542](https://git.internal/PyxisOS/pyxis-os/pulls/542):

1. **Stack boundary:** kernel USB/xHCI, Intel initialization and HCI ownership;
   one trusted userspace service for GAP policy, L2CAP, SMP, ATT/GATT and HID.
2. **Pairing security:** LE Secure Connections with a 128-bit key; unauthenticated
   Just Works only during explicitly authorized enrollment, with no silent legacy
   fallback. Missing SC support stops for an owner decision, not permanent
   rejection of the mouse. Measure SMP features before task 5's crypto work.
   Just Works does not authenticate the peer against an active attacker.
3. **Closure:** cold AX200 initialization, durable bond reuse and native pointer
   use are required; warm-host development can come first. Cold upload is the
   riskiest task: bootloader bulk transport and possibly USB re-enumeration put
   it on the critical path, and closure needs native owner evidence. The warm-only
   alternative would avoid that work but depend on another OS having initialized
   the controller.

## Accepted second round

Accepted by the owner through the orchestrator on **2026-10-08**:

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
   [credentials direction](credentials-and-biometrics.md); this milestone makes
   no disk-encryption claim.

The [three completed task 1 rounds](bluetooth-task1-contracts.md) settle the
remaining policies. Later [measurement/implementation gates](#decision-and-investigation-handoff)
remain prerequisites, not pending task 1 questions or implementation assignments.

## Accepted ownership and service lifetime

The accepted stack boundary assigns these responsibilities. The service and HCI
adapter are not implemented by the existing probes; their lifetime contracts
below are accepted contracts for future implementation.

| Owner | Responsibility |
| --- | --- |
| Kernel USB/xHCI worker | Checked descriptors, endpoint admission, rings/DMA, physical completion identity, copied receive queues and controller quarantine. All mutation stays on the owning BSP worker. |
| Kernel Bluetooth/HCI adapter | AX200 selection and initialization, complete HCI command/event/ACL framing, command credits, controller ACL packet credits and bounded copied packet delivery. It alone changes that bookkeeping. |
| Trusted userspace Bluetooth service | Enrollment/reconnect policy, connection procedures, L2CAP fragmentation/reassembly and LE signaling, SMP, ATT/GATT discovery, bond storage and HID report interpretation. It consumes HCI results rather than maintaining a second credit authority. |
| Kernel system pointer | Physical position, button state, surface routing, cursor, activation and pointer lock. The Bluetooth service submits normalized input as an authorized source. |

The controller grant is accepted as exclusive and process-owned, with copied HCI
packets/results and explicit capacity, deadline, sequence/loss and terminal-state
semantics. It grants controller control to the trusted service, not raw USB,
physical addresses or DMA mappings. Kernel initialization finishes before handoff;
service commands cannot race its firmware transaction. Numeric protocol/right
declarations belong to review of later assigned implementation under these
accepted ownership contracts; no placeholder ABI is proposed here.

Commands and ACL data need separate, bounded queues and the controller's real
completion/credit events. A command timeout is not permission to associate a late
same-opcode reply with a new request. Idle interrupt or bulk reception must not
quarantine the controller. The current synchronous bulk-IN timeout policy is
therefore insufficient for both runtime ACL reception and Intel bootloader
events. Extend owned asynchronous bulk reception for these concrete uses; keep
storage's request ownership and failure behavior separate.

Service exit, connection loss, queue discontinuity or terminal transport failure
stops input and clears that source's held buttons. It also cancels protocol work
and reports that source unavailable; no silent dropped-button state or implicit
re-pairing. Accepted input reset/drag cancellation occurs only if the lost source
held buttons; lock revokes only then or when no live source remains. A buttonless
Bluetooth disconnect leaves the live PS/2 pointer visibly unchanged. These
input-loss rules and controller cleanup/re-grant policy are accepted; the
[confirmed-cleanup requirements](bluetooth-task1-contracts.md#implementation-and-measurement-gates)
remain explicit.
Teardown cannot recycle outstanding DMA. Start with the existing terminal
transport limits and explicit reboot recovery where ownership remains uncertain.
Mouse radio disconnect/sleep is distinct from USB controller removal and should
not quarantine USB storage. Reconnect to the stored bond is accepted with bounded
backoff; authentication failure stops for owner action rather than deleting the
bond or enrolling whoever advertises the same name.
Tag pending work and input with a connection generation so a reused HCI
connection handle cannot deliver a previous connection's notifications as fresh
input. Losing that continuity makes the source unavailable.

Trusted startup starts one system-wide controller service and supplies controller,
clock, randomness, private storage and input-source grants directly to it; it
does not give each space a radio owner. Its configured placement and bootstrap
handoff are implementation details within that accepted singleton/grant policy.
A read-only status export can use
the [native service namespace](../interfaces/namespaces.md); enrollment/forget
management must be granted separately to a trusted local control tool, never
ordinary shell children by incidental namespace lookup. Existing startup has no
supervisor; service restart is explicit. A retained client must not silently bind
to a replacement provider. Reacquiring the controller requires confirmed cleanup
or an explicit unavailable/reboot result, not recycled ownership.

## Proposed firmware integration

The [task 4 provenance](../development/experiments/bluetooth-task4/README.md#deferred-cold-upload-and-asset-provenance)
identifies host files `intel/ibt-20-1-3.sfi` and `.ddc`, their alias targets,
decompressed sizes and hashes. These are candidates, not chosen Pyxis assets or
proof of cold compatibility. Read cold version/boot parameters before choosing
the AX200 profile; do not choose files from the running build/week/year alone.

Before a build relies on firmware, propose the exact
[linux-firmware upstream](https://gitlab.com/kernel-firmware/linux-firmware)
commit, requested aliases, SFI/DDC targets, sizes/hashes, WHENCE entry and
`LICENCE.ibt_firmware` to the owner. The owner supplies the mirror/cache entry;
the build uses only that mirror and rejects missing or mismatched assets, with
no upstream fallback. Record the actual pin and mirror URL with the recipe.
Package unchanged decompressed firmware and its full license/provenance in the
boot archive so initialization does not require networking or a mounted disk.

Intel's [published firmware license](https://kernel.googlesource.com/pub/scm/linux/kernel/git/iwlwifi/linux-firmware/+/4590121e2ecc02935ec26674e63837c46b422310/LICENCE.ibt_firmware)
permits binary redistribution without modification subject to notice/disclaimer
and other conditions. The selected pin's WHENCE and complete license must be
checked and distributed; this older license reference does not select an asset
or replace that review. Firmware is separate from kernel source licensing.

Proposed initialization happens once under exclusive kernel ownership before
exporting a ready controller: read state, upload only a recognized bootloader,
observe the real operational boot event, apply DDC as required, and check the
resulting version/capabilities. Under the accepted policy, an explicitly qualified
compatible operational warm build skips SFI upload, avoiding a reset solely to
enforce the packaged pin; unknown builds return to the owner. Record qualified
versions separately from the packaged file pin. Accepted qualification requires
transport, encrypted bond/reconnect and measured HID/pointer evidence, with
revision/configuration and native owner evidence. Explicitly assigned provisional
development can produce that evidence without claiming production readiness.
The actual compatibility list follows qualification; the investigation's warm
acceptance does not qualify a build for production.

Cold support needs secure-send bulk OUT, independently owned bootloader bulk IN
and interrupt events, and bounded progress while commands wait. Use actual vendor
boot notifications rather than Linux's internally synthesized completions. If
startup requires USB re-enumeration, design its retirement/reattachment boundary
before attempting it; today's active-removal quarantine is not a recovery path.
Do not merge the warm probes as a substitute. The accepted
[cold-upload debt](../technical-debt.md#bluetooth-cold-firmware-upload-and-running-version-policy)
stays open until cold initialization is measured.

## Pairing gates and proposed bond storage

Discover a candidate with the accepted bounded active scan; surface parsed names,
HID service/appearance and run-local labels only. Scanning is an explicit control
action, not a perpetual background enrollment loop. An authorized local user
selects a candidate while physically putting the mouse in pairing mode. Ambiguous
candidates stop for selection; a matching name/service does not establish identity.
The initial public scanner-address and bounded scan/reconnect profile are
accepted; addresses remain unrecorded. Later privacy-address changes require a
separate owner decision. Runtime connection parameters follow measurement rather
than become fixed ABI values here.

The [owner's Fedora account](../development/bluetooth-investigation.md#owner-reported-linux-pairing)
supports an explicit scan-and-select flow: `bluetoothctl` found and paired the mouse
when KDE's scan did not show it. The first attempt failed and the second worked.
Enrollment failure must permit another explicitly authorized user attempt after
the previous attempt is accounted for, with no automatic pairing retry loop.
Uncertain transport ownership can still require reboot, and missing SC remains
an owner-decision gate; retry does not bypass either boundary.

Read the actual SMP feature exchange before choosing an association method. The
[Security Manager specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/security-manager-specification.html)
distinguishes LE Secure Connections from authenticated pairing. The owner's
later Fedora `btmon` account identifies NoInputNoOutput and SC Just Works with
key size 16; the earlier scan alone established none of those capabilities.
The legacy-only stop is not indicated for this mouse. Pyxis still checks the
actual feature exchange and completed encryption in tasks 4/5.
The accepted policy permits that method only with explicit enrollment
consent. Missing SC support or a required unsupported association method stops
for an owner decision before crypto implementation; it does not authorize legacy
pairing. Verify the negotiated key size and encryption success before accepting
any HID input. Reject legacy downgrade, debug keys and malformed exchanges.

Task 4 first exchanges SMP Pairing Request/Response and aborts with Pairing Failed
before key generation, then disconnects without completing pairing or storing a
bond. Record only IO capability, AuthReq (including the SC bit) and maximum key
size, with addresses excluded before recording. Check SC, key size and association
prerequisites; advertised features are a gate before task 5's crypto work, not
successful Pyxis pairing, encrypted HID or permission to use legacy SMP. The
owner has since supplied that capability evidence from Fedora, answering the
early device question. It does not replace the Pyxis-side checks. No further
capture or pairing is performed by task 1.

Prefer the existing pinned [Mbed TLS/TF-PSA-Crypto port](../../ports/mbedtls/README.md)
for P-256 and AES-CMAC primitives, subject to checking its exported configuration
and required operations. Keep Bluetooth's SMP transcript/state logic in the
service. Do not hand-roll cryptography or change TLS configuration silently.
Any recipe change belongs in a focused ports PR before updating the parent pin,
following [repository ownership](../development/sdk-and-repositories.md).
Use the explicit [random grant](../devices/randomness.md) through a checked
crypto entropy adapter to the [kernel ChaCha20 generator](../devices/random-generator.md).
Initial/required seed failure aborts pairing; timestamps and predictable fallback
keys are not acceptable.

Accepted bond ownership uses a dedicated writable npfs root granted only
to trusted startup and the Bluetooth service. Ordinary applications receive
neither that root nor raw keys; a hidden subdirectory of shared `home://` or
`tmp://` is not isolation. The service alone writes keys and private peer identity
(including identity/type needed for bond lookup/resolution). The bond must hold
the SC-derived LTK and peer IRK required by the owner's SMP account; filenames,
status and diagnostics use an opaque local bond label. These runtime private
records never enter source, reports, PRs, raw packet dumps or debugger captures.

The storage authority is capability isolation, not an invented UID or permission
bit. The accepted initial protection has no encryption at rest: possession of
the disk or raw mount/backup authority can expose keys. Wrapping and separate
unlock authority are deferred to
[credentials and biometric unlock](credentials-and-biometrics.md); a key beside
the encrypted bond is not protection. Live boots
without a private persistent root should report
bonding unavailable, rather than claim RAM storage is persistent.

Write a bounded bond record to a temporary file, atomically replace its entry and
explicitly synchronize through [native filesystem operations](../interfaces/filesystem-mutations.md).
Under accepted decision 13, report usable enrollment and activate input only
after encryption and durable storage; closing a file alone is insufficient.
On persistence failure, stop input and reconnect, disconnect and report
durability/peer-bond uncertainty without claiming rollback.
Reboot must load the saved security properties and authenticate possession of
the bond without falling back to new pairing. Authorized forgetting stops
reconnect, disconnects, removes/syncs the record and reports durability; it cannot
promise erasure of old disk blocks, backups or the mouse's retained key. Durable
forgetting succeeds only after removal plus sync. Storage/control authority,
at-rest policy and these success/failure rules are accepted; record encoding is
an ordinary implementation choice.

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

Accepted scope is measured relative X/Y, left/right/middle and ordinary vertical
wheel from the actual report map. Back/forward buttons and the thumb wheel are
possible later additions, not assigned. Mandatory vendor/layout/unit changes
return to the owner. Preserve releases and wheel units, with bounded arithmetic
and explicit sequence/loss handling. Reject absolute or unsupported reports
rather than interpreting them as relative motion. Initially rediscover GATT
after reconnect; caching/service-change policy can follow only if needed.

The owner has accepted the [system-pointer milestone](pointer.md), with kernel
positions. Pointer tasks 1+2 merged in
[#545](https://git.internal/PyxisOS/pyxis-os/pulls/545). Task 1 records the
[owner-accepted producer, aggregation and source-loss contract](bluetooth-task1-contracts.md#input-source-contract-for-coordination)
against initial inspected revision `304d1d7`. The orchestrator relayed alpha's
[no-conflict agreement at `a2ff720`](bluetooth-task1-contracts.md#alpha-agreement)
on 2026-10-08. It uses the same
kernel position, routing, cursor and lock path as PS/2;
consumer APIs do not confer input-injection authority. Physical held state is
per source. Reset/cancel only when the lost source held buttons; revoke lock then
or when no live source remains. Idle loss of an unused mouse is invisible while
PS/2 is live. Terminal/mux wheel remains pointer task 3, not implemented by
those tasks or Bluetooth. The agreement adds no second source or producer API;
the future Bluetooth adapter remains to be implemented in its assigned task.

## Proposed sharing with USB HID

Reuse private xHCI endpoint configuration, receive ownership, completion copying,
progress and terminal-loss reporting. Bluetooth ACL and USB HID report delivery
remain separate consumers. Do not combine storage, Bluetooth and HID state or
introduce a public raw-USB interface to enable reuse.

The current one-stream, root/full-speed, boot-present profile is enough for the
observed AX200, not a general USB HID claim. USB HID mice may need other speeds,
hub topology, report sizes or multiple interfaces. Broaden each admission only
for a concrete later HID device, with its own task/qualification. Do not add
endpoint recovery or cancellation speculatively; retained DMA and controller-wide
quarantine remain visible limits unless explicitly replaced. Sharing transport
now does not assign USB HID implementation to this milestone. A common bounded
HID report decoder may be extracted when both consumers have demonstrated needs;
sharing report normalization and the pointer input contract is accepted alongside
private transport reuse, with class/report policies separate. This assigns no
USB HID implementation; a general parser still waits for a concrete second need.

## Proposed tasks and completion evidence

Every task starts only after its necessary decisions and explicit owner
assignment. Use focused kernel/userspace/ports PRs, publish dependency commits
first and state merge order. Probe branches remain historical evidence.

- [x] **1. Contracts and dependencies.** Complete 2026-10-08: all three
  [decision rounds, contracts and measurement gates](bluetooth-task1-contracts.md)
  are recorded, including owner-reported SMP evidence and alpha's agreement
  against `a2ff720` (now merged in #545). No policy questions remain. Firmware
  assets/compatibility data and report/runtime qualification are later gates;
  no implementation task is assigned by this completion.
- [ ] **2. Runtime HCI transport.** Production AX200 binder, exclusive controller
  grant, event and asynchronous ACL reception, command/data credits, bounded
  progress and process-exit/loss behavior. Validate warm passthrough framing and
  idle reception; qualify real ACL traffic and storage coexistence with task 4's
  connection consumer. Do not block the xHCI worker for a 30-second userspace
  scan as the investigation probe did.
- [ ] **3. Firmware readiness.** Choose/mirror/license the exact assets, implement
  cold bootloader upload/DDC and real boot-event handling, then verify warm skip
  and cold initialization. Address re-enumeration ownership only if required.
  QEMU passthrough may remain warm; cold qualification needs owner-run native
  evidence rather than forcing the controller into a guessed state.
- [ ] **4. LE connection and discovery.** Bounded authorized scan/selection,
  connection/disconnection, ACL/L2CAP framing and signaling, and minimal public
  ATT/GATT discovery. At the start, measure SMP Pairing Request/Response fields
  and abort before key generation: missing SC or inadequate key size stops for
  an owner decision before task 5. This warm-path gate can precede cold task 3
  when explicitly assigned after runtime transport; cold remains required for
  closure. Protected attributes, the secured HID map and all input
  wait for the required encryption and bond state; encryption-required replies
  are a boundary, not a failure to bypass.
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

The owner completed the native-check batch in #547 and returned the ThinkPad to
Fedora with Bluetooth disabled. That batch and the owner's Fedora SMP account
do not qualify Pyxis Bluetooth input or cold upload. Task 1 performs no host
service changes, probes, pairing or native checks.

## Decision and investigation handoff

Milestone rounds one and two and all three task 1 rounds are
**accepted 2026-10-08**. Task 1 is complete and presents no further questions.
Remaining measurement/implementation prerequisites are:

- Exact firmware pin/mirror and actual compatibility list, selected after measurement.
- Later parameter qualification and ordinary implementation choices such as record encoding.

Producer authority, OR aggregation, conditional source loss and initial
scan/reconnect policy are settled, as are HID scope, USB sharing and controller
lifetime. None is reopened here. Exact firmware metadata/mirror remains a build
prerequisite under the accepted policy, not a blind choice before cold evidence.

New evidence that requires a policy change returns to the owner. Every
implementation task needs an explicit assignment. Task 2 is assigned; tasks 3–8
remain unassigned.

With the direction decided, the completed investigation is now the
[AX200 reference](../devices/ax200-bluetooth.md), linked to the final
report and detailed task reports. This production proposal stays in WIP; no
later implementation task is assigned and cold-firmware debt remains open. The owner
reports the native batch completed and Fedora Bluetooth disabled. Task 1 used
branch `docs/bluetooth-task1-contracts`, rebased onto main `abbeded`, with only
documentation edits. Pointer drafts `304d1d7` and `a2ff720` were read, not
changed. No QEMU, debugger, passthrough or probe job was started for task 1.

Task 2 now uses branch `bluetooth/runtime-hci`, from fresh main `3bda2c3` after
#548 merged. The attached-controller baseline must be
captured before code changes; USB-node access is pending. Baseline preparation,
current toolchain inputs and cleanup are recorded in the runtime transport note.
