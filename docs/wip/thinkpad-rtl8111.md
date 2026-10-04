# ThinkPad RTL8111 driver

Status: **plan accepted, 2026-10-03.** The owner approved and merged PR #355 with
the review note “read and accepted the proposal”, then explicitly authorized
task 1. [Hardware identification](../devices/rtl8111-hardware.md) is complete;
controller preparation is implemented. [NIC passthrough](../development/thinkpad-nic-passthrough.md)
is complete. The owner accepted task 2's firmware/power/initial-state defaults
in PR #359, then authorized task 2 after merging it. Tasks 1–4 are complete;
task 5 qualification is complete, including the owner-reported native cold/PXE
boot with the dock attached on 2026-10-04.

## Goal and machine configuration

Use the ThinkPad's built-in RJ45 controller for Pyxis networking,
first through VFIO in QEMU/KVM, then on a native boot. Completion means the existing remote
terminal is reachable through this port, with ordinary loopback and VirtIO
operation preserved.

The owner confirmed these settings for the **built-in port profile**:

| Setting | Value |
| --- | --- |
| IPv4 address | `192.168.0.50` |
| Prefix | `/24` (`255.255.255.0`) |
| Gateway | `192.168.0.1` |

These are machine configuration. Apply them through the
existing userspace network configuration authority and keep the QEMU user-network
profile separate. Profile parsing and packaged defaults belong to the separately
versioned userspace repository. The parent image assembler accepts a private
`NETWORK_CONFIG` file without modifying that repository. Publish any dependency
PR before updating the parent pin, following [SDK and repository integration](../development/sdk-and-repositories.md).

## Controllers and selection

