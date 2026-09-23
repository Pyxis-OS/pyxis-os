# PCI, VirtIO and the first host filesystem mount

Status: working milestone draft, not an implementation assignment. Agreed
direction is distinguished from proposals and open decisions below. See the
[planning index](boot-sdk-ports.md) for sequencing.

A likely dependency order is PCI discovery, modern VirtIO PCI transport,
virtqueues/DMA ownership/completion handling, then a small virtio-fs client.
The agreed first slice is a read-only host directory exposed as `host://`,
providing lookup/enumeration/read before writable access or performance work.
Select the supported protocol and operation subset explicitly when defining
that milestone.

Virtio-fs carries FUSE requests to a host filesystem service. It gives useful
host access without choosing an on-disk format or first implementing networking.
It still needs concrete directory/file backend integration and mount exposure.

Mount management is explicit authority held by init. Init mounts the host tree
before launching the shell and passes its directory root to the session; normal
child launches can then delegate access through existing grants. Today's startup
roots are copied bindings, so this does not update already-running processes.

Attaching mounts beneath directories, live namespace updates and unmounting are
later work. Define backend/root ownership and open-handle lifetime for the initial
mount without requiring those features.

## Completion boundary

Init mounts a QEMU host directory through virtio-fs and launches a program that
can enumerate and read `host://` using native directory/file capabilities.
Writable host access needs a separate explicit scope.

PCI discovery, VirtIO transport, queue ownership and filesystem integration are
focused steps within this milestone, not a single large driver PR. Define those
steps once the mount and device contracts are settled.

## Decisions before implementation

- Select initial PCI/VirtIO transport, interrupt and DMA ownership constraints.
- Define the FUSE protocol/operation subset and host-service setup.
- Define the mount-management request, root ownership and failure unwinding.

Networking, block storage, a disk format, installer, DAX and performance tuning
are outside this milestone. The agreed driver order is virtio-fs, virtio-net,
then virtio-blk; later drivers get their own milestones when selected.

## References

- [Init setup and handoff](init-and-scripts.md).
- [Virtio-fs design and FUSE transport](https://virtio-fs.gitlab.io/design.html).
- [Existing filesystem direction](../vfs.md).
