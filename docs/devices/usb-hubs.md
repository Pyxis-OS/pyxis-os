# USB boot hub inventory

Caelum traverses boot-present USB 2 and USB 3 hubs on every prepared xHCI controller
for the immutable [USB inventory](usb-enumeration.md), including nested hubs.
USB 2 supports low/full/high-speed children; USB 3 supports standard symmetric
Gen1/Gen2 links with one or two lanes. Hotplug, power management and storage
binding remain outside this slice. The first
[owner-reported ThinkPad run](../targets/t14-gen1-amd/usb-bringup.md) exercised
full-speed descendants behind high-speed hubs. A later owner-reported run also
traversed the dock's SuperSpeedPlus hub and completed reads from a SuperSpeed
storage descendant. Recovery and broader native qualification remain pending.

The core activates the first fully checked ordinary hub configuration in
descriptor order, with one interface and a hub interrupt IN endpoint. USB 3 hubs
use device protocol 3 and default interface protocol 0, with an endpoint companion.
Multi-TT
hubs use their required single-TT default alternate; no SET_INTERFACE is sent.
Other classes remain outside hub traversal; supported storage has a separate
[read-only probe](usb-storage.md). Hub activation gives system_info READ clients no
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

USB 3 uses its fixed 12-byte type `0x2a` hub descriptor, one to fifteen ports,
and SET_HUB_DEPTH before forwarding child traffic. Direct hubs have depth zero;
hub depths zero through four allow terminal children at depth five. USB 3
power/status/reset handling uses power bit 9 and explicit selectors for warm-reset,
link-state and configuration-error changes. Failed links remain partial even
when no connection bit was set. Reset completion requires an enabled U0 link.
A normal reset can cause the hub to perform hot or warm reset; no separate host
recovery retry is added. Existing stability/recovery windows are conservative
USB 3 boot policy rather than USB 3 timing requirements.

Each USB 3 hub's bounded BOS is structurally checked. A present SSP capability
supplies paired directional attributes indexed by hub-local sublink IDs; zero
is valid. Its presence selects extended port status, even when the hub itself
connects at SuperSpeed. Negotiated lane rates/counts determine the child category
and directional aggregate rates. Legacy hubs without SSP capabilities use Gen1 x1.
BOS IDs are never copied into controller speed IDs. Defaults retain generation/lane
distinctions; explicit xHCI profiles must match protocol and directional rates
uniquely. Missing, ambiguous, asymmetric or unsupported profiles remain partial.

USB 3 rank order is Gen2 x2, Gen2 x1, Gen1 x2, Gen1 x1. Children behind a higher-rank
ancestor carry its Slot ID and the intervening downstream port in xHCI parent
fields, independently of USB 2 transaction translation. No machine identity or
fixed topology selects these paths.

SET_SEL and SET_ISOCH_DELAY remain omitted from this EP0-only inspection slice.
The USB 3 enumeration specification requires them; complete inventory does not
claim full enumeration conformance. Actual latency accounting belongs with power
management/non-control scheduling, rather than fabricated zero values. See
[technical debt](../technical-debt.md#usb-descriptor-bounds-and-per-port-preparation).

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

The public record retains an earlier parent device index and one-based downstream
port. Direct roots use `SYSTEM_INFO_USB_NO_PARENT` and port zero. Parents are on
the same controller/root port and precede descendants. [lsusb](../userland/lsusb.md)
prints the full physical path, such as `Port 3.2.4`, independently of compressed
xHCI route encoding and Linux bus/address numbering. Naming and exit-status
rules are unchanged.

The protocol follows [USB 2.0](https://www.usb.org/document-library/usb-20-specification)
chapters 9 and 11, [USB 3.2](https://www.usb.org/sites/default/files/usb_32_202206_0.zip)
chapters 9 and 10, and [xHCI 1.2b](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf)
sections 4.5.2, 6.2.1 and 6.2.2. Hub Slot fields require Configure Endpoint,
not Evaluate Context. See the [bring-up record](../development/usb-hub-bringup.md)
for measured coverage and limits.
