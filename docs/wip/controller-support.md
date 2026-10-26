# DualSense Edge wired input

Assigned to Codex delta (chronium, 2026-10-26). Target: native wired gamepad
input for Ship of Harkinian, with an independently usable native and SDL viewer.
The release direction is 0.0.10 or 0.0.11. Controller input can be qualified
before llvmpipe; a working viewer or short game scene does not establish
start-to-end game usability.

## Accepted decisions (chronium, 2026-10-26)

1. Use a dedicated **DualSense Edge USB** report decoder, admitted only after
   checking descriptor evidence. The actual unit is Sony `054c:0df2`, not the
   standard DualSense `054c:0ce6`. Generic HID and 8BitDo are deferred. Mouse and
   touchpad paths keep their current behavior.
2. Provide one aggregate gamepad INPUT session per space, with keyboard-style
   focus and process ownership. **Wherever keyboard authority is granted by
   default, gamepad authority is too.** This is not an opt-in space setting.
   Bundles still declare `gamepad/input`. Held buttons and hat directions need
   release/neutral followed by a fresh press after focus returns. Sticks and
   triggers resume their physical position on a fresh complete focused report.
3. SDL core may receive exactly the minimal guarded named-driver declaration
   and registry entry, plus build/config wiring. All controller behavior stays
   in the Pyxis backend. No dummy-driver alias, Linux/HIDAPI impersonation,
   database rewrite or application-specific core behavior is authorized.

The owner explicitly authorized the milestone docs PR, then task 0 closure,
then task 1. Later tasks remain separately assigned. A new material behavior,
authority, lifetime or policy decision stops the affected work for discussion.

## Scope and existing boundaries

Input only: two sticks, independent triggers, hat and verified buttons on the
Edge, including verified extra buttons. A touchpad click is an ordinary button;
touch coordinates do not become pointer input. No raw USB access, rumble,
lightbar, adaptive-trigger output, sensors, controller audio or Bluetooth.
Unsupported operations must report unsupported, never successful fake effects.

The owned 8BitDo is an **Ultimate 2**, not a 2C; its mode is unknown. It is a
later item, with descriptor/protocol and mapping decisions of its own. Generic
HID parsing, standard DualSense, Steam Deck and Xbox GIP are also later work.
This milestone requires the owned Edge, not a second controller purchase.

Current USB HID report parsing is mouse-specific. Its working mouse/I2C
pointer paths are not a gamepad parser to generalize in place. Root/hub runtime
HID discovery, interrupt-IN transport and source-loss/retirement fencing are
available. Existing finite HID record retention and possible whole-controller
quarantine remain limits; a new pad decoder cannot promise unlimited reconnects
or independent retirement of every composite sibling.

The [keyboard interface](../devices/keyboard.md),
[input policy](../interfaces/input-policy.md) and
[USB HID reference](../devices/usb-hid.md) supply the neighboring contracts.
The [SDK/repository boundary](../development/sdk-and-repositories.md) applies:
Pyxis owns kernel/ABI/SDK/image, userland owns wrappers and launch forwarding,
and ports owns SDL/LUS integration. Publish dependencies before parent pins;
ordinary SDK changes do not require a compiler-container rebuild.

## Task 0 evidence

The owner-coordinated, read-only horse capture on 2026-10-26 contains Linux
`lsusb -v` and the bound HID device's sysfs report descriptor. The Linux driver
was `playstation`; no hidraw reads or unbind occurred in that capture. Durable
artifacts are in `/shared/present/controller-support/evidence/`:
`dualsense-edge-horse.txt`, the extracted descriptor binary and its decoded JSON.
These external artifacts are evidence, not files required to build Pyxis.

The unit reports USB `054c:0df2`, bcdDevice `1.00`, high-speed operation,
configuration 1 and four interfaces. Interfaces 0–2 are audio; interface 3,
alternate 0, is HID subclass/protocol 0. Its interrupt-IN endpoint is `0x84`,
64 bytes, bInterval 4; interrupt-OUT is `0x03`, 64 bytes, bInterval 6. At high
speed these encode 1 ms and 4 ms service opportunities respectively, not a
measured application latency or report-production guarantee. Audio alternate
settings and the output endpoint are outside this milestone.

