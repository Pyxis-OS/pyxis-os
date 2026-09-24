# Virtio-fs transport and host setup

The optional virtio-fs device currently establishes a FUSE session with a host
service. It does **not** expose a mount or `host://` yet. Ordinary boot needs
neither the device nor the daemon. Remaining client/backend/mount work is in
[the milestone](wip/virtio-fs.md).

## Start the host service

The exercised host combination is Linux, QEMU 10.2.2 and virtiofsd 1.14.0.
The guest targets FUSE 7.38 with no optional FUSE features. Install virtiofsd
through your host distribution if needed; it is often separate from QEMU and
may live at `/usr/libexec/virtiofsd`. The build does not download or start it.

Choose a directory to export. In one terminal, run the daemon in the foreground:

```sh
export_dir="$HOME/pyxis-share"
socket_dir="${XDG_RUNTIME_DIR:-/tmp}/pyxis-virtiofs-$(id -u)"
mkdir -p "$export_dir"
(umask 077; mkdir -p "$socket_dir")
chmod 700 "$socket_dir"

unshare -Ur -- /usr/libexec/virtiofsd \
  --shared-dir "$export_dir" \
  --socket-path "$socket_dir/fs.sock" \
  --readonly \
  --sandbox namespace \
  --inode-file-handles=never \
  --no-announce-submounts \
  --rlimit-nofile=0
```

Change the executable path if your distribution installs it elsewhere. `unshare`
comes from util-linux. This command requires unprivileged user namespaces; the
host user appears as root inside that namespace without gaining host-root
access. The daemon can access only files that the launching host user can access.
Use the same host user for QEMU so it can connect to the socket. The private
socket directory restricts access to that user.

`--readonly` enforces read-only access at the host service, independently of the
guest implementation. `--inode-file-handles=never` uses file descriptors rather
than privileged filesystem handles. Submount announcements are disabled because
the guest does not negotiate them. `--rlimit-nofile=0` keeps the inherited host
file-descriptor limit; it does not set the limit to zero.

## Connect QEMU

In another terminal, from the Pyxis repository:

```sh
socket_dir="${XDG_RUNTIME_DIR:-/tmp}/pyxis-virtiofs-$(id -u)"
make run CPUS=4 VIRTIO_FS_SOCKET="$socket_dir/fs.sock"
```

Usual toolchain, firmware, display and accelerator overrides still apply.
`make debug` accepts the same socket option. The script checks that the socket
exists and adds a shared memfd RAM backend plus a modern `vhost-user-fs-pci`
device tagged `pyxis-host`. The memfd size follows `MEMORY`. Socket paths must
not contain commas, which delimit QEMU options.

Successful initialization logs the prepared queue addresses followed by a FUSE
session-ready message. The shell remains on CPU 1 when available; Super+Right
selects that tab. A successful handshake does not yet let the shell read the
exported directory.

Exit QEMU with Ctrl-a x in its serial terminal, or use the monitor's `quit`.
Stop the daemon with Ctrl+C if it remains running. After **both** have stopped,
remove any leftover socket before starting a fresh pair:

```sh
rm -f "$socket_dir/fs.sock"
```

Start a new daemon for each QEMU run. Do not remove a socket belonging to a live
service. Omitting `VIRTIO_FS_SOCKET` returns to the ordinary archive-only boot.

If `unshare` reports `Operation not permitted`, the host disallows this namespace
setup; enable it through your host's policy or use an administrator-managed,
read-only virtiofsd service. If QEMU reports connection refused, check the daemon
terminal and remove a stale socket only after confirming that the daemon stopped.
If the daemon reports too many open files later, increase its inherited file
limit before launch. No socket option weakens the daemon's export permissions.

## Queue and worker contract

`virtio_fs_pci_prepare` allocates and maps both queues on the BSP before AP
startup. Each owns a contiguous 16 KiB physical extent: one page of ring storage,
a 4 KiB request buffer and an 8 KiB reply buffer. Mappings are kernel-only,
read/write, non-executable, ordinary write-back RAM. Device MMIO remains uncached.
CPU virtual addresses are never used as descriptor addresses.

