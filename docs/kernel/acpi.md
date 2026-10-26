# ACPI interpreter

Caelum runs the [uACPI](../../third_party/uacpi/UPSTREAM.md) interpreter to
use ACPI devices and AML methods. Static tables that early boot needs (the
MADT, FADT flags, HPET and MCFG) are still read directly by
`arch/x86_64/acpi.c` before the CR3 switch. Through uACPI the kernel powers
off and restarts cleanly, also on a power-button press, and reads the battery
and AC adapter through the embedded controller for the
[space-bar widget](../userland/init.md#space-bar) and the
[`system_info` power queries](../interfaces/system-information.md#power-and-batteries).
A [UCSI client](#usb-type-c-connector-interface-ucsi) reads and acknowledges
USB Type-C connector changes.

The ACPI milestone (2026-10-06 to 2026-10-07) chose uACPI, then added clean
power-off and reboot, the battery widget, the power button and the userspace
battery queries. Its policy choices, all made by the owner:

- **One owner.** A single BSP worker owns uACPI, AML, the SCI and deferred work;
  other code reaches it through BSP requests.
- **Firmware access.** Firmware memory is mapped once in a window reserved
  before AP startup; AML may read PCI configuration but not write it.
- **Power authority.** The `power` capability (OFF, RESTART) goes to spaces
  configured with `power = true`, never to remote shells, and the shell keeps
  it for its builtins. A physical button press needs no capability.
- **What "clean" means.** User tasks are held, not asked to exit; native FAT
  operations drain and retained RW mounts are flushed/sealed before npfs pools,
  NVMe shutdown and S5/reset. An unresolved FAT mutation or failed storage flush
  keeps the system up; held detached FAT chains alone do not.
- **Events.** The embedded controller's GPE, applicable fixed power event and
  device power notifications are supported. Additional button GPEs require
  runtime-route AML proof; `_PRW` alone is insufficient. No general runtime-GPE
  initialization. An unclaimed SCI is re-armed after one second.
- **Battery data.** The kernel polls every five seconds. The widget is always
  three characters on a red-to-green gradient; userspace reads firmware
  values, unconverted, through `system_info` READ. Fastfetch has no Power
  Adapter module, since ACPI reports no wattage.

## Ownership

One BSP kernel worker owns uACPI. It makes every uACPI call, runs all AML and
deferred ACPI work, and handles the SCI. Other kernel code reaches ACPI through
BSP requests forwarded to this worker. The host interface (`kernel/acpi/host.c`) asserts that
its waiting, mapping, PCI, port and work callbacks run on that worker.

Because nothing else calls uACPI, its locks never contend with another
thread:

- uACPI builds its mutexes and events on kernel semaphores, which are unit
  counters. AML and the SCI handler signal them, both on the worker, so a
  waiting worker services SCIs while it sleeps. This is how the firmware's
  release of the ACPI global lock reaches a waiter.
- AML mutexes count recursion by thread inside uACPI, so a semaphore without a
  unit can only gain one from an SCI the worker services. A timed wait sleeps
  until then or until the timeout. An endless wait for a semaphore that no SCI
  will signal blocks the worker, which keeps servicing SCIs; the host cannot
  tell such a wait apart and reports no error.
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
3. installs the bounded legacy `SystemCMOS` handler, unless FADT declares no
   CMOS RTC, then calls `uacpi_namespace_initialize()` to run `_STA` and `_INI`.

It logs one summary line with the elapsed time, uACPI's heap use and the
window pages used. A failure at any step is logged and leaves ACPI
unavailable; boot continues. GPEs and fixed events stay disabled until the
worker starts the embedded controller and the power button, after loading.

After EC, battery and button setup, the worker makes one HID-I2C discovery pass
before publishing readiness. This does not replay `_INI`, run power methods or
touch the I2C/GPIO controller. See [the vela task](../wip/vela-i2c-touchpad.md).

uACPI's informational messages, such as the table list and AML load
statistics, are debug records, read with `log`. Warnings and errors are logged
at those levels, keeping their `ACPI: warning:` or `ACPI: error:` prefix.

## HID-I2C endpoint snapshot

[`acpi_hid_i2c_snapshot`](../../include/kernel/acpi/hid_i2c.h) copies a private,
immutable result on the BSP with interrupts disabled. Only `READY` copies an
endpoint; `PENDING`, `ABSENT` and `REFUSED` leave the caller's output untouched.
No interpreter nodes, strings or resource buffers escape the ACPI worker.

Discovery currently selects one `ELAN06FA` device with `PNP0C50` compatibility,
requires present/enabled/functioning `_STA`, one consumer/controller-initiated
I2C resource, and one consumer GPIO interrupt or extended IRQ. Canonical source
paths, source indices, addressing/speed/sharing, interrupt electrical attributes
and the HID-I2C `_DSM` revision-1 descriptor register are copied before releasing
uACPI allocations. A source-bearing IRQ remains a source-relative interrupt,
not a direct GSI. Neither a GPIO pin nor a PCI BDF is guessed from Linux output.

Missing, malformed, duplicate or unrepresentable resources refuse the whole
result with one info diagnostic naming the step; an admitted endpoint gets one
summary. Absence is quiet. The 256-byte path capacity is an internal bound, not
a firmware/ABI limit. The snapshot implies neither a controller claim nor a
usable clock/power state, GPIO ownership or a successful HID descriptor read.

The initial LPSS attachment resolves the controller's `_ADR` and its immediate
PCI root's `_SEG`/`_BBN` on this worker, rejecting bridges, nonzero segments and
out-of-range addresses before narrowing. Only owned BDF/timing scalars reach
the native driver. `SSCN`/`FMCN`, when present, must contain three representable
integers; absent counts use the supported controller's checked clock profile.
The native owner must match that exact BDF to its already prepared claim before
starting a descriptor-read worker. No AML object or mapping window is lent.

## SCI routing

FADT/MADT SCI routing is independent of i8042 presence and the keyboard route.
Boot maps each distinct I/O APIC needed by the keyboard, mouse and SCI once,
uncached; all external destinations remain the BSP's eight-bit physical APIC ID.
A pin outside its controller's redirection range or conflicting electrical
settings is refused with a diagnostic.

Compatible shared pins use one vector and dispatch every enabled consumer.
PS/2 consumption is tracked across polling and interrupt entry so a drained,
pending input delivery can still claim the shared interrupt. The ACPI worker
always checks its own sources; an interrupt claimed by PS/2 is not an unclaimed
SCI. A genuinely unclaimed interrupt retains the one-second re-arm rule. While
SCI service or its backoff masks a shared pin, other consumers of that physical
pin wait too. Uninstalling SCI preserves another consumer's enabled route.
Separate-controller and shared-pin native hardware remain unqualified; see the
[QEMU/source record](../development/experiments/hardware-recovery/README.md).

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

## SystemCMOS

`kernel/acpi/cmos.c` attaches firmware `SystemCMOS` regions before namespace
initialization. The x86 owner in `arch/x86_64/cmos.c` validates the entire byte
range in [0,128), accepting widths 1/2/4/8 only, before touching ports 0x70/0x71.
Transfers use little-endian consecutive register bytes; the callback offset is
already absolute. Invalid regions/accesses fail without partial I/O. FADT's
CMOS-RTC-absent flag withholds this legacy handler.

One short BSP IF-disabled transaction protects the index/data sequence. There
are no kernel RTC readers today (UTC comes from Limine); future callers must
share this owner, and NMI handlers must not access CMOS. The owner accepted
2026-10-15 that index writes keep bit 7 clear, leaving legacy NMIs unmasked.
There is no attempt to read or preserve an unknown firmware NMI mask, no second
CMOS bank and no RTC interrupt/alarm service.

Vela's `_Q18` reads raw hour/minute/second CMOS bytes into EC shared memory,
then sets its completion flag. The handler supplies those bytes rather than
inventing values or hiding a failed handshake. The
[qualification record](../development/experiments/vela-system-cmos/README.md)
separates synthetic QEMU behavior from pending native `_Q18` evidence.

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
the BSP also meets uACPI's requirement that GPE work run on CPU 0.

Each top-level turn runs at most **8** queued items, then handles power
requests, a button press and the battery and UCSI polls before the next batch.
Firmware that queues work faster than it drains, for example an SCI raised on
every EC query, therefore cannot keep the worker from those. A turn with work
still queued does not sleep.

A request to wait for work completion services a pending SCI, then runs the
work already queued; items queued meanwhile wait for the next top-level turn.
Work cannot wait for work.

The power button is disarmed during startup and during a power operation, and
presses delivered then are dropped. Before arming, the worker drains: it keeps
servicing SCIs and running deferred work, including work those queue, until no
work is queued and no SCI is pending, for at most **1 s**. An SCI still in the
interrupt controllers also counts as pending: the I/O APIC's delivery-status bit
or the SCI vector in the local APIC's IRR. It checks for quiet and arms in one
IF=0 section. Any later SCI or work is therefore a fresh event,
whatever kind of work delivers it. This covers both a boot-latched GPE method
that notifies the power button and a press that would otherwise be replayed
after a failed power operation. If events are still arriving at 1 s (a firmware
storm), the worker arms anyway and logs `ACPI: events still arriving after 1 s;
power button armed anyway` once per boot. A boot-latched press can then still
count. The bound can also run long, since a work item already running finishes
first.

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
queues the EC's one work item, allocated when the EC starts, and leaves the
GPE disabled. That item reads
query numbers while the status register reports an event, at most 32 per item,
and runs `_Qxx` for each one on the EC device, then lets the GPE fire again. A
missing `_Qxx` is normal. Query completion status, including success, goes to
the trace log (`LOG_LEVEL=trace`), without a normal-boot completion line.

A notify handler on the namespace root receives every `Notify`, including those
with a device-specific handler, and writes it to the trace log. Power-button
handlers act only on their admitted power events; other notifications remain
trace-only. The root listener also prevents uACPI warning for unhandled events,
such as the T14's AC, USB-C and GPU notifications when AC is plugged in or out.

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

Each poll also publishes one record per battery and the AC state for the
`system_info` [power queries](../interfaces/system-information.md#power-and-batteries):
the `_BST` values, the information package's capacities, cycle count and
strings, and that battery's own percentage. The BSP executor copies them with
IF=0; no AML runs for a query. Polling evaluates the same methods as before.

## USB Type-C connector interface (UCSI)

`kernel/acpi/ucsi.c` is a minimal UCSI client: it reads connector status and
acknowledges what the PPM (the platform's power-delivery manager) reports, and
asks for nothing else. The owner accepted its scope on 2026-10-15 after the
[vela UCSI observation](../development/experiments/vela-system-cmos/README.md#cci-interpretation-and-accepted-follow-up),
where firmware kept repeating an unacknowledged connector change. It sends no
role, power, alternate-mode or connector-reset command, changes no USB data
path or SCI/GPE behavior, and has no userspace interface.

**Transport.** The client binds the first present device with `PNP0CA0` as its
`_HID` or `_CID`. That device's `_CRS` must give a fixed 32-bit memory range of
at least 32 bytes, the mailbox, which is mapped through the
[firmware window](#physical-memory). Its `_DSM` (UUID
`6f8398c2-7ca4-11e4-ad36-631042b5008f`, revision 1) must offer:
- function 1, which copies CONTROL to the platform and rings its doorbell;
- function 2, which copies CCI and MESSAGE IN back into the mailbox.

Function 0's support mask must be a Buffer. Functions 1 and 2 act through the
mailbox, so, as in Linux's `ucsi_acpi_dsm()`, any returned object is accepted
(the ThinkPad returns an Integer). Only a failed evaluation or a missing object
counts as failure. The client accepts UCSI 1.x
(VERSION `0x01MN`), whose versions 1.0 through 1.2 share the layout it uses:
VERSION at 0, CCI at 4, CONTROL at 8, 16 bytes of MESSAGE IN at 16. They also
share the commands and the reply fields it reads. MESSAGE OUT is never written.
UCSI 2.0 and later widen MESSAGE IN and move MESSAGE OUT, so those versions,
like any other, are reported (`UCSI version 0x… is not supported; only 1.x is`)
and the client stays off. Vela's mailbox showed 1.2 (`0x0120`) at client start
and `0x0100` to the earlier A/B observer after `_Q16` copies. The first build
accepted only 1.0 and stayed off on the owner's 2026-10-15 run.

VERSION is read only after a `_DSM` function 2 refresh, as Linux's
`ucsi_acpi_read_version()` does (v6.19.10). On vela, VERSION is written by the
EC query that also refreshes CCI (`_Q16`) and may arrive later. So while
VERSION is zero, the client refreshes every 100 ms and also rereads after each
notification. It waits up to 10 s, then parks. The ThinkPad parks this way;
see [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1325).

**Sequence.** All work runs at the ACPI worker's top level, one command at a time:
1. One PPM interface reset (PPM_RESET, never CONNECTOR_RESET), with a 10 s deadline.
2. SET_NOTIFICATION_ENABLE for command completion.
3. GET_CAPABILITY. It must report 16 bytes and one to eight connectors.
4. For each connector, GET_CONNECTOR_CAPABILITY (logged, otherwise unused) and
   then GET_CONNECTOR_STATUS. The initial status read acknowledges only the
   command's completion, so a change already pending stays pending.
5. SET_NOTIFICATION_ENABLE for command completion and Connect Change. CCI is
   then read once from the mailbox.

This is the order of Linux's `ucsi_init()` (v6.19.10), which completes on vela
(captured on Fedora 44, 2026-10-15), and of UCSI 1.2's example initialization
flow (section 4.3). Linux's later SET_NOTIFICATION_ENABLE enables every
notification the PPM supports; on vela that is `0xDA050000`. The client enables
only the two it consumes; see [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1325).

In vela's runs 3 and 4, without GET_CONNECTOR_CAPABILITY and with `_DSM`
polling, the PPM never completed the command-only acknowledgement of connector
2's status read. Which difference mattered is not known.

The client is then idle. When CCI reports a change on a connector, it reads
that connector's status (the first 9 bytes; UCSI 1.2 may append the optional
`bcdPDVersion`, for 11) and acknowledges the command completion and
the change together in one ACK_CC_CI. For connector 1 those controls are
`0x00010012` and `0x00030004`. Every ordinary command's completion is
acknowledged, and the client then waits for ACK completion, which it never
acknowledges itself.

**Completion.** A `Notify(UBTC, 0x80)` marks that firmware has refreshed the
mailbox, and the worker reads CCI from memory on its next turn. As in Linux's
`ucsi_acpi`:
- **Commands and ACKs** complete on that Notify alone; CCI and MESSAGE IN are
  read from memory.
- **The PPM reset** is the only phase that refreshes CCI through `_DSM`
  function 2, every 20 ms.

Each command has an absolute deadline: 5 s for a command or ACK, and 10 s for
the reset, the same as Linux. Repeated notifications never extend it. On a
timeout, a debug line reports the elapsed time, the notifications seen while
waiting and the last CCI. The deadline is checked against a fresh clock
reading after each sample, including after a slow `_DSM` returns, so a
completion that arrives after the deadline is not accepted. The 10 s VERSION
wait works the same way: it is checked before each refresh, and again after the
refresh, before a VERSION is accepted. A busy CCI is ignored until the PPM
completes.

**No bound on a single AML call.** These deadlines bound the client's state,
not AML execution. Each `_DSM` runs synchronously on the ACPI worker, and an
AML `Sleep` wakes only to service SCIs. So a slow call holds the worker until it
returns, and a power-button press waits for it too. A call that never returns
has no enforceable bound.

A completion bit is accepted only if it was clear in the CCI sample taken
before the command was sent. Otherwise a cached completion from earlier could be
mistaken for this command's, so the client parks instead of sending it. A
completion must also have its phase's shape (UCSI 1.2 table 3-2 and the command
status tables):
- **Reset:** reset completion alone.
- **ACK:** ACK completion, optionally with a connector change.
- **Ordinary command:** command completion, optionally with a connector change,
  data length, error or not-supported, and never reset, busy, cancel or ACK
  completion. A failed command carries no data, and no reply exceeds MESSAGE IN.
- **Busy:** a busy CCI sets no other bit.

Any other value parks the client.

**Failures.** A missed deadline, a failed `_DSM` call, a CCI outside its
phase's shape or an uncorrelatable completion parks the client. It logs one
warning with the phase, CONTROL and CCI, and sends no further commands until
reboot. There is no automatic reset, retry or second ACK.

**Parking with a completion outstanding (owner, 2026-10-15).** If a command
completed in time with a valid CCI but its result is rejected, the client first
acknowledges that completion alone, so the PPM is not left waiting for it, and
parks once that ACK completes. A result is rejected when it reports an error,
when the command is unsupported, or when the reply is malformed (wrong length,
connector count outside one to eight, or data from SET_NOTIFICATION_ENABLE).
A connector change is never acknowledged on this path. A change already
pending, or one reported later, stays unacknowledged while the client is
parked, so a PPM that waits for that ACK may go on repeating the notification
until reboot. A completion that arrives after its deadline, or with an invalid
CCI, is not acknowledged; the client parks directly.

**Notification choice (owner, 2026-10-15).** Only command completion and Connect
Change are enabled. Nothing in Pyxis consumes the other required notifications
(power operation mode, connector partner, power direction, battery charging
status, error), so they stay off until something does; see
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1325).

**Back-off (owner, 2026-10-15).** If more than 16 connector changes arrive within
10 s, the client enters a back-off and handles at most one change per second,
still acknowledging each, so the PPM is never left waiting on it. Once no change
has been handled for 10 s, the normal rate resumes. Entry and exit are each
logged once at info, at most once a minute; individual changes are not. The
back-off never parks the client.

**Logging.** Info level has the ready line (`ACPI: UCSI 1.2 ready: N
connectors…`, with the PPM's version), back-off entry/exit and park warnings. Debug adds the mailbox
address, the VERSION after the first refresh, each reset `_DSM` call with the
CCI it leaves, the capability summary, each connector's capability and every connector status (`ACPI: UCSI
connector N initial|change: change=… connected=…`). Trace adds each CONTROL
write, with the CCI it followed, and each completion, with its latency and CCI.
Together these correlate every command, reply and ACK.

## Table export

The `acpi_tables` resource ([ABI](../../include/abi/acpi_tables.h)) is a
read-only directory root over uACPI's installed-table array: the firmware
tables and those AML `Load`/`LoadTable` installs later, not the RSDP, RSDT or
XSDT. Boot init receives it with LOOKUP, ENUMERATE and READ_FILES and binds it
as `acpi` only in spaces that set
[`acpi_tables = true`](../userland/init.md#boot-configuration). The owner
accepted the contract on 2026-10-15; the [acpidump command](../userland/acpidump.md)
is its consumer.

- **Names.** A signature occurring once is its own name; one occurring more
  than once is numbered from 1 in array order (`SSDT1`..`SSDTn`). Bytes a
  directory name cannot hold (`/` and nonprintable) become `_` first, and
  signatures are counted after that, so `AB/C` and `AB_C` are `AB_C1` and
  `AB_C2`. Names are recomputed from the current array on every call. The enumeration cursor's
  generation is the table count plus one, so a table installed later reports
  CHANGED. The numbering may differ from Linux's.
- **Worker.** Every operation is a `BSP_SERVICE_ACPI_TABLES` request. The
  executor forwards it to the ACPI worker's queue, the only place uACPI's table
  calls run, and the worker answers at its top level in arrival order,
  completing the request on the BSP with IF=0 like a power request. A turn of
  the worker's loop serves at most as many table requests as it runs deferred
  work items (`WORK_PER_TURN`); the rest stay queued and keep the worker awake
  for its next turn, so table traffic cannot starve power requests, the power
  button or UCSI polling.
- **Copies.** The first LOOKUP of a name with kind FILE copies the table into
  an immutable READ-only FILE (`file_prepare_snapshot`) and drops the
  interpreter's reference; a user mapping cannot reference the firmware window
  or uACPI's heap. The worker keeps that FILE, one per table, and every later
  LOOKUP shares it, so retained memory is at most the size of the installed
  tables however often they are opened, and each table is copied once.
  Allocation and the scratch-mapped copy run with IF=0. Bytes are the table as
  first opened, exactly as published, whatever the checksum; a table's bytes
  do not change after installation. The worker releases its references if it
  finds the interpreter's tables gone; open handles keep theirs.
- **Information.** `ACPI_TABLES_INFO`, a directory operation only this root
  accepts, returns a name's signature, length, physical address (zero unless
  the firmware published the table in memory), uACPI's checksum state and the
  withheld and runtime-loaded flags. It requires ENUMERATE. The ENUMERATE
  record itself carries only name and kind, so this query is the explicit
  extension.
- **Withheld.** `kernel/acpi/tables.c` keeps a short list of withheld
  signatures, today `MSDM`. A withheld table is listed and described with its
  length and flag; LOOKUP returns DENIED and its bytes never leave the kernel.
- **Availability.** The root exists on every boot. Requests complete with
  UNAVAILABLE until `uacpi_initialize()` has installed the tables, and the
  worker keeps serving them after a failed namespace load or initialization
  instead of exiting. uACPI itself resets its state, including the table array,
  on its fatal namespace-load failures, so the export then has nothing to read;
  a table whose AML fails to load is not fatal and stays exported.

Mutation operations on the root return READ_ONLY. The root's INFO answers like
the archive's: immutable identity, no entries of its own.

## Power-off and restart

The `power` capability ([ABI](../../include/abi/power.h)) carries OFF and RESTART
rights. The kernel grants both to boot init, which forwards them to spaces that
set `power = true` ([boot configuration](../userland/init.md#boot-configuration)).
The shell's `poweroff` and `reboot` builtins use it.

A short press runs the same clean power-off without a capability: physical
access already permits the firmware's forced power-off by holding the button.
`kernel/acpi/buttons.c` handles the fixed feature only when FADT declares it,
and registers present `PNP0C0C` devices for Notify `0x80` and `INTC10CC` devices
for Intel power press `0xce`. Intel release `0xcf` does nothing. Generic Intel
Notify `0xc0` reads HDEM and accepts only power index 9. Intel admission requires
simple mode (HDMM=0) and BTNL power-event setup: advertised event-filter `_DSM`
functions, revision 1, or named-method fallback. Command execution accepts a void
or arbitrary return; integer queries remain typed. HEBC's advertised array uses
BTNC/BTNE before BTNL, preserving the firmware's final policy. Only power events
are consumed; HDSM hotkeys/tablet input are not enabled. Contexts and
registrations remain owned until reboot.

Startup clears the applicable fixed latch, then services SCI and drains deferred
work while presses are disarmed. During any power operation, fixed and Notify
presses are dropped; after failure, deferred work drains before rearming. This
avoids a queued operation-time Notify retrying shutdown. Queued deliveries
coalesce into one latch, with no assumed physical debounce. The top-level worker
logs `power: power button pressed` and runs power-off. Failure leaves the system
up; a new press tries again. Force remains exclusive to the explicit overlay.

The accepted event-route rule (2026-10-14) permits only existing event sources
and a firmware-proven additional runtime power-button GPE, enabled through its
existing handler. `_PRW` supplies wake-routing candidates, not runtime proof.
Preparation reports additional routes **unqualified** and enables no extra GPE.
General runtime-GPE finalization is not called. Vela's supplied AML proves an
existing EC query route: `_Q10` → PWPR → HIDD Notify `0xce` after BTNL; `_Q11`
delivers release `0xcf`. The EC worker already evaluates its firmware-selected
`_GPE` and handles queries. No additional enable is needed. The
[qualification record](../development/experiments/vela-power-button/README.md)
separates static AML evidence, QEMU Notify coverage and the owner's native
qualification (2026-10-14). Vela firmware ignores short taps, also observed on
Fedora. A held press reaches the Pyxis power worker and runs orderly power-off,
verified by slow-motion video of its press, pool-flush and NVMe-stop lines. No
software hold threshold is added. Diagnostic code is excluded from this backend.

The Ctrl+Alt+Delete [power overlay](power-overlay.md) has the same
physical-access authority. `acpi_power_local()` hands its power-off or restart
to the worker, which runs it like a press and records a failure's status for
the overlay to collect; it returns BUSY while another operation is pending or
running, and UNAVAILABLE without a worker. A request or press arriving during
it is answered as during a press's power-off. After collecting a failed local
operation, the overlay may explicitly choose `acpi_power_local_force()` with
an unsafe-shutdown and data-loss warning. That worker operation bypasses
FAT/npfs storage checks and NVMe shutdown, and logs the force choice. Forwarded
userspace requests and short power-button presses always use the clean sequence.

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
3. It asks the native filesystem worker to flush. The worker closes new storage
   admission and drains admitted operations, including upstream executor work,
   and deferred cleanup. It first flushes each mutated RW
   [FAT instance](../devices/fat32-mounts.md#failure-and-clean-power). With no
   detached allocated chains it clears only that instance's owned dirty mark and
   flushes the clear; otherwise it leaves dirty set without refusing power.
   An unresolved FAT mutation failure refuses clean power. This seal retains
   every view, handle and publication interval; it does not retire the mounts.
   The worker then writes every writable npfs pool's dirty data and checkpoints
   until its journal is EMPTY. Failed/read-only npfs pools take no writes and are
   skipped. On success storage is sealed: mutations fail with UNAVAILABLE,
   background writeback stops and FAT/raw-disk cleanup waits for cancellation.
4. It asks NVMe to stop admitting work, drain and confirm bounded normal
   controller shutdown. Any unconfirmed shutdown refuses clean S5 or reset.
   Controllers remain retired until reboot, even if firmware entry then fails.
5. Power-off evaluates the S5 preparation methods and enters S5 through uACPI.
   A failing `_PTS` is logged but does not stop power-off, since the pools are
   already flushed; entry itself needs a valid `_S5` sleep type.
   Restart writes the FADT reset register. If that register is absent, unusable
   (one in PCI configuration space is refused like any PCI write) or has not
   reset the machine after a second, the architecture fallback pulses the 8042
   reset line and then triple-faults the CPU.

When S5 entry returns, uACPI has already disabled the runtime GPEs and armed
only wake GPEs, so the worker runs uACPI's S5 wake path, which enables the
runtime GPEs again and runs `_WAK`.

If the flush or NVMe shutdown fails, or the firmware does not power off, storage
admission reopens, sealed pools are unsealed and held tasks are released. FAT
views and publication intervals survive; unresolved FAT failures remain frozen.
On a healthy instance whose owned mark sealing cleared, the first subsequent
mutation durably sets dirty before its first effect.
`power: power-off failed (status N); the system stays up` is logged and the call
returns that status.
NVMe retirement is not undone. A successful operation never returns.

Host file systems (`host://`) and RAM volumes need no flush. Raw disk handles
used by the installer are not flushed; the installer flushes them itself.
FAT scratch-media checks are recorded separately in
[mounted-write qualification](../development/experiments/fat32-mounted-writes.md);
they do not establish native hardware power-loss durability.

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
busy-waiting worker uses under 0.2% of the BSP. With the root notify handler
(main after the #467–#470 merges), plugging and unplugging AC printed no
`Notify` warnings in the normal log (owner).

### Power button

QEMU's `system_powerdown` monitor command presses the fixed-feature power
button. Nested-VM checks with the same QEMU, KVM and OVMF:

- Live image, 512 MiB, one and four CPUs: `power: power button pressed` and
  `power: flushing pools; powering off` followed the press within 2 ms, and
  QEMU exited.
- Installed mode with a disposable 64 MiB pool on partition 2 of a virtio-blk
  disk: the `pyxis` space ran `cat boot://config/live.lua > system://X`
  without `sync`, then the button was pressed, on one CPU and then on four
  (writing `Y`). Both presses fell between periodic flushes. Afterwards, on the
  host, `npfs-inspect` showed both files identical to the source with
  `journal_state empty`, `fsck.npfs` passed, and the four-CPU boot mounted the
  pool without a journal replay.
- The shell's `poweroff` still exits QEMU.
- uACPI heap and the firmware window are unchanged; kernel text grew by 368
  bytes.

On the ThinkPad (owner, 2026-10-07, installed system updated to main after
the #467–#470 merges), a short press, not a hold, switched the machine off. A
file created and written just before a press was present on the next boot,
with no journal replay.

Repeated presses, a press during a running operation and a request during a
press's power-off were checked by code inspection only.

### Battery queries

In QEMU without a battery, Fastfetch's default output has no Battery line, and
`fastfetch -s battery --show-errors` reports no batteries. Local test tables,
not part of the change, checked the rest with one and four CPUs:

- a `_BIX` battery discharging at 10 W from 40,000 mWh showed
  `Battery (FAKE01): 86% (3 hours, 28 mins remaining) [Discharging]` for 34,600
  mWh left;
- its JSON gave the table's manufacturer, model, technology, serial, cycle count
  42 and a 11,160 s time remaining for 31,000 mWh;
- with `_BST` charging and `_PSR` online, the status was
  `[AC Connected, Charging]`.

On the ThinkPad (owner, 2026-10-07), Fastfetch's Battery line matched Fedora's
reading. Polls evaluate the same methods as before, and queries copy published
data, so no new timing was taken.

## Limits

These are recorded in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1319)
and [power-off limits](https://git.internal/PyxisOS/pyxis-os/issues/1321):

- PCI configuration writes are refused.
- The mapping window is never reused.
- AML has unrestricted port access and uncached aliases of kernel-owned
  registers.
- The SCI must share the keyboard's I/O APIC.
- Inline completion of deferred work has not yet been exercised.
- Power-off skips failed npfs pools; an unresolved FAT mutation blocks clean
  power. It cannot stop services in order.
- EC transactions busy-wait on the BSP and do not take the ACPI global lock.
- Battery changes are polled; notifications are not handled yet.
- Power buttons work through the FADT fixed feature, `PNP0C0C` Notify `0x80` and
  `INTC10CC` power events. Only Vela's `INTC10CC` route is natively qualified;
  other control-method routing, lid and hotkey events and additional runtime
  GPEs are unsupported or unqualified.
- The UCSI client enables only command-completion and Connect Change
  notifications and supports only UCSI 1.x over `_DSM`.

The embedded controller and battery limits are in
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1324),
and the UCSI client's in [UCSI client limits](https://git.internal/PyxisOS/pyxis-os/issues/1325).

### Read-only HID-I2C GPIO input

The HID-I2C attachment resolves the exact GPIO resource-source device after
namespace initialization. INTC105F community/group `_DSD` properties and one
current memory aperture become an owned scalar snapshot, with no namespace or
resource-buffer pointers retained. Intel native preparation reserves mapping
space before AP startup; attachment fills fresh UC addresses once and retains
all partial failures until reboot. REVID/PADBAR and host-owned, RX-enabled
GPIO input configuration must agree. The BSP reader returns the electrical
level or a refusal; it changes no pad, ownership or interrupt register.
The client applies the live polarity. See [task 3](../wip/vela-i2c-touchpad.md#task-3-implementation-and-limits-2026-10-16).