The descriptor is 389 bytes, SHA-256
`962368681aeeecaf4c384c37458fa98a7e58de2905aafad8ee0b81986ee50891`.
It has one Generic Desktop/Game Pad Application and 41 distinct report IDs:

| Type | Report ID | Payload bytes | Bytes including ID |
| --- | --- | --- | --- |
| Input | `01` | 63 | 64 |
| Output | `02` | 63 | 64 |
| Feature | `05` | 40 | 41 |
| Feature | `08` | 47 | 48 |
| Feature | `09` | 19 | 20 |
| Feature | `0a` | 26 | 27 |
| Feature | `21` | 4 | 5 |
| Feature | `82` | 9 | 10 |
| Feature | `85` | 2 | 3 |
| Feature | `a0` | 1 | 2 |
| Feature | `f2` | 52 | 53 |
| Feature | `f5` | 3 | 4 |
| Feature | `20`, `22`, `80`, `81`, `83`, `84`, `e0`, `f0`, `f1`, `f4`, `60`–`65`, `68`, `70`–`7b` | 63 each | 64 each |

There are 39 Feature reports. Report size/count inherit across report-ID
changes, including `61`–`65`, `68` and `70`–`7b`. Counting these against the
mouse parser's 16-layout budget would wrongly reject this device. The Edge
admission walker must bound and account for unused Output/Feature items without
constructing mouse layouts or enabling feature/output operations.

Input `01` has 504 payload bits. Wire byte offsets below include its ID:

| Wire location | Descriptor declaration |
| --- | --- |
| 1–6 | Six unsigned 8-bit absolute axes, logical 0–255; Generic Desktop usages X, Y, Z, Rz, Rx, Ry |
| 7 | Vendor page `ff00`, usage `20`, one 8-bit field |
| 8 low nibble | Hat, logical 0–7, physical 0–315 degrees, null-state flag; values outside 0–7 are declared null |
| 8 high nibble, 9, 10 low three bits | Button usages 1–15, one bit each |
| 10 bits 3–7 and 11 | Thirteen vendor bits, page `ff00`, usage `21` |
| 12–63 | 52 vendor bytes, page `ff00`, usage `22` |

Pinned SDL 2.32.10 source (`5d249570393f7a37e037abf22cd6012a4cc56a71`,
`src/joystick/hidapi/SDL_hidapi_ps5.c`) provides independent source evidence
for complete USB report `01` of 64 bytes: first four axes are sticks, last two
are triggers; the hat is clockwise from up; button byte 10 includes Edge
function/paddle bits. This is a profile hypothesis grounded in source, not a
measurement of chronium's live unit. The descriptor alone does not name vendor
bits, prove an observed neutral value or show trigger-stop behavior. The SDL
source also handles zero analog trigger values with asserted digital trigger
bits, which must be checked against this Edge rather than guessed away.

Task 0 still needs labeled live input reports, actual lengths, neutral/range
observations and the physical button/profile correspondence. The requested
capture is read-only with Linux's driver bound: idle, independent stick/trigger
sweeps (all trigger-stop positions), hat directions, individual buttons including
Fn/paddles, and releases. No output/feature request, ioctl, grab or unbind is
needed. Task 1 starts after this gate closes; descriptor evidence is not native
Pyxis or QEMU qualification.

## Native contract to finalize in task 0

Use one typed per-space `gamepad` object with INPUT authority. Every default
keyboard grant/forwarding path gains gamepad alongside it: first-process
bootstrap, session/shell launches and ordinary bundle grant handling. Reserve
its bootstrap name and export its ABI through the SDK. Bundle consumers declare
`gamepad/input`; no new trusted-space opt-in flag is introduced.

Acquisition is same-space, exclusive and process-owned, independent of display
ownership. A competing/repeated acquisition is BUSY. Handle copy/close neither
transfers nor releases ownership; RELEASE or owner exit ends it. An empty
catalog is valid so startup before attachment works. Missing authority is a
real denial. Object copies do not authorize cross-space capture.

Use keyboard focus: selected space, no hidden existing graphics layer and no
focused system overlay. A console-only viewer needs no graphics surface.
Acquiring input does not change focus. A controller button never becomes system
navigation or a keyboard action. SDL background-input hints cannot bypass this.

