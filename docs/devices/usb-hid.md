# USB keyboard and report-mouse input

The BSP USB worker binds boot keyboards and standard relative report mice to
[the shared keyboard path](keyboard.md) and
[system pointer](../interfaces/pointer.md#input-source-coordination). Existing
input grants, focus and process ownership apply; there is no raw USB grant or
device-bypass interface. PS/2 stays available alongside USB. On a USB-only boot,
initial userspace waits on the BSP for the first keyboard or the end of boot
USB discovery, checking every 10 ms. A present PS/2 keyboard keeps direct startup.
Discovery waits hold no input lock; console unavailable errors are unchanged,
and startup does not wait for a device attached after discovery completes.
A USB-only pre-scheduler debug checkpoint precedes initial process creation;
the deferred startup worker creates it after scheduling begins.

## Binding and ownership

Select the first configuration with eligible HID interface metadata, with at most
one boot keyboard and one relative Mouse application interface per device,
including composite dongles. Existing
storage and Bluetooth class owners take priority; a configuration mixing those
classes with HID is not partly claimed. Each interface needs one interrupt IN
endpoint. Boot keyboards require SET_PROTOCOL(boot); admitted boot mice select
report protocol, and nonboot mice already use it. Configuration precedes bounded
report admission and selected endpoint setup; receives start only after protocol,
Feature negotiation and initial snapshots. Initial GET_REPORT queries every
button-bearing report ID when supported; a STALL starts that ID all-up (or the
boot keyboard all-up), without clearing another ID or rejecting its composite
sibling. Before continuing, xHCI
resets halted EP0, clears the owning transaction translator when applicable,
and advances the dequeue pointer past the failed transfer. Failed recovery or
other GET_REPORT errors still fail closed.
Keyboard SET_IDLE(0) is required; a mouse may stall it. Report-protocol
keyboards, tablets, gamepads, vendor protocols, media keys and keyboard LED updates
are unsupported.

Boot admission covers low/full/high-speed leaves on roots or through at most
five external USB 2 hubs. Existing transaction-translator routing selects the
nearest high-speed hub for low/full-speed descendants. High-speed interrupt
intervals and transaction counts come from endpoint descriptors. USB 3 HID is
outside this profile; a dock's USB 2 companion path can qualify.

Hub interrupt notifications support post-boot low/full-speed leaf attachment on
roots and boot-present supported hub chains. New hub topology and high-speed
post-boot attachment are not admitted. The boot inventory remains a snapshot;
`lsusb` does not become a live device browser.

Each controller prepares 32 HID attachment records and up to two streams per
record before AP startup. Initial, failed and retired admissions consume records;
DMA and physical transfer identities stay retained until reboot. Hardware slots
and existing descriptor/hub limits also bound admission. Exhaustion refuses a
new source without replacing a live one. No input callback allocates, sleeps
or submits a USB command. Worker progress drains HID alongside command, control,
Bluetooth and storage completions; input publication briefly holds BSP IF=0.

Ordinary HID-leaf removal stops its endpoints, proves completion/context fencing
and disables its slot before numerical slot reuse. Source state is lost
immediately; a replacement gets fresh software and DMA records. Unprovable
retirement, hardware errors or hub-subtree removal may quarantine the entire
controller until reboot, including its other classes. See
[USB switch debt](../technical-debt.md#usb-switch-subtree-removal).
Attach/detach diagnostics use ktrace.

## Reports and continuity

Keyboards decode the eight-byte boot report, ignore its OEM padding byte, and
map HID usages to the existing physical key positions. Per-device holds
aggregate across keyboards. USB host repeat starts after 500 ms and repeats
every 33 ms on the latest repeatable key; PS/2 hardware typematic is unchanged.
Unchanged reports and repeats supply no new activation. Rollover resets accepted
input; recovered unknown holds are quarantined until release. Source loss resets
accepted keyboard input across destinations and quarantines surviving holds.

Mice admit one standard relative Mouse application collection, including boot,
nonboot and composite interfaces. The parser retains complete Input/Output/Feature
lengths by ID, accounting for padding and ignored fields, and scoped X/Y, Wheel,
Consumer AC Pan and Button usages 1–32. Signed bit extraction uses the declared
logical ranges. Each button-bearing ID owns a snapshot; wheel-only IDs preserve
holds. Unknown IDs, malformed lengths or invalid values retire the whole binding,
including a keyboard sibling. Unsupported admission preserves a supported boot
keyboard; it does not add independent interface retirement.

Admission is bounded to the prepared 4,096-byte control and 1,024-byte Input
capacity, 16 layouts, 64 relevant fields and eight collection/global pushes.
Local usage expansion stays bounded in Mouse/root scopes. Ignored collections
retain only their first usage for Collection identity, validate ranges without
expanding them, and still account for all report lengths/global state.
The largest declared Input report determines the receive length, independently
of endpoint packet size. HID §8.4 requires shorter reports to terminate with a
short packet, including a zero-length packet for an exact packet multiple; one
transfer can span multiple packets. One worker-only adjustment before receives
start handles confirmed boot fallback; backing remains retained until reboot.

Understood logical wheel-scoped Resolution Multiplier Features use a complete read,
scalar-only modification and readback. Unrelated bits survive. Only confirmed
positive integral effective multipliers qualify, including logical-to-physical
mapping and Unit Exponent. Unknown wheel scale disables the affected axis;
a scope covering other controls refuses report admission. The coordinator keeps
per-source/axis conversion fractions and applies mouse direction once. No
output-report driver or guessed native wheel extension is added. Report-wide or
otherwise unknown multiplier scopes refuse this first profile; alternate
configurations are not retried after report admission refuses the selected one.

Unsupported report admission may fall back on an actual boot mouse only after
confirmed SET_PROTOCOL(boot), decoding the defined three-byte prefix. The exact
qualified QEMU four-byte extension remains available; unknown trailers convey no
scroll or extra buttons. Runtime failure never switches protocol silently.

`usb.hid.describe=1` opts into bounded candidate report descriptors and admitted
protocol/layout/multiplier dumps at info level, for native qualification. Normal
boots add no dump or packet logging. Device string/serial descriptors are never
read or printed by this diagnostic.
Dumping consumes the existing admission/progress budget; use it on a small
qualification topology, not as a timing measurement. Maximum descriptor/field
output is roughly 35 KiB per binding (about three seconds at 115200 baud before
other logging costs); many candidates may exhaust the shared discovery deadline.

Holds observed in a successful initial snapshot supply no new presses; release
those keys/buttons before that source can activate input. A device that stalls
GET_REPORT instead starts all-up; its first interrupt report can supply presses,
since attachment-time holds are unknown. QEMU's established keyboard and mouse
profiles instead consume buffered input: keyboard priming takes sixteen snapshots;
mouse priming takes sixteen zero-motion snapshots, since a large movement can
retain a queue head. Only the final state is published. The existing binding
deadline bounds priming and refuses an unresolved snapshot or timeout. This also
consumes invisible extended-key prefixes before deciding that a keyboard is all-up.

These adapters implement the accepted per-source button aggregation and
conditional loss rule in the pointer reference. Bluetooth HID remains a separate
producer. Steam Deck lizard mode would be eligible only if its actual USB
interfaces fit these admitted protocols; no Deck compatibility is established.

References: [USB HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf),
§§7.2.4–7.2.6, §8.4 and appendices B/C/F; QEMU 10.2.2
[HID reports](https://raw.githubusercontent.com/qemu/qemu/v10.2.2/hw/input/hid.c),
[queue capacity](https://raw.githubusercontent.com/qemu/qemu/v10.2.2/include/hw/input/hid.h)
and [USB descriptors](https://raw.githubusercontent.com/qemu/qemu/v10.2.2/hw/usb/dev-hid.c).
Protocol constants and descriptor bytes identify those layouts; no upstream
implementation code is copied. The [earlier boot-input qualification](../development/experiments/usb-hid/README.md)
and [report-mouse qualification](../development/experiments/pointer-input-quality/README.md)
keep their QEMU/native evidence distinct; [technical debt](../technical-debt.md#usb-hid-native-coverage)
records coverage still open.
