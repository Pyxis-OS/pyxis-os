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

The BSP topology owner admits post-boot low/full/high-speed HID leaves on roots
and supported USB 2 hub chains, including new and returning hubs on any prepared
controller. Hub notifications schedule inspection; physical admission and
retirement stay outside report collection. The boot inventory remains a snapshot;
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
retirement or controller corruption may quarantine the entire controller until
reboot. Proven device/subtree removal leaves unrelated classes running, with
fresh hub/HID admission through the shared topology owner. See
[USB switch debt](https://git.internal/PyxisOS/pyxis-os/issues/1293).
Attach/detach diagnostics are debug records.

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
holds. For a known Input ID, trailing bytes are ignored and a short payload is
zero-extended before field extraction. An absent ID byte is never fabricated;
unknown IDs (except the reserved-ID rule below) and invalid field values are
dropped without changing input state. Each interface counts malformed reports and its consecutive
streak; a valid report resets that interface's streak. Sixteen consecutive malformed
reports retire the whole binding, including a keyboard sibling. Transport errors
and lost transport continuity still retire it; interrupt STALL recovery is not
implemented. This replaces the single-malformed-report policy by owner decision
2026-10-11. Unsupported admission preserves a supported boot keyboard; independent
interface retirement is not added.

Admission is bounded to the prepared 4,096-byte control and 1,024-byte Input
capacity, 16 layouts, 64 relevant fields and eight collection/global pushes.
Local usage expansion stays bounded in Mouse/root scopes. Ignored collections
retain only their first usage for Collection identity, validate ranges without
expanding them, and still account for all report lengths/global state.
Receive TDs cover at least the endpoint maximum packet size and the largest
admitted Input report, within the existing capacity; boot keyboards also receive
at least eight bytes. Boot keyboard decoding retains its defined eight-byte
length. Known mouse Input layouts use the owner-accepted 2026-10-11 length
tolerance above, within receive capacity; range checks and exact Feature images
are unchanged. Padding does not infer a boot-format fallback. HID §8.4 requires shorter reports to terminate with a
short packet, including a zero-length packet for an exact packet multiple; one
transfer can span multiple packets. One worker-only adjustment before receives
start handles confirmed boot fallback; backing remains retained until reboot.

Understood logical wheel-scoped Resolution Multiplier Features use a complete read,
scalar-only modification and readback. Unrelated bits survive. Only confirmed
positive integral effective multipliers qualify, including logical-to-physical
mapping and Unit Exponent. Unknown wheel scale disables the affected axis;
a scope covering other controls refuses report admission. The coordinator keeps
per-source/axis conversion fractions and applies mouse direction once. No
output-report driver is added; boot-wheel decoding requires the reserved-ID rule
below or the qualified QEMU extension. Report-wide or
otherwise unknown multiplier scopes refuse this first profile; alternate
configurations are not retried after report admission refuses the selected one.

Unsupported report admission may fall back on an actual boot mouse only after
confirmed SET_PROTOCOL(boot), decoding the defined three-byte prefix. The exact
qualified QEMU four-byte extension remains available; unknown trailers convey no
scroll or extra buttons. Other malformed input does not select a new decoder.

**Reserved-ID switch (owner accepted 2026-10-11).** If a boot-subclass Mouse
interface with an admitted ID-bearing descriptor receives a nonempty Input
packet beginning with 0, permanently select boot decoding for this attachment.
HID reserves report ID 0, so it cannot be an admitted report ID here. Apply the
rule to initial GET_REPORT snapshots and interrupt reports, before extracting
fields. Never switch on a nonzero first byte, a nonboot interface, or a descriptor
without IDs. There is no device-ID quirk table.

The triggering packet and later packets use the defined boot prefix: the low
three button bits in byte 0, signed X/Y in bytes 1/2, and signed wheel in byte 3
when at least four bytes arrived; longer trailers are ignored. Three-byte packets
have no wheel. Pan is unavailable; wheel conversion becomes 120/1 and old
fractional residues are cleared. Shorter packets remain malformed under the
same drop/count/sixteen-consecutive retirement policy. Prior nonzero packets may
have been decoded as report IDs before the first reserved-ID observation.

No second SET_PROTOCOL is sent: the observed stream already speaks boot, and a
control write could alter its proven format while input is active. Endpoint
receive capacity, attachment lifetime and the keyboard sibling are unchanged.
The decoder stays boot until detach; fresh admission starts again from the
descriptor. The switch logs once under either diagnostic option, with no new
default output.

Native ASUS 1ea7:0066 motion, clicks and wheel are qualified with this switch.
Its back/forward side buttons are unsupported; the owner accepts this limit
without further work (2026-10-11). G502/Lightspeed 046d:c53a remains in report
mode and passes motion/buttons/free-spin/Cmd+R/replug. See the
[native result](../development/experiments/pointer-input-quality/README.md#native-task-1-closure).

After SET_PROTOCOL(report) on a boot mouse, admission reads GET_PROTOCOL.
The readback (including an unavailable or unexpected result) is diagnostic
evidence and does not switch decoders or trigger an automatic boot fallback.
Initial malformed GET_REPORT snapshots are also dropped, preserving the
other snapshots and proceeding with unknown/all-up state for that report.

`usb.hid.describe=1` opts into bounded candidate report descriptors and admitted
protocol/layout/multiplier dumps at info level, for native qualification.
It or `usb.xhci.diagnose=1` also gates protocol readback, transport loss and
malformed-report counts. Malformed-report diagnostics are limited to one
observation per interface per five seconds, plus the final retirement. The
first differing Input length is logged once per layout. Up to four interrupt Input packets per admission on a report-protocol Mouse
interface may log their ID/length and at most 16 actual bytes. Keyboard interfaces never have
payload dumps; a Mouse descriptor declaring Keyboard/Keypad collections or
keyboard Input usages suppresses raw bytes too. No device strings or serials are
logged. The unavailable-device loss path reports retained failed
completion code and requested/residual length before endpoint retirement, after
releasing input state. No keyboard payload or key identity is dumped. Normal
boots add no dump or packet logging. Device string/serial descriptors are never
read or printed by this diagnostic.
The opt-in `usb.xhci.diagnose=1` [burst-freeze capture](../development/experiments/pointer-input-quality/README.md#one-diagnostic-retest-through-luna)
adds bounded controller/HID-loss observations at info level; it changes no
recovery rules and is not a timing qualification.
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
keep their QEMU/native evidence distinct; [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1291)
records coverage still open.

## Wireless receiver interference

USB 3 devices and cables can interfere with nearby 2.4 GHz wireless receivers,
even when the storage link is idle. On vela (2026-10-18), placing the boot stick
on the dock beside the `1ea7:0066` mouse receiver made the mouse unusable under
both Pyxis and Fedora; moving the stick off the dock restored smooth input.
This is a hardware radio-interference limit, not a Pyxis USB scheduling defect.
Separate the receiver from USB 3 devices, for example with a USB 2 extension;
see Intel's [USB 3.0 interference paper](https://www.usb.org/sites/default/files/327216.pdf),
sections 3.2 and 4.3.
