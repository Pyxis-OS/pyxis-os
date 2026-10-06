# xHCI controllers

Native xHCI initialization defaults to enabled by `CONFIG_XHCI=y` in
[`.config`](../../.config) and Kconfig. XHCI under Caelum in
[`make menuconfig`](../development/configuration.md) controls controller preparation
and worker startup. Set `CONFIG_XHCI=n` and rebuild to disable them: the boot log
reports `xHCI: disabled at build time`, and the kernel makes no xHCI claim or DMA
allocation.
Every discovered xHCI function is inspected independently when enabled.
Owner-reported ThinkPad inventory and reads provide limited native evidence;
broader controller and recovery qualification remain pending.
Firmware can still load the kernel and boot archive from USB.

When enabled, Caelum prepares each discovered PCI xHCI function and its own
slots/contexts for boot-present root-port devices and bounded hub descendants.
[USB enumeration](usb-enumeration.md)
addresses them, checks descriptors and publishes a read-only boot inventory.
[BOT/SCSI storage](usb-storage.md) provides native block access and qualified
write/flush support. The archive-backed shell and existing VirtIO block behavior
remain available. [Native observations](../targets/t14-gen1-amd/usb-bringup.md)
cover the owner's ThinkPad profiles; they do not establish broad hardware qualification.

## Preparation and activation

`xhci_prepare()` runs on the BSP with interrupts disabled before AP startup.
Class/subclass matching retains all discovered USB host controllers. The driver
uses programming interface `0x30` for xHCI, and the inventory lists other interfaces
as unsupported. Incomplete PCI discovery makes the USB snapshot incomplete but
does not prevent inspecting retained controllers. The initial profile requires
PCI xHCI 1.x,
a firmware-assigned, page-aligned memory BAR0 with decoding already enabled,
64-bit DMA, 4 KiB pages and disabled firmware MSI/MSI-X. It records the advertised
32- or 64-byte context stride for device input/output contexts.

