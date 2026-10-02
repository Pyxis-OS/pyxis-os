# USB boot and first physical installation

Status: proposal, 2026-10-02. The owner wants a replaceable USB drive as the first
physical installation target, with QEMU development before laptop validation.
Phase A may be assigned independently to a parallel agent. Kernel USB and writable
installation stages remain unassigned; this document does not authorize physical
writes or reorder the active filesystem, spaces/SMP and display work.

## Intended result and layout

Boot Pyxis independently from a USB drive, then read and eventually persist
files on a Pyxis pool on that same drive. Keep the internal NVMe outside the
initial storage scope. The proposed GPT layout is:

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
alone does not exclude that path. The ThinkPad T14 Gen 1 AMD controller, firmware
handoff and external-port topology remain uninspected. Its chosen port may require
hub support; desktop topology does not establish laptop topology.

## QEMU-first development

The installed QEMU 10.2.2 advertises `qemu-xhci`, `usb-storage`, `usb-bot`,
`usb-uas` and `usb-host`. QEMU documents image-backed Bulk-Only storage behind
xHCI. Availability has been checked; no USB boot or driver experiment has run.

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

## Native hardware inspection and text databases

The owner also wants native `lspci` and `lsusb` tools with packaged, plain-text
name databases. This is an agreed direction for focused follow-up tasks, not an
expansion of boot-image Phase A. PCI discovery already exists, so `lspci` can be
assigned independently; `lsusb` follows USB enumeration and should help inspect
devices even when no class driver supports them.

- `lspci`: PCI address, numeric vendor/device IDs, resolved names, class and
  driver ownership where the kernel exposes it.
- `lsusb`: controller/port topology, numeric IDs, device-provided strings and
  resolved names, negotiated speed, interface classes and bound/unsupported state.
  Endpoint detail is useful for debugging once that information exists.

Expose observed inventory through explicit read-only observation authority.
Listing must not grant raw configuration access, reset authority, arbitrary USB
transfers or access to storage contents. Settle the inventory scope, lifetime and
delegation contract before adding an ABI; reuse existing observation facilities
where appropriate. Userland owns name lookup and formatting. Device strings and
database labels are descriptive data, never authentication, grants or driver
selection policy; escape device text for terminal output.

Package pinned upstream snapshots, preserving provenance and license notices,
at proposed paths `app://share/hwdata/pci.ids` and
`app://share/hwdata/usb.ids`. Both databases offer BSD-3-Clause or GPL-2.0-or-later;
use the BSD option. No udev hardware database, libusb port, full pciutils port or
background updater is required by these native tools. Missing database entries
must leave numeric identification usable, with device strings where available.

Downloaded source observations on 2026-10-02:

| Database | Snapshot date | Uncompressed bytes | Lines |
| --- | --- | ---: | ---: |
| `usb.ids` | 2026-06-26 | 730,605 | 25,705 |
| `pci.ids` | 2026-10-01 | 1,671,363 | 43,261 |

Together these are about 2.3 MiB of useful text for hardware naming, editor use,
buffered reads and future `wc`, `grep`, `sort` and pipeline workloads. These
observations select no permanent version or size. Pin the chosen revisions when
packaging; database updates must not break validation through frozen line counts,
specific vendor entries or copies of today's contents. Use small independently
defined inputs for parser behaviour; the real corpus is ordinary packaged data.

1. [ ] **Package the two text databases.** Choose immutable upstream revisions,
   preserve BSD notices and stage through existing asset assembly. Keep the
   files readable by ordinary text tools; no compiled database is needed initially.
2. [ ] **Expose PCI inventory and implement native `lspci`.** Settle observation
   authority first, then use existing enumeration without rescanning hardware
   from userland. Exercise known and unknown IDs and missing name data.
3. [ ] **Expose USB inventory and implement native `lsusb`.** Follow enumeration,
   showing supported and unbound devices through the same observation boundary.
   Validate topology and interface reporting alongside the selected storage device.

### Everyday pipeline performance target

Once `wc` is available, use the packaged PCI database for a small end-to-end
workload, with the proposed asset path:

```sh
cat app://share/hwdata/pci.ids | wc
wc < app://share/hwdata/pci.ids
```

