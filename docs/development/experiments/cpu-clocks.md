# Native CPU clocks baseline

2026-10-25, orchestrator-run hardware captures, analysed offline. Accepted
response: [Intel HWP initialization](../../kernel/cpu-power.md), with
[layout-aware placement](../../wip/cpu-layout.md) deferred as a separate change.
This record establishes the baseline, not a measured HWP-fix result.

## Configuration and commands

Vela: Core Ultra 7 356H, family 6/model `0xcc`/stepping 2, microcode `0x115`,
16 physical/logical CPUs, AC. Fedora 7.2.10-200.fc44 uses active intel_pstate,
`powersave`, balance_performance EPP and balanced platform profile; turbo is
allowed. This is an unchanged balanced-AC reference, not forced performance.
Pyxis: private read-only probe `a1a2f13b7d82a8b94f5dc93f8df58804a60c4ef5` on
main `b6cf9f7e5b388eaa2f7bdb91b02ffc5433b191ca`, warm Fedora-to-PXE boot,
clean RAM profile, no policy/MSR writes. Candidate base `147add34` adds cpuclock
packaging without changing kernel/architecture behavior.

The calibrated [cpuclock](../../userland/cpuclock.md) uses the same source and
dependent-ADD chain on Linux/Pyxis. Each cohort has five 1000 ms samples:

```text
cpuclock --threads 1 --samples 5 --milliseconds 1000
cpuclock --threads 16 --samples 5 --milliseconds 1000
```

Vela ran three alternating one/all-worker cohorts, with idle and per-CPU
APERF/MPERF observations. Linux reads use read-only MSR devices and sequential
snapshots. Pyxis samples local timer interrupts into static storage, tags known
cpuclock processes and emits only after the workloads; the private collector
never ships. Raw captures, exact image hashes and scripts remain under
`/shared/present/cpu-clocks/`: `vela-fedora-baseline-01/`,
`pyxis/native/results-vela-02/`, and `analysis/`. Pyxis attempt 1 is void because
the Fedora reboot did not occur. PXE restoration was verified byte-for-byte.

## Vela measurements

| Mean GHz, repeat 1 / 2 / 3 | Fedora | Pyxis |
|---|---|---|
| One worker | 4.569 / 4.572 / 4.572 | 1.716 / 1.722 / 1.790 |
| Sixteen workers | 2.314 / 2.317 / 2.316 | 2.143 / 2.156 / 2.158 |

Individual worker/sample ranges: one-worker Fedora 4.524–4.584 and Pyxis
1.407–1.794; sixteen-worker Fedora 1.857–3.027 and Pyxis 1.096–2.654 GHz.
Every cohort passes with zero invalid worker samples. All 288 Fedora inventory
MSR reads and 752 counter intervals succeed; all 112 attempted Pyxis MSR reads
and 1024 retained counter intervals succeed.

**Pyxis HWP is disabled on all 16 CPUs**, although CPUID advertises support.
Capability/request entries are deliberately skipped, not measured zero. Fedora
HWP is enabled everywhere, with EPP 128 and request min/max fields 5/52 or
5/53 on P cores, 4/37 on E and 4/33 on the no-L3 group. These are performance
units, not MHz. Pyxis startup `PERF_STATUS` is `0x2e00` on the BSP and `0x0400`
on APs; `PERF_CTL=0x1300` and `MISC_ENABLE=0x851089` everywhere. Startup snapshots
must not be described as workload-time frequencies.

Every complete Pyxis single-worker interval puts tagged load ticks on
**CPU 1/APIC 8, a P-core**, matching Fedora's dominant single-worker CPU.
Pyxis APERF/MPERF ratios are 0.470/0.470/0.486 versus Fedora's approximately
1.243. Pyxis wall-normalized APERF rates 1.735/1.735/1.796 agree with the lower
ADD rate. These runs do not support an LP-E placement explanation.

Topology matches by APIC ID: P cores 0/8/16/24 have private 3 MiB L2s; E cores
32–38 and 40–46, step 2, form two 4 MiB L2 clusters sharing 18 MiB L3 with P;
the Atom/no-L3 group 64–70, step 2, shares another 4 MiB L2. The last is vela's
LP-E topology, not a distinct CPUID `0x1A` type.

Sixteen-worker wall-normalized APERF rates show a BSP/AP split: BSP P about
3.01–3.06, AP P 1.096–1.101, E 2.386–2.390, no-L3 Atom 2.240–2.288 active
cycles per wall nanosecond. These are not C0-only clocks or worker identities;
timer tick cadence differs across CPUs. The aggregate mean hides slow AP P
cores. HWP state is a concrete configuration difference; its entire performance
effect and the reason for that split remain unproved. Endpoint temperatures
and this capture do not establish or exclude transient thermal/power limiting.

## AMD control

Orchestrator-reported ThinkPad context: Ryzen 5 PRO 4650U, 6 cores/12 threads,
AC, Fedora acpi-cpufreq/performance/boost enabled; unchanged-kernel Pyxis PXE.
Saved cohort summaries are under `thinkpad/` in the same evidence directory.

| Mean GHz, repeat 1 / 2 | Fedora | Pyxis |
|---|---|---|
| One worker | 3.976 / 3.977 | 3.982 / 3.983 |
| Twelve workers | 3.967 / 3.965 | 3.897 / 3.799 |

All pass; Pyxis parallel minimum is 3.496 GHz. This supports leaving AMD
controls unchanged; absent OS policy code does not universally imply slow
clocks. These files contain no AMD MSR/placement capture or exact Pyxis kernel
hash, and do not explain the smaller parallel deficit.

## Qualification gates

Interleave unchanged A and HWP-only B with identical tools/archive/configuration,
AC and workload parameters. Confirm each B CPU's enable, own capability range
and local request; collect counters and placement again. Verify baseline A's
HWP state after reset rather than assuming it reverted. Then repeat original
Git/OpenArena/ImGui workloads without probes, retaining old results and adding
the required **CPU at full power** column with its actual policy and placement.
No claim of every core sustaining its rated maximum follows. A later placement
change compares separately against the same HWP policy. Native candidate and
application reruns are pending.

## Candidate functional checks

HWP-only code `2d89ec15`, existing LLVM 23.1.3 builder, q35/OVMF/2 GiB:
KVM host Intel without HWP and TCG EPYC AMD, four vCPUs each. GDB instruction
breakpoints on the guarded MSR sites were not reached before task initialization;
each CPU retained unavailable/zero-request state. Both reached ordinary userland,
clock and log operations. KVM also completed one/four-worker C11 cpuclock checks
with zero invalid samples; these are functional outputs, not frequency results.
The production kernel has no private sample/tag/log hooks. The exposed virtual
CPUs do not qualify HWP application; that path requires the prepared native A/B.
The generic MSR guard was source-reviewed, not fault-injected in this check.

The later implementation audit identified modern HWP selector polarity in
`IA32_HWP_CTL`. The Fedora baseline did not capture that register, so request
bit 42 being zero alone does not prove local/package selection in that run.
Matched private A/B probes include the gated control read to verify the
candidate's local-field encoding, plus read-only PECI override information.
