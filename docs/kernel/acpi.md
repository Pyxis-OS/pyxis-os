# ACPI interpreter

Caelum runs the [uACPI](../../third_party/uacpi/UPSTREAM.md) interpreter to
use ACPI devices and AML methods. Static tables that early boot needs (the
MADT, FADT flags, HPET and MCFG) are still read directly by
`arch/x86_64/acpi.c` before the CR3 switch. This page describes what the
[ACPI milestone](../wip/acpi-and-bar-widgets.md) has implemented so far: the
namespace is loaded, the kernel powers off and restarts through it, and it reads
the battery through the embedded controller for the space bar.

## Ownership

One BSP kernel worker owns uACPI. It makes every uACPI call, runs all AML and
deferred ACPI work, and handles the SCI. Other kernel code reaches ACPI through
BSP requests forwarded to this worker. The host interface (`kernel/acpi/host.c`) asserts that
its waiting, mapping, PCI, port and work callbacks run on that worker.

Because nothing else calls uACPI, its locks never contend with another
thread:

- A kernel mutex that is already held belongs to the worker itself. An endless
  wait for it fails with an internal error; a timed wait sleeps for the
  timeout and then times out.
- Events are counters. Only AML and the SCI handler signal them, both on the
  worker, so a waiting worker services SCIs while it sleeps. This is how the
  firmware's release of the ACPI global lock reaches a waiter.
- uACPI spinlocks are ordinary kernel spinlocks held with IF=0.

