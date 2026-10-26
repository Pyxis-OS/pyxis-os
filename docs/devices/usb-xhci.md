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
64-bit DMA, 4 KiB pages, and MSI-X or 32-/64-bit MSI left disabled by firmware;
MSI supports optional per-vector masking and enables only message zero. There
is no INTx fallback. It records the advertised
32- or 64-byte context stride for device input/output contexts.

A staged [PCI claim](pci.md#driver-owned-resources) leaves firmware command state
intact for provisional access to BAR0's first 4 KiB. The extended capability list
can start well beyond that prefix: Intel's Panther Lake controllers put it 32 KiB
into the BAR. The walk maps its own bootstrap window from the list's first page and
grows it only as far as the list reaches, at most 256 KiB into BAR0; after BAR
sizing the controller is refused if anything read lay beyond the BAR. Each
capability body must fit the window and avoid defined register regions.

- **Unknown IDs** (vendor, debug, power management) are skipped.
- **A malformed Supported Protocol capability** (bad name, port range or speed
  entries, or ports already described) is logged and skipped. Its ports stay
  unusable and the rest of the controller works.
- **Refused:** a list that leaves the window or overlaps the register regions, a
  next pointer inside the current body, or a second legacy capability.

With trace logging each capability's ID and offset is logged. Supported
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
If BIOS release times out, the controller is taken anyway, as Linux does: the
BIOS-owned semaphore is cleared, the time-out is logged and SMI sources are
disabled as usual.

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
(or the single MSI message) on shared static vector 39 and Run/Stop. IRQ work
acknowledges and notifies every active controller, then detaches/wakes its wait.
MSI or MSI-X delivery may already clear IP, so
it cannot identify the originating controller from that bit. Workers inspect only
their own cycle-owned events. The worker parses and
validates cycle-owned events, matches command/transfer completions to admitted
physical TRB identities, advances dequeue and releases EHB after consumption.
Empty polls do not
rewrite an unchanged dequeue pointer. Payload stores precede cycle publication
and the doorbell. Each command has an absolute five-second limit bounded by
the startup enumeration deadline.
After the boot scan, a running controller logs `boot USB enumeration finished` as
a debug record.
Inventory completeness is reported separately by the immutable system_info
snapshot and `lsusb`, including partial observations from unsupported devices.
Changing IMAN interrupt enablement writes zero to the W1C pending bit to preserve
notification; acknowledgement remains explicit in the IRQ path.

`usb_host_notify()` accepts any CPU at IF=0, including revocation after an AP
CUT or final controller-grant release. A short per-controller guard protects
only the notification latch and waiter. IRQ notification shares that guard;
wait publication/expiry cleanup use it, and wake keeps it through scheduler
notification so the waiter cannot be reused concurrently. No guard spans sleep
or logging. Event, command, DMA and class progress stay BSP-owned. See the
[cross-CPU notification check](../development/experiments/usb-controller-notify/README.md).

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

USB 2 root reset completion is followed by at least 10 ms of recovery before
addressing. Address Device completion is followed by at least 2 ms before the
next request, as required by USB 2.0 sections 9.2.6.2–9.2.6.3. Boot and runtime
admission enforce these waits explicitly; incidental worker scheduling is not
a recovery guarantee. The waits retain the existing deadline and drain progress.
External hubs share the same reset recovery minimum.

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
remain unbound. Private class consumers can configure the
[class-specific interrupt-IN profiles](usb-interrupt-in.md), with independent
pre-AP receive resources and post-event rearm during every controller wait.

A connection change during startup still retires that uncommitted candidate.
For a reserved root port, a connect-change notification alone preserves the
existing device/subtree when PORTSC still says connected and enabled without
over-current. The worker acknowledges the captured changes without advancing
that connection's generation; presence and inventory checks use the same health
rule. Disconnect, disable or over-current still retires the subtree. Debug
records include the observed PORTSC on ignored connect changes and retirement.
Ordinary HID leaves have
[device-local endpoint and slot fences](usb-interrupt-in.md#collection-loss-and-retirement),
with fresh prepared records for bounded low/full-speed reattachment.
Disappearance retires the affected device and its subtree, including active
non-HID endpoints, without stopping unrelated ports. Admission/rearm closes
before child-first endpoint and Disable Slot fences; old DMA stays retained.
Reconnection never inherits an old device's DMA or software state. Writes
acknowledge only observed PORTSC changes and preserve neutral power/wake fields;
they never echo PED or reset bits. The
[disappearance qualification record](../development/experiments/xhci-device-removal/README.md)
separates QEMU evidence from the pending native dock check.

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
