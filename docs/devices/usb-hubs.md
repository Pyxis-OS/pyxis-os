# USB 2 boot hub inventory

Caelum traverses boot-present USB 2 hubs on every prepared xHCI controller for
the immutable [USB inventory](usb-enumeration.md), including nested hubs and
low/full/high-speed children. SuperSpeed hubs, newer link speeds, hotplug,
power management and storage binding remain outside this slice. The first
[owner-reported ThinkPad run](../targets/t14-gen1-amd/usb-bringup.md) exercised
full-speed descendants behind high-speed hubs; broader native qualification
remains pending.

The core activates the first fully checked ordinary hub configuration in
descriptor order, with one interface and a hub interrupt IN endpoint. Multi-TT
hubs use their required single-TT default alternate; no SET_INTERFACE is sent.
Other classes remain unbound. Hub activation gives system_info READ clients no
transfer or reset authority.

Hub class requests use EP0. The core checks the variable-length USB 2 hub
descriptor, requiring room for both bitmaps while accepting extra compatibility
padding as opaque data. It updates xHCI hub Slot metadata, powers ports and waits
for the advertised power-good delay, with the existing minimum settling delay,
then the USB 2 maximum 100 ms signal-attachment interval. It checks
overcurrent and every port's logical power state, then captures connected
candidates before resetting any child. Observed candidates survive later failure
as unidentified/incomplete records. Each candidate has a stable connection
interval, reset completion and recovery wait before addressing. Initial change
bits are acknowledged explicitly; later connection changes make traversal partial.
Final status checks include empty ports and power. No hub interrupt endpoint is
configured: boot polling supplies this snapshot, without runtime monitoring of
hub descendants.

The boot log reports each retained device's full physical path, numeric IDs and
inspection detail. Hub lines are emitted after traversal so partial branch
reasons are visible without debugger access.

Roots are inspected first, followed by an iterative breadth-first hub walk.
USB requests remain in core; xHCI owns routing, Slot commands, speed identities,
contexts and DMA. Children inherit the physical root port and discovered path.
Low/full-speed children behind high-speed hubs name the nearest transaction
translator, including through full-speed hubs. Routing follows xHCI's five route
fields and port encoding; an unrepresentable path reports unsupported.

Before AP startup, each controller reserves a descendant device pool capped by
its remaining advertised Slot capacity and `USB_DESCENDANT_BUDGET`, initially
32 in `kernel/usb/settings.h`. Potential root reservations take precedence.
One owned DMA arena supplies page-strided contexts, EP0 rings and control buffers.
Children borrow slices; they never own or free the arena. No runtime allocation
or mapping occurs. Backing and admitted Slot identities remain until reboot.
Root removal retires descendant Slots before the root; active requests quarantine
the controller under the existing unresolved-DMA rule.

Budgets and deadlines are implementation choices, not machine topology. Existing
descriptor/interface budgets and the controller startup deadline also bound
traversal. Exhaustion, unsupported hub shapes/speeds, missing power, changing
connections or unreadable branches retain observations and make inventory partial.

The public record adds an earlier parent device index and one-based downstream
port. Direct roots use `SYSTEM_INFO_USB_NO_PARENT` and port zero. Parents are on
the same controller/root port and precede descendants. [lsusb](../userland/lsusb.md)
prints the full physical path, such as `Port 3.2.4`, independently of compressed
xHCI route encoding and Linux bus/address numbering. Naming and exit-status
rules are unchanged.

The protocol follows [USB 2.0](https://www.usb.org/document-library/usb-20-specification)
chapters 9 and 11 and [xHCI 1.2b](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf)
sections 4.5.2, 6.2.1 and 6.2.2. Hub Slot fields require Configure Endpoint,
not Evaluate Context. See the [bring-up record](../development/usb-hub-bringup.md)
for measured coverage and limits.