Keep attachment identity separate from mapping identity. Each attachment gets
a fresh non-reused boot-local ID; reconnect never rebinds an old open handle.
Metadata carries USB VID/PID/bcdDevice, the admitted Edge layout and an honest
bounded profile name. These are model/mapping metadata, not a serial or proof
of physical uniqueness. Do not expand unrelated boot-only `lsusb` inventory.

The session covers separate per-device snapshots and a bounded catalog.
Operations provide acquire/release, catalog/info, state read and accepted-state
recovery. One level-triggered READABLE interest covers input, focus/reset and
attachment changes; readiness neither acquires nor reserves input. No periodic
SDL polling thread is needed. Exact ABI layouts and finite capacities must be
written down before implementation, against the confirmed Edge state fields.

Publish a full state only after a complete admitted input report. Require ID
`01` and exactly 64 bytes; no mouse-style short-payload extension or trailers.
Validate descriptor semantics and identity before binding; VID/PID or ID alone
is insufficient. Unknown formats cannot partially mutate accepted state.

Buttons/hat held through acquisition, reset or focus change require release or
neutral before a fresh action. Continuous sticks/triggers resume their physical
values on the next complete focused report, with no kernel deadzone. Focus loss
invalidates state instead of manufacturing a zero-valued raw-axis report.
Unfocused queries do not expose the hidden physical stream.

Preserve discrete transitions or report an explicit RESET. Continuous state may
coalesce only for the same attachment/focus and unchanged discrete state.
Overflow discards unreliable pending input and requires catalog/state recovery;
it cannot silently lose a button edge. Removal invalidates the device before
publishing disappearance. No precise timestamp or every-report delivery promise
is required. Input callbacks allocate nothing, issue no USB commands and do not
block; preserve current source and DMA/retirement ownership rules.

## Work order

- [ ] **0. Evidence and native contract.** Finish the labeled Edge input capture,
  confirm ordinary USB input needs no vendor initialization, settle explicit
  state/operation layouts and bounds, and record the evidence/remaining limits.
- [ ] **1. Edge native input.** Dedicated descriptor-checked decoder, retained
  gamepad source, object/focus/readiness/lifetime, SDK exports and userland
  wrappers/default forwarding/bundle recognition. A hand-run native viewer
  validates real controls, missing grant/competing ownership, empty startup,
  focus, held-input rearming and reconnect with a fresh ID. Deliver dependency
  PRs before the parent pin; update this task here, not in the milestone index.
- [ ] **2. Pyxis SDL backend.** Named native driver, real USB identity, verified
  automatic GameController mapping, lifecycle/reset handling and blocked event
  wakeup in both SDL profiles. Validate raw and mapped axes/buttons/hat in a
  hand-run viewer; unsupported effects fail explicitly. Separate assignment.
- [ ] **3. Edge in LUS/SoH.** Remove native controller-init/default-map exclusions,
  declare bundle input, and use the existing controller manager/settings UI.
  Verify fresh and existing keyboard-only configs, editable bindings without
  erasure, save/reopen, simultaneous controls, menus/aiming and reconnection.
  Renderer, assets and private game distribution are outside this task.
- [ ] **4. Native/release closure.** Owner-attended Vela/ThinkPad/root/hub checks
  as assigned, ordinary launcher paths, binding persistence after reboot and
  separate start-to-end game usability once rendering permits it. A second Edge
  can qualify simultaneous devices if available; Ultimate 2 is not a gate here.

## Validation and limits

Documentation changes need link/anchor review and `git diff --check`, not a
boot. Implementation uses ordinary builds, interactive QEMU and debugger
inspection. Real Edge passthrough requires owner coordination on the physical
host; a no-pad QEMU boot proves neither report decoding nor native hardware.
Do not add a generic USB-host launcher, tests, fault injection or CI automation.
Capture completion status, requested/actual length and source identity for real
failures; disappearing input alone does not establish a controller freeze.

## You should know

Gamepad authority follows keyboard defaults. Focus return can resume a held
stick or trigger; held buttons/hat need a fresh action. Existing finite USB
record retention/quarantine limits remain. This plan introduces a typed input
session and fresh attachment identity, not raw HID authority. No controller
implementation or Pyxis/QEMU/native qualification is claimed by this document.
