# Host-backed development overlay

Status: deferred experiment, not an implementation assignment.

Try an opt-in view with a host development
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

The [host mount](../virtio-fs.md) is implemented. Settle this experiment's
contract alongside the [overlay design](../vfs.md) before implementation.

## Deferred work

[Writable host access](../virtio-fs.md) is available without an overlay. The
[user/authority checkpoint](users-and-authority.md) still
applies: explicit directory grants and a single host-service identity do not
settle future guest-user ownership or host identity mapping.

Networking, block storage, a disk format, installer, compositor and VirtIO GPU
remain separate milestones. The agreed driver order is virtio-fs, virtio-net,
then virtio-blk. PCI/queue code should be reusable where concrete needs align,
without designing every future driver in advance.
