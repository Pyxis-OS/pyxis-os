# USB 2 hub bring-up

Manual validation on 2026-10-03 used base main `132aef1`, host implementation
`8d53cb2`, and integrated core/ABI implementation `f46b9a6`. Userland is the
published dependency `93afd00b8657a6ff166867c3072039c782c6dbe2`
([PR #103](https://git.internal/PyxisOS/pyxis-userland/pulls/103)); other pins
remain unchanged. Ordinary kernel, SDK and full source image builds used the
existing GCC 16.2 toolchain. No compiler-container rebuild is needed.

Boots used QEMU 10.2.2 with the upstream AHCI fix, q35, nested KVM, CPU `max`,
four vCPUs, 8 GiB RAM, matching raw OVMF CODE/VARS, ISO boot and modern VirtIO
network/entropy devices. These are experiment inputs, not driver requirements.
Enabled cases locally selected `CONFIG_XHCI=y`; the checked-in default remains
`n`. Remote commands used the existing native client, and GDB inspected retained
controller/device records after startup. No tests, fault injection, CI changes
or boot/output automation were added.

## Matched inventory and resource comparison

The baseline used kernel main `132aef1` and independently verified matching
SDK/userland/ports bundles from the merged root-inventory work. The same USB
launch parameters were used before and after implementation:

```text
-device qemu-xhci,id=usb,p2=2,p3=2
-device usb-hub,bus=usb.0,port=1,ports=3
-device usb-hub,bus=usb.0,port=1.2,ports=5
-device usb-mouse,bus=usb.0,port=1.1
-device usb-kbd,bus=usb.0,port=1.2.4
-device usb-mouse,bus=usb.0,port=2
```

| Observation | Root-only baseline | Hub implementation |
| --- | ---: | ---: |
| Retained devices/interfaces | 2 / 2 | 5 / 5 |
| Controller commands | 4 | 12 |
| Events consumed | 14 | 105 |
| USB inventory | Incomplete | Complete |
| Device DMA backing | 64 KiB | 64 KiB roots + 512 KiB descendant arena |

The added work configures two hubs and addresses three descendants. GDB checked
routes `1`, `2` and `0x42`, zero TT identity for these full-speed hubs, retained
parent links and one owned arena. Physical paths include `3.1`, `3.2` and
`3.2.4`; QEMU virtual USB attachment numbers are not xHCI root-port numbers.
Named, numeric, missing-database and piped listings returned status 0. These
are protocol/resource observations, not throughput, elapsed-time or idle power
measurements. IRQ notification counts are not isolated device interrupt counts.
The initial integration boot consumed 106 events and the final change-acknowledgment
boot consumed 105; both used 12 commands and retained the same five devices.
These samples do not establish a timing or power improvement.

## Controller and hub variation

A mixed boot used four xHCI functions and one EHCI function, with one working
xHCI behind a PCIe root port. Advertised root capacities were two, four and six
ports; supported hub capacities were three and seven ports, including individual
port power switching. The two healthy controllers retained nested hubs, HID and
vendor-class serial observations. Paths included `6.7` and `3.1.7`, with global
parent indices correctly adjusted across controllers. GDB showed five
controllers, nine devices/interfaces, two complete controllers, one partial
hub controller, one failed controller without MSI-X, and unsupported EHCI.
Named/numeric/missing-database listings returned status 1; `lspci -n` returned 0
and the shell remained usable.

The initial exact-length check rejected QEMU's built-in eight-port hub's ten-byte
descriptor, retaining only its identity and interface. That check incorrectly
required the compatibility power mask to include DeviceRemovable's reserved bit
zero. USB 2 Table 11-13 requires one power-mask bit per port. The review follow-up
below corrects this generic descriptor bound; no emulator-specific exception is
needed. QEMU rejects more than eight ports.

A larger ordinary tree attached four seven-port hubs and 29 mice below one
seven-port root hub, presenting 33 descendants. The initial 32-entry descendant
budget retained 33 devices/interfaces including the root, marked the exhausted
hub/controller partial and kept the controller running. The shell and numeric
listing remained usable with status 1. This also exercised natural command/event
ring wrap. No artificial register or transfer failures were injected.

## Review follow-up

The follow-up accepts a declared hub descriptor length large enough for the
seven-byte prefix, `ceil((ports + 1) / 8)` removable bitmap and `ceil(ports / 8)`
compatibility power mask. It reads the declared length within the existing
descriptor budget and leaves trailing data opaque. Hub connection capture now
waits an additional 100 ms after the power-good delay for USB 2 TSIGATT; debounce
and the existing shared startup deadline remain unchanged. This adds 100 ms of
requested settling per traversed hub, not a measured boot-time result. Slow
physical attachment remains unqualified because QEMU attaches immediately.

An ordinary enabled kernel/SDK/full source image build passed with the published
userland README clarification at `a6e1a409`. The same QEMU/CPU/RAM/firmware profile
used four/three/five-port nested hubs, a default eight-port hub with a keyboard on
its eighth port, and a direct mouse. The device arguments were:

```text
-device qemu-xhci,id=usb
-device usb-hub,bus=usb.0,port=1,ports=4
-device usb-hub,bus=usb.0,port=1.2,ports=3
-device usb-hub,bus=usb.0,port=1.2.3,ports=5
-device usb-kbd,bus=usb.0,port=1.1
-device usb-tablet,bus=usb.0,port=1.2.1
-device usb-mouse,bus=usb.0,port=1.2.3.5
-device usb-hub,bus=usb.0,port=2
-device usb-kbd,bus=usb.0,port=2.8
-device usb-mouse,bus=usb.0,port=3
```

GDB showed a complete nine-device/nine-interface snapshot, six admitted
descendants, 22 controller commands and 215 consumed events. The serial boot log
reported all nine paths, including `6.8` and `5.2.3.5`; hub lines followed their
traversal. Named, numeric, missing-database and piped listings all exited 0, as
did `lspci -n`. Native remote command completion and final drain were successful.

The 33-descendant tree was repeated to check failure diagnostics after the new
logging. It retained the root plus 32 descendants, reported partial inventory
with a running controller, and logged the exhausted hub's full path and
`hub reserved device budget exhausted` reason at `3.4`. Numeric `lsusb` exited 1,
`lspci -n` exited 0 and the remote session drained successfully. GDB showed 71
commands and 745 events, compared with the earlier sample's 746 events; this
variation is not a performance result. This checks bounded diagnostics without
changing resource admission or failure policy.

The default `CONFIG_XHCI=n` was restored, and the ordinary kernel/SDK/full image
build passed again. Follow-up kernel and userland builds emitted no warnings;
the enabled full source rebuild included existing vendored-port warnings. All
QEMU, debugger and remote client jobs used here were closed.

## Qualification limits

The default-disabled full image also built and booted with an attached hub.
GDB showed UNAVAILABLE and no xHCI controller contexts; `lsusb -n` returned 1
while `lspci -n` returned 0. The shell remained usable.

QEMU's built-in hub exposes full-speed links. A subsequent
[owner-reported native ThinkPad run](../targets/t14-gen1-amd/usb-bringup.md)
observed full-speed enumeration behind nested high-speed hubs, 64-byte contexts
and nonzero scratchpads. Low-speed descendants, recovery and delayed physical
attachment remain unqualified. The ThinkPad's SuperSpeed dock and 10 Gb/s links remain
unsupported, alongside the existing firmware/controller profile limits.
Hotplug, idle downstream removal monitoring, endpoint-local recovery and USB
storage remain deferred. See [implemented hub behavior](../devices/usb-hubs.md)
and [retained costs](../technical-debt.md#usb-descriptor-bounds-and-per-port-preparation).
