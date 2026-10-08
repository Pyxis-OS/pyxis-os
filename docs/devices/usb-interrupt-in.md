# Root full-speed USB interrupt IN

With `CONFIG_XHCI=y`, the private USB host interface supports one interrupt-IN
stream per boot-present, root-connected full-speed device. Class code supplies
checked endpoint facts and selects the device's USB configuration. Other speeds
and downstream devices return `USB_UNSUPPORTED`; there is no public raw-USB ABI
or automatic Bluetooth/HID binding.

The initial implementation follows the
[accepted investigation scope](../wip/bluetooth.md#accepted-interrupt-in-decisions).
The [qualification record](../development/experiments/usb-interrupt-in/README.md)
distinguishes real AX200 passthrough traffic, emulated storage coexistence and
source-reviewed error paths from native hardware qualification.

## Admission and configuration

`usb_host_configure_interrupt_in` admits an endpoint during boot enumeration on
its existing BSP controller worker. The endpoint has an IN address, a full-speed
packet size of 1–64 bytes and a nonzero interval. The caller supplies a receive
length bounded by `usb_host_interrupt_capacity()`. Packet size and receive length
are distinct: Bluetooth requests 257 bytes, while a later HID consumer can
request its report length.

Configuration constructs Interrupt IN context fields, translates full-speed
`bInterval` into the xHCI microframe exponent, reserves periodic payload, and
publishes an owned ring through Configure Endpoint. The class then completes
SET_CONFIGURATION/any required alternate selection through EP0 before calling
`usb_host_interrupt_start`. Host configuration does not send those device
requests. There is no runtime reconfiguration or stream replacement.

Interrupt and bulk configuration preserve the highest existing Slot Context
Entries value and reject collisions within a device's DCIs. Different devices
can use the same DCI; completion dispatch first identifies their slot.

## Prepared resources and ownership

Before AP startup, each advertised root port receives one ring page, two
page-backed DMA receive buffers and metadata containing eight copied completion
entries. Their initial 257-byte capacity and counts live in
`kernel/usb/settings.h`, separately from storage budgets. Each receive buffer
fits its own page and a single Normal TRB's 64 KiB boundary. A controller-owned
DMA arena supplies the borrowed slices; unused and admitted resources remain
retained until reboot after controller startup.

All stream mutation remains on the owning BSP controller worker. IRQ notification
does not count packets or collect data. Each posted receive keeps its own buffer
and physical TD identity. Publishing payload/cycle bits precedes its DCI doorbell;
producer wrap checks that the next span has no posted or held owner.

Transfer Events match controller, slot, DCI and physical TD. Successful full or
short completions copy actual bytes into the FIFO before the receive becomes
reusable. Rearm runs after event traversal and ERDP publication at the end of
`drain_events`. Command, control and bulk waits all reach that same progress
point. It performs no class commands, recovery, allocation or recursive waits.

## Wait, collection and failure

Start posts both receives. An ordinary NAK can leave them pending indefinitely.
`usb_host_interrupt_wait` waits for copied data or terminal failure; wait expiry
returns `USB_TIMEOUT` without canceling hardware work or consuming data.
The worker retains its existing health polling interval, rather than adding a
CPU polling loop for every USB service interval.

`usb_host_interrupt_take` returns actual bytes and a monotonically increasing
sequence. A missing destination for nonzero bytes, an undersized destination or
missing completion output returns `USB_INVALID` and preserves the head. An empty
queue returns `USB_BUSY`. A successful zero-byte transfer accepts a null
destination and is delivered as zero bytes for the class to interpret. No caller
destination is retained beyond the call.

Queue overflow latches `USB_DISCONTINUITY` and stops rearm. STALL latches
`USB_STALL`, retains the stalled span and stops rearm without automatic recovery.
Remaining posted receives still retain identifiable owners and can be accounted
for if they complete. Terminal failure takes precedence over already queued
bytes; the retained valid prefix cannot masquerade as a healthy stream.

Unowned/corrupt or other failed completions quarantine the controller. Removal
with posted or held receives also uses the accepted controller-wide quarantine,
which stops unrelated storage on that controller. Shutdown marks posted receive
backing held and retains all DMA; successful halt, masked interrupts or disabled
bus mastering do not permit recycling it. The immutable boot inventory remains
an observation snapshot, not runtime transfer authority.

HCI packet framing, firmware transport, HID reports and pointer delivery belong
to their consumers. Canceling a posted receive, endpoint recovery, broader speed
or hub profiles and lossless delivery remain outside this initial interface.
The [accepted limit and revisit point](../technical-debt.md#usb-interrupt-in-initial-profile-and-failure-retention)
remain explicit.