Allocation, mapping and PCI access follow the BSP kernel-task rules
([SMP](smp.md#memory-and-output-boundaries)): the host disables interrupts
around heap and VM calls. AML executes with interrupts enabled, so the
scheduler can preempt it like any other kernel worker.

## Startup

Before AP startup, `acpi_prepare()` copies the RSDP address and the firmware
memory map and reserves the mapping window. After `task_init()`,
`acpi_start()` creates the worker. The worker then:

1. calls `uacpi_initialize()`, which reads the tables and switches the
   firmware into ACPI mode if it is not there already;
2. calls `uacpi_namespace_load()`, which loads the DSDT and SSDTs and installs
   the SCI handler;
3. calls `uacpi_namespace_initialize()`, which runs `_STA` and `_INI`.

It logs one summary line with the elapsed time, uACPI's heap use and the
window pages used. A failure at any step is logged and leaves ACPI
unavailable; boot continues. GPEs and fixed events stay disabled: the
embedded controller driver and GPE enabling come with the battery task, and
the power button with its own task.

uACPI's informational messages, such as the table list and AML load
statistics, go to the trace log (`LOG_LEVEL=trace`). Warnings and errors go to
the normal kernel log with an `ACPI: warning:` or `ACPI: error:` prefix.

## Physical memory

AML and uACPI map physical memory for tables and `SystemMemory` operation
regions, including after the APs start. The worker maps it into a 64 MiB
kernel window reserved before AP startup and filled in address order:

- A page address in the window is mapped at most once and never reused, so no
  CPU can hold a stale translation and mapping needs no TLB shootdown.
- Unmapping only checks the address. A request for pages that an earlier
  mapping already covers returns that mapping.
- ACPI and firmware memory is mapped cached. Reserved memory and physical
  space the memory map does not list is mapped uncached, as device memory.
- Usable RAM, kernel, loader, framebuffer and bad memory are refused, as is a
  request that mixes classes. A refusal is logged and fails the AML access.

Device memory includes register pages the kernel itself uses, such as the
HPET, I/O APIC and local APIC. QEMU's `HPET._STA` reads the HPET this way.

## PCI configuration and I/O ports

AML reads PCI configuration space through the ECAM aperture. Segment-zero
functions outside the MCFG bus range are reported as unimplemented. Writes are
refused and logged with the function, offset and value: configuration pages of
unowned functions are read-only, and writes to functions that drivers own
would bypass their ownership.

AML may use any I/O port range that it describes. Accesses keep their exact
width and must fit the mapped range.

## SCI

Early boot copies the FADT's SCI interrupt and its MADT route. Without an
override, or for an override's conforming fields, the SCI is level-triggered
and active-low. The I/O APIC routes it to the BSP on its own vector, masked,
and the worker unmasks it when uACPI installs its handler. Only the routed SCI
is supported; uACPI interrupt handlers for other inputs, such as GPE block
devices, are refused.

SCI interrupt entry masks the input, records the event and wakes the worker.
No uACPI code runs in interrupt entry. The worker runs uACPI's handler with
IF=0, as interrupt entry would, and unmasks the input afterwards. If the
handler does not claim the SCI, the input stays masked for one second, because
a level-triggered SCI that nothing clears would fire again at once. The worker
then unmasks it from its top level. The first unclaimed SCI is logged and later
ones go to the trace log, so an SCI that keeps firing costs one worker wake-up
per second instead of disabling ACPI events until reboot.

Only the embedded controller's GPE is enabled. GPEs with AML handlers (`_Lxx`
and `_Exx` methods) stay disabled until a later ACPI use needs them.

## Deferred work

uACPI's deferred work, GPE methods and notifications, is queued on the worker
and runs in order at the worker's top level, never inside a wait. Running on
the BSP also meets uACPI's requirement that GPE work run on CPU 0. A request to
wait for work completion services a pending SCI, then runs every queued item,
including items they queue. Work cannot wait for work.

## Embedded controller and battery

The embedded controller (`kernel/acpi/ec.c`) is taken from the ECDT when the
firmware has one, before `_INI` and `_STA` run, and otherwise from the first
present `PNP0C09` device after namespace initialization: its `_CRS` gives the
data port, then the command and status port, and its `_GPE` the GPE number.
Installing the `EmbeddedControl` handler on that device runs its `_REG`
methods; on the T14, `_REG` switches the firmware's own methods from an SMI
path to EC fields.

Each EC access is one byte transaction: a read is the command, the address and
one result byte; a write is the command, the address and the value. The worker
polls the status register between bytes, waiting at most 500 ms each time. A
wider field is moved one byte at a time. A timeout fails that access; the first
one is logged and later ones go to the trace log.

The EC's GPE is edge-triggered and serves its query events. The SCI handler
queues one deferred work item and leaves the GPE disabled. That item reads
query numbers while the status register reports an event, at most 32 per item,
and runs `_Qxx` for each one on the EC device, then lets the GPE fire again. A
missing `_Qxx` is normal and goes to the trace log.

A notify handler on the namespace root receives every `Notify` and writes it to
the trace log. Nothing acts on notifications yet; without that handler, uACPI
would print a warning for each one, such as the T14's AC, USB-C and GPU
notifications when AC is plugged in or out.

`kernel/acpi/battery.c` finds up to two `PNP0C0A` batteries and the first
`ACPI0003` AC adapter. Every five seconds, at its top level, the worker:

- evaluates each battery's `_STA`;
- when a battery appears, or until its full capacity is known, reads `_BIX`, or
  `_BIF` without it, for the last full charge capacity, falling back to the
  design capacity;
- reads `_BST` for the remaining capacity and the charging bit, and the adapter's
  `_PSR`.

The percentage is the summed remaining capacity over the summed full capacity,
rounded down and capped at 100, which is how Linux computes `capacity`. The
reading is published with IF=0 on the BSP. Without a battery, or when no battery
gives a valid reading, it is not present. The kernel presenter draws it in the
[space bar](../userland/init.md#space-bar).

## Power-off and restart

The `power` capability ([ABI](../../include/abi/power.h)) carries OFF and RESTART
rights. The kernel grants both to boot init, which forwards them to spaces that
set `power = true` ([boot configuration](../userland/init.md#boot-configuration)).
The shell's `poweroff` and `reboot` builtins use it.

A call travels as the `BSP_SERVICE_POWER` request: the executor forwards it to
the ACPI worker, which runs one power operation at a time. A second request
while one runs completes with BUSY; without a running ACPI worker, because
the namespace failed to load or there is no RSDP, it completes with
UNAVAILABLE. The worker runs the operation at its top level, like deferred work:

1. It logs `power: flushing pools; powering off` (or `restarting`).
2. It holds user execution. Each user task parks at its next return to user
   mode, from a syscall, a timer preemption or the ready queue. A task already
   inside a syscall finishes that syscall first. Programs are not asked to exit;
   kernel workers keep running.
3. It asks the native filesystem worker to flush. After the requests already
   queued, that worker writes every writable pool's dirty data and checkpoints
   it until its journal is EMPTY. Pools that failed earlier or are read-only
   take no writes and are skipped. On success the pools are sealed: requests
   that could change a pool or a device fail with UNAVAILABLE, and background
   writeback stops.
4. Power-off evaluates the S5 preparation methods and enters S5 through uACPI.
   A failing `_PTS` is logged but does not stop power-off, since the pools are
   already flushed; entry itself needs a valid `_S5` sleep type.
   Restart writes the FADT reset register. If that register is absent, unusable
   (one in PCI configuration space is refused like any PCI write) or has not
   reset the machine after a second, the architecture fallback pulses the 8042
   reset line and then triple-faults the CPU.

When S5 entry returns, uACPI has already disabled the runtime GPEs and armed
only wake GPEs, so the worker runs uACPI's S5 wake path, which enables the
runtime GPEs again and runs `_WAK`.

If the flush fails, or the firmware does not power off, the pools are unsealed,
held tasks are released, `power: power-off failed (status N); the system stays
up` is logged and the call returns that status. A successful operation never
returns.

Host file systems (`host://`) and RAM volumes need no flush. Raw disk handles
used by the installer are not flushed; the installer flushes them itself.

## Measurements

On the ThinkPad T14 Gen 1 AMD (owner's PXE boots, 2026-10-07), the SCI is IRQ 9
routed to GSI 9, level-triggered and active-low. The namespace loaded without
AML errors, warnings or refusals in 52.1 ms and, on a later boot, 37.2 ms. uACPI
held 711,583 bytes in 18,519 blocks both times, and the firmware window used
2,127 pages (8.3 MiB of address space, which includes operation regions mapped
whole).
No ThinkPad baseline boot time was taken.

The QEMU comparison below uses nested-VM numbers from the agent's development
host:

- QEMU 10.2.2 with the local AHCI fix, KVM, 256 MiB, Fedora OVMF, virtio-net
  and entropy;
- five boots per configuration;
- times taken from host timestamps on serial lines.

The baseline is `5d65d05`; the implementation is that revision plus this change.

| CPUs | Measure | Baseline | uACPI |
| --- | --- | ---: | ---: |
| 1 | Kernel entry to `Caelum ready` | 174 ms (173–174) | 178 ms (177–247) |
| 1 | Kernel entry to the Remote space starting | 222 ms (220–222) | 233 ms (231–305) |
| 1 | Allocator use after boot, Fastfetch | 41.62 MiB (41.62–41.87) | 42.14 MiB (41.89–42.14) |
| 4 | Kernel entry to `Caelum ready` | 210 ms (201–211) | 216 ms (205–217) |
| 4 | Kernel entry to the Remote space starting | 256 ms (245–257) | 268 ms (255–271) |
| 4 | Allocator use after boot, Fastfetch | 41.71 MiB (all five) | 41.98 MiB (41.98–42.23) |

Each value is the median of all five boots, with the range in brackets. The
top of the 1-CPU uACPI ranges comes from one boot in which every phase,
including those before ACPI runs, was slower; the other four stayed within
177–184 and 231–237 ms.

- **Namespace load:** 1.64–1.75 ms on one CPU. On four CPUs it was 10.3–11.6 ms
  in four boots and 1.75 ms in one, while it shared the BSP with the other
  startup work.
- **Heap:** uACPI holds 106,383 bytes in 3,454 blocks on one CPU and 108,765
  bytes in 3,532 blocks on four, because QEMU describes more processors. These
  are requested sizes, before allocator overhead.
- **Firmware window:** 14 pages.
- **Kernel image:** text grew from 584,382 to 768,941 bytes, data from 1,180 to
  2,676 and bss from 393,568 to 396,272.

Most of the added boot time is serial logging rather than ACPI work. Before
`Caelum ready`, the new SCI route line and the larger kernel file account for
the difference. With uACPI's informational lines in the normal log, the
namespace took 41–61 ms to load and boot took about 40 ms longer, so those
lines now go to the trace log. Allocator use grows in 256 KiB heap pools, so it
moves in steps.

A local experiment, not part of the change, enabled the power-button fixed
event. Three `system_powerdown` presses from the QEMU monitor each delivered
one claimed SCI on CPU 0, with one and with four CPUs, and no interrupt storm.
GDB confirmed the following:

- the worker runs AML on CPU 0 with IF=1;
- the SCI redirection entry is vector 42, level-triggered, active-high (QEMU's
  override) and unmasked;
- after boot, the worker is parked, with no pending SCI or queued work.

### Power-off and restart

Nested-VM checks on QEMU 10.2.2 with the local AHCI fix, KVM, Fedora OVMF and a
disposable 64 MiB pool on partition 2 of a virtio-blk disk, booted in installed
mode (`MOUNT_DISK`) with one and four CPUs:

- `cat boot://config/live.lua > system://…` without `sync`, then `poweroff`:
  QEMU exited, and on the host `npfs-inspect` showed the file with identical
  contents and `journal_state empty`; `fsck.npfs` passed.
- The same with `reboot`: the firmware and Limine ran again through the FADT reset
  register, the next boot mounted the pool without a replay, and the file and
  empty journal checked out the same way.
- In the live image, `poweroff` from the Development shell exited QEMU, while
  `poweroff` and `reboot` from a Remote shell printed `this space has no power
  authority` and the kernel logged nothing.

On the ThinkPad T14 Gen 1 AMD (owner, 2026-10-07, installed system updated to
this build), `poweroff` turned the machine off and `reboot` restarted it. A file
edited immediately before each survived without `sync`. The journal state and
which reset path the restart took were not inspected there.

The flush-failure, firmware-failure and fallback-reset paths were not exercised.

The scheduler hold adds a check at every syscall return and user-mode timer
preemption. `iobench read bin://share/iobench.bin` (one warmup, five 1 MiB
samples, 257 reads each) compared `58ed3b9` with this change. Payload-read
medians, with ranges:

| CPUs | Before | After |
| --- | ---: | ---: |
| 1 | 0.275 ms (0.255–0.972) | 0.255 ms (0.253–0.264) |
| 4 | 0.259 ms (0.258–0.273) | 0.255 ms (0.253–0.261) |

The difference is within run-to-run variation.

### Embedded controller and battery

QEMU has neither a battery nor an embedded controller. Nested-VM checks used
the same QEMU, KVM and OVMF with 256 MiB, comparing `7e4fe0c` with this change,
three boots each:

| CPUs | Measure | Baseline | Change |
| --- | --- | ---: | ---: |
| 1 | Namespace load | 1.57–1.64 ms | 1.57–1.64 ms |
| 4 | Namespace load | 1.68–8.43 ms | 1.71–1.75 ms |
| 1 | uACPI heap | 106,135 bytes in 3,447 blocks | unchanged |
| 4 | uACPI heap | 108,517 bytes in 3,525 blocks | unchanged |
| both | Firmware window | 14 pages | 14 pages |

The 8.43 ms baseline load was one outlier boot.
Kernel text grew from 771,789 to 776,333 bytes and bss from 396,288 to 396,464.
Without a battery the space bar is unchanged.

A local test table, not part of the change, added a `PNP0C0A` battery and an
`ACPI0003` adapter through `-acpitable`. Its `_BST` drained by 4.5% per read to
zero, then charged back to full. With one and four CPUs the widget showed
`100`, two-digit values, `05%`, `01%` and `00%` with the gradient background,
and the tabs gave up its width. Under GDB, a forced unclaimed SCI logged the
error line once, and the worker unmasked the SCI 1.009 s later, woken by the
re-arm deadline rather than the next poll. A variant of the table that sent
`Notify` from `_BST` printed nothing in the normal log, and one `ACPI: Notify`
trace line per poll in a trace build.

On the ThinkPad (owner, 2026-10-07, PXE live boot with an uncommitted patch
that logged the first polls), the EC was found as
`\_SB_.PCI0.LPC0.EC0_`, ports 0x62/0x66, GPE 0x03 from `PNP0C09`, with no
timeouts. The widget matched Fedora's reading and rose on AC; after a
`poweroff` at 36%, Fedora also reported 36%. Plugging and unplugging AC ran
the EC's query methods, whose `Notify` calls uACPI then warned about; the root
notify handler was added afterwards and checked only in QEMU.

| Poll | BSP time | Firmware window | uACPI heap |
| ---: | ---: | ---: | ---: |
| Before the first poll | | 2,127 pages | 711,583 bytes |
| 0 (includes `_BIX`) | 13.4 ms | 2,144 pages | 712,320 bytes |
| 1 | 1.8 ms | 2,144 pages | 712,328 bytes |
| 2 | 8.3 ms | 2,144 pages | 712,328 bytes |

The first poll mapped 17 more pages of operation regions; the next two mapped
none. Later polls were not recorded. At up to 8.3 ms every five seconds, the
busy-waiting worker uses under 0.2% of the BSP.

## Limits

These are recorded in [technical debt](../technical-debt.md#acpi-interpreter-host-limits)
and [power-off limits](../technical-debt.md#power-off-and-restart-limits):

- PCI configuration writes are refused.
- The mapping window is never reused.
- AML has unrestricted port access and uncached aliases of kernel-owned
  registers.
- The SCI must share the keyboard's I/O APIC.
- Inline completion of deferred work has not yet been exercised.
- Power-off skips pools that already failed and cannot stop services in order.
- EC transactions busy-wait on the BSP and do not take the ACPI global lock.
- Battery changes are polled; notifications are not handled yet.

The embedded controller and battery limits are in
[technical debt](../technical-debt.md#embedded-controller-and-battery-limits).
