# NVMe block storage

Caelum prepares every observed PCI NVMe controller independently and publishes
its setup result through the [block registry](block-storage.md). Each supported
controller serves one namespace as its block device: the first eligible entry of
Identify's active list, not an assumption that namespace 1 exists. Unsupported
neighbours remain inventoried without disabling a working controller.

## Supported controller profile

The driver requires NVMe 1.3 or newer with no version ceiling, an I/O controller,
NVM commands, 4 KiB controller pages, MSI-X or MSI, and at least one eligible
active namespace. Advertised normal shutdown times above five seconds are
refused. Transfers are at most 64 KiB, reduced by MDTS. MDTS zero means no
advertised limit, not zero capacity.

## Namespaces

A namespace is eligible with 512-byte or 4 KiB logical blocks and no metadata,
protection information or thin capacity. Command-set-capable controllers must
also identify it as NVM (CSI 0). The driver examines the active list in order,
at most eight entries, and serves the first eligible namespace. Each ineligible
namespace before it is logged as refused with its reason. Later active
namespaces are logged as not served, without being examined. Only when none of
the examined namespaces is eligible is the controller unsupported. An admin
command failure while examining a namespace remains a controller setup failure,
and a nonzero entry after the list's end makes the list malformed.

The served namespace's GPT, pool and raw claims behave as on a single-namespace
controller. Other namespaces have no block device, so their partitions are
neither discovered nor writable.

Before AP startup, it reads CAP/VS through a bounded BAR0 mapping, halts the
firmware controller before BAR sizing, validates doorbell and MSI-X extents,
and prepares contiguous DMA. There are 16-entry admin queues and one I/O queue
pair of up to 16 entries, reduced to CAP.MQES. At most eight captured request
slots fit that pair. Each slot has separate data and a page of PRP entries;
64 KiB requests use PRP1 plus a list of the remaining 15 pages.

When CAP.CSS offers NVM-only operation, CC.CSS selects it. Otherwise the driver
selects the I/O-command-set mode only after Identify/Get Features prove the
current profile includes NVM; it never changes that profile. Admin-only
controllers refuse, and ZNS namespaces are ineligible. Format indices use all
six FLBAS bits within the 64-entry bounds; additional advertised formats alone
do not make a namespace ineligible. Legacy unreported controller type is
accepted only before NVMe 1.4.

Ready-with-media is selected (CC.CRIME=0). Enable/initial firmware readiness
uses CRTO when CAP.CRMS supplies it; the inherited firmware wait honors its
CRIME mode. Disable uses CAP.TO. Every ready wait uses a conservative 500 ms
minimum and a 127.5-second software ceiling even if CRTO advertises more.
Reset/CRIME may change CAP.TO: it is re-read before enabling, while changes to
other capabilities refuse preparation. Runtime checks use that final snapshot.
NVMe 2.0 requires the advertised ready-with-media capability. HMB stays disabled;
controllers must operate without it, with possible DRAM-less performance cost.
Admin and published I/O commands have five-second deadlines. One BSP worker
owns queue processing; interrupts only wake workers. A completion must match the
queue, phase, live CID and consumed submission sequence before returning DMA.
Queue storage, claims and mappings remain retained until reboot, including
partial preparation or retirement after failure. No hotplug, additional I/O
pairs, power-state programming, discard or namespace management is provided.

## Interrupts

MSI-X entry zero is used when the function has an MSI-X capability. Otherwise
the driver enables one MSI message through the shared PCI helper, which accepts
32- or 64-bit MSI with optional per-vector masking. Preparation leaves delivery
disabled and supported messages masked; enable unmasks only message zero. Pin interrupts are not routed, so a
controller without a usable MSI or MSI-X capability is unsupported. Both modes
use the shared static NVMe vector, and its handler wakes every active worker.

Single MSI follows the interrupt status rules of NVMe 1.3 section 7.5.1: a
message is sent only when unmasked status rises or when a set status is
unmasked, and status stays set until every completion is acknowledged. The
handler therefore masks vector zero through INTMS. The worker drains its
completion queue and unmasks through INTMC before it sleeps; completions posted
in the meantime send a fresh message. MSI-X needs neither register.

## Write authority

NVMe WRITE and FLUSH admission is enabled by default. `nvme.write=0` makes
both return READ_ONLY independently of userspace grants; use it for read-only
development/PXE boots. Enabling the driver path does not grant write authority:
filesystem writes still require the existing trusted native-mount authority. A write captures its
bytes at admission; a later successful NVMe Flush supplies the durability fence.
Tickets, abandonment and write-failure latching follow the existing block
contract; caller timeouts never recall device-owned DMA.

Ordinary whole-disk claims still require a fresh, bounded zero-data scan on
NVMe; ordinary partition claims refuse. The trusted
[installer authority](installer-authority.md) instead provides explicitly
reviewed single-use erase/ESP-rebuild grants, GPT append, bound pool opening
and native FAT views. Those selected-partition paths preserve the existing GPT
and extent exclusion contracts and permit the installer's ESP step on scratch
NVMe media without broad writable OPEN. Booting an already prepared disk and
mounting its pool requires no raw claim. The boot write gate remains independent;
physical-media writes still require separate owner authorization.

## Health and shutdown

A read-only SMART Get Log Page is printed at preparation and again during normal
shutdown. It reports full 128-bit data-unit counters (512,000 bytes per unit),
percentage used, spare/threshold, temperature in kelvin, critical warnings and
unsafe shutdowns. It logs PCI location, not serial/model fields. QEMU's mostly
zero health values do not qualify the physical controller's wear accounting.

[Clean power operations](../kernel/acpi.md) first flush/seal npfs pools, seal
admission on all NVMe controllers, drain existing requests, delete the I/O queues
and request normal shutdown with CC.EN retained. Success requires CSTS.SHST=2
and disabled MSI or masked MSI-X. The combined operation has a 20-second budget; the shutdown
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
QEMU/GDB checks from the owner's read-only ThinkPad boot, which passed on
2026-10-11. The 2026-10-21
[ThinkPad native round](../development/experiments/grub-limine-native.md) qualifies
manual Coexist Install/Update and normal/rescue boots on its internal NVMe.
Other native write scopes require separate approval; vela remains unqualified. Namespace selection and the MSI path were
checked on QEMU 10.2.2 on 2026-10-15. Stock QEMU's NVMe device offers only pin
interrupts and MSI-X, so the MSI runs used a local, uncommitted patch that adds
a single-message MSI capability following section 7.5.1. No compiler/container rebuild or userspace
ABI change is needed.

Register/command formats follow [NVM Express 1.3](https://nvmexpress.org/wp-content/uploads/NVM_Express_Revision_1.3.pdf)
(queues/completions, PRPs, Identify, SMART and normal shutdown), plus
[Base 2.0c](https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-2.0c-2022.10.04-Ratified.pdf)
(readiness, controller/namespace type and I/O profiles) and
[NVM command set 1.0c](https://nvmexpress.org/wp-content/uploads/NVM-Express-NVM-Command-Set-Specification-1.0c-2022.10.03-Ratified-1.pdf)
(namespace layout). Specifications © 2008 to 2022 NVM Express, Inc. ALL RIGHTS RESERVED.
Development uses
[QEMU 10.2.2's documented NVMe device](https://github.com/qemu/qemu/blob/v10.2.2/docs/system/devices/nvme.rst).
The driver is original MPL-2.0 code; no external driver code is vendored.