A staged [PCI claim](pci.md#driver-owned-resources) leaves firmware command state
intact for provisional access to BAR0's first 4 KiB. Every interpreted extended
capability body must fit that prefix and avoid defined register regions. Supported
Protocol capabilities associate root ports with protocol versions, Slot Type and
speed identities. With PSIC zero, exact BCD versions select the specification's
speed-ID defaults: USB 2.0 IDs 1–3, USB 3.0 ID 4, USB 3.1 IDs 4–5 and USB 3.2
IDs 4–7. ID 4 is SuperSpeed; IDs 5–7 are SuperSpeedPlus. Other revisions have
no implied map. Nonzero PSI entries replace all defaults. Symmetric USB 2
rates map to low/full/high speed. USB 3 entries require a positive rate and
full duplex, with Link Protocol selecting SuperSpeed or SuperSpeedPlus;
receive/transmit pairs are supported when both entries identify that same
category. Directional rates and lane counts may differ. Reserved or unsupported
profiles remain unknown. Raw speed IDs remain in Slot contexts; no rate or
lane count is inferred for public inventory. Private profiles retain directional
rates and default generation/lane counts. Explicit root lane counts come from
PORTLI; downstream counts come from hub extended status. Child setup requires
one matching enhanced speed identity and supplies higher-rank parent fields.
No port numbering or vendor identity selects a device. These mappings follow [xHCI 1.2b §7.2.2](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf).

If legacy ownership is present, request OS ownership, wait for BIOS release and
then disable legacy SMI sources. Wait for controller readiness, halt before
clearing bus mastering, reset, disable decoding and probe BAR sizes. Validate the
bootstrap extent and sized register/MSI-X regions before mapping them. Initial
handoff, halt and reset waits each have a one-second deadline. Unsafe failure
retains the claim; safe boot failure unwinds unpublished resources.
If BIOS release times out, the OS-owned request remains asserted; firmware may
release ownership later, but this boot does not retry controller preparation.

Preparation allocates port records, DCBAA, scratchpads when advertised, one
command-ring page, one event-ring page and an ERST allocation. All backing and
mappings exist before AP startup. Each advertised port also receives input/output
contexts and an EP0 ring/control buffer. CPU and
device physical addresses remain distinct. Rings are 64-byte aligned and remain within a 64 KiB segment
boundary. The command ring ends with a Toggle Cycle Link TRB; the event ring uses
one ERST entry. The initial choice is 256 TRBs per ring, derived from the supported
4 KiB page and hardware TRB layout, with one command admitted at a time.

After scheduler setup, `xhci_start()` creates one BSP worker per prepared controller.
Each context owns its claims, mappings, ports, rings, commands, waits and discovery
buffers. Device helpers derive their controller from the device; caller validation
checks both the worker entry and its borrowed controller argument. Each worker enables bus
mastering before programming rings: a halted controller may fetch the ERST as
soon as its base is written. It then enables interrupter zero, MSI-X entry zero
on shared static vector 39 and Run/Stop. IRQ work acknowledges and notifies every
active controller, then detaches/wakes its wait. MSI-X may already clear IP, so
it cannot identify the originating controller from that bit. Workers inspect only
their own cycle-owned events. The worker parses and
validates cycle-owned events, matches command/transfer completions to admitted
physical TRB identities, advances dequeue and releases EHB after consumption.
Empty polls do not
rewrite an unchanged dequeue pointer. Payload stores precede cycle publication
and the doorbell. Each command has an absolute five-second limit bounded by
the startup enumeration deadline.
After the boot scan, a running controller logs `boot USB enumeration finished` to
the trace log (`LOG_LEVEL=trace`).
Inventory completeness is reported separately by the immutable system_info
snapshot and `lsusb`, including partial observations from unsupported devices.
Changing IMAN interrupt enablement writes zero to the W1C pending bit to preserve
notification; acknowledgement remains explicit in the IRQ path.

## Root-port and failure ownership

The worker powers controllable ports, allows 20 ms after a power change and takes
one startup connection snapshot. Recognized USB 2 ports are reset when required;
USB 3 ports use their enabled link state. Setup shares a thirty-second deadline.
Connected unknown protocol versions remain explicitly unsupported. This snapshot
has only been qualified in QEMU. There is no explicit USB 2 connect debounce
before reset or startup link-settling wait when no port-power write occurred,
including on controllers without port power control. A physical USB 3 link can
still be initializing after controller reset and miss reservation for this boot.
See [qualification limits](../technical-debt.md#xhci-hardware-profile-and-runtime-retention).

For each supported enabled port, Enable Slot uses its advertised Slot Type and
retains the checked returned slot ID with that port. Address Device then publishes
its output context in DCBAA and configures EP0. Output contexts belong to hardware;
input contexts are constructed independently and remain immutable until command
completion. The input Slot advertises only EP0; Configuration Information Enable
is left disabled and Input Control configuration/interface/alternate fields remain
zero. Supported hubs receive Slot-only Configure Endpoint metadata for
[boot traversal](usb-hubs.md). Supported storage configures its checked bulk
endpoints from a separate pre-AP device pool for an internal read-only probe.
Every inspectable device receives an address/descriptor record; other classes
remain unbound.

A connection change after the snapshot retires that startup candidate. Loss of
an enabled reserved port runs Disable Slot after prior command completion, then
keeps that port retired until reboot. It does not stop unrelated ports when that
device has no active request; removal during active transfer work fails the
controller and retains unresolved DMA. Later
insertion cannot inherit a reservation. Writes acknowledge only observed PORTSC
changes and preserve neutral power/wake fields; they never echo PED or reset bits.

Command timeout, invalid completion/event identity, controller error, failed
port setup or failed slot retirement stops the controller for this boot. The
worker attempts bounded halt, masks delivery and disables bus mastering. Other
controllers continue independently. Failed/unsupported controllers remain explicit
inventory records, making the aggregate snapshot incomplete. All
runtime claims, mappings, command state and DMA allocations remain until reboot,
including when halt succeeds. Masking interrupts or disabling bus mastering alone
does not establish returned DMA ownership. The shared VM contract supplies no
runtime unmapping or allocation path here.

The worker uses a ten-millisecond notification/health polling interval today.
Even when idle, it schedules up to 100 polling opportunities per second.
Actual wakeups and laptop power cost are unmeasured.
Deadlines and polling/ring budgets are implementation choices in
`kernel/usb/settings.h`, separate from image configuration. There are no unit-test
constants for RAM, disk/ESP sizes, port numbering or controller capacities.

The [bring-up record](../development/xhci-bringup.md) describes manual validation
and its limits. Register and Enabled-slot behavior follow the
[Intel xHCI 1.2b specification](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf),
particularly §§4.2, 4.5.3, 4.22, 5 and 6.
