# ThinkPad KVM boot and invariant-TSC investigation

Status: **owner direction accepted, 2026-10-03:** implement software-extended
32-bit HPET first to continue native ThinkPad bring-up. The intended future
direction is TSC with extended-HPET fallback; TSC work is deferred. The findings
below were recorded on 2026-10-02. This document records the implementation
handoff; the clock code is unchanged and native boot still stops at the HPET
capability check.

## Accepted direction and implementation handoff

The owner selected the following sequence on 2026-10-03:

1. **Now: software-extended 32-bit HPET.** Preserve the existing direct 64-bit
   HPET path and add software accumulation for a 32-bit counter. The objective
   is to pass the observed clock blocker and continue native ThinkPad bring-up.
   This does not establish that later initialization will succeed.
2. **Future: TSC with extended-HPET fallback.** Use TSC where it qualifies and
   retain a usable HPET source where it does not. This is the chosen future
   direction, not a requirement to implement TSC, calibration, source switching
   or a clock-source framework in the first change. Boot-time fallback and any
   later runtime failure detection/switching are separate work.

The ThinkPad implementation agent should begin with the HPET task below. The
source choice is settled; the remaining implementation questions concern the
sampling and concurrency contract, not whether to choose TSC instead.

- [x] Record the HPET-first decision and deferred TSC-with-fallback direction.
- [ ] Implement and validate software-extended HPET in a focused code PR.
  Preserve shared monotonic nanoseconds, the initialization epoch, saturation,
  wall-clock anchoring and existing deadline semantics. Keep comparator/legacy
  HPET interrupts disabled and retain the existing LAPIC scheduler timer.
  Log the hardware counter width, selected direct/software-extension path and
  advertised period during clock initialization, so the owner can identify the
  native path on the early console. A locally forced path must be labelled as
  forced, without misreporting the hardware width.
- [ ] Record the native result and any next blocker, update the implemented
  timekeeping reference and carry remaining costs/limits into technical debt.
  Do not start the next hardware or TSC task implicitly.

Start from current main after checking this documentation PR's merge status.
The handoff was prepared against main `1f5624e`, with #332 (investigation) and
#337 (xHCI configuration) merged. Reuse that checkout's pinned dependencies;
this clock task needs no dependency or compiler-container change. Keep
`CONFIG_XHCI=n` for the first native qualification, as in current main.

Relevant code is [clock initialization and reads](../../arch/x86_64/clock.c),
its [public arch contract](../../arch/x86_64/include/arch/clock.h),
[early architecture initialization](../../arch/x86_64/init.c),
[interrupt dispatch](../../arch/x86_64/idt.c),
[AP startup](../../arch/x86_64/smp.c) and
[kernel startup](../../kernel/init.c). Read the
[timekeeping reference](../kernel/timekeeping.md) and
[SMP ownership contract](../kernel/smp.md) before changing them.

