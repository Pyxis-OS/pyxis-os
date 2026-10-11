# NVMe block storage

Caelum prepares every observed PCI NVMe controller independently and publishes
its setup result through the [block registry](block-storage.md). Each supported
controller serves exactly one active namespace; its ID comes from Identify's
active list, not an assumption that namespace 1 exists. Unsupported neighbours
remain inventoried without disabling a working controller.

## Supported controller profile

The driver requires NVMe 1.3 or 1.4, NVM commands, 4 KiB controller pages,
MSI-X, and one active namespace with 512-byte or 4 KiB logical blocks. Metadata,
protection information, thin capacity, multiple namespaces and advertised normal
shutdown times above five seconds are refused. Transfers are at most 64 KiB,
reduced by MDTS. MDTS zero means no advertised limit, not zero capacity.

Before AP startup, it reads CAP/VS through a bounded BAR0 mapping, halts the
firmware controller before BAR sizing, validates doorbell and MSI-X extents,
and prepares contiguous DMA. There are 16-entry admin queues and one I/O queue
pair of up to 16 entries, reduced to CAP.MQES. At most eight captured request
slots fit that pair. Each slot has separate data and a page of PRP entries;
64 KiB requests use PRP1 plus a list of the remaining 15 pages.

Controller readiness uses CAP.TO's 500 ms units (a conservative 500 ms minimum).
Admin and published I/O commands have five-second deadlines. One BSP worker
owns queue processing; MSI-X only wakes workers. A completion must match the
queue, phase, live CID and consumed submission sequence before returning DMA.
Queue storage, claims and mappings remain retained until reboot, including
partial preparation or retirement after failure. No hotplug, additional I/O
pairs, power-state programming, discard or namespace management is provided.

## Write authority

Absent `nvme.write=1`, kernel WRITE and FLUSH admission returns READ_ONLY. This
boot gate applies independently of userspace grants. With it, filesystem writes
still require the existing trusted native-mount authority. A write captures its
bytes at admission; a later successful NVMe Flush supplies the durability fence.
Tickets, abandonment and write-failure latching follow the existing block
contract; caller timeouts never recall device-owned DMA.

Until coexistence task 3, [installer raw authority](installer-authority.md)
permits whole-disk claims only after a fresh, bounded scan proves every byte
zero. Nonzero or uncertain media refuse. NVMe partition claims also refuse in
this slice, so the current installer cannot finish its ESP step on NVMe. Booting
an already prepared disk and mounting its pool does not require a raw claim.
This restriction is temporary, not permission to wipe the ThinkPad.

## Health and shutdown

A read-only SMART Get Log Page is printed at preparation and again during normal
shutdown. It reports full 128-bit data-unit counters (512,000 bytes per unit),
percentage used, spare/threshold, temperature in kelvin, critical warnings and
unsafe shutdowns. It logs PCI location, not serial/model fields. QEMU's mostly
zero health values do not qualify the physical controller's wear accounting.

[Clean power operations](../kernel/acpi.md) first flush/seal npfs pools, seal
admission on all NVMe controllers, drain existing requests, delete the I/O queues
and request normal shutdown with CC.EN retained. Success requires CSTS.SHST=2
and masked MSI-X. The combined operation has a 20-second budget; the shutdown
notification wait uses at least RTD3E and at least one second. A shortened
remaining budget can only refuse clean power, never falsely confirm shutdown.
The controller worker owns retirement and completion of pending tickets. All
resources stay pinned; controllers never resume before reboot.

An unconfirmed DMA-owning controller, including an unsupported controller that
needed boot admin DMA, refuses clean power-off/restart. The local Ctrl+Alt+Del
overlay displays failure and an unsafe-shutdown warning, keeps Cancel selected,
and offers **Force off / Force reboot**. Earlier queued input and held/repeated
activation cannot force it: the warning must be presented and a fresh activation
must follow. Force bypasses storage checks and may lose data. Forwarded POWER
requests and the short physical power-button path remain clean-only.

## Qualification and sources

[Task 1's record](../development/experiments/nvme-task1/README.md) distinguishes
QEMU/GDB checks from the owner's pending read-only ThinkPad boot. Native writes
require separate owner authorization. No compiler/container rebuild or userspace
ABI change is needed.

Register/command formats follow [NVM Express 1.3](https://nvmexpress.org/wp-content/uploads/NVM_Express_Revision_1.3.pdf)
(queues/completions, PRPs, Identify, SMART and normal shutdown). Development uses
[QEMU 10.2.2's documented NVMe device](https://github.com/qemu/qemu/blob/v10.2.2/docs/system/devices/nvme.rst).
The driver is original MPL-2.0 code; no external driver code is vendored.
