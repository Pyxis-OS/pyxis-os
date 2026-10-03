# USB boot and first physical installation

Status: Phase A image assembly/USB boot and Phase B.3 enumeration/control
transfers implemented, 2026-10-02; BOT/SCSI media and block access remain pending.
Native xHCI hardware qualification and storage remain pending. Inspection-first
work now includes [USB 2 hub traversal](../devices/usb-hubs.md), accepted by the
owner on 2026-10-03. The [build-time switch](../devices/usb-xhci.md) defaults to disabled;
firmware USB boot and archive-backed programs remain available.
The owner wants a replaceable USB drive as the first
physical installation target, with QEMU development before laptop validation.
Implemented image behavior lives in the [USB image reference](../development/usb-image.md).
Phase B follows the read-only contracts below. Implemented controller behavior
lives in the [xHCI reference](../devices/usb-xhci.md), with checked discovery and
request ownership in [USB enumeration](../devices/usb-enumeration.md). Writable
installation is unassigned. This document does not
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

The owner accepted unique supported-disk selection, failing on ambiguity. The
following design is the accepted implementation contract for the read-only
milestone. B.2 implements its controller slice; the rest remains pending.
Its first consumer is the directly attached QEMU disk. The later accepted
inspection-first slice now handles multiple controllers and does not configure
a BOT transport. Before resuming B.4, settle how unique supported-disk selection
spans those controllers; the single-controller profile below is the earlier
storage proposal, not an inventory requirement. Physical qualification remains Phase C.

### Selection and authority

Add a kernel command-line setting `block.backend=virtio-blk|usb-bot`, parsed
before device preparation. Omission selects the existing VirtIO profile. Invalid
or duplicate settings fail configuration. One parsed setting owns selection for
the boot; later init configuration must not parse a second copy of that state.
Selecting USB never falls back to VirtIO after absence, unsupported hardware,
ambiguity or failure. Inventory in the unselected backend does not choose the
disk or make selected-backend discovery ambiguous.

USB requires exactly one xHCI PCI function in a complete inventory, matched by
class/subclass/programming interface rather than vendor/product identifiers.
Inspect boot-present devices on its advertised supported root ports. Exactly one
supported SCSI/Bulk-Only storage interface selects the disk; multiple candidates
are ambiguous. Select the candidate before media-capacity setup, so a setup error
cannot silently make another disk win. The initial storage profile accepts one
non-composite interface and one logical unit, LUN 0. Multiple logical units or
unsupported interface shapes produce an explicit unsupported result. Descriptor
selection must allow the target's BOT/UAS alternate settings, selecting BOT.

For B.3, the owner accepted inspecting every advertised configuration within
bounded descriptor storage, then choosing the first supported one-interface
SCSI/Bulk-Only configuration and alternate setting in descriptor order. Each
physical device contributes at most one candidate; its alternate configurations
are not additional disks. Configuration values, interface numbers and alternate
numbers come from descriptors. Unreadable/unclassifiable descriptors or exhausted
bounds still make discovery incomplete, even after a candidate was found.

A positively identified unrelated class stays unbound. A mouse alongside the
disk must work as that case. Per-device unsupported results do not automatically
fail the backend. Keep these final selection outcomes distinct:

| Completed discovery | Selected-backend outcome |
| --- | --- |
| One supported disk, with unrelated classes or fully classified unsupported storage | Select that disk; retain the other devices' unsupported/unbound diagnostics. |
| Several supported disks | Ambiguous; select none. |
| No supported disk, with recognized unsupported storage | Unsupported, not absent. |
| No storage candidate, with all connected devices classified | Absent. |
| A hub with uninspected downstream devices, unclassifiable descriptors or exhausted classification resources | Incomplete; a discovered supported disk cannot bypass this result. |

The initial policy treats a connected unsupported hub as incomplete relevant
inventory, because its downstream storage is unknown. Absence and uniqueness
both require complete discovery. Setup failure after identifying the selected
candidate remains setup failure; it cannot silently remove that candidate.

The existing `mount.disk` GPT GUID verifies the selected disk after discovery;
it does not select among several disks. Existing principal, partition, volume
and binding checks remain with native mounting and trusted init. USB addresses,
ports, serial numbers and GUID knowledge grant no filesystem authority. The
selected backing stays fixed for the boot. Replacement media cannot inherit live
tickets, GPT metadata or native pools through reconnection.

### Controller resources and startup

The current [PCI inventory](../devices/pci.md) retains class fields and exposes
read-only indexed records after the native `lspci` integration. Reuse those
fields for a narrow class-based selector that returns the unique claimable
function and preserves unavailable/incomplete/ambiguous outcomes. The remaining
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

Publish a final block preparation result only after enumeration, disk selection
and geometry setup finish or fail. Pending setup is not absence. GPT currently
queries geometry immediately in `gpt_start()`; USB integration must introduce a
bounded readiness wait before that query and before native mount authority is
created. Preserve the immutable preparation reason separately from later I/O
availability. Configured authority is omitted only for confirmed disk absence;
present-but-unusable and incomplete discovery remain visible failures.

