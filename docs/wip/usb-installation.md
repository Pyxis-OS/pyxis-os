# USB boot and first physical installation

Status: Phase A image assembly/USB boot and Phase B.3 enumeration/control
transfers, BOT/SCSI reads and kernel block/GPT integration implemented,
2026-10-04; native USB mounts and Phase C.1 write/flush support are implemented.
Qualified disks support explicitly requested writable mounts. Phase C.2's
QEMU persistent edit/build/run loop is qualified; physical validation remains
pending.
Owner-reported [ThinkPad observations](../targets/t14-gen1-amd/usb-bringup.md) now
include root and USB 3 hub-descendant storage reads. Broader hardware/recovery
qualification of native mounting remains pending. Inspection-first work includes
[USB 2/3 hub traversal](../devices/usb-hubs.md). The [build-time switch](../devices/usb-xhci.md) defaults to disabled;
firmware USB boot and archive-backed programs remain available.
The owner wants a replaceable USB drive as the first
physical installation target, with QEMU development before laptop validation.
Implemented image behavior lives in the [USB image reference](../development/usb-image.md).
Phase B follows the read-only contracts below. Implemented controller behavior
lives in the [xHCI reference](../devices/usb-xhci.md), with checked discovery and
request ownership in [USB enumeration](../devices/usb-enumeration.md). The
[persistent development walkthrough](../development/edit-build-run.md#persistent-usb-development)
keeps a private QEMU disk across sessions. Physical validation remains unassigned. This document does not
authorize physical writes or reorder the active filesystem, spaces/SMP and
display work.

## Intended result and layout

Boot Pyxis independently from a USB drive, then read and eventually persist
files on a Pyxis pool on that same drive. Keep the internal NVMe outside the
initial storage scope. Phase A builds this GPT layout:

- An EFI System Partition, FAT32, containing Limine, its configuration, the
  Caelum kernel and matching boot archive.
- A Pyxis pool partition containing persistent volumes.

Limine loads the kernel and archive into RAM. Booting packaged programs therefore
does not require a Caelum USB driver; accessing the drive after firmware boot
services end does. The first stage needs neither a runtime FAT driver nor a
guest installer. Partition sizes are image configuration, not filesystem limits.
Image preparation and later updates must keep the kernel/archive pair consistent;
atomic updates, rollback and an in-guest update command need separate design.

## Known target and missing evidence

The owner's SanDisk Ultra Dual Drive Go, labelled SDDDC3 and sold as 128 GB,
was inspected under Linux on 2026-10-02:

| Observation | Reported value |
| --- | --- |
| USB identity | `0781:55a9`, device revision 1.00 |
| Capacity | 240,328,704 logical blocks of 512 bytes, approximately 114.6 GiB |
| Connection | SuperSpeed, 5 Gb/s, xHCI, through a Realtek hub on the desktop |
| Interface 0, alternate setting 0 | SCSI subclass, Bulk-Only Transport, two bulk endpoints |
| Interface 0, alternate setting 1 | USB Attached SCSI (UAS), selected by Linux |
| Cache report | Write cache enabled; FUA unsupported; preferred minimum I/O 4096 bytes |

The drive offers the simpler Bulk-Only transport even at SuperSpeed. USB-A and
USB-C identify connectors, not different mass-storage protocols. A separate
32 GB SanDisk (`0781:55a3`) was also connected; its descriptors are not evidence
about this target. Linux device names, bus paths and these sample values must
not become driver matching rules or test invariants.

These observations do not prove cache-flush support or power-loss durability.
Explicit cache synchronization needs validation before writable use; lack of FUA
alone does not exclude that path. The [ThinkPad T14 Gen 1 AMD inventories](../targets/t14-gen1-amd/notes.md)
now record three xHCI functions, directly attached laptop routes and hub-backed
dock routes under Fedora. Inventory now inspects each discovered controller
independently without selecting from those addresses. Native qualification and firmware handoff
remain unverified; Linux topology does not establish Pyxis support.

## QEMU-first development

The installed QEMU 10.2.2 advertises `qemu-xhci`, `usb-storage`, `usb-bot`,
`usb-uas` and `usb-host`. QEMU documents image-backed Bulk-Only storage behind
xHCI. Phase A has booted the archive-backed shell through emulated USB;
Phase B.2 has exercised native controller commands and interrupts in QEMU. See the
[validation record](../development/usb-image.md#validation) and its firmware limit.

1. **Emulated controller and drive.** Use Q35, xHCI and a directly attached
   `usb-storage` device backed by a raw GPT image. Verify that the selected OVMF
   build boots its EFI partition through USB. Develop against the same image's
   Pyxis partition, without a second virtio-blk attachment hiding missing USB I/O.
2. **Real drive behind an emulated controller.** Later, use `usb-host` to forward
   the selected SanDisk. This exercises its descriptors, Bulk-Only selection,
   commands and media behaviour. The host must relinquish filesystem access to
   that device while the guest owns it. Writable passthrough writes the real stick.
3. **Native ThinkPad.** Validate the physical xHCI controller, firmware ownership
   handoff, DMA, interrupts and actual port routing. Device passthrough in stage 2
   still uses QEMU's controller and cannot establish those properties.

No hardware topology is hard-coded from QEMU. Existing verified RAM/no-swap
controls remain the boundary for mutation and recovery campaigns; a raw image
file is not inherently RAM-backed. Reuse those controls without adding fixed
machine, memory or image-size requirements. Physical media is for explicit,
bounded installation and persistence checks, not amplification campaigns.

## Reusable layers, bounded first consumer

The owner requires reusable subsystem boundaries. Storage is the first supported
USB class, not the definition of a USB device. Adding a later mouse or keyboard
driver must not require replacing enumeration, controller ownership or the
transfer-completion machinery.

- **xHCI** owns controller registers, rings, DMA, endpoint scheduling and completion.
  It does not interpret storage commands or assume every endpoint belongs to a disk.
- **USB core** owns device/configuration/interface/endpoint descriptions,
  enumeration and transfer/lifetime contracts. Class matching uses the relevant
  interface descriptors, not a blanket assumption that an attached device is storage.
- **Mass storage** owns Bulk-Only command/data/status handling and SCSI operations,
  translating them into the block contract. Transport-specific recovery remains
  here, using USB endpoint/reset operations below it.
- **Block, GPT and filesystem layers** consume their existing contracts without
  knowing USB endpoint numbers or xHCI rings. Boot-image assembly is independent
  of the kernel transport that later reads the image.

Unknown classes, unsupported interfaces and unsupported topologies must produce
an explicit unsupported/unbound result without entering storage code or crashing
the controller. A directly attached unsupported mouse alongside the supported
drive must not prevent the drive from operating. Parse descriptors defensively;
do not assume one interface, two endpoints or a particular vendor outside the
specific class driver that requires that shape. Composite-device support may
remain limited, but its rejection must be explicit and safe.

Keep state owned by the actual controller/device/interface/request where it
belongs. A bounded initial selection policy must not leak into globals that
implicitly make every transfer belong to one storage device. Settle reset,
disconnect, cancellation and DMA lifetime at the layer owning them, so class
drivers can rely on the same completion rules.

This does not require a generic driver framework, dynamic plugin registry or
unused transfer APIs. Implement the concrete control/bulk path first through
small internal interfaces; later HID support can add interrupt transfers and
class parsing without rewriting the working storage path. Phase B validation
should include an unrelated unsupported USB device as well as the chosen disk.
Phase A itself needs no kernel USB abstractions or placeholder drivers.

Native hardware-listing tools and their name databases are described separately
in [hardware inspection](../devices/hardware-inspection.md). `lsusb` consumes the
USB boot inventory; neither tool nor its benchmark is a boot-image prerequisite.

## Phase B.1 read-only contract

The owner accepted per-device read-only support on 2026-10-04, superseding
unique supported-disk selection and the earlier single-controller backend
proposal. B.4 introduced a kernel-internal BOT/SCSI probe; the first B.5 slice adds
kernel block registration and GPT discovery. Configured mount authority
now covers observed USB disks; C.1 extends the initial read-only profile with
qualified writes and flushes under the same selection policy.
Physical qualification remains Phase C.

### Selection and authority

Inspect supported boot-present devices independently across discovered xHCI
controllers and traversed hubs. Each physical device contributes at most one
storage candidate. Select the first supported configuration and alternate in
validated descriptor order: one non-composite interface, SCSI transparent command
set (`08/06/50`), exactly one Bulk-In and one Bulk-Out endpoint, no streams, and
only LUN 0. Multiple LUNs and unsupported shapes have explicit per-device results.
BOT/UAS alternative configurations can select BOT; values and endpoint identities
come from descriptors. Unrelated classes stay unbound. No vendor, controller,
port number or sampled capacity selects a disk.

Incomplete device inspection prevents binding that device. An incomplete branch
or unsupported controller remains visible in `lsusb`, while inspected supported
devices can be probed independently. A failed media probe does not remove its
candidate or choose another disk in its place. Unsupported media and command
failures retain individual diagnostics; unsafe host errors can quarantine their
controller, while other controllers continue.

The internal read-only boot probe retains private geometry, sense, counters and
byte samples. Terminal candidates now register with the kernel block interface,
including individual unsupported/setup-failure results; supported devices serve
queued reads and GPT discovery. Configured GUID authority now supports native
USB mounts, including explicit writable requests on qualified media. Public
raw-disk operations remain deferred.
Enabling `CONFIG_XHCI` enables these consumers; the checked-in default remains `n`.

Block integration must preserve today's stable per-device IDs, explicit disk
handles and native mount authority. USB addresses, topology, serial numbers and
GUID knowledge grant no filesystem authority. B.5 preserves a separate VirtIO-only
installer domain while configured native GUID authority searches the full
registry; the old global `block.backend` and
unique-disk policy are superseded, not implemented requirements.

### Controller resources and startup

The current [PCI inventory](../devices/pci.md) retains class fields and exposes
read-only indexed records after the native `lspci` integration. Reuse those
fields to inspect each class-matched function independently, retaining explicit
unsupported and failed outcomes. The remaining
resource requirements are initial MMIO access before BAR sizing and shared
MSI-X mechanics. B.2 now provides these with the xHCI consumer.
The initial controller profile is PCI xHCI 1.x with firmware-assigned memory
BAR0, 64-bit DMA addressing, 4 KiB page support and MSI-X. Firmware must leave
MSI/MSI-X disabled, as required by the current PCI claim. Support both advertised
context strides. Controller port, slot and scratchpad requirements come from
checked capability fields, with allocation failure reported explicitly.
Other address widths, interrupt mechanisms and page sizes are unsupported in
this first profile; these are deployment limits, not USB architectural rules.

Current `pci_claim_device` immediately disables bus mastering. B.2 needs a
staged claim for the first MMIO consumer: reserve exclusive software/configuration
ownership while preserving firmware decoding and bus-master state, perform
handoff and halt, then disable bus mastering/INTx and proceed to resource probing.
Keep existing VirtIO preparation behavior unchanged. As
[xHCI sections 4.21.2–4.22.1](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf)
explain, clearing bus mastering on a running controller can cause a host-controller
error, and firmware ownership must transfer before controller use.

Use a narrow owned bootstrap mapping of BAR0's first 4 KiB for capability and
operational registers. This is provisional access to an assigned BAR, not a claim
that its size was probed. Validate the assigned base, arithmetic, alignment and
existing boot/platform MMIO exclusions before access. Every bootstrap access,
including complete extended-capability bodies, must fit that prefix. A chain
that leaves it is unsupported in the initial profile. Do not access port, runtime
or doorbell registers outside the prefix before sizing. No kernel DMA is
published during this stage.

If legacy ownership is advertised, request OS ownership and wait for firmware
release; never force a stuck BIOS-owned controller into use. Disable legacy SMI
sources only after handoff. Wait for `CNR=0` before operational writes, confirm
halt before clearing bus mastering, then reset and wait for both reset completion
and `CNR=0`. Disable decoding, size BARs and validate the bootstrap extent before
preparing the remaining owned mappings.
Preserve stop-before-probe and ordinary sized-BAR bounds. Partial preparation
unwinds only while device access and interrupt delivery are demonstrably safe.
The bootstrap mapping API and PCI MSI-X extraction were implemented in B.2 with
their first real consumer; B.1 added no placeholder interfaces.

Prepare controller/device records, contexts, scratchpads, rings and transfer
buffers under the current pre-AP allocation/VM contract. The existing coherent
contiguous allocator supplies backing; checked structure placement, alignment,
page/ring-segment boundaries and transfer-TRB splitting establish the additional
xHCI layout requirements. Page alignment alone does not establish them. Keep CPU
and physical addresses distinct. After scheduler setup, a worker activates the
controller and performs bounded
enumeration. IRQ handling acknowledges activity and wakes the worker; parsing,
commands, recovery and logging remain worker work. Today's BSP affinity follows
current APIs and is not a permanent USB ownership requirement.

The first B.5 slice registers terminal candidates without allocation, preserving
per-device preparation separately from later availability. Sealed boot discovery
and exhaustive discovery are separate queries. GPT waits for sealed discovery
and scans USB media sequentially through ordinary block tickets, with one shared
pre-AP scratch allocation. Partial USB discovery does not disable individually
READY disks or existing VirtIO authority. Private queues, DMA and recovery stay
with each backend. Configured USB mount authority is implemented;
public raw USB authority remains deferred.

### Requests, deadlines and failure ownership

Keep command and transfer tickets with their owning controller, device and
endpoint. Small internal control/bulk interfaces capture outbound bytes and
retain no caller read destination. A checked completion supplies actual bytes;
the caller collects or abandons exactly once. Ticket generations and checked ring
retirement must prevent a late completion from referring to a reused request; a
software generation alone cannot identify a stale hardware event carrying a
reused ring address. Class-specific expected lengths and short-transfer handling
stay above xHCI.

The first class consumer reserves four device entries per controller, each with
two bulk ring pages and one boundary-aligned transfer buffer for reads up to
64 KiB. One controller-owned DMA arena avoids multiplying VM range records.
A controller also reserves one non-DMA read scratch buffer before AP startup.
A failed admission is explicit; entries are never recycled for another device
this boot. Each owning worker runs synchronous private transfers, capturing
outbound bytes and retaining no caller read destination. One exchange runs at a
time per interface, as required by
[BOT section 3.4](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf).
B.5 adds two captured read slots per supported device before AP startup. C.1
reuses their buffers for captured writes and ordered flush tickets. Queued,
active and completed-uncollected requests count against that budget; the owning
controller worker serializes the exchanges and collection copies only successful
whole reads. No caller read pointer is retained.

Initial deadline choices are one second for firmware handoff and controller
halt/reset, five seconds for a controller command or control transfer and for
an entire BOT exchange, and thirty seconds for enumeration/media setup.
Protocol-directed status retry shares the exchange deadline. Recovery has its
own five-second absolute bound. Retries never restart a deadline. These values,
slot count and buffer size are starting implementation choices, maintained in
one place when coded; changing them does not change the ownership contract.
Reassess them against measured QEMU/device behavior, without freezing them or
image sizes in unit-test expectations.

B.5 preserves the [block ticket contract](../devices/block-storage.md#tickets-and-caller-ownership):
caller wait timeout neither cancels nor consumes; successful read collection
copies exactly the requested bytes, and failure leaves caller storage untouched.
Set `submitted` when the original BOT command first becomes device-visible.
B.5 initially reports read-only capabilities. C.1 qualifies write protection and
blocking cache synchronization before reporting writable/flush support.
Media geometry must satisfy the existing block/GPT logical-block profile and
checked range arithmetic, independent of image defaults or sampled drive sizes.

Queued abandonment can cancel unpublished work. An active request, including
worker preparation before publication, cannot be recycled merely because its
client abandoned it; preparation may still publish. Published work retains its
request, ring and DMA ownership until checked completion or confirmed safe
retirement. Stopping an endpoint alone leaves queued transfer descriptors;
establish safe dequeue/retirement before reuse or restart. Endpoint stall handling
belongs to USB; required BOT reset and clear-halt ordering belongs to mass storage.
A recovered channel does
not turn the failed block read into success or authorize an automatic block retry.
Unexpected removal, controller protocol corruption or device-work timeout stops
the affected controller for this boot. A failed class recovery abandons that
device; unsafe host recovery also stops its controller.
Disabling PCI bus mastering or masking an interrupt alone is not proof that all
device ownership returned. Unresolved DMA storage cannot be reused.

Runtime claims, mappings and allocations remain retained until reboot under the
current shared-VM contract, even after successful reset. Late IRQ state remains
valid. Safe request reuse after ordinary completion is separate from runtime
unmapping. No reset or recovery may substitute a new disk into existing authority.

## Staged milestones

Each stage should be assigned separately and delivered in focused PRs. USB read
support does not require completion of native writable integration.

### A. Bootable USB image

Accepted scope: configurable image/ESP sizing (1 GiB / 256 MiB defaults), explicit
sample-pool owner input, host-only assembly without root/mounts, fresh complete
image rebuilds and separate launch of an existing read-only image. Sizes remain
image configuration, not filesystem limits or fixed validation expectations.
The [image and launch contract](../development/usb-image.md) documents layout,
host tools and the later physical-preparation procedure.

1. [x] **Define and build an opt-in raw installation image.** Reuse existing
   kernel/archive assembly and pinned Limine. Settle image sizing and host-tool
   requirements, and populate the EFI partition plus a read-only-test Pyxis pool.
   Do not add automatic host-disk discovery or formatting of attached drives.
2. [x] **Boot through emulated USB.** Add explicit QEMU options, verify the OVMF
   USB boot path and exercise the archive-backed shell. Preserve ordinary ISO
   boot. Document preparing the eventual selected physical target separately.

### B. Native read-only USB storage

- [x] **Pause native xHCI initialization by default.** Guard preparation and
  worker startup with the `CONFIG_XHCI` Kconfig option while broader controller
  and recovery qualification remain pending. The checked-in `.config` defaults
  to `n`; direct edits or `make menuconfig` can explicitly re-enable initialization
  for a rebuilt bring-up image.

1. [x] **Settle controller, request and disk-selection contracts.** Inventory
   current PCI resource/interrupt/DMA facilities and block-interface coupling.
   Propose bounded request storage, timeouts, failure ownership and explicit disk
   selection. Follow the scheduler/allocation model current at implementation;
   do not preserve today's BSP placement as a new USB requirement.
2. [x] **Bring up one xHCI controller.** Handle ownership/reset, DMA command/event
   rings, interrupt delivery and root-port state. Start with one directly attached
   supported storage device present at boot. Leave UAS, external hubs, insertion
   after boot, legacy UHCI/OHCI/EHCI and power management outside this first slice.
3. [x] **Enumerate and transfer.** Implement bounded control transfers, checked
   descriptor parsing and addressing. Publish root-device and interface observations
   independently of class binding; unsupported inspection remains explicit.
   - [x] Extend the inspection snapshot through USB 2 hubs with low/full/high-speed
     descendants, discovered parent/port paths, full-path boot diagnostics and
     bounded pre-AP resources. Capture follows power-good and attachment settling;
     unused compatibility padding does not reject an otherwise bounded descriptor.
     Hotplug and storage remain deferred; broader high-speed TT and physical
     hardware qualification remain pending. See the [hub reference](../devices/usb-hubs.md).
   - [x] Recognize SuperSpeedPlus roots from protocol-version defaults or explicit
     PSI link metadata and inspect their device/configuration descriptors. Expose
     a distinct `super-plus` inventory category without numeric rate/lane fields.
     Root link recognition precedes the hub traversal slice below; the owner
     subsequently observed the dock's SuperSpeedPlus hub natively. Broader link
     qualification remains pending.
   - [x] Traverse boot-present SuperSpeed/SuperSpeedPlus hubs with checked BOS
     speed attributes, USB 3 hub descriptors/depth/status/reset, discovered
     routes and unique controller-profile matching. Keep unsupported links and
     exhausted branches partial. The owner subsequently observed the dock's
     USB 3 hub and storage descendant natively; recovery remains unqualified.
     Traversal introduces no hotplug or power-management policy.
   The initial BOT matcher/endpoint setup was removed for the inspection-only
   slice; reintroduce class transfers with their first consumer and an explicit
   pre-AP resource policy in B.4.
4. [x] **Implement Bulk-Only/SCSI reads.** Identify media, obtain capacity and
   logical-block geometry, report command failures and read bounded block ranges.
   Handle short transfers, stalls, protocol status and required reset recovery.
   Keep IRQ work short; timeouts or unexpected removal must fail pending work
   without freeing DMA storage until device access has safely ended.
   Implemented as an internal per-device [read-only media probe](../devices/usb-storage.md);
   [QEMU bring-up](../development/usb-storage-bringup.md) records coverage and limits.
5. [x] **Integrate kernel block reads and GPT discovery.** Register terminal USB
   candidates through the per-device block contract; use bounded asynchronous
   read tickets and retain per-device setup/failure outcomes. Seal boot discovery
   separately from completeness, preserving usable devices under partial inventory.
   Installer disk grants remain VirtIO-only; configured filesystem mounts use
   the full registry in the following task.
6. [x] **Integrate read-only USB mounting.** Reuse GPT, filesystem core and native
   object interfaces, preserving explicit device authority. Trusted init selects
   partition, volume and binding within supplied disk authority; programs receive
   directory/file grants, not ambient raw-disk access. Read known files and launch
   a captured executable from the USB-backed volume in QEMU.
   The owner accepted selection of the sole observed valid GUID match after
   sealed discovery and terminal GPT scans, even under partial discovery. Duplicate
   observed matches and selected-device errors fail. Without a match, complete
   discovery reports NOT_FOUND and partial discovery reports UNAVAILABLE. Unseen
   devices could conceal another matching GUID; this accepted limit does not
   grant public raw-disk access. C.1 separately qualifies writable mounts.

### C. Persistent USB installation and hardware validation

1. [x] **Establish write and flush behaviour.** Bounded WRITE (10)/(16) and real
   whole-medium SYNCHRONIZE CACHE (10), with IMMED clear, use the existing block
   tickets and captured buffers. Known clear write protection and successful
   blocking synchronization qualify each disk; unsupported/unknown capabilities
   preserve reads on healthy transport. The owner accepted explicitly requested
   writable mounts through configured GUID authority; installer raw USB access
   remains deferred. Per-device FIFO orders flushes, failed or abandoned published
   mutations latch write failure, and no mutation is replayed. See the
   [C.1 implementation](../devices/usb-storage.md) and
   [QEMU qualification record](../development/usb-storage-bringup.md#2026-10-04-qualified-writes-and-cache-synchronization).
   Error/recovery branches remain source-reviewed without forced-error validation;
   QEMU persistence does not qualify physical durability.
2. [x] **Integrate the persistent development loop.** Existing Kilo, TCC,
   configured USB mounts and the native writer support source/object/executable
   files under a delegated USB root. Manual QEMU sessions edited, built and ran
   two program versions, explicitly synced the pool, then reopened matching
   files and launched/rebuilt after a fresh process. Separate read-only grant
   and read-only attachment sessions retained reads and execution while denying
   mutations. The [walkthrough](../development/edit-build-run.md#persistent-usb-development)
   preserves a private disk while rebuilding only the ISO; the
   [qualification record](../development/usb-storage-bringup.md#persistent-usb-development-loop-c2)
   includes revisions, configuration, hashes and host structural inspection.
   No runtime, authority or installer changes were needed.
3. [ ] **Validate the physical installation.** First inspect laptop hardware,
   then select the expendable USB target explicitly. Progress from firmware boot
   to read-only mounting and bounded persistence checks. Record device, firmware,
   topology and observed differences. An orderly reboot is not a power-loss test.
   Include the deferred [configured-mount discovery latency measurement](../technical-debt.md#configured-mount-discovery-latency)
   on the first native USB mount boot and the
   [write-qualification compatibility notes](../technical-debt.md#usb-writable-media-qualification-limits)
   if the selected device rejects or disrupts optional qualification commands.

## Remaining assignment and qualification decisions

- Phase A and B.3 enumeration/control transfers are implemented; B.1 records the
  accepted per-device read-only contract. Enumeration publishes root devices and
  supported USB 2/3 hub descendants. B.4 adds an internal BOT/SCSI media probe;
  B.5 read-only mount integration and C.1 qualified writes/flushes are implemented.
  C.2's QEMU persistent development loop is qualified. C.3 physical validation
  remains unassigned; native ThinkPad testing is deferred.
- Image update/preservation ownership remains open for persistent installation.
  Read-only disk selection does not qualify a write target or authenticate media.
- The [GUID/boot-device identity follow-up](../technical-debt.md#configured-guid-and-boot-device-identity)
  is deferred until internal-disk installation support; boot-device preference
  remains a proposal, and current observed-uniqueness selection is unchanged.
- If the physical target requires hubs, firmware capabilities outside the
  bootstrap prefix or another unsupported controller feature, inspect and discuss
  that expansion before changing the initial hardware profile.
- Agree the writable-device qualification and recovery evidence before physical
  writes. Replaceability limits the cost of failure; it proves no endurance or
  durability guarantee and does not remove the filesystem deployment gates.

## References

- Existing [block storage](../devices/block-storage.md), [GPT](../devices/gpt.md),
  [PCI resources](../devices/pci.md) and [read-only mounts](../devices/native-readonly-filesystem.md).
- [Boot archive assembly](../development/boot-archive.md).
- [QEMU USB emulation](https://www.qemu.org/docs/master/system/devices/usb.html)
  and [device boot ordering](https://www.qemu.org/docs/master/system/bootindex.html).
- [USB-IF Bulk-Only Transport specification](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf).
- [Intel xHCI 1.2b specification](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf),
  especially initialization (§4.2), BIOS/OS ownership (§4.22.1), register layout
  (§5.2.1) and addressing/context capabilities (§5.3.6).
- [Linux cache-control contract](https://docs.kernel.org/block/writeback_cache_control.html).
