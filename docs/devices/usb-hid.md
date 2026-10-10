# USB boot keyboard and mouse input

The BSP USB worker binds HID boot-subclass keyboard and mouse interfaces to
[the shared keyboard path](keyboard.md) and
[system pointer](../interfaces/pointer.md#input-source-coordination). Existing
input grants, focus and process ownership apply; there is no raw USB grant or
new public input ABI. PS/2 stays available alongside USB. On a USB-only boot,
initial userspace waits on the BSP for the first keyboard or the end of boot
USB discovery, checking every 10 ms. A present PS/2 keyboard keeps direct startup.
Discovery waits hold no input lock; console unavailable errors are unchanged,
and startup does not wait for a device attached after discovery completes.
A USB-only pre-scheduler debug checkpoint precedes initial process creation;
the deferred startup worker creates it after scheduling begins.

## Binding and ownership

Select the first supported configuration, with at most one boot keyboard and
one boot mouse interface per device, including composite dongles. Existing
storage and Bluetooth class owners take priority; a configuration mixing those
classes with HID is not partly claimed. Each interface needs one interrupt IN
endpoint and successful SET_PROTOCOL(boot). Initial GET_REPORT supplies a
snapshot when supported; a STALL starts that interface with no held keys or
buttons, without rejecting its composite sibling. Before continuing, xHCI
resets halted EP0, clears the owning transaction translator when applicable,
and advances the dequeue pointer past the failed transfer. Failed recovery or
other GET_REPORT errors still fail closed.
Keyboard SET_IDLE(0) is required; a mouse may stall it. General report-protocol
HID, tablets, gamepads, media keys and keyboard LED updates are unsupported.

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

Mice decode the three-byte boot prefix: left/right/middle buttons and signed
relative X/Y, without acceleration. Unknown trailing bytes are ignored. Wheel is
supported only for the established QEMU four-byte layout, checked against its
identity, endpoint and exact report descriptor. An unrecognized native wheel
extension requires descriptor evidence before it is decoded.

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
interfaces expose these boot protocols; no Deck compatibility is established.

References: [USB HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf),
§§7.2.4–7.2.6 and appendices B/C/F; QEMU 10.2.2
[HID reports](https://raw.githubusercontent.com/qemu/qemu/v10.2.2/hw/input/hid.c),
[queue capacity](https://raw.githubusercontent.com/qemu/qemu/v10.2.2/include/hw/input/hid.h)
and [USB descriptors](https://raw.githubusercontent.com/qemu/qemu/v10.2.2/hw/usb/dev-hid.c).
Protocol constants and descriptor bytes identify those layouts; no upstream
implementation code is copied. The [qualification record](../development/experiments/usb-hid/README.md)
holds the QEMU and native results, and [technical debt](../technical-debt.md#usb-hid-native-coverage)
the coverage still open.
