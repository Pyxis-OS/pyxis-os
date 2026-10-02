# ThinkPad KVM boot and invariant-TSC investigation

Status: findings recorded on 2026-10-02; implementation proposals and policy
decisions remain open. No clock, scheduler, ABI or QEMU-launcher implementation
is included in this report.

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

## Proposed bounded implementation and fallback

The options below preserve `arch_monotonic_ns()`, the public clock interfaces
and the independent scheduler timer interrupts. Source selection occurs at boot;
conversion parameters become immutable before scheduler release. No option has
been chosen. Each native-enabling option addresses the observed clock blocker;
none establishes that later native initialization will succeed.

1. **TSC optimization only:** retain mandatory 64-bit HPET initialization,
   qualify TSC across all CPUs, then switch while preserving the existing epoch.
   Rejection leaves HPET selected. Native 32-bit HPET remains unsupported.
2. **Native-enabling TSC:** accept 32-bit HPET solely
   as a calibration reference, establish provisional BSP TSC before wall-clock
   anchoring and device deadlines, and qualify APs before scheduler release.
   Rejection uses 64-bit HPET where available, with a boot-only rebase preserving
   already-issued timestamps and deadlines. With only 32-bit HPET, rejection
   produces an explicit boot failure. This does not include counter extension.
3. **Software-extended 32-bit HPET:** keep HPET as the source and extend its
   main counter in software, without TSC qualification. Native 64-bit counters
   retain today's direct path. Initialization and epoch establishment remain at
   their current point, without a provisional source or boot-only rebase.
   Correctness requires the sampling bound and concurrent-read discipline below.
4. **TSC with extended-HPET fallback:** implement both TSC qualification and
   software extension. Use TSC where it qualifies; rejection uses 64-bit HPET
   or qualified software-extended 32-bit HPET. This avoids option 2's failure
   solely for lacking 64-bit HPET, at the cost of implementing and qualifying
   both paths. Failure to establish either source's requirements still prevents
   admitting that source.

If no supported source qualifies under the selected option, boot fails explicitly.

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

The second scope needs careful boot ordering because
[`kernel_init`](../../kernel/init.c) anchors wall time and prepares devices before
starting APs. It cannot reset the clock epoch after SMP qualification. APs must
remain gated from normal clock use until source selection and any fallback
rebase are complete. Keep the HPET mapping rather than introducing remote
page-table mutation during this selection.

In the tested default KVM configuration, invariant TSC is not advertised, so
the TSC options would retain the existing 64-bit HPET. No TSC-qualified guest
configuration was validated here.

Runtime clock switching, a clock framework, a runtime watchdog, suspend/resume,
CPU hotplug, VM migration support and scheduler timer reprogramming are proposed
outside these initial options. HPET extension would nevertheless require the
bounded maintenance sampling described above. KVM pvclock would be a separate
clock-source decision:
its stable-clock flag governs converted pvclock readings, not unconditional
agreement of raw TSC values. See
[KVM clock MSRs](https://docs.kernel.org/virt/kvm/x86/msr.html).

## Decisions still open

- TSC optimization only, native-enabling TSC with explicit rejection behavior,
  software-extended 32-bit HPET alone, or TSC with extended-HPET fallback.
- For HPET extension, maintenance ownership, the less-than-one-wrap sampling
  guarantee through early boot and runtime, concurrent-read ordering, overflow
  behavior and the policy for states where the bound cannot be established.
- Platform acceptance criteria for strict SMP ordering, or a shared atomic floor.
- Conservative ordered reads or qualified faster paths; policy for any MSR setup.
- Calibration and SMP sampling, error and retry budgets.
- Support boundary for resumed, migrated or otherwise unqualified environments.
- Whether to investigate KVM pvclock later as a separate source.

These proposals do not authorize implementation or establish accepted policy.
