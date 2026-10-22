# KVM clock (2026-10-21)

Task 2 of [clock reads](../../interfaces/clocks.md). Ordinary builds before
`f25b735d` and with clock code `128df032` pass using the existing
`pyxis-llvm23.1.3-3d69e83` image. No public ABI, SDK, program, package extraction
or compiler-container change. The [timekeeping contract](../../kernel/timekeeping.md#kvm-selection)
describes admission, lifetime, versioning and the shared floor.

## Installed workload

Use the [baseline](clock-read-baseline.md) package, command, VM and installed
program/configuration/archive bytes: q35/KVM, `-cpu host` without invtsc,
four CPUs, 4 GiB, OVMF, VirtIO block/network/RNG, raw disk and cache=writeback.
Every package run uses a fresh clone of the frozen installed image. Rebind only
the kernel and required revision/provenance records through the existing record
codec; rename the existing binary directory natively to the new selector. Its
program bytes and original pool configuration remain unchanged. Every boot
confirms committed selection and the expected binary revision, without fallback.
Before kernel SHA-256:
`0e0006c73922316d3b86dc17e42238f8ddd3b776725ca6842a39443840114ce9`;
qualified clock-code kernel SHA-256:
`9b0b0edb9bef786319ac9a31bdd1c7c63fc93e04a127f5d3f519c4e7ea589c94`.
Kernel configurations are byte-identical. Record/header-comment commit
`72e4d74e` changes no clock implementation; the table remains the `128df032`
measurement set.

A current-main control was captured **before implementation**. It selects HPET
and passes the whole command: verification 266.57 s, extraction zero to first
positive 72.68 s, extraction zero to displayed total 517.25 s. The VM showed
about 32% CPU steal during this control; it is labelled contended. The previous
[task 1 HPET series](clock-cache.md) is context: verification 179.63–195.02 s,
extraction 245.84–281.32 s. Its clock implementation is identical, but the
current-main control also includes intervening package-index handling.

| KVM sample | Verify display (s) | Extract zero → first positive (s) | Extract zero → total (s) |
| --- | ---: | ---: | ---: |
| 1 | 32.56 | 11.19 | 64.85 |
| 2 | 31.67 | 9.34 | 46.75 |
| 3 | 31.23 | 8.86 | 45.97 |

All three commands exit 0 with complete Remote drain. Verification median is
31.67 s (31.23–32.56), displayed extraction median 46.75 s (45.97–64.85),
and zero-to-first-positive median 9.34 s (8.86–11.19). An excerpt during the
first after run shows 0–5% steal; load differs from the contended control.
These observations show the workload completes with KVM and materially lower
observed elapsed time, not an attributable percentage improvement.

No perf or debugger session runs alongside these verify/extract intervals. The shared VM has no
exclusive-host/load guarantee. Host receipt intervals include Remote delivery
and roughly 200 ms redraw quantization; the first positive count remains
2/8778 after the large Clang executable. Displayed total precedes final sync
and publication. These are neither native times nor guest extraction+sync
measurements, and the contended control does not isolate the clock's causal
share of elapsed time.

## Functional and debugger checks

The four-CPU no-invtsc VM selects KVM with the stable flag clear on every CPU.
Existing probes pass 32/64-bit contention, mismatch/refusal and future expiry,
process stop, unmap, TLS/errno runtime, a short sleep and verified CALL/SEND at
64/4096 bytes (256 messages, five rounds). Interactive unmap returns BAD_BUFFER
to the old waiter, with no waiter on the replacement mapping. GDB sees empty
parking buckets and deferred expiry lists afterward.

GDB inspects four usable, versioned records, the physical registration base,
common epoch pair and the shared floor. Thirty-two hardware-breakpoint samples
cover all four CPUs: the shared floor never decreases and each result is at
most that floor. These observations exercise the implementation; the CAS and
version/identity ordering arguments are source-inspected, not an exhaustive
concurrency proof. No injected debugger calls or guest state mutation is used.

Separate four-vCPU 10-second perf windows count **zero HPET counter MMIO reads**
and **zero GDTR/IDTR exits** (`kvm:kvm_exit`, Intel reason 46). Thus the GDT
lookup did not replace HPET exits with descriptor exits on this VM. This is a
host/configuration-specific observation; conservative CPUID ordering can still
exit. The instruction path is independently inspected in disassembly.

The one-CPU boot explicitly disables `kvmclock-stable-bit` and passes contention,
refusal/expiry, runtime, short sleep and IPC checks. A single-CPU hardware
breakpoint sees address-WAIT cancellation on the scheduler stack: its cached
expiry sample 53,350,931,412 ns is after the 53,350,881,947 ns deadline, with
empty parking/deferred lists after completion.

Four CPUs with `-rdtscp,kvmclock-stable-bit=off` also select KVM and pass future
expiry/refusal, TLS/errno runtime and CALL. GDB confirms every record uses the
CPUID ordering path. The unsupported-RDTSCP path is functional, but two CPUID
serializers per precise read still exit and are not a performance claim.

Four CPUs with `kvmclock=off` retain HPET because invariant TSC is absent;
future expiry/refusal, TLS/errno runtime and SEND pass. GDB confirms KVM is
unavailable, no records were registered, and all CPUs return to interrupt wait.
Requesting `+invtsc` with KVM time disabled cannot qualify the native TSC path:
QEMU warns that the L1 host does not support the feature, the guest still lacks
it and correctly retains HPET. Native TSC selection is therefore preserved by
source review, not newly measured here. Public
precision-right denial belongs to task 3; no such API or claim is introduced
here. Native source behavior is preserved by inspection; task 1's ThinkPad
after packet remains a separately scheduled measurement, not a native KVM test.
Suspend/resume, migration, invalid host tuples and indefinite host version
updates are not qualified.

Raw commands, builds, GDB captures, perf filters/counts and installed preparation
are under `/shared/present/hpet-hot-reads/task2/`. The source audit found and
corrected two issues before qualification: a missing compiler barrier on the
trailing CPUID, and an epoch-switch race fixed by including old-source fallback
reads in the same floor before selection. The existing runtime-time aggregate
has stale detach expectations, as [task 1](clock-cache.md) records, and is not
counted as a passing timing test.

Follow-up clock code `5e2ad202` refuses zero-multiplier/saturated tuples before
floor publication and scans relocated KVM signature blocks. Ordinary builds and
a one-CPU default boot pass contention, future expiry/refusal and TLS/errno;
GDB sees an empty parking/deferred state afterward. The preceding `5f3c8014`
four-CPU Hyper-V-enlightened boot finds KVM at `0x40000100` (maximum leaf
`0x40000101`) and passes expiry/refusal, runtime and CALL; the scan is unchanged
in `5e2ad202`. Invalid-host refusal is source-reviewed, without fault injection.
These functional checks do not replace or relabel the earlier timing samples.
