# Native CPU clocks and Intel HWP qualification

2026-10-25, orchestrator-run hardware captures, analysed offline. Accepted
response: [Intel HWP initialization](../../kernel/cpu-power.md), with
[layout-aware placement](../../wip/cpu-layout.md) deferred as a separate change.
The original baseline and the matched native HWP-only qualification are
recorded separately below.

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

## HWP-only native A/B qualification

2026-10-25, orchestrator-run on vela, AC, same wired adapter/cable, idle input
and quiet horse. Order **A1 B1 A2 B2 A3 B3**: A1 starts from a confirmed cold
power-on; subsequent boots use Remote reboot with a checked new clock epoch.
Every A boot independently confirms HWP disabled. Cold/warm differences are
retained in the records; the improvement also holds against warm A2/A3.

A is main `147add345e028dc25f3bd79293b4c962bdaf15b1` plus the private read-only
probe, branch head `ab8b910ba03bcb211284af8fb948c1c2ce5f9494`. B is production
HWP candidate `29fc7d09891121bdbc0217d2142056fea794fa4c` plus the same probe,
branch head `43d68fdfbcd2c82c0d8a1243d01f0dcc27f59fd6`. Compiler, kernel
configuration, archive, command line, cpuclock and sampling source match;
only the production HWP change, documentation and revision strings differ.
Neither diagnostic branch merges. No layout policy is changed.

Evidence is under `/shared/present/cpu-clocks/hwp-ab/native/`: `metadata.json`
retains exact image hashes and pins; `results/` contains the six complete
captures, clock-epoch evidence and operator conditions. The checked packet seal
is `adfaf387d59014c25814874044989a55a6f1913a04c1f830d92a6044b7339eea`.
Offline analysis is reproducible with `analysis/hwp-ab.py` under the parent
CPU-clocks evidence directory; `analysis/hwp-ab-analysis.json` retains each
CPU, phase, raw capability/request and tool result. PXE menu restoration is
verified byte-for-byte. Vela remains running B; HWP stays enabled until reset.

### Applied policy

All **48 B CPU initializations** report applied: each of 16 CPUs on three
boots. Enable, supported request fields and subsequent scheduler-startup reads
agree; no initialization is unavailable or failed. Every B CPU reports
`HWP_CTL=0`, local selection bit 42 zero, desired zero and EPP 128. Read-only
PECI information is zero in all captured B reads; that does not exclude other
firmware or hardware limits. Initial capability-lowest is 1 everywhere:

| CPUs / APIC IDs | Own lowest / highest | Confirmed request |
|---|---|---|
| P CPUs 0, 1, 3 / 0, 8, 24 | 1 / 52 | `0x80003401` |
| P CPU 2 / 16 | 1 / 53 | `0x80003501` |
| E CPUs 4–11 / 32–46, step 2 | 1 / 37 | `0x80002501` |
| No-L3 Atom CPUs 12–15 / 64–70, step 2 | 1 / 33 | `0x80002101` |

These are performance units, not MHz. Intermediate capability fields vary
between CPUs/boots and some subsequent reads; each request uses its own
initial lowest/highest rather than copying the BSP's entire capability word.
All 864 attempted inventory MSR reads and 4316 retained counter intervals
succeed. A's 192 dependent-HWP entries are deliberately skipped, not hardware
zeros. All 36 cpuclock cohorts pass with zero invalid worker samples.

### Delivered rates and placement

Each cell lists the three cohort means within that boot, in GHz, using the
same five-sample commands as the original baseline:

| Boot | One worker | Sixteen workers |
|---|---|---|
| A1 | 1.701 / 1.789 / 1.790 | 2.109 / 2.129 / 2.139 |
| B1 | 4.349 / 4.381 / 4.384 | 2.219 / 2.219 / 2.220 |
| A2 | 1.718 / 1.709 / 1.708 | 2.149 / 2.153 / 2.157 |
| B2 | 4.377 / 4.370 / 4.370 | 2.218 / 2.219 / 2.218 |
| A3 | 1.719 / 1.791 / 1.787 | 2.159 / 2.160 / 2.159 |
| B3 | 4.350 / 4.344 / 4.377 | 2.220 / 2.219 / 2.218 |

The nine-cohort single-worker mean rises from 1.746 to 4.367 GHz, **2.50×**;
B is about 95.5% of the separate Fedora balanced-AC reference. All B single
loads remain on CPU 1/APIC 8; A uses that P core or CPU 2/APIC 16. The uplift
is not a move from LP-E to P. The all-worker mean rises from 2.146 to 2.219 GHz
(3.4%); its minimum individual worker/sample rises from 0.996 to 1.990 GHz.
Every all-worker phase has tagged load activity on all sixteen CPUs.

Whole-interval APERF divided by elapsed wall nanoseconds shows the previous
large BSP/AP P-core split is gone. Ranges across the nine all-load phases and
CPUs in each group are:

| CPU group | A active cycles / wall ns | B active cycles / wall ns |
|---|---|---|
| BSP P | 2.964–3.048 | 2.652–2.664 |
| AP P | 0.995–1.100 | 2.690–2.792 |
| E | 2.372–2.393 | 2.093–2.094 |
| No-L3 Atom | 2.194–2.286 | 1.994–1.995 |