The public block API is already transport-neutral in shape; its implementation
currently lives entirely in `kernel/virtio/blk.c`. Move selected-backend dispatch
into `kernel/storage/block.c` as USB becomes its first additional consumer. Keep
request queues, tickets, DMA and recovery with each backend. Immutable selection
allows the existing single-device tickets, GPT snapshot and native pool identities
to remain sufficient; a driver registry or multi-disk interface is unnecessary.

### Requests, deadlines and failure ownership

Keep command and transfer tickets with their owning controller, device and
endpoint. Small internal control/bulk interfaces capture outbound bytes and
retain no caller read destination. A checked completion supplies actual bytes;
the caller collects or abandons exactly once. Ticket generations and checked ring
retirement must prevent a late completion from referring to a reused request; a
software generation alone cannot identify a stale hardware event carrying a
reused ring address. Class-specific expected lengths and short-transfer handling
stay above xHCI.

For the initial storage implementation, start with two admitted block-request
slots, each with a reserved buffer and a 64 KiB maximum read. Queued, active and
completed-but-uncollected work all count against this budget. BOT runs one
command/data/status exchange at a time per interface, as required by
[BOT section 3.4](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf).
Reserve control, command and status storage independently so recovery cannot
depend on a client releasing a completed block slot. Derive other ring and
descriptor budgets during controller implementation and document their limits;
exhaustion must not silently skip ports or descriptors needed for selection.

Initial deadline choices are one second for firmware handoff and controller
halt/reset, five seconds for a controller command or control transfer and for
an entire BOT exchange, and thirty seconds for enumeration/selected-media setup.
Protocol-directed status retry shares the exchange deadline. Recovery has its
own five-second absolute bound. Retries never restart a deadline. These values,
slot count and buffer size are starting implementation choices, maintained in
one place when coded; changing them does not change the ownership contract.
Reassess them against measured QEMU/device behavior, without freezing them or
image sizes in unit-test expectations.

Preserve the [block ticket contract](../devices/block-storage.md#tickets-and-caller-ownership):
caller wait timeout neither cancels nor consumes; successful read collection
copies exactly the requested bytes, and failure leaves caller storage untouched.
Set `submitted` when the BOT command first becomes device-visible. Report
`writable=false` and `flush_supported=false`; writes and flushes return read-only.
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
Unexpected removal, controller protocol corruption, device-work timeout or
failed recovery stops the selected backing for the boot and fails pending work.
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
  worker startup with the `CONFIG_XHCI` Kconfig option while the ThinkPad profile
  remains unqualified. The checked-in `.config` defaults to `n`; direct edits or
  `make menuconfig` can explicitly re-enable QEMU bring-up for a rebuilt image.

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
     Root link recognition precedes the hub traversal slice below; enhanced
     native link qualification remains pending.
   - [x] Traverse boot-present SuperSpeed/SuperSpeedPlus hubs with checked BOS
     speed attributes, USB 3 hub descriptors/depth/status/reset, discovered
     routes and unique controller-profile matching. Keep unsupported links and
     exhausted branches partial; native validation remains deferred. No storage,
     hotplug or power-management policy is introduced.
   The initial BOT matcher/endpoint setup was removed for the inspection-only
   slice; reintroduce class transfers with their first consumer and an explicit
   pre-AP resource policy in B.4.
4. [ ] **Implement Bulk-Only/SCSI reads.** Identify media, obtain capacity and
   logical-block geometry, report command failures and read bounded block ranges.
   Handle short transfers, stalls, protocol status and required reset recovery.
   Keep IRQ work short; timeouts or unexpected removal must fail pending work
   without freeing DMA storage until device access has safely ended.
5. [ ] **Integrate block access and read-only mounting.** Generalize the current
   single-virtio-blk selection only as needed for an explicitly selected backend.
   Reuse GPT, filesystem core and native object interfaces. Trusted init selects
   partition, volume and binding within supplied disk authority; programs receive
   directory/file grants, not ambient raw-disk access. Read known files and launch
   a captured executable from the USB-backed volume in QEMU.

### C. Persistent USB installation and hardware validation

1. [ ] **Establish write and flush behaviour.** Implement bounded writes and real
   cache synchronization through the block contract. Unsupported flushes must
   prevent a mount requiring durability, never succeed as placeholders. Preserve
   uncertain-outcome and sticky-failure semantics on disconnect/error.
2. [ ] **Integrate the persistent development loop.** Depends on qualified
   [writable core](writable-filesystem-core.md) and
   [native persistent volumes](native-persistent-volumes.md), including their
   stack, authority and recovery prerequisites. Edit/build/run, checkpoint,
   reboot and verify persisted files; exercise a separately read-only session.
3. [ ] **Validate the physical installation.** First inspect laptop hardware,
   then select the expendable USB target explicitly. Progress from firmware boot
   to read-only mounting and bounded persistence checks. Record device, firmware,
   topology and observed differences. An orderly reboot is not a power-loss test.

## Remaining assignment and qualification decisions

- Phase A and B.3 enumeration/control transfers are implemented; B.1 defines the
  read-only contract. Inspection-first enumeration now publishes root devices and USB 2 hub descendants
  without configuring a storage transport. Multi-controller disk selection must be settled
  before B.4 establishes LUN/media support. BOT/SCSI reads and native integration
  remain pending. Writable
  work and its roadmap ordering remain unassigned.
- Image update/preservation ownership remains open for persistent installation.
  Read-only disk selection does not qualify a write target or authenticate media.
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
