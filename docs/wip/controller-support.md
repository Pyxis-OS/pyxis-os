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

The owner authorized task 2 after task 1 merged and the Vela check passed
(chronium, 2026-10-26), with task 3 following it. Later tasks remain separately assigned. A new material behavior,
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

The descriptor and two pinned upstream sources establish the input-only profile:
[SDL 2.32.10](https://github.com/libsdl-org/SDL/blob/5d249570393f7a37e037abf22cd6012a4cc56a71/src/joystick/hidapi/SDL_hidapi_ps5.c)
(`5d249570393f7a37e037abf22cd6012a4cc56a71`) and
[Linux stable v6.12.1 hid-playstation](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/drivers/hid/hid-playstation.c?id=d390303b28dabbb91b2d32016a4f72da478733b9)
(`d390303b28dabbb91b2d32016a4f72da478733b9`). Unmodified sources, licences
and file hashes are retained under `/shared/present/controller-support/sources/`.
The Linux file has SHA-256
`51973e6db85bb914d4186fa0fffc14d2a754f28123cc48994189a3d55ddf8bed`
and SPDX `GPL-2.0-or-later`; it is reference evidence, not code vendored into
Pyxis or a build dependency.

Both sources decode the complete USB report `01` at 64 bytes. Bytes 1–4 are
left X/Y and right X/Y; bytes 5–6 are left/right analog triggers. The hat is
clockwise from up. The native button assignments are:

| Wire location | Native buttons, in ascending bit order |
| --- | --- |
| 8 bits 4–7 | Square, Cross, Circle, Triangle |
| 9 bits 0–7 | L1, R1, L2, R2, Create, Options, L3, R3 |
| 10 bits 0–2 | PS, touchpad click, microphone mute |
| 10 bits 4–7 | Left function, right function, left paddle, right paddle |

Linux explicitly names touchpad click at byte 10 bit 1 and microphone mute at
bit 2. SDL's internal `MISC1`/`PS5_TOUCHPAD` labels at those positions must not
be copied as physical names. SDL supplies the Edge function/paddle masks; the
captured descriptor declares those positions as vendor bits. Byte 10 bit 3 and
all other vendor data are ignored.

SDL first reads a 64-byte USB report before its feature/output setup; Linux
also treats USB `01` as the full report. This supports trying the standard
input path without vendor activation. The horse capture had Linux's driver
bound, so it does not prove Pyxis initialization needs. Task 1 must confirm
reports after ordinary USB configuration alone and stop for discussion before
adding any vendor sequence.

Task 0 is closed on this descriptor and source evidence (chronium, 2026-10-26).
The owner explicitly skipped the long labeled input capture. Physical control
correspondence and live report behavior remain a brief, owner-attended Vela
check in task 1: plug in and exercise both sticks, both triggers, the D-pad and
all buttons in the native visualizer, taking roughly one minute. This does not
qualify every trigger-stop position, firmware profile or observed axis range.
SDL synthesizes a full analog trigger value when the analog byte is zero and
the digital trigger bit is set; the native ABI instead preserves both raw
values independently. No trigger synthesis or exhaustive trigger-stop
qualification is part of this milestone's input-only contract.

## Native contract fixed in task 0

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
SDL polling thread is needed. The initial implementation admits at most eight
live attachments; this is a finite implementation bound, not a hardware limit.

The SDK exports `abi/gamepad.h`. `gamepad_state` is 12 bytes: a 32-bit mask of
19 named buttons in the table's order, six raw unsigned bytes (left X/Y, right
X/Y, left/right trigger), one hat byte and one zero reserved byte. Axes remain
0–255 with no kernel calibration, deadzone, inversion or trigger synthesis;
buttons L2/R2 remain independent of the analog trigger bytes. Hat values are
0–7 clockwise from up, with every other wire value normalized to neutral 8.
A 48-byte event carries attachment ID, sequence, catalog generation, type,
FOCUSED/VALID flags and accepted state. A 64-byte info record carries attachment
ID, VID/PID/bcdDevice, profile and a trusted 48-byte name. The 80-byte catalog
holds generation, count and eight attachment IDs; reserved fields are zero.

Operations are ACQUIRE, READ, RELEASE, CATALOG, INFO and STATE. ACQUIRE,
RELEASE and CATALOG are header-only; READ uses a 24-byte request whose flags
select blocking (zero) or POLL (TIMED_OUT when empty). INFO and STATE use a
24-byte request selecting attachment ID. STATE returns an event-shaped
accepted snapshot; its VALID flag determines whether state can be consumed.
Acquisition queues RESET. RESET uses attachment zero and requires catalog/state
recovery. Event types also include INPUT, ATTACHED, REMOVED, FOCUS_GAINED and
FOCUS_LOST. Catalog generation changes on attachment/removal; callers repeat
enumeration if it changes while collecting records. Gone IDs return NOT_FOUND.

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
The initial queue holds 64 events; its size is an implementation choice.
Overflow discards unreliable pending input and requires catalog/state recovery;
it cannot silently lose a button edge. Removal invalidates the device before
publishing disappearance. No precise timestamp or every-report delivery promise
is required. Input callbacks allocate nothing, issue no USB commands and do not
block; preserve current source and DMA/retirement ownership rules.

