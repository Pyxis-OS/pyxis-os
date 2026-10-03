# Initial xHCI controller

Native xHCI initialization is temporarily disabled by `XHCI_ENABLED` in
[`kernel/init.c`](../../kernel/init.c). Both controller preparation and worker
startup are skipped, with `xHCI: disabled at build time` in the boot log. The
kernel makes no xHCI claim or DMA allocation. Set the constant to `1` and rebuild
to resume explicit QEMU bring-up. The ThinkPad's three-controller inventory is
outside the current unique-controller profile; native hardware remains unqualified.
Firmware can still load the kernel and boot archive from USB.

When enabled, Caelum prepares a unique PCI xHCI function and hardware
slots/contexts for boot-present, directly attached root-port devices.
[USB enumeration](usb-enumeration.md)
addresses them, checks descriptors and configures a provisional BOT transport.
USB block access remains pending. The archive-backed shell and existing VirtIO block behavior remain available.
[Phase B](../wip/usb-installation.md#b-native-read-only-usb-storage) tracks those
remaining layers. QEMU is the temporary target; physical hardware is unqualified.

## Preparation and activation

`xhci_prepare()` runs on the BSP with interrupts disabled before AP startup.
Class/subclass/programming-interface matching preserves absent, ambiguous and
incomplete PCI inventory outcomes. The initial profile requires PCI xHCI 1.x,
a firmware-assigned, page-aligned memory BAR0 with decoding already enabled,
64-bit DMA, 4 KiB pages and disabled firmware MSI/MSI-X. It records the advertised
32- or 64-byte context stride for device input/output contexts.

A staged [PCI claim](pci.md#driver-owned-resources) leaves firmware command state
intact for provisional access to BAR0's first 4 KiB. Every interpreted extended
capability body must fit that prefix and avoid defined register regions. Supported
Protocol capabilities associate root ports with protocol versions, Slot Type and
speed identities. PSIC zero permits the protocol-defined defaults. Nonzero PSI
entries replace those defaults: supported symmetric USB 2 rates map to low/full/high
speed and 5 Gb/s SuperSpeed maps to SuperSpeed. Raw speed IDs remain in Slot
contexts; unknown rates, newer link protocols and valid asymmetric pairs remain
unclassifiable. No QEMU port numbering or vendor identity selects a device.

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
contexts, an EP0 ring/control buffer and two initial non-control rings. CPU and
device physical addresses remain distinct. Rings are 64-byte aligned and remain within a 64 KiB segment
boundary. The command ring ends with a Toggle Cycle Link TRB; the event ring uses
one ERST entry. The initial choice is 256 TRBs per ring, derived from the supported
4 KiB page and hardware TRB layout, with one command admitted at a time.

After scheduler setup, `xhci_start()` creates one BSP worker. It enables bus
mastering before programming rings: a halted controller may fetch the ERST as
soon as its base is written. It then enables interrupter zero, MSI-X entry zero
on static vector 39 and Run/Stop. IRQ work only acknowledges observed activity,
records notification and detaches/wakes the worker wait. The worker parses and
validates cycle-owned events, matches command/transfer completions to admitted
physical TRB identities, advances dequeue and releases EHB after consumption.
Empty polls do not
rewrite an unchanged dequeue pointer. Payload stores precede cycle publication
and the doorbell. Each command has an absolute five-second limit bounded by
the startup enumeration deadline.
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
completion. Configure Endpoint adds the selected bulk DCIs with A1 clear; endpoint
count does not replace the highest enabled DCI. Configuration Information Enable
is left disabled, so Input Control configuration/interface/alternate fields remain
zero. The USB core sends those actual values through standard requests.
An unrelated mouse receives its own address/descriptor record and remains unbound.

A connection change after the snapshot retires that startup candidate. Loss of
an enabled reserved port runs Disable Slot after prior command completion, then
keeps that port retired until reboot. It does not stop unrelated ports when that
device has no active request; removal during active control work fails the
controller and retains unresolved DMA. Later
insertion cannot inherit a reservation. Writes acknowledge only observed PORTSC
changes and preserve neutral power/wake fields; they never echo PED or reset bits.

Command timeout, invalid completion/event identity, controller error, failed
port setup or failed slot retirement stops the controller for this boot. The
worker attempts bounded halt, masks delivery and disables bus mastering. All
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
