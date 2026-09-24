# PCI, VirtIO and the first host filesystem mount

Status: agreed initial direction with focused implementation tasks below. This
document does not assign all tasks at once. Discuss unresolved choices before
implementing the affected task, and update its checkbox in the delivering PR.
See the [planning index](boot-sdk-ports.md) for sequencing.

## Completion boundary

Init mounts a QEMU host directory through virtio-fs and launches a program that
can enumerate and read `host://` using native directory/file capabilities.
Existing `ls` and `cat` should exercise that path without knowing about PCI,
VirtIO or FUSE. The host export is opt-in; the ordinary archive-backed boot stays
usable without the device or host daemon.

Virtio-fs transports file-level FUSE requests to a host service. It does not
require a guest disk format, block driver or network stack. This milestone does
not overlay or replace `app://`; the host-backed development overlay is a
[separate follow-up experiment](#follow-up-host-backed-development-overlay).

## Platform and PCI boundary

Target the existing x86_64 QEMU Q35 machine, UEFI/Limine boot and firmware-assigned
PCI resources. Discover memory-mapped PCI configuration space (ECAM) through the
ACPI MCFG table. The initial supported topology is the Q35 segment starting at
bus zero, including multifunction devices and firmware-configured bridges.
Unsupported layouts need an explicit diagnostic, not silently partial discovery.

MCFG describes configuration access; it is not the full host-bridge resource or
interrupt-routing model. Retain firmware assignments rather than implementing
ACPI AML, PCI bus numbering or resource allocation. No legacy configuration-port
fallback, hotplug or general physical-machine support is required.

Discovery reads device identity, class, header, capability and BAR information.
It must not reset devices, enable bus mastering or write all ones to BARs to
size them. Resource sizing and activation belong to the later driver-owned
resource task, with decoding controlled and register values restored correctly.

Capture needed ACPI information before replacing Limine's mappings, without
retaining firmware pointers or requiring the heap during early initialization.
Keep x86 configuration access, cache attributes and interrupt delivery in arch;
PCI, VirtIO and filesystem code should have focused subsystem homes. Do not add
a generic driver framework or speculative registration layers.

## VirtIO and memory ownership

Use VirtIO 1.4 as the specification reference, modern PCI transport and split
virtqueues. Negotiate only implemented features; reject incompatible devices
explicitly. Legacy transport, packed queues, indirect descriptors, DAX and
throughput tuning are deferred.

Use MSI-X directed to the BSP, initially one vector shared by the device's
queues. Legacy PCI INTx routing is outside the slice. Queue processing inspects
all relevant completion state; interrupt counts are not completion counts.

Kernel-owned queue and transfer buffers have explicit CPU virtual and device
physical addresses. The initial QEMU configuration has no IOMMU; translated DMA
and userspace DMA are not supported. Copy file data through owned transfer
buffers rather than publishing userspace pointers. Keep MMIO cache attributes
separate from normal RAM and use the required publication/completion barriers;
volatile access alone is not a queue-ordering contract.

Allocate on the BSP under the existing PMM/VM/heap rules. Establish persistent
device mappings and initial queue storage before application CPUs can use
their address spaces. Do not add arbitrary shared-mapping mutation without a
translation-invalidation contract or restore an HHDM dependency.

A submitted buffer remains accessible to the device until completion or a
confirmed reset has stopped that access. A timeout or exiting caller does not
authorize freeing it. Bound request sizes and descriptor use, validate returned
lengths and request identities, and unwind initialization failures explicitly.

## Execution and completion

A BSP kernel task owns queue submission and completion processing. Callers
submit work and wait; the interrupt handler records activity, acknowledges the
interrupt as required and wakes the worker. It does not allocate, parse FUSE
responses or perform filesystem operations.

The current event-wait API accepts user tasks, while kernel tasks support timed
sleep. Extend event waiting to kernel tasks in a focused prerequisite. Preserve
the wake-before-park ordering and ensure work arriving as the worker goes idle
cannot be lost. Keep device queues and request state in the owning subsystem.

Do not run blocking host I/O in the scheduler's BSP service loop. The worker
must sleep while awaiting completion so presentation and other tasks continue.
Disable interrupts only for the short operations that require it, including
existing allocator calls. This does not change allocator ownership or require
a general scheduler/service framework.

## Filesystem and mount boundary

Start with one ordinary filesystem request in flight. Provide the separate
high-priority queue required for operations such as releasing FUSE lookup
references; serialization does not remove that lifecycle responsibility.

The first client supports regular files and directories: session negotiation,
lookup, attributes needed by native operations, enumeration, open/read and
release. Select the exact FUSE version, operations, feature flags and limits
before implementing the client. Track host lookup references and open handles
separately from local capabilities; close and failure paths must release them.
Unsupported file kinds and operations must report errors rather than behave
like empty files or successful no-ops.

Translate these operations behind native directory/file objects. FUSE wire
structures, host node IDs and host permission bits must not become the Pyxis
application ABI. Integrate remote I/O with existing ownership and waiting rules;
audit executable loading and object cleanup before allowing them to block on
the host. Start with enumeration and reading, without promising executable
launch from this backend until its loading path is accounted for.

Mount management is explicit authority held by init. Init selects an export,
mounts it before launching the session and delegates the returned directory
capability under `host://`. Carry it through the current session launcher to
the shell; ordinary child launches can delegate it through existing grants.
Today's startup roots are copied bindings, not live namespace updates.

Define ownership among device, FUSE session, mount, directory/file objects and
in-flight requests. Closing the original mount handle must not invalidate
still-authorized open objects. Mount attachment below directories, public
unmounting and live namespace replacement remain later work.

## Decisions at the relevant task boundary

- PCI resources: the exact ECAM mapping strategy and supported BAR types,
  sizing procedure and MSI-X resource/vector ownership.
- Transport: queue and transfer-buffer sizes, supported feature mask and reset
  completion checks. Keep ordinary allocation failure explicit.
- Host setup: select and record a virtiofsd version and negotiated FUSE subset;
  define QEMU shared-memory/socket setup, read-only export enforcement and
  actionable dependency diagnostics. Do not automatically install host packages.
- Filesystem behavior: symlinks and special files, directory cookies, caching
  and visibility of concurrent host changes, plus native error translation.
  Guest read-only access does not mean the host tree is immutable.
- Failure behavior: deadlines, caller exit, daemon disconnection, malformed
  replies, pending-call errors and resource reclamation after reset. Automatic
  reconnection need not be part of the first client.
- Mount ABI: request shape, device/export selection, authority grants, lifetime
  and startup failure behavior. No ambient authority from knowing an export tag.

These choices need not all be settled before PCI discovery. Stop and discuss
an unresolved contract before its implementation, rather than filling it with
stubs or silently expanding the milestone.

## Focused task list

Each numbered item is an intended PR boundary, not a requirement to combine
all supporting work into one commit. Split a task further if its review scope
grows. No later item is implied by completing an earlier one.

1. [x] **PCI discovery.** Capture MCFG information, implement ECAM reads and
   enumerate the supported firmware-configured topology. Decode identity,
   capabilities and assigned BAR addresses with named fields. Completion: an
   accurate, concise PCI inventory during an otherwise normal boot; devices
   remain untouched by discovery. Implemented behavior and limits are in
   [PCI discovery](../pci.md).
2. [ ] **Driver-owned PCI resources.** Add the configuration writes and BAR
   sizing/mapping needed by the selected device, including paired 64-bit BARs
   and capability extent checks. Completion: the driver's register regions are
   mapped with explicit ownership and failure unwinding; no resource reassignment.
3. [ ] **Modern VirtIO PCI setup.** Discover transport capabilities, reset the
   selected device, negotiate supported features and inspect queue information.
   Completion: readable transport diagnostics and defined failed-init cleanup;
   do not mark the device ready before its queues and handlers are ready.
4. [ ] **Kernel task event waits.** Extend the existing prepare/park/wake
   contract to BSP kernel tasks. Completion: the upcoming worker can block on
   an event without polling sleeps or weakening wake-before-park guarantees.
5. [ ] **PCI MSI-X delivery.** Configure the selected device's table and vector,
   route it to the BSP and connect short interrupt handling to worker wakeups.
   Completion: masking, activation and teardown ordering are defined; actual
   queue-completion interrupts are exercised by the next task.
6. [ ] **Split queues and BSP worker.** Implement owned DMA buffers, descriptor
   publication, completions and worker lifecycle. Add the opt-in host-service
   setup needed for a real virtio-fs session-negotiation request. Completion:
   that request completes through the device and interrupt path while normal
   scheduling continues. No synthetic boot requests or test-only driver API.
7. [ ] **Read-only FUSE client.** Implement the selected traversal/read subset,
   high-priority reference release, bounded replies and request failure handling.
   Completion: the backend has explicit session, node and open-handle lifetimes;
   use debugger inspection as needed before public mount exposure exists.
8. [ ] **Native filesystem backend.** Connect the client to directory/file
   capabilities and their existing rights, waiting and destruction contracts.
   Completion: native lookup/enumeration/read dispatch to the backend without
   exposing FUSE details; unsupported mutations fail explicitly.
9. [ ] **Init mount and session handoff.** Add the selected mount authority and
   request, pass `host://` through session startup, and document normal host
   setup and archive-only recovery. Completion: `ls` and `cat` operate on host
   files after an ordinary boot; boot without the optional export still works.

Validate implementation PRs with ordinary builds, QEMU boots and manual debugger
inspection, using TCG and KVM where available. Do not add tests, self-tests,
fault injection, CI jobs or boot/output automation for this milestone. Preserve
the existing shell, presentation, input and archive filesystem behavior.

When complete, rewrite this document as the implemented interface and usage
reference under `docs`, update links and retain unresolved follow-ups elsewhere.

## Follow-up: host-backed development overlay

After the plain host mount works, try an opt-in view with a host development
tree above the read-only boot archive. Higher-layer files would override archive
files and missing names would fall back to the archive. Init would construct
and delegate that view as `app://`, keeping normal application paths usable.
There is no global POSIX root implied by this composition.

The host build could publish a rebuilt userspace program or port into that tree
without recreating the ISO. Guest access can remain read-only: the host writes
the updates. This is an experiment toward installation and selectable system
trees, not a new default, package manager or disk installer.

Start with additional and replacement files. Directory merging and name/type
conflicts need a contract; deletion markers, copy-up and guest-writable overlays
can follow separately. Settle host-change visibility and existing-handle behavior
before claiming that replacing a file is a live update. Host builds should
publish finished files atomically rather than expose partially written images;
multi-file updates may eventually need publication of a complete tree.

Keep the bootstrap init available from the archive and retain an archive-only
boot selection for recovery. A changed kernel ABI may still require a matching
kernel image; overlays do not make incompatible userspace builds safe. Executable
loading from host-backed files and concurrent replacement must be accounted for
before demonstrating launch through the composed view.

Promote this experiment to its own milestone after the mount works, alongside
the [overlay design](../vfs.md). It is not part of the checklist above.

## Deferred work

Writable host access needs a separate scope and the
[user/authority checkpoint](users-and-authority.md). Explicit directory grants
do not settle future guest-user ownership or host identity mapping. Accounts
are not a prerequisite for this read-only mount.

Networking, block storage, a disk format, installer, compositor and VirtIO GPU
remain separate milestones. The agreed driver order is virtio-fs, virtio-net,
then virtio-blk. PCI/queue code should be reusable where concrete needs align,
without designing every future driver in advance.

## References

- [Init setup and handoff](../init.md).
- [Virtio-fs design and FUSE transport](https://virtio-fs.gitlab.io/design.html).
- [VirtIO 1.4, Committee Specification 01](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf), especially PCI transport, split queues and the filesystem device.
- [PCI host bridges and ACPI](https://www.kernel.org/doc/html/latest/PCI/acpi-info.html).
- [MSI and MSI-X](https://www.kernel.org/doc/html/latest/PCI/msi-howto.html).
- [BSP allocation and VM constraints](../technical-debt.md#bsp-only-allocation-and-vm-mutation).
- [Existing filesystem direction](../vfs.md).