## Work order

- [x] **0. Evidence and native contract.** Captured descriptor and pinned SDL/Linux
  source establish the Edge profile and fixed native layouts. The owner waived
  the long capture; task 1 retains the brief native correspondence and standard
  input-path check, with no exhaustive trigger-stop qualification.
- [x] **1. Edge native input.** Dedicated descriptor-checked decoder, retained
  gamepad source, object/focus/readiness/lifetime, SDK exports and userland
  wrappers/default forwarding/bundle recognition. The brief Vela visualizer
  check plugs the Edge into vela, presses a few buttons and moves the sticks
  for about a minute; no exhaustive control/trigger-stop matrix. Local checks
  cover the ordinary launch path, empty startup, presentation and clean exit;
  authority/lifetime/reset paths also receive source review. The owner-attended Vela check passed, including
  connected-at-boot (chronium, 2026-10-26). Deliver dependency
  PRs before the parent pin; update this task here, not in the milestone index.
- [x] **2. Pyxis SDL backend.** Named native driver, real USB identity, verified
  automatic GameController mapping, lifecycle/reset handling and blocked event
  wakeup in both SDL profiles. Validate raw and mapped axes/buttons/hat in a
  hand-run viewer; the backend refuses real output requests. SDL core retains
  upstream cached-LED and zero-rumble shortcuts (chronium, 2026-10-26).
- [ ] **3. Edge in LUS/SoH.** Remove native controller-init/default-map exclusions,
  declare bundle input, and use the existing controller manager/settings UI.
  Verify fresh and existing keyboard-only configs, editable bindings without
  erasure, save/reopen, simultaneous controls, menus/aiming and reconnection.
  Renderer, assets and private game distribution are outside this task.
- [ ] **4. Native/release closure.** Owner-attended Vela/ThinkPad/root/hub checks
  as assigned, ordinary launcher paths, binding persistence after reboot and
  separate start-to-end game usability once rendering permits it. A second Edge
  can qualify simultaneous devices if available; Ultimate 2 is not a gate here.

The native interface and visualizer are described in the
[gamepad reference](../interfaces/gamepad.md). Task 1 was delivered in
[#1258](https://git.internal/PyxisOS/pyxis-os/pulls/1258), with
[userland #390](https://git.internal/PyxisOS/pyxis-userland/pulls/390) first.
Fresh kernel, SDK, full ports/userland and image builds passed. QEMU (q35/KVM,
four CPUs, Bochs 1024x768, empty xHCI) passed empty acquisition, fonts package
resolution, the normal `gamepadtest` launch, rendered empty state and Escape.
Native vela check (chronium, 2026-10-26): all directly exercised buttons
and triggers worked, including physical trigger stops. Paddles were
remapped in the onboard profile and their independent bits remain unqualified;
that limitation is accepted. Connected-at-boot passed after the USB 2
reset/address recovery correction; chronium confirmed everything works
(2026-10-26). Both PRs are merged and task 1 is complete. All six stick/trigger
report axes are
unsigned eight-bit values (0–255), not values truncated by Pyxis; this does not
establish the controller's internal ADC resolution.

## Task 2 implementation and qualification

The Pyxis SDL joystick backend and asset-free manual viewer are implemented.
Both ordinary and OSMesa archives build, and a hand-run QEMU viewer checks raw
and automatically mapped six-axis/19-button/hat input. The ordinary profile
also passes empty startup, hotplug/removal, distinct SDL instances after replug,
focus rearming and missing-grant refusal. The OSMesa check uses its software
window surface, not an OpenGL context. Input reports are external synthetic
valid Edge packets; this is not a second physical qualification or latency test.
SDL's existing 1 ms attached-device and 3 second enumeration polling caps remain;
native gamepad readiness participates in the video wait with no new thread.

SDL core remains registration-only. Its upstream repeated-LED cache and
zero-rumble success shortcuts are retained (chronium, 2026-10-26). The Pyxis
backend reports zero output capabilities and returns Unsupported for real
output requests; no LED, rumble or sensor implementation is added. Task 2 is
complete; task 3 uses LUS/SoH's existing controller manager and settings UI.

## Validation and limits

Documentation changes need link/anchor review and `git diff --check`, not a
boot. Implementation uses ordinary builds, interactive QEMU and debugger
inspection. The brief native Vela check requires owner coordination; no
physical-pad QEMU matrix is a gate. A no-pad QEMU boot proves neither report decoding nor native
hardware.
Do not add a generic USB-host launcher, tests, fault injection or CI automation.
Capture completion status, requested/actual length and source identity for real
failures; disappearing input alone does not establish a controller freeze.

## You should know

Gamepad authority follows keyboard defaults. Focus return can resume a held
stick or trigger; held buttons/hat need a fresh action. Existing finite USB
record retention/quarantine limits remain. This plan introduces a typed input
session and fresh attachment identity, not raw HID authority. The native control check above qualifies
the exercised input, including connected-at-boot. Independent remapped paddles
remain unqualified; generic HID and output remain outside this milestone.