The owner observed `cat ~/Downloads/pci.ids | wc` on the Linux host completing
in approximately 5 ms, with output `43261 244004 1671363` (lines, words, bytes).
That single wall-clock sample used surrounding `date +%s%3N` commands and includes
shell/timing overhead; it is context, not a precise pipeline-only baseline.

The initial Pyxis target is **under one second** for this approximately 1.6 MiB
file from the boot archive or RAM filesystem in an agreed QEMU configuration.
Exceeding it calls for investigation, not an automatic conclusion about which
subsystem failed. The pipeline exercises file reads, libc, process launch, pipe
transfers, scheduling/wakeups and counting. Comparing direct stdin redirection
helps identify the extra producer/pipe cost, without fully isolating it.

Record the input revision/hash and size, tool versions and counting semantics,
backend, QEMU resources/accelerator, nested versus host execution, and profiling
state. Repeat samples and report median/range; distinguish first reads from
cached runs and use a monotonic elapsed-time source when available. Verify counts
against the same input and agreed semantics; the observed counts above are not
permanent assertions. Keep launch and completion timing boundaries consistent.
Native disk, HOST and HTTPS need separately labelled results and targets.

This is a configuration-specific responsiveness goal, not a filesystem invariant,
a universal CI deadline or a claim of current Pyxis performance. Larger future
database snapshots require the workload/target to be reconsidered explicitly.
It does not assign a `wc` port, new benchmark infrastructure or optimisation work,
and adds no dependency to boot-image Phase A.

## Proposed staged milestones

Each stage should be assigned separately and delivered in focused PRs. USB read
support does not require completion of native writable integration.

### A. Bootable USB image

1. [ ] **Define and build an opt-in raw installation image.** Reuse existing
   kernel/archive assembly and pinned Limine. Settle image sizing and host-tool
   requirements, and populate the EFI partition plus a read-only-test Pyxis pool.
   Do not add automatic host-disk discovery or formatting of attached drives.
2. [ ] **Boot through emulated USB.** Add explicit QEMU options, verify the OVMF
   USB boot path and exercise the archive-backed shell. Preserve ordinary ISO
   boot. Document preparing the eventual selected physical target separately.

### B. Native read-only USB storage

1. [ ] **Settle controller, request and disk-selection contracts.** Inventory
   current PCI resource/interrupt/DMA facilities and block-interface coupling.
   Propose bounded request storage, timeouts, failure ownership and explicit disk
   selection. Follow the scheduler/allocation model current at implementation;
   do not preserve today's BSP placement as a new USB requirement.
2. [ ] **Bring up one xHCI controller.** Handle ownership/reset, DMA command/event
   rings, interrupt delivery and root-port state. Start with one directly attached
   supported storage device present at boot. Leave UAS, external hubs, insertion
   after boot, legacy UHCI/OHCI/EHCI and power management outside this first slice.
3. [ ] **Enumerate and transfer.** Implement control transfers, checked descriptor
   parsing, addressing/configuration and endpoint setup. Select supported
   SCSI/Bulk-Only interfaces by descriptors; unsupported devices fail explicitly.
   Support the endpoint packet/burst requirements of the chosen SuperSpeed path.
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

## Decisions before assigning implementation

- Phase A can proceed independently in a separate worktree. Confirm assignment
  and ordering of later stages relative to the existing roadmap; Phase A does
  not implicitly authorize kernel USB or writable integration.
- Settle image preparation/update scope and the device-identity/authority mapping
  as the block layer gains another backend. Never select a write target merely
  because it was enumerated first; distinguish disk identity from USB identifiers.
- Choose the initial xHCI hardware/profile and per-request resource/error contract
  after inspection. If a real target requires hubs or unsupported controller
  features, discuss that expansion rather than quietly adding them.
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
- [Intel xHCI specification](https://www.intel.com/content/www/us/en/content-details/625472/extensible-host-controller-interface-for-universal-serial-bus-xhci-requirements-specification.html).
- [Linux cache-control contract](https://docs.kernel.org/block/writeback_cache_control.html).
- [USB ID database and licensing](https://usb-ids.gowdy.us/) and its
  [text source](https://github.com/usbids/usbids).
- [PCI ID database and licensing](https://pci-ids.ucw.cz/) and its
  [text source](https://github.com/pciutils/pciids).
