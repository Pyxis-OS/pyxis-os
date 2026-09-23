# PCI, VirtIO and the first host filesystem mount

Status: working milestone draft, not an implementation assignment. Agreed
direction is distinguished from proposals and open decisions below. See the
[planning index](boot-sdk-ports.md) for sequencing.

A likely dependency order is PCI discovery, modern VirtIO PCI transport,
virtqueues/DMA ownership/completion handling, then a small virtio-fs client.
A read-only shared-directory slice can establish lookup/enumeration/read before
writable coherency, DAX or performance work. Select the supported protocol and
operation subset explicitly when defining that milestone.

Virtio-fs carries FUSE requests to a host filesystem service. It gives useful
host access without choosing an on-disk format or first implementing networking.
It still needs concrete directory/file backend integration and mount exposure.

Mount management should be an explicit authority held by init, not granted to
any process that knows a mount name. Decide whether the first operation binds a
new scheme root (for example host://), attaches beneath an existing directory,
or both. Define namespace ownership, visibility to existing processes, lifetime
of open handles and unmount behavior. Today's startup roots are copied bindings,
so mounting in init does not automatically update previously launched processes.
Mount-before-launch can keep the first slice simple.

## Completion boundary

Init mounts a QEMU host directory through virtio-fs and launches a program that
can enumerate and read it using native directory/file capabilities. A read-only
first slice is proposed; writable host access needs a separate explicit scope.

PCI discovery, VirtIO transport, queue ownership and filesystem integration are
focused steps within this milestone, not a single large driver PR. Define those
steps once the mount and device contracts are settled.

## Decisions before implementation

- Select initial PCI/VirtIO transport, interrupt and DMA ownership constraints.
- Define the FUSE protocol/operation subset and host-service setup.
- Settle mount authority, namespace ownership and propagation to children.

Networking, block storage, a disk format, installer, DAX and performance tuning
are outside this milestone. The agreed driver order is virtio-fs, virtio-net,
then virtio-blk; later drivers get their own milestones when selected.

## References

- [Init setup and handoff](init-and-scripts.md).
- [Virtio-fs design and FUSE transport](https://virtio-fs.gitlab.io/design.html).
- [Existing filesystem direction](../vfs.md).