B restores AP P throughput close to the BSP while E/no-L3 rates fall; the
aggregate mean hides both changes. This is measured redistribution under the
HWP policy, not proof of a particular package power/thermal mechanism. These
rates include wall time and kernel work, not C0-only frequency or worker-index
assignments; timer tick cadences differ across CPUs. No temperature, energy,
limit-reason or battery-lifetime claim follows from these captures.

The HWP enable/request and matched-clock gates are met. The application
follow-up below retains the original results alongside the applied policy.
No claim of every core sustaining its rated maximum follows. A later placement
change compares separately against this HWP policy; it is not part of this
delivery.

## Vela application follow-up (2026-10-25)

Measured by the orchestrator, with interactive observations from chronium.
The application comparison uses the same image `82ae3620` before and after
cherry-picking only HWP code `2d89ec15`. Luna's kernel files are `caelum.elf`
(before, reported hash prefix `66b79922`) and `caelum-hwp.elf` (after,
`c4c5e64a`). These are application runs, separate from the private probe A/B
above; they do not contain a layout-policy change. Raw Git/Clang captures and
the ImGui screenshot are in `/shared/present/cpu-clocks/after-hwp/`.
OpenArena frame statistics and listening observations were relayed by the
orchestrator from chronium, rather than reconstructed from those files.

**CPU at full power** labels the applied policy: HWP autonomous, EPP 128,
16/16 CPUs applied. It does not mean a maximum-clamped request, a promised
frequency, or a power/energy measurement. The cpuclock row is the independently
qualified interleaved probe A/B above; the application rows use the image pair
just described. The application runs are not a new interleaved six-boot study.

| Workload | Original result | CPU at full power (HWP autonomous, EPP 128, 16/16 applied) |
|---|---|---|
| cpuclock, one worker, cohort mean GHz | 1.70–1.79 | 4.34–4.38 |
| [Git clone](git-slice1/README.md#vela-native-hwp-comparison-2026-10-25), fresh RAM destination, seconds | 25.2 / 31.5 / 29.7; median 29.7 | 20.4 / 24.3 / 24.3; median 24.3 |
| clang-hosted, 268 MiB LAN fetch, seconds | 6.2 | About 6.0 |
| clang-hosted archive verification, seconds | 5.4 | 3.7 |
| clang-hosted extraction, seconds | 7.3 | 5.3 |
| ImGui, 1920×1048, FPS | 11.8 | 25; 15 with upstream demo window open |
| [OpenArena](ioquake3-softpipe.md#vela-native-hwp-comparison-2026-10-25), demo088-test1, 320×240, FPS | 5.5 | 13.4 |

The Git logs retain exact elapsed values and all six successful checkouts at
`8dd01594d5bc1841e6e18c8fa0694f1280a1981e`. The end-to-end clone includes the
network path, server and RAM checkout: its network-bound portion limits the
observed speedup; it is not an isolated CPU-cost measurement. The paired medians
improve by about 18%, substantially less than the cpuclock uplift.

Clang fetch is largely unchanged: the package progress line reports 5.9 seconds
after HWP, while the host trace reaches the completed fetch at about 6.0 seconds.
Verification at roughly 72 MiB/s raises an **open observation** about whether
the SHA path uses SHA-NI; these timings do not establish instruction selection
or authorize a fix. Both captures finish extraction, then report that the
`pyxis-dev` dependency is unavailable and exit unsuccessfully. They qualify
those phases, not a complete installation or compiler run.

ImGui's extra demo window changes the content, so its 15 FPS reading is a
separate interactive observation, not another sample of the 25 FPS workload.
OpenArena's reported minimum/mean/maximum/standard-deviation frame times change
from 96.0 / 182.4 / 786.0 / 29.5 ms to 40.0 / 74.7 / 130.0 / 12.4 ms.
Chronium reports that game audio no longer glitches. That listening observation
does not replace broader audio qualification, and no SoH performance was measured.

## Candidate functional checks

HWP-only code `2d89ec15`, existing LLVM 23.1.3 builder, q35/OVMF/2 GiB:
KVM host Intel without HWP and TCG EPYC AMD, four vCPUs each. GDB instruction
breakpoints on the guarded MSR sites were not reached before task initialization;
each CPU retained unavailable/zero-request state. Both reached ordinary userland,
clock and log operations. KVM also completed one/four-worker C11 cpuclock checks
with zero invalid samples; these are functional outputs, not frequency results.
The production kernel has no private sample/tag/log hooks. The exposed virtual
CPUs do not qualify HWP application; the native A/B above supplies that evidence.
The generic MSR guard was source-reviewed, not fault-injected in this check.

The later implementation audit identified modern HWP selector polarity in
`IA32_HWP_CTL`. The Fedora baseline did not capture that register, so request
bit 42 being zero alone does not prove local/package selection in that run.
Matched private A/B probes include the gated control read to verify the
candidate's local-field encoding, plus read-only PECI override information.