Use the [extension sketch](#software-extension-sketch-costs-and-required-bound)
as a starting point, not an already approved line-by-line implementation.
Before coding, state how maintenance ownership and early-boot/runtime sampling
satisfy the less-than-one-wrap condition, including concurrent and interrupting
readers. A BSP timer-triggered sample plus explicit coverage before interrupts
are enabled is a candidate, not an owner-selected mechanism. Identify any
unsupported pause/suspend/debugger behavior explicitly; the counter alone cannot
detect or reconstruct a missed full wrap. Resolve a material support-policy
change with the owner; routine placement and helper choices need no new approval.

### Implementation validation

Use ordinary `make -j16` builds, interactive QEMU and debugger inspection, then
an owner-run native boot. Use existing workloads to compare clock-sensitive
behavior before/after where both revisions can run; native pre-change evidence
is the panic, not a working performance baseline. Record revisions, CPU count,
accelerator, durations and measurement variation. The procedures below are
planned checks, not completed validation.

**QEMU:** check the unchanged direct 64-bit path, then exercise the actual
extension implementation across multiple wraps with SMP activity and idle
periods. [QEMU 10.2.2's HPET](https://github.com/qemu/qemu/blob/v10.2.2/hw/timer/hpet.c)
advertises a 64-bit main counter. For this task, a temporary uncommitted local
change selecting the extension path and supplying only the low 32 bits of each
sample is permitted for manual validation. Merely masking the result of the
direct path would not exercise extension. Use the same accumulation and
maintenance code intended for hardware. At the recorded QEMU period of
10,000,000 fs, low-32-bit wraps occur about every 42.95 seconds; derive the
interval from the reported period if the configuration differs.

Record the temporary patch and identify its images/results separately. Remove
it and rebuild before final normal-path/native validation and delivery. This
exception does not introduce a permanent configuration option, new tests, fault
injection or boot automation. Use GDB to inspect extension state and maintenance
execution, recording debugger pauses separately from uninterrupted runs.
Observation supplements code review of concurrent/interrupting readers; it does
not prove every interleaving correct.

**Native ThinkPad:** the owner runs the local console; there is no supported
native NIC/remote-terminal or hardware-debugger path for this check. Follow the
existing [USB-image procedure](../development/usb-image.md) for owner-selected
boot media; this handoff does not select a drive to overwrite. Use a normal
unforced build with `CONFIG_XHCI=n`. Photograph or transcribe the clock-path log
and any next blocker. If boot reaches an interactive shell, use a phone
stopwatch for one roughly 15-minute run:

1. Run `date -u` at the prompt and start the stopwatch, recording the displayed
   seconds and initial offset from an external clock. Firmware-seed accuracy is
   separate from elapsed-time correctness; Fastfetch's minute-scale uptime is
   too coarse for this check.
2. During the first six minutes, run `date -u` repeatedly, every few seconds
   around the expected first wrap, and compare progression with the stopwatch.
   If another CPU's shell is available, optionally repeat there too. This is a
   coarse local-console check, not a continuous concurrent-read workload.
   Guest [Lua](../userland/lua.md#libraries-and-runtime) has no `os` library, so
   an `os.time()` loop is not available; no new library or probe is required.
3. Leave the shells at their prompts for another six minutes, without running
   commands. The native counter wraps roughly every 300 seconds from clock
   initialization, so each six-minute interval spans at least one wrap if time
   advances normally. These durations are observation settings, not kernel limits.
4. Run `date -u` again around minutes 12–15. Compare the displayed elapsed
   seconds with the stopwatch; the initial offset should remain approximately
   stable within manual reading precision (roughly a second or two). Record
   discrepancies, freezes or backward steps rather than assuming their cause.
   An error or jump of roughly five minutes is evidence to investigate for a
   missed or double-counted wrap.

These native observations can expose gross wrap failures. An idle shell still
has kernel readers such as the presentation task, so passing the idle interval
alone does not establish maintenance independent of incidental reads. Review
that ownership/bound explicitly and inspect the maintainer in QEMU. Neither
native seconds output nor a GDB session proves all SMP ordering properties.

## Recorded build and boot

Pyxis booted under KVM on the ThinkPad T14 Gen 1 AMD running Fedora. All four
CPUs came online; preemptive userspace, display presentation and the network
worker started, and a remote shell successfully ran `fastfetch`. No panic was
observed in the normal run. This establishes the KVM configuration below, not
successful native Caelum boot, TSC qualification or broad stress testing. The
owner's separate native boot failure is recorded below.

| Setting | Recorded value |
| --- | --- |
| Built revision | `d06ecb1ebd9aff3069f6ab17ede487946a71c022` |
| Main at the time of the run | `93a2ae4767203285e0b22c73a9fa040cc0fcdc60`, identical tree to the built revision |
| QEMU | 10.2.2, Fedora package `qemu-10.2.2-1.fc44` |
| Machine / accelerator | Q35 / KVM |
| CPU option / reported model | `-cpu max` / AMD Ryzen 5 PRO 4650U with Radeon Graphics |
| vCPU topology | One socket, four cores, one thread per core |
| RAM | 256 MiB allocated; guest allocator reported 188.26 MiB usable |
| Firmware / boot media | OVMF / Pyxis ISO |
| Optional devices | VirtIO network and entropy; no external block disk or USB controller |

The pinned dependency revisions were:

| Repository | Revision |
| --- | --- |
| `fs` | `4b1e81dfbc71d46ca287a93c69b7b83917a84f87` |
| `ports` | `0cf0a8ae5dafa1135dfa0d2b47687f7542a189ff` |
| `third_party/lwip` | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |
| `userspace` | `d0a3b4378e76cea7818209280f936be1daf74be8` |

The native cross toolchain was built with `JOBS=16` from pinned,
checksum-verified archives: GCC 16.2.0 and binutils 2.47.20260726, installed at
`$HOME/opt/pyxis-cross` on the recording host. The image and ordinary boot
commands were:

```sh
export PATH="$HOME/opt/pyxis-cross/bin:$PATH"
make -j16 image
make run CPUS=4 MEMORY=256M ACCEL=kvm QEMU_DISPLAY=gtk \
  VIRTIO_NET=1 TCP_FORWARD=2323:2323 \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd
build/tools/pyxis-remote 127.0.0.1 2323
```

The remote `fastfetch` reported Caelum `d06ecb1ebd9a`, Ryzen 5 PRO 4650U and four
guest CPUs. Artifact SHA-256 values were:

```text
caelum.elf  0a7c085b0c467a53c3457eb6a7524babac9893e88d16c8412c8710bdd3e41988
pyxis.iso   cf310b180c421b1e737595a99ac870398733e60e3239850b307f8d3051d81fb9
```

This is a recorded baseline; subsequent main revisions were not booted for this
report. Local artifacts at the time of recording were `build/image-build.log`,
`build/thinkpad-kvm-boot.png`, `build/thinkpad-kvm-cpu-properties.json` and
`$HOME/.cache/pyxis-toolchain-build/build.log`. They are untracked build
artifacts, not prerequisites for reading this report. All QEMU, remote-client
and debugger processes created for the investigation were stopped.

## Guest timers compared with native inventory

Both the [undocked inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt)
and [docked inventory](../targets/t14-gen1-amd/thinkpad-inventory-docked.txt)
record Linux using TSC. The virtual HPET differs materially from the native one.

| Capability | Native Fedora inventory | Tested KVM configuration |
| --- | --- | --- |
| HPET main counter | 32-bit, 14.318180 MHz | 64-bit, 100 MHz |
| HPET comparators / physical base | Three / `0xfed00000` | Three / `0xfed00000` |
| Invariant TSC | Linux reports `constant_tsc` and `nonstop_tsc` | Effective CPU property `invtsc=false` on all four vCPUs |
| RDTSCP | Reported available | Effective CPU property `rdtscp=true` |
| TSC frequency | Linux refined calibration: 2,096.061 MHz | QEMU metadata: 2,096.059 MHz |
| Selected monotonic source | Linux: TSC | Caelum: HPET |
| TSC deadline timer | `tsc_deadline_timer` absent from inventory flags | Effective CPU property `tsc-deadline=true`; unused by Caelum |
| Alternative sources | Linux lists HPET and ACPI PM timer | KVM clock advertised; unused by Caelum |

The guest boot log reported an HPET period of 10,000,000 fs. GDB read its mapped
capabilities and period registers as `0x8086a201` and `0x00989680`, and confirmed
`hpet_physical=0xfed00000` and `period_fs=10000000`.

CPU properties were queried through QMP in a paused, equivalent Q35/KVM
configuration with `-cpu max`, four cores and 256 MiB RAM. KVM reported enabled.
All four vCPUs had `tsc=true`, `invtsc=false`, `rdtscp=true`,
`tsc-frequency=2096059000`, `tsc-deadline=true`, `kvmclock=true` and
`kvmclock-stable-bit=true`.

These are effective CPU-model properties, not independent guest frequency
calibration or proof of raw TSC offset agreement. An attempted GDB re-execution
of CPUID was unreliable; its feature-register results were discarded.
Debugger-perturbed runs are not used as normal boot-success or failure evidence.
No guest TSC calibration or cross-CPU skew measurement was completed.

QEMU 10.2.2 defaults its `max` CPU to migratable and filters the unmigratable
invariant-TSC feature. The missing guest flag therefore does not refute native
support. A future TSC validation run needs an explicit configuration exposing
`invtsc`, with its migration policy recorded. The frequency property comes from
KVM frequency metadata; its proximity to Linux's calibration is encouraging,
but does not independently validate Caelum's conversion. See the
[QEMU CPU model implementation](https://github.com/qemu/qemu/blob/v10.2.2/target/i386/cpu.c),
[KVM frequency interface](https://docs.kernel.org/virt/kvm/api.html#kvm-get-tsc-khz)
and [QEMU KVM implementation](https://github.com/qemu/qemu/blob/v10.2.2/target/i386/kvm/kvm.c).

## Current Caelum contract and native limitation

[Monotonic timekeeping](../kernel/timekeeping.md) exposes shared nanoseconds
since clock initialization through
[`arch_monotonic_ns()`](../../arch/x86_64/include/arch/clock.h). All CPUs observe
one epoch. The public clock `NOW`, `WALL_NOW` and `SLEEP_UNTIL` requests and
library interfaces can remain unchanged.

[Current clock initialization](../../arch/x86_64/clock.c) requires a valid,
page-aligned ACPI HPET with a 64-bit main counter. It explicitly panics for a
32-bit counter. That explains the native inventory limitation and the following
owner-observed failure, independently of the successful virtual boot.

### Owner-observed native USB boot

During [PR #332 review](https://git.internal/PyxisOS/pyxis-os/pulls/332), the
owner's native USB boot result was supplied and its revision confirmed as main
`93a2ae4767203285e0b22c73a9fa040cc0fcdc60`. This is owner-observed evidence,
not a native run repeated by this investigation. Its tree matches the recorded
KVM build at `d06ecb1`, but the two runs and their evidence remain separate.

The supplied early-console output records a 1920x1080 framebuffer, Limine base
revision 6 and 75 memory regions. Caelum installed its GDT, IDT and double-fault
IST, reported 48-bit physical addressing with NX and supervisor write protection,
discovered PCI ECAM at `0xf8000000` for buses 0..63, and switched to its owned
page-table root at `0x2000`. It then stopped with:

```text
Caelum panic: monotonic clock requires a 64-bit HPET counter
```

This confirms progress through Limine handoff, early architecture setup, ECAM
discovery and the owned CR3 switch, followed by the HPET capability rejection.
It does not establish successful native boot or execution beyond that point.

### Clock and scheduler boundaries

HPET supplies elapsed time only, with comparator interrupts and legacy
replacement disabled. The scheduler uses the existing PIT-calibrated local
APIC timer at 120 Hz. Changing the clock source does not require changing that
interrupt machinery or introducing TSC-deadline scheduling.

Cross-CPU timestamp ordering already matters. Memory-request profiling spans
AP publication, BSP service and AP resumption in
[`kernel/object/memory.c`](../../kernel/object/memory.c), and
[`profile_duration_add`](../../include/kernel/service/profile.h) asserts
`end >= start`. Per-CPU monotonicity alone would not preserve this behavior.

## What Caelum must establish for TSC

The following is deferred design work for the accepted future direction. It is
not a prerequisite for implementing extended HPET.

### Capability detection

Check CPUID maximum leaves before querying features. Require TSC availability
and invariant-TSC bit `CPUID.80000007:EDX[8]` on every participating CPU; detect
RDTSCP independently through `CPUID.80000001:EDX[27]`. Invariant TSC establishes
rate stability through specified power states, not cross-CPU offset agreement,
instruction ordering or continuity through suspend. See the
[AMD CPUID specification](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/design-guides/25481.pdf).

Linux's `constant_tsc` and `nonstop_tsc` reports are useful observations, but
they are not two independent proofs: Linux's AMD initialization derives both
from the invariant-TSC bit. Its successful selection and CPU setup belong to
that Linux boot; Caelum must perform its own checks and setup. See
[Linux AMD initialization](https://github.com/torvalds/linux/blob/master/arch/x86/kernel/cpu/amd.c).

### Frequency and conversion

Use a supported, complete CPUID `0x15` ratio only when denominator, numerator
and crystal frequency are nonzero, sanity-checked against HPET. Otherwise
calibrate against HPET using repeated bracketed samples over bounded intervals,
rejecting disturbed or inconsistent samples. CPUID `0x16`, advertised CPU MHz
and QMP metadata are not substitutes for accurate counter frequency. See the
[Intel SDM, volume 1](https://cdrdv2-public.intel.com/929351/253665-093-sdm-vol-1.pdf).

A 32-bit HPET can serve as a short calibration reference using modular deltas;
the native counter wraps after roughly 300 seconds. It cannot serve as an
unextended runtime fallback. Calibration duration, retries, reference-progress
bounds and error budget need explicit choices before implementation.

Keep one shared epoch and conversion state. Conversion must use checked integer
math and retain saturation instead of wrapping. Freestanding kernel code must
not accidentally introduce unavailable compiler-runtime division helpers.

### Ordered reads

Plain `RDTSC` is insufficient. A conservative initial baseline is
`CPUID; RDTSC; CPUID`, with compiler barriers, correct clobbers and preserved
timestamp registers. The relevant cost comparison is with today's HPET
high/low/high sequence of uncached MMIO reads, not plain `RDTSC` alone. Native
HPET MMIO can be sufficiently expensive that the conservative ordered TSC path
is still cheaper. Under QEMU/KVM, HPET MMIO is handled by the QEMU device model,
whereas ordinary CPUID exits are handled within KVM. That makes a lower cost
plausible there too. These are unmeasured expectations, not measured timings or
a performance result for this ThinkPad. See the
[QEMU HPET implementation](https://github.com/qemu/qemu/blob/v10.2.2/hw/timer/hpet.c),
[KVM CPUID implementation](https://github.com/torvalds/linux/blob/master/arch/x86/kvm/cpuid.c)
and [KVM MMIO exit protocol](https://docs.kernel.org/virt/kvm/api.html#the-kvm-run-structure).

A faster RDTSCP/fence path requires vendor-specific ordering guarantees.
On AMD, LFENCE dispatch serialization must be established through advertised
support or deliberate supported CPU setup. Do not assume that a native Caelum
boot inherits Linux's MSR setup. Relevant references are the
[Intel instruction reference](https://cdrdv2-public.intel.com/929354/253667-093-sdm-vol-2b.pdf),
[AMD ordering guidance](https://docs.amd.com/v/u/en-US/software-techniques-for-managing-speculation)
and the [AMD-authored APM volume 3](https://www.scs.stanford.edu/~zyedidia/docs/x86/amd-manual-v3.pdf)
(mirror).

### Agreement across CPUs

Qualify each AP during serialized startup, before ordinary clock consumers run
there. Repeated BSP/AP bracketed exchanges can reject excessive offset and
inconsistent rate. Finite measurements cannot prove exact lifetime agreement;
invariance alone does not supply that guarantee. See
[KVM timekeeping](https://docs.kernel.org/virt/kvm/x86/timekeeping.html).

The unresolved contract is whether supported-platform guarantees plus startup
qualification suffice, or whether a shared atomic nondecreasing floor is needed
to preserve existing cross-CPU ordering. With nanosecond readings from a GHz
counter, such a floor would normally update on almost every read, contending
for a shared cache line across CPUs. It cannot repair bad frequency or large
drift. An arbitrary skew tolerance smaller than a scheduler tick would not
justify weakening the existing timestamp-ordering contract. Independent per-CPU
epochs are unsuitable.

## Implementation scope and future fallback

The selected first change preserves `arch_monotonic_ns()`, public clock
interfaces and independent scheduler timer interrupts. HPET initialization stays
at its current point, with one shared epoch established before ordinary clock
consumers run. Counter width selects the existing direct path or software
extension. No TSC qualification, provisional clock source or boot-time rebase is
needed for this first change.

The future combined direction adds TSC qualification and source selection while
preserving the same epoch and deadlines. TSC optimization that still requires a
64-bit HPET would not solve this machine's blocker; TSC without a usable 32-bit
fallback would fail boot if qualification rejected it. Neither is the selected
first task. Exact TSC acceptance and fallback mechanics remain future decisions.

### Software extension: sketch, costs and required bound

Keep one shared 64-bit last-observed tick value. For each attempt, load that
value before an ordered, fresh read of the 32-bit HPET counter. Compute the
unsigned modular delta `(uint32_t)(now32 - (uint32_t)last)` and add it to the
shared snapshot, with checked overflow behavior. Publish the candidate using
compare-and-swap. On failure, retry with both a fresh shared snapshot and a
fresh MMIO sample. Reusing a stale counter sample against a newer shared value
can fabricate almost a whole wrap. Read ordering and atomic publication must
preserve the existing cross-CPU monotonicity contract.

The deliberate correctness condition is **strictly less than one full counter
wrap between successfully incorporated HPET samples**, including initialization,
early boot and every supported runtime state. At 14.318180 MHz this is roughly
300 seconds. A longer gap can silently lose whole wraps; the 32-bit value alone
cannot recover how many elapsed. A periodic maintainer needs an explicit owner
and a stated bound that covers stalled execution and long interrupt-disabled
sections. Suspend or debugger/VM pauses during which HPET advances also need
an explicit support policy.

The existing 120 Hz LAPIC interrupt is a possible maintenance trigger, but its
rate alone is not a guarantee. The current
[`timer interrupt path`](../../arch/x86_64/idt.c) does not unconditionally read
the clock; scheduler reads are conditional on timed waits and sleepers. An
implementation would need a deliberate bounded sampling path, including the
interval before interrupts become available. A maintenance sample triggered by
an existing tick would extend HPET elapsed time; it would not turn tick counts
into the monotonic clock or change the scheduler interrupt rate.

Each advancing read normally publishes to the shared cache line, with contention
and possible retries across CPUs, similar to a TSC floor. Reads still incur
uncached MMIO; under QEMU they still use the emulated HPET path. The advertised
period remains the conversion authority. Native HPET ticks are about 69.8 ns,
compared with the virtual HPET's 10 ns; this is counter quantization relevant to
profiling and deadline readings, not a scheduler wakeup-precision guarantee.
These costs and the sampling invariant must be considered alongside avoiding
TSC calibration and synchronization work.

### Boot ordering and scope limits

The future TSC stage needs careful boot ordering because
[`kernel_init`](../../kernel/init.c) anchors wall time and prepares devices before
starting APs. It cannot reset the clock epoch after SMP qualification. APs must
remain gated from normal clock use until source selection and any fallback
rebase are complete. Keep the HPET mapping rather than introducing remote
page-table mutation during this selection.

In the tested default KVM configuration, invariant TSC is not advertised, so
the future combined implementation would retain the existing 64-bit HPET. No
TSC-qualified guest configuration was validated here.

Runtime clock switching, a clock framework, a runtime watchdog, suspend/resume,
CPU hotplug, VM migration support and scheduler timer reprogramming are proposed
outside the first HPET implementation. HPET extension nevertheless requires
the bounded maintenance sampling described above. KVM pvclock would be a separate
clock-source decision:
its stable-clock flag governs converted pvclock readings, not unconditional
agreement of raw TSC values. See
[KVM clock MSRs](https://docs.kernel.org/virt/kvm/x86/msr.html).

## Remaining implementation and future decisions

**For the selected HPET task:** maintenance ownership and the less-than-one-wrap
sampling guarantee through early boot and runtime; concurrent-read ordering and
overflow behavior; and the explicit policy for states where that bound cannot
be established. The extension sketch describes the correctness constraints;
it does not establish that current boot/runtime paths already satisfy them.

**For the deferred TSC stage:** platform acceptance criteria for strict SMP
ordering versus a shared atomic floor; ordered-read mechanisms and any MSR
setup; frequency calibration and SMP sampling/error/retry budgets; and epoch
preservation during boot-time selection. Runtime switching, suspend/resume,
CPU hotplug and VM migration need their own scope. KVM pvclock remains a
separate possible future source.

The owner has selected HPET first and TSC with extended-HPET fallback later.
These remaining choices do not reopen that direction or authorize starting the
TSC stage with the HPET task.