Each split queue selects 16 descriptors, or the device's smaller supported
power-of-two size; fewer than two is unsupported. One direct descriptor chain
may be in flight per queue. There is no indirect-descriptor or event-index
negotiation. The high-priority queue is configured but has no requests until the
FUSE client needs interrupt/forget operations.

Submission publishes the request and descriptors before the available index,
then uses a full ordering barrier before inspecting device notification
suppression and sending the 16-bit queue notification. Driver interrupt
suppression stays clear. Completion observes the used index before consuming
its entry and reply. It validates the index advance, descriptor head and written
length against the outstanding buffer; a malformed completion stops the device.
These barriers rely on coherent x86 DMA and ordinary write-back RAM, without an
IOMMU. Buffers remain device-owned until completion or a confirmed reset.

After task initialization, `virtio_fs_pci_start` creates one BSP kernel worker.
The worker enables bus mastering, sets `DRIVER_OK`, then unmasks MSI-X entry zero
and the function. It sends one real `FUSE_INIT` request on the ordinary queue.
The FUSE layer checks the reply length, request identity, error, protocol version
and feature flags. Request/reply data is copied through the owned DMA buffers;
no private task stack is published to the device.

The worker inspects both queues and device status, then sleeps for interrupt
activity or the five-second monotonic request deadline. IRQ entry only remembers
activity and detaches/wakes its waiter. The BSP-local check/publication handoff
runs with IF=0 and preserves interrupts arriving before parking. The worker runs
with IF=1 otherwise; presentation and userspace continue to be scheduled.
After successful INIT it retains the session and sleeps indefinitely for device
activity. There are no public filesystem callers or background polling requests.

## Failure and lifetime

Before AP startup, failed preparation disables MSI-X and confirms reset before
unmapping and freeing queue storage. An unconfirmed reset retains the claim and
storage. DMA has not been enabled at that point.

A runtime timeout, malformed reply, unexpected completion, status change or
configuration-generation change makes the session unavailable. The worker masks
delivery, disables MSI-X and bus mastering, requests reset, and waits up to one
second while yielding between reset-status reads. It reports whether stopping
was confirmed, then exits with no published waiter. Static interrupt state stays
valid for a late APIC delivery.

Runtime failure retains the device claim, DMA storage and mappings until reboot,
even after confirmed reset. Shared kernel mappings must remain stable while APs
run; this slice does not add TLB shootdowns or reconnection. An idle disconnected
daemon may not generate a guest-visible event; disconnection is otherwise detected
through device status or a request deadline, not a heartbeat. See
[the recorded lifetime tradeoff](technical-debt.md#virtio-fs-runtime-resource-retention).

## Debugger inspection

Use `make debug` with the socket option and connect GDB to `build/caelum.elf`.
Break at `boot_start_cpus` to inspect `filesystem.hiprio` and `filesystem.request`
after preparation; DMA and interrupt delivery are still disabled there. Each
queue records its kernel mapping and physical base separately.

A hardware breakpoint on `virtio_fs_pci_interrupt` observes real MSI-X delivery.
The worker may already have a published waiter, or the interrupt may arrive
before it parks; both are normal. Inspect the used ring and reply buffer without
calling inferior functions under KVM. After initialization, inspect
`filesystem.session`, `filesystem.interrupt_wait` and the queue indices. The
worker should have no request in flight and should be sleeping. Read MMIO at its
individual register widths; see [PCI inspection](pci.md#inspection-and-next-step).

References: [VirtIO 1.4 split queues and filesystem device](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf),
[virtiofsd 1.14.0 FUSE definitions](https://gitlab.com/virtio-fs/virtiofsd/-/blob/v1.14.0/src/fuse.rs),
[virtiofsd host setup](https://gitlab.com/virtio-fs/virtiofsd/-/tree/v1.14.0).
