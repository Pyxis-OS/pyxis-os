# ACPI interpreter

Caelum runs the [uACPI](../../third_party/uacpi/UPSTREAM.md) interpreter to
use ACPI devices and AML methods. Static tables that early boot needs (the
MADT, FADT flags, HPET and MCFG) are still read directly by
`arch/x86_64/acpi.c` before the CR3 switch. This page describes what the
[ACPI milestone](../wip/acpi-and-bar-widgets.md) has implemented so far: the
namespace is loaded, and no ACPI device is used yet.

## Ownership

One BSP kernel worker owns uACPI. It makes every uACPI call, runs all AML and
deferred ACPI work, and handles the SCI. Other kernel code will reach ACPI by
asking this worker. The host interface (`kernel/acpi/host.c`) asserts that
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
handler does not claim the SCI, the input stays masked: a level-triggered SCI
that nothing clears would fire again at once. This is logged, and ACPI events
stop until reboot.

## Deferred work

uACPI's deferred work, GPE methods and notifications, is queued on the worker
and runs in order at the worker's top level, never inside a wait. Running on
the BSP also meets uACPI's requirement that GPE work run on CPU 0. A request to
wait for work completion services a pending SCI, then runs every queued item,
including items they queue. Work cannot wait for work.

## Measurements

These are nested-VM numbers from the agent's development host, not ThinkPad
results:

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

## Limits

These are recorded in [technical debt](../technical-debt.md#acpi-interpreter-host-limits):

- PCI configuration writes are refused.
- The mapping window is never reused.
- AML has unrestricted port access and uncached aliases of kernel-owned
  registers.
- The SCI must share the keyboard's I/O APIC.
- Inline completion of deferred work has not yet been exercised.
