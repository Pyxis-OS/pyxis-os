# Private USB interrupt IN

With `CONFIG_XHCI=y`, checked kernel class consumers use retained interrupt-IN
streams on their existing BSP xHCI controller worker. Each opaque
`usb_host_interrupt` handle names one endpoint, separately from its device.
There is no public raw-USB ABI or general report-descriptor HID interpreter.

The supported class profiles are:

| Consumer | Speed and topology | Receive capacity |
| --- | --- | --- |
| [AX200 HCI](bluetooth-hci.md) | Boot-present root-connected full speed | 257 bytes |
| Boot keyboard and mouse | Boot low/full/high speed on roots or behind at most five supported external USB 2 hubs | 1024 bytes |
| HID leaf hotplug | Runtime low/full speed on roots or ports of boot-present supported USB 2 hub chains | 1024 bytes |
| [Hub notifications](usb-hubs.md) | Boot-present supported full/high-speed USB 2 hubs with representable child paths | 257 bytes |

One HID device can bind one boot keyboard and one boot mouse, including a
composite device with distinct interrupt-IN endpoints. Runtime high-speed HID,
new hub topology, USB 3 periodic endpoints and runtime HCI attachment are outside
these profiles. The earlier [interrupt-IN qualification record](../development/experiments/usb-interrupt-in/README.md)
covers its original HCI transport scope; it does not establish HID or broader
native periodic-transfer qualification.

## Admission and configuration

`usb_host_configure_interrupt_in` takes checked endpoint facts, a class kind and
a receive length, and returns an endpoint handle. HCI and hub receive lengths
are bounded by `usb_host_interrupt_capacity()`; HID uses the separate
`usb_host_hid_interrupt_capacity()`. Packet size, receive length and class
report length are separate values. Class consumers reserve at least the endpoint
packet size so a valid packet cannot overrun a shorter boot-report request.

Low/full-speed `bInterval` is translated from frames to the xHCI microframe
exponent, rounding down to a power of two. High-speed interrupt intervals use
`bInterval - 1`; additional transactions per microframe set Max Burst, and Max
ESIT Payload reserves the complete periodic payload. Interrupt and bulk
configuration preserve the highest existing Slot Context Entries value and
reject DCI collisions within a device. Different devices can use the same DCI.
Known slot/resource or periodic-bandwidth admission refusal leaves the
controller running and existing storage/HCI transfers intact.

The host configures contexts, not USB class requests. HCI and HID configure host
endpoints before class SET_CONFIGURATION, then start receives after their class
setup. Hub activation selects its USB configuration to read hub metadata before
configuring and starting its notification endpoint. There is no replacement of
an admitted stream or runtime hub configuration.

## Prepared resources and ownership

Before AP startup, each controller prepares independent pools: one HCI stream
per advertised root port, 64 HID endpoint streams and 32 hub notification streams.
Each stream has one ring page, two page-backed DMA receives and eight copied
completion entries. Shared completion storage accommodates 1024-byte HID packets;
HCI and hub admission retain their smaller limits. Counts and capacities live in
`kernel/usb/settings.h`, independently from the storage and HCI bulk pools.

A separate 32-record HID attachment budget covers boot claims and runtime
attempts, including failed and retired generations. Runtime attempts consume
fresh prepared input/output contexts, EP0 rings and control buffers. Boot claims
consume the same budget while keeping their original device records. Hardware
slot availability and periodic bandwidth impose separate admission limits.
Exhaustion refuses further attachment for that boot; it does not reclaim an old
record. Controller-owned DMA arenas supply borrowed slices, and all admitted and
unused backing stays retained until reboot. No runtime allocation or mapping is
performed.

All stream mutation remains on the owning BSP worker. IRQ notification does not
collect reports. Each posted receive keeps its buffer and physical TD identity.
Payload and cycle publication precede the DCI doorbell; producer wrap checks that
the next span has no posted or held owner. Transfer Events match controller,
active slot, DCI and physical TD. Successful full or short completions copy
actual bytes into the FIFO before that receive can be reused.

Rearm follows event traversal and ERDP publication at the end of `drain_events`.
Command, control and bulk waits reach the same bounded HCI/HID progress pass.
HID progress collects copied reports and hub change bitmaps without commands,
waits or allocation; the outer worker performs attachment and retirement.

## Collection, loss and retirement

Start posts both receives. NAK can leave them pending indefinitely. Wait expiry
returns `USB_TIMEOUT` without canceling hardware work or consuming data. Take
returns actual bytes and a monotonically increasing sequence; rejected
collection preserves the head, an empty FIFO returns `USB_BUSY`, and no caller
destination survives a call. A successful zero-byte transfer is delivered to
its class consumer for interpretation.

FIFO overflow latches `USB_DISCONTINUITY` and stops rearm. Terminal failure takes
precedence over queued bytes. HCI/hub STALL retains the stalled span without
automatic stream recovery. An owned HID STALL, transaction error or babble
retires that device's input source; unrelated or corrupt completions quarantine
the controller. Class source loss releases only that source's held input, then
acknowledges the terminal stream. Pending reports and unresolved source loss
remain visible to input freshness checks.

Ordinary HID leaf removal suppresses rearm and fences each configured interrupt
endpoint and EP0 before Disable Slot. Stop Endpoint completion must agree with
the hardware endpoint state and any stopped event's retained ring/dequeue
identity, including a stop at the ring frontier. A Context State Error is
accepted only when the output context proves the endpoint raced into Halted or
Error. Numeric slot ownership ends after Disable Slot completion and event
retirement; old DMA and TD owners remain retained, and reconnection uses fresh
records.

Unprovable retirement, malformed/unowned events and unsupported hub-subtree
removal fail closed by quarantining the controller, which can stop unrelated
storage and Bluetooth. Active non-HID removal retains the existing quarantine
policy. Halt, interrupt masking or disabled bus mastering alone do not permit
recycling DMA. The immutable boot inventory remains an observation snapshot,
not runtime transfer authority. Broader hotplug, recovery and reclamation limits
remain in [technical debt](../technical-debt.md#usb-interrupt-in-initial-profile-and-failure-retention).
