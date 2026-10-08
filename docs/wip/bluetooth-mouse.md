# MX Master 3S on Pyxis

Status: **documentation proposal, assigned 2026-10-08; no decisions accepted.**
Prepared from main `85e1b749550194d167019e62c7718907ca4db6b1`, after
[merged #536](https://git.internal/PyxisOS/pyxis-os/pulls/536). The owner has seen
the [investigation results](../development/bluetooth-investigation.md) and
assigned this proposal. Publishing, reviewing or merging it does not authorize
implementation. The owner chooses the milestone and explicitly starts its tasks.

The goal is one bonded MX Master 3S providing ordinary relative motion, primary
buttons and vertical wheel to the system pointer on the ThinkPad's AX200. The
scan established its name, HID service and appearance; it established no pairing
capability, GATT layout or input behavior. BR/EDR, SCO/audio, keyboards, arbitrary
peripherals, vendor gestures, DPI configuration and general Bluetooth APIs stay
outside this proposed milestone. Extra buttons, horizontal/high-resolution wheel
and any vendor report needed for basic input require inspection and a scope
decision, rather than successful fake support.

## First owner decision round

Only these three decisions are put to the owner now. Each remains **open** until
an explicit answer is recorded here. All later recommendations are tentative;
later rounds wait for this round, with at most three decisions per round.

1. **Stack boundary.** Recommended: kernel USB transport, Intel initialization
   and HCI packet/flow-control ownership; one trusted userspace service owns GAP
   policy, L2CAP, SMP pairing, ATT/GATT and HID interpretation. This keeps DMA
   and controller state with their existing owner and pairing/key policy out of
   the kernel. Alternatives are a userspace HCI owner over a narrower transport,
   or a kernel Bluetooth stack; either changes the contracts below.
2. **Pairing security.** Recommended: require LE Secure Connections and a
   128-bit encryption key, permit unauthenticated Just Works only during an
   explicitly authorized enrollment of this mouse, and never fall back to LE
   legacy pairing. Just Works does not authenticate the selected mouse against
   an active attacker. Requiring authenticated pairing instead may block the
   mouse if it has no suitable I/O/OOB capability; that capability is unmeasured.
3. **Closure.** Recommended: staged warm-host development, but cold AX200
   initialization, durable bond reuse and native pointer use are required before
   calling the mouse milestone complete. A warm-only milestone is an alternative
   with an explicit dependency on another OS having initialized the controller.
   This proposal recommends resolving the deferred cold upload in this milestone.

## Proposed ownership and service lifetime

The lean boundary in decision 1 would assign ownership as follows. It is not
implemented by the existing probes.

| Owner | Responsibility |
| --- | --- |
| Kernel USB/xHCI worker | Checked descriptors, endpoint admission, rings/DMA, physical completion identity, copied receive queues and controller quarantine. All mutation stays on the owning BSP worker. |
| Kernel Bluetooth/HCI adapter | AX200 selection and initialization, complete HCI command/event/ACL framing, command credits, controller ACL packet credits and bounded copied packet delivery. It alone changes that bookkeeping. |
| Trusted userspace Bluetooth service | Enrollment/reconnect policy, connection procedures, L2CAP fragmentation/reassembly and LE signaling, SMP, ATT/GATT discovery, bond storage and HID report interpretation. It consumes HCI results rather than maintaining a second credit authority. |
| Kernel system pointer | Physical position, button state, surface routing, cursor, activation and pointer lock. The Bluetooth service submits normalized input as an authorized source. |

The controller grant would be exclusive and process-owned, with copied HCI
packets/results and explicit capacity, deadline, sequence/loss and terminal-state
semantics. It grants controller control to the trusted service, not raw USB,
physical addresses or DMA mappings. Kernel initialization finishes before handoff;
service commands cannot race its firmware transaction. The exact protocol and
rights need review in task 1; no placeholder ABI is proposed here.

Commands and ACL data need separate, bounded queues and the controller's real
completion/credit events. A command timeout is not permission to associate a late
same-opcode reply with a new request. Idle interrupt or bulk reception must not
quarantine the controller. The current synchronous bulk-IN timeout policy is
therefore insufficient for both runtime ACL reception and Intel bootloader
events. Extend owned asynchronous bulk reception for these concrete uses; keep
storage's request ownership and failure behavior separate.

Service exit, connection loss, queue discontinuity or terminal transport failure
stops input and clears that source's held buttons. It also cancels protocol work
and reports unavailable; no silent dropped-button state or implicit re-pairing.
Teardown cannot recycle outstanding DMA. Start with the existing terminal
transport limits and explicit reboot recovery where ownership remains uncertain.
Mouse radio disconnect/sleep is distinct from USB controller removal and should
not quarantine USB storage. Reconnect to the stored bond is proposed with bounded
backoff; authentication failure stops for owner action rather than deleting the
bond or enrolling whoever advertises the same name.
Tag pending work and input with a connection generation so a reused HCI
connection handle cannot deliver a previous connection's notifications as fresh
input. Losing that continuity makes the source unavailable.

Trusted startup starts one system-wide controller service and supplies controller,
clock, randomness, private storage and input-source grants directly to it; it
does not give each space a radio owner. Its configured placement and bootstrap
handoff need review with task 1. A read-only status export can use
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
resulting version/capabilities. A known compatible operational warm build would
skip SFI upload, avoiding a reset solely to enforce a byte-for-byte pin; unknown
builds remain unavailable pending an explicit compatibility decision. Record
accepted running-version compatibility separately from the packaged file pin.
That warm-version policy remains a later owner decision, not the investigation's
accept-any-operational-build policy extended by assumption.

Cold support needs secure-send bulk OUT, independently owned bootloader bulk IN
and interrupt events, and bounded progress while commands wait. Use actual vendor
boot notifications rather than Linux's internally synthesized completions. If
startup requires USB re-enumeration, design its retirement/reattachment boundary
before attempting it; today's active-removal quarantine is not a recovery path.
Do not merge the warm probes as a substitute. The accepted
[cold-upload debt](../technical-debt.md#bluetooth-cold-firmware-upload-and-running-version-policy)
stays open until cold initialization is measured.

## Proposed pairing and bonds

Discover a candidate with the accepted bounded active scan; surface parsed names,
HID service/appearance and run-local labels only. Scanning is an explicit control
action, not a perpetual background enrollment loop. An authorized local user
selects a candidate while physically putting the mouse in pairing mode. Ambiguous
candidates stop for selection; a matching name/service does not establish identity.
Keep the public scanner-address policy for initial development only as a proposed
default; privacy-address configuration and discovery/reconnect parameters need
their own later policy review.

Read the actual SMP feature exchange before choosing an association method. The
[Security Manager specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/security-manager-specification.html)
distinguishes LE Secure Connections from authenticated pairing: a NoInputNoOutput
peer normally selects unauthenticated Just Works. No such capability was measured
in the scan. If accepted, decision 2 would permit that method only with explicit
enrollment consent;
a required unsupported association method stops for a decision. Verify the
negotiated key size and encryption success before accepting any HID input, and
reject legacy downgrade, debug keys and malformed pairing exchanges.

Prefer the existing pinned [Mbed TLS/TF-PSA-Crypto port](../../ports/mbedtls/README.md)
for P-256 and AES-CMAC primitives, subject to checking its exported configuration
and required operations. Keep Bluetooth's SMP transcript/state logic in the
service. Do not hand-roll cryptography or change TLS configuration silently.
Any recipe change belongs in a focused ports PR before updating the parent pin,
following [repository ownership](../development/sdk-and-repositories.md).
Use the explicit [random grant](../devices/randomness.md) through a checked
crypto entropy adapter; there is currently no kernel CSPRNG. Entropy failure
aborts pairing; timestamps and predictable fallback keys are not acceptable.

Proposed bonds persist in a dedicated writable npfs volume/root granted only
to trusted startup and the Bluetooth service. Ordinary applications receive
neither that root nor raw keys; a hidden subdirectory of shared `home://` or
`tmp://` is not isolation. The service alone writes keys and private peer identity
(including any address/type or IRK needed for bond lookup/resolution); filenames,
status and diagnostics use an opaque local bond label. These runtime private
records never enter source, reports, PRs, raw packet dumps or debugger captures.

The storage authority is capability isolation, not an invented UID or permission
bit. Initial proposal has no encryption at rest: possession of the disk or raw
mount/backup authority can expose keys. If that is unacceptable, stop to design
key wrapping and its separate unlock authority; a key beside the encrypted bond
is not protection. Live boots without a private persistent root should report
bonding unavailable, rather than claim RAM storage is persistent.

Write a bounded bond record to a temporary file, atomically replace its entry and
explicitly synchronize through [native filesystem operations](../interfaces/filesystem-mutations.md).
Report durable bonding only after successful sync; on persistence failure,
disconnect and report it, preserving uncertainty about the peer's stored bond.
Reboot must load the saved security properties and authenticate possession of
the bond without falling back to new pairing. Authorized forgetting stops
reconnect, disconnects, removes/syncs the record and reports durability; it cannot
promise erasure of old disk blocks, backups or the mouse's retained key. Bond
scope, at-rest policy and enrollment/forget authority need a later owner round.

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

Normalize checked relative X/Y, primary buttons and vertical wheel from the
actual report map. Preserve releases and wheel units, with bounded arithmetic
and explicit sequence/loss handling. Reject absolute or unsupported reports
rather than interpreting them as relative motion. Initially rediscover GATT
after reconnect; caching/service-change policy can follow only if needed.

The owner has accepted the [system-pointer milestone](pointer.md), with kernel
positions; Codex alpha is implementing its task 1. This is an accepted dependency,
not a proposal to move positions into Bluetooth userspace. After its source
contract lands, coordinate a narrow trusted input-source grant: normalized input
feeds the same kernel routing, cursor, selection and lock paths as PS/2. Exact
ABI and multi-source aggregation need review with alpha; this proposal introduces
none. Clear only the lost source's held buttons, apply the accepted device-loss
lock behavior and preserve PS/2 operation. Surface-owner warp authority does not
authorize physical input or clicks that satisfy user activation.

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
sharing report normalization and the pointer input contract is useful regardless
of parser placement. The sharing scope needs a later owner decision.

## Proposed tasks and completion evidence

Every task starts only after its necessary decisions and explicit owner
assignment. Use focused kernel/userspace/ports PRs, publish dependency commits
first and state merge order. Probe branches remain historical evidence.

- [ ] **1. Contracts and dependencies.** Record accepted stack/security/closure
  choices, then resolve the remaining firmware, bond-authority, scan/reconnect,
  HID/sharing and failure-lifetime decisions in rounds of at most three. Agree
  the packet/control and input-source contracts with pointer task 1. Identify
  capability/report questions and their measurement gates in the later tasks;
  neither scan evidence nor this design task establishes pairing/report support.
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
  ATT/GATT discovery. Protected attributes, the secured HID map and all input
  wait for the required encryption and bond state; encryption-required replies
  are a boundary, not a failure to bypass.
- [ ] **5. Secure Connections enrollment.** Check the actual association model,
  capture SMP feature exchange, implement pairing with reviewed crypto/entropy
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
  the proposed hardware goal. Document remaining limits/debt, retire this WIP
  to subsystem references and update links after the owner accepts completion.

The ThinkPad's already-requested native-check batch, including re-enabling Fedora
Bluetooth, comes after the investigation and is not run by this documentation
task. Its results may refine this proposal; that batch does not automatically
authorize the production milestone or its new native qualification tasks.

## Decision and investigation handoff

First-round answers: **pending**. The firmware pin/mirror and warm-version policy,
private bond authority/at-rest policy, discovery/reconnect behavior, report scope,
input-source lifetime and USB HID sharing are queued topics, not further questions
put to the owner now. Refine their recommendations after the first round, and
obtain explicit answers before implementing dependent behavior.

Once the owner has decided on this proposal, retire
[the investigation WIP](bluetooth.md) into an appropriate concise reference,
linking the existing task reports and final report instead of duplicating them.
Keep this proposed milestone in WIP until it is accepted and implemented. This
document neither marks it assigned for implementation nor closes the accepted
cold-firmware debt.