The built-in function is host `0000:05:00.0`, `10ec:8168`, PCI revision `0x15`.
The qualified guest assigned it `00:03.0`; driver selection must not depend on
that guest address. The dock-facing RJ45 uses another onboard `10ec:8168`
at host `02:00.0`, PCI revision `0x0e`. Its different revision and shared IOMMU
group are recorded in
[ThinkPad next steps](thinkpad-next-steps.md#2-ethernet-passthrough-to-qemu-then-a-driver).

**Owner decision: configuration chooses the interface.** The built-in port
profile binds its controller through a locally supplied MAC selector; the QEMU
user profile binds VirtIO. Discovering RTL8111 does not automatically switch away
from a configured VirtIO interface or its host-forwarded remote terminal.
Full and partial MAC bytes stay out of the repository and published captures.

Accepted controller model:

- Keep driver state per controller, with explicit PCI, register, DMA and interrupt
  ownership. A global singleton is not the driver model.
- Enumerate all candidates. Diagnose unsupported variants individually; another
  NIC's presence must not prevent the supported built-in controller from working.
- Keep hardware support separate from the configuration binding above. Guest
  PCI addresses may differ from native addresses.
- Initially qualify the built-in controller. Supporting the dock adds its own
  hardware profile and qualification; it does not justify an exactly-one-NIC
  restriction.

The stack keeps one external interface, `net0`, with per-controller state distinct
from its binding. Exposing multiple active interfaces also needs address, ARP and routing
ownership work; its scope remains a decision before implementation.

**Accepted task 3 expansion, 2026-10-04:** include explicit selector ABI and
userspace configuration now. A `net0` table requires exactly one of
`driver = "virtio"` or a locally supplied `mac`; matching must be unique.
The first binding lasts until reboot, without fallback. Reapplying settings
to the same controller is allowed; clearing removes IPv4 settings only.
Absent/ambiguous matching preserves the binding, and switching controllers
is rejected. The [network reference](../devices/networking.md#native-configuration-capability)
records binding and read-only lookup authority. Task 3 connected VirtIO; task 4 also connects supported RTL8111 controllers.
Unsupported RTL XIDs remain diagnosed but are excluded from selectable interfaces.

## Tasks

The five-task outline and planning constraints are accepted. The
[hardware profile](../devices/rtl8111-hardware.md#controller-preparation)
records preparation, I/O ownership and accepted choices. Task 4 validation is
recorded below.

- [x] **1. Identify the hardware.** Begin with the owner's Fedora r8169
  `dmesg`/`ethtool`/`lspci` output, with MAC bytes removed. Record the chip name,
  TxConfig-derived MAC/XID, firmware reported by Linux, BARs and MSI/MSI-X
  capabilities. Then confirm the same XID and capabilities from Caelum before
  reset, driver DMA or interrupt enablement. Finish with a documented built-in
  hardware profile and proposed bounded preparation contract; PCI revision alone
  does not identify the MAC implementation.
- [x] **2. Prepare the controller.** Implement ownership, quiescence, reset and
  PHY initialization with bounded waits and per-controller state. Account for
  native PXE firmware state as well as VFIO. Leave DMA and delivery disabled
  until worker activation. Finish with a known stopped/prepared state; missing
  or failed hardware leaves ordinary boot usable.
- [x] **3. Integrate networking.** Replace direct VirtIO dependencies in Ethernet,
  ARP, IPv4 configuration/status and worker dispatch with a small driver-selection
  layer with VirtIO as its only implementation. The owner expanded the planned
  pure refactor to include explicit selector ABI and userspace configuration.
  Configuration chooses the interface. Finish with existing QEMU ping, UDP and
  TCP workloads
  preserving VirtIO behavior; capture matched runs before and after the refactor.
- [x] **4. Implement Ethernet I/O.** Add owned RX/TX rings, coherent DMA ordering,
  validated completions and interrupts serviced through the existing BSP network
  worker. Connect RTL8111 to the selection layer from task 3. Finish with ARP and
  gateway ping through the built-in port profile and clear buffer ownership.
- [x] **5. Qualify operation.** Exercise existing UDP/TCP tools, remote terminal,
  link changes and sustained traffic, then native boot. Finish with the built-in
  port working in VFIO and natively, including with the dock attached.

### Task 5 qualification and handoff

Branch `net/rtl8111-qualification` starts at merged main `eaeb417` (#367).
Driver changes are `36c45f2`; qualification used its source files from the
working tree before the commit (the kernel reports base `eaeb417`). The saved
qualified ELF has SHA-256
`7d7ef29474273998338f47fdd5a9fea481c0a1796dfe26bbf43a6738bec5878b`.
Dependency pins are unchanged, including userspace
`08e3c4b4a1385a6da978d64eced2c4aece528237`. The owner accepted suspending pending
TX deadlines while carrier is down, with a fresh five-second budget on return.
The worker preserves OWN and the binding throughout; carrier-up stalls still
stop until reboot. Internal tally capture adds one retained DMA page per
controller, without periodic polling or a userspace ABI.

The owner confirmed that the dock was connected throughout these checks.
Matched runs on the bare ThinkPad host used QEMU 10.2.2/KVM, 4 CPUs, 2 GiB,
`VFIO_PCI=0000:05:00.0`, `VIRTIO_NET=0`, Fedora's OVMF pair, the private built-in
profile and the stock init. Configuration kept HPET maintenance at 120 ticks,
NPFS flush at 30 seconds and xHCI disabled. The peer was the same host on Wi-Fi
at `192.168.0.51`; these results do not measure the port's 1 Gbps ceiling.

```sh
make -j16 image PYTHON=build/hpet-config-venv/bin/python3 \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis- \
  NETWORK_CONFIG=/tmp/pyxis-thinkpad-network.lua
```

Host `socat -u TCP4-LISTEN:5001,bind=192.168.0.51,reuseaddr OPEN:<capture>,creat,trunc`
received guest `ttcp -t -n 8192 -l 8192 192.168.0.51`, three times per revision.
Independent host byte counts were 67,108,864 for all six transfers, and their
SHA-256 values matched. Timing includes TCP closure:

| Revision | Seconds, three runs | Mean seconds | Range seconds |
| --- | --- | --- | --- |
| `eaeb417` baseline | 25.107563, 24.645992, 24.734602 | 24.829386 | 0.461571 |
| `36c45f2` | 24.736208, 25.213221, 25.092571 | 25.014000 | 0.477013 |

The mean rose 0.74%; the difference is smaller than within-revision variation.
Both revisions passed gateway ping and the existing UDP echo tool. The updated
revision also passed loopback ping; all commands ran through the real-port
remote terminal at `192.168.0.50:2323`.

On the baseline the owner unplugged during an 8 GiB `ttcp` request and replugged
after about ten seconds. Link down/up was observed, the NIC stayed active and
new gateway ping succeeded. At subsequent debugger inspection 191,830 submitted
TX descriptors had all completed, with none outstanding and no malformed RX.
This run did not observe device-owned TX during carrier loss and did not
reproduce a permanent stop. The long transfer was cancelled after inspection.

Hardware tally snapshots on `36c45f2` before/after the three TX runs showed
`rx_missed=91` unchanged, with zero TX/RX/alignment errors. This count was
inherited from earlier VFIO runs; qualification does not reset it. For receive,
host `socat -u OPEN:<64-MiB-capture> TCP4-LISTEN:5002,bind=192.168.0.51,reuseaddr`
served the same payload to guest `tcp 192.168.0.51 5002 | sha256sum`.
All three 64 MiB receive runs returned the host payload's SHA-256. After them,
`rx_missed` was still 91 and hardware TX/RX/alignment errors were still zero:

| Snapshot | TX packets | RX packets | RX missed |
| --- | --- | --- | --- |
| Before TX workloads | 442,937 | 95,476 | 91 |
| After TX / before RX | 819,770 | 175,301 | 91 |
| After RX | 1,055,272 | 557,313 | 91 |

The worker had received 461,868 frames with zero malformed completions and no
outstanding TX at the final snapshot. These bounded runs give no evidence for
increasing the 32-entry rings. They do not establish line-rate capacity or rule
out a full wrap of the 16-bit counter.

The owner repeated the ten-second unplug on `36c45f2` during another 8 GiB
`ttcp` request. A breakpoint in the carrier-down branch observed zero TX
outstanding, descriptor OWN clear and the controller active, then immediately
resumed service. After replug the controller remained active with no stop reason;
1,184,275 submitted descriptors had completed and RX malformed remained zero.
A new remote session passed three gateway pings and UDP echo. This checks link
recovery but leaves the device-owned TX case unobserved. The long transfer was
cancelled after inspection.

GDB snapshots stop the BSP in scheduler code with kernel mappings and IF=0,
outside locks. On this GDB/QEMU combination an ordinary injected call's dummy
return executed a non-executable stack address and halted the guest; that boot
was discarded. Qualification instead uses an executable return breakpoint,
preserving/restoring the caller registers and stack. Counter stops are between
workloads, not during timed transfers. The final untimed link check used a
one-shot breakpoint to read ownership at the carrier-down branch. Raw captures
remain under ignored `build/rtl-task5-*`.

A separate stock-profile boot kept both VirtIO and the real RTL present
(`VIRTIO_NET=1`, `TCP_FORWARD=2323:2323`, same CPU/RAM/KVM configuration).
VirtIO activated while the RTL remained prepared with DMA/delivery disabled.
Remote terminal, three `10.0.2.2` pings and UDP echo passed through VirtIO.
Both image builds passed; no kernel warning was emitted. No new tests or CI jobs
were added. QEMU, GDB and host echo/transfer jobs are stopped.

Native completion is owner-reported in [PR #368](https://git.internal/PyxisOS/pyxis-os/pulls/368)
on 2026-10-04, from the saved private-profile pair on a cold/PXE boot with the
dock attached. XID `541` prepared and activated, XID `502` was diagnosed as
unsupported without variant-specific writes, and link rose after activation.
No FIFO recovery-reset message appeared. This demonstrates the observed UEFI
handoff; it does not establish every firmware or power state.

The wired desktop at `192.168.0.213` received 7/7 ping replies from
`192.168.0.50`: first reply 3.4 ms, subsequent replies 0.58–0.68 ms. Through the
remote terminal at `192.168.0.50:2323`, the owner ran
`ttcp -t -p 5001 -n 8192 -l 8192 192.168.0.213` against the desktop's `socat` sink.
The guest reported 67,108,864 bytes in 2.192838 seconds, 29.186 MiB/s including
closure. Desktop `wc -c /tmp/rx.bin` confirmed 67,108,864 bytes; content was not
hashed. The firewall exception was temporary.

The same native boot fetched `https://example.com` through `cat` and piped
`cat https://duckduckgo.com | sha256sum`. These owner observations exercise name
lookup, outbound TCP/TLS, native HTTPS reads and the shell pipeline. The hash is
an observed page snapshot, not a stable expected value or an independent content
integrity check. Native and VFIO timings use different peers/environments; their
roughly elevenfold difference does not isolate Wi-Fi, VFIO/QEMU or stack costs.

The remote-terminal completion goal is met in VFIO and natively with the dock
attached. Native unplug and device-owned TX at carrier loss remain unobserved;
only XID `541` is supported, addressing is static, and line-rate throughput is
unqualified. No further owner action is required for this milestone.

### Task 1 owner capture

With the VFIO VM stopped, the owner can return the port to r8169, capture the
following output and restore the VFIO binding. Use Wi-Fi or dock Ethernet for
host connectivity. Replace `enp5s0` with the interface identified by `ip -br link`.

```sh
sudo driverctl unset-override 0000:05:00.0
sudo dmesg | grep -iE 'r8169|rtl_nic'
ip -br link
sudo ethtool -i enp5s0
sudo lspci -vvv -s 0000:05:00.0
sudo driverctl set-override 0000:05:00.0 vfio-pci
```

Remove all MAC bytes before sharing or committing the capture. Linux's reported
firmware establishes what its driver used; firmware-free operation remains a
measurement rather than an assumption. The owner supplied this capture on
2026-10-03; the hardware profile contains its redacted summary and the agent's
Caelum-side confirmation.

## Accepted ownership and preparation constraints

The plan follows the existing [PCI](../devices/pci.md),
[networking](../devices/networking.md), [SMP](../kernel/smp.md) and
[memory](../kernel/memory.md) contracts. Boot preparation
and allocation remain BSP-owned before AP startup; the network worker owns
protocol state and runtime ring service. Interrupt entry handles only the
necessary device acknowledgement/notification and wakes that worker; it does not
process packets, allocate or log.

Borrow valid RX bytes only while processing, then return the buffer to hardware.
TX copies into driver-owned storage; successful queueing does not mean delivery.
Failure and timeout do not return DMA ownership. Quiescence precedes boot failure
unwinding; runtime failures retain owned storage and shared mappings until reboot.
Exact ring sizes and initialization timings are implementation choices established
for the identified variant, not inherited VirtIO values or architectural limits.

The host reports I/O BAR0, 4 KiB memory BAR2 and 16 KiB memory BAR4.
`pci_size_bars()` sizes all memory BARs and skips I/O BARs; `pci_map_bar()` maps
any sized BAR, including BAR2 and BAR4. Reuse those existing helpers.
Task 2 generalized the provisional 4 KiB `pci_map_bootstrap_bar()` helper:
xHCI selects BAR0, RTL8111 selects BAR2 before sizing, then retains its checked
register mapping. The existing MSI-X helper maps BAR4's table/PBA; delivery
stays function-masked during preparation. Task 4 routes entry zero to BSP
vector 40 and enables delivery only after explicit binding.

**Accepted task 2 choices:** firmware-free first, with initial I/O measured in
task 4 and sustained/native qualification in task 5; disable endpoint ASPM/CLKREQ and run the PHY at full power;
move to D0 with a 10 ms wait, disable PME/wake, and enable initially disabled
memory decoding only with bus mastering off. Inconsistent initial states leave
the controller unavailable while boot continues. The
[hardware profile](../devices/rtl8111-hardware.md#accepted-task-2-choices) records
the defaults and conditional firmware import. These are the owner's current
direction, revisable by the owner; their acceptance does not qualify operation.
The owner also accepted temporary PCI wake/decode before XID identification only
with bus mastering off, with prior command/PMCSR restored for unsupported variants.
Reset-causing D3hot wake is rejected before writing to preserve assigned BARs.
Variant-specific writes require confirmed XID `541`; uncertain restoration or
quiescence retains ownership until reboot.
Keep only the confirmed variant's necessary setup, with bounded failure handling.

## Accepted validation and boundaries

Use ordinary builds, interactive VFIO boots and debugger inspection. Before
stack changes, capture a VirtIO baseline with existing ping/UDP/TCP workloads;
repeat matched runs afterward, recording revisions, configurations, commands,
samples and variation. Qualify real-NIC behavior separately rather than treating
different transports as a matched performance comparison. Native qualification
is run by the owner. No new benchmark or boot automation is assigned.

DHCP, broadcast discovery, reverse remote terminal, Wi-Fi, offloads, jumbo frames
and hot-plug remain separate work. The dock's profile and multiple active
interface routing remain explicit follow-ups/decisions, rather than accidental
restrictions in the built-in controller implementation.

## Accepted hardware references

Use pinned primary sources as hardware references, with provenance and licensing
recorded for any imported material. Linux v6.18 identifies the MAC implementation
from TxConfig/XID and contains variant-specific PHY/firmware setup:

- [r8169 controller identification and initialization](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_main.c)
- [r8169 PHY configuration](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_phy_config.c)
- [r8169 firmware format and interpreter](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_firmware.c)

Task 1 confirms XID `541`, corresponding to Linux's MAC version 46
(`RTL8168h/8111h`), and the MSI-X layout. Native preparation is owner-reported
in PR #362; task 4 checks VFIO interrupt delivery and initial traffic. Cold-start
firmware-free reliability and native I/O remain unqualified. PCI revision
`0x15` alone is not proof of a particular Realtek MAC implementation.


## Task 4 validation

Baseline `b8a5255` and implementation `630f86a`, both pinning userspace `08e3c4b`,
were built with GCC 16.2 and `make -j16 image`, using
`CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis-` and
`PYTHON=build/hpet-config-venv/bin/python3`. Both include the merged installer and
selector dependency changes. Subsequent validation-note edits do not change code.
Builds passed; existing vendor port warnings remain. No compiler rebuild,
dependency pin change, test harness or fault injection was added.

Runs used QEMU 10.2.2/KVM on the owner's ThinkPad host (`systemd-detect-virt`: none),
4 CPUs, 2 GiB RAM, the raw edk2 OVMF pair, default init, and physical passthrough
`VFIO_PCI=0000:05:00.0`. These are host KVM results, not nested-VM measurements or
native Pyxis qualification. GDB inspected state without profiling workloads.

With `NETWORK_CONFIG=/private/path/network.lua` and both NICs present, RTL bound
by its private boot-loaded MAC and VirtIO stayed unstarted with DMA unpublished.
The existing host remote client connected to `192.168.0.50:2323` and ran three
`ping -c 5 192.168.0.1` batches: 15/15 replies, batch means 1.383, 0.622 and 0.749 ms
(range 0.335–2.504 ms). `ping -c 3 127.0.0.1` returned 3/3. GDB found 157 interrupts,
112 received frames, 50 submitted/completed TX frames, no outstanding TX and no
malformed RX. Both rings wrapped; the device mask was `0x002f`.

A warm boot initially failed FIFO drain after an active guest exited. The new
firmware handed off Command `0x0007`, ChipCmd `0x0c` and MCU `0x22`. Recovery now
requires one confirmed software reset followed by fresh drain/OOB/stop checks;
it does not assume timeout means stopped. The final RTL boot and following
VirtIO boot both exercised recovery successfully.

Matched VirtIO runs used `VIRTIO_NET=1 TCP_FORWARD=2323:2323` with the packaged
profile, keeping RTL inactive (ChipCmd/mask zero, no driver DMA or interrupts).
The existing remote client ran three `ping -c 5 10.0.2.2` batches and three
`ttcp -t -p 5001 -n 128 -l 8192 10.0.2.2` sends. Host `socat` accepted TCP port
5001 and `wc -c` confirmed 1,048,576 bytes per run. UDP used the existing host
port-18080 echo with `udp-send 10.0.2.15 10.0.2.2 18080 "RTL I/O baseline"`.

| Measurement | Baseline | After |
| --- | --- | --- |
| Ping batch means, ms | 0.443 / 0.833 / 0.869 | 0.996 / 0.941 / 0.466 |
| Ping aggregate mean / median, ms | 0.715 / 0.431 | 0.801 / 0.555 |
| Ping range, ms; replies | 0.208–2.900; 15/15 | 0.203–2.911; 15/15 |
| TCP seconds, including closure | 0.470191 / 0.469106 / 0.467918 | 0.487508 / 0.472137 / 0.474428 |
| TCP mean seconds | 0.469072 | 0.478024 |
| UDP echo | exact 16-byte reply | exact 16-byte reply |

TCP mean time rose 1.9%, with more variation after the change; the small sample
cannot establish cause. Ping likewise varies within both runs. This is functional
and bounded timing evidence, not proof of unchanged throughput or sustained
reliability. Raw local captures are `build/rtl-io-baseline-*`,
`build/rtl-io-final-physical.jsonl` and `build/rtl-io-final-virtio-*`.

A separate four-CPU no-NIC boot reached userspace with both controller lists
empty, binding NONE, worker ready and external IPv4 unassigned.

Manual profile assembly accepted a path containing spaces, replaced only the
initrd profile and restored the packaged profile when omitted. The userspace
checkout stayed clean. Native/cold-start I/O, link changes, long transfers,
firmware-free reliability and other RTL variants remain unqualified; task 5
has not started.
